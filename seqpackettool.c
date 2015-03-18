#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>

enum PartType {
    LISTEN = 1,
    CONNECT = 2,
    STDIO = 100,
    START = 101
};

struct Part {
    enum PartType type;
    
    int family;
    int socktype;
    int proto;
    
    const char** arguments;
    int fd_recv;
    int fd_send;
};

static void free_part(struct Part *p) {
    if (p->fd_recv == p->fd_send) {
        close(p->fd_recv);
    } else {
        close(p->fd_send);
        close(p->fd_recv);
    }
    free(p->arguments);
}


#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 104
#endif 

static int init_addr(struct Part *p, struct sockaddr_storage *s, socklen_t *len)
{
    switch (p->family) {
        case AF_UNIX: {
            struct sockaddr_un *sa = (struct sockaddr_un*) s;
            
            *len = sizeof(*sa);
            memset(sa, 0, *len);
            sa->sun_family = AF_UNIX;
                
            snprintf(sa->sun_path, UNIX_PATH_MAX, "%s", p->arguments[0]);
            if(sa->sun_path[0]=='@')sa->sun_path[0]=0; // abstract socket address; man 7 unix
        
            return 0;
        }
        case AF_INET: {
            struct sockaddr_in *sa = (struct sockaddr_in*) s;
            
            *len = sizeof(*sa);
            memset(sa, 0, *len);
            sa->sin_family = AF_INET;
            sa->sin_port = htons(atoi(p->arguments[1]));
            int ret = inet_pton(AF_INET, p->arguments[0], &sa->sin_addr);
            
            if (ret!=1) {perror("inet_pton"); return -1; }
            return 0;
        }
        case AF_INET6: {
            struct sockaddr_in6 *sa = (struct sockaddr_in6*) s;
            
            *len = sizeof(*sa);
            memset(sa, 0, *len);
            sa->sin6_family = AF_INET6;
            sa->sin6_port = htons(atoi(p->arguments[1]));
            int ret = inet_pton(AF_INET6, p->arguments[0], &sa->sin6_addr);
            
            if (ret!=1) {perror("inet_pton"); return -1; }
            return 0;
        }
        default:
            fprintf(stderr, "Assertion failed 6\n");
            return -1;
    }
}

static int init_part_socket(struct Part *p, int listen_once) {
    int sso;
    struct sockaddr_storage sa;
    socklen_t salen;
    int ret;
    
    sso = socket(p->family, p->socktype, p->proto);
    if (sso == -1) { perror("socket"); return -1; }
    
    ret = init_addr(p, &sa, &salen); if (ret == -1) return -1;
    
    if (p->type == CONNECT) {
        ret = connect(sso, (struct sockaddr*)&sa, salen);
        if (ret==-1) { close(sso); perror("connect"); return -1; }
        
        p->fd_send = sso;
        p->fd_recv = sso;
        return 0;
    } else 
    if (p->type == LISTEN) {
        struct sockaddr_storage sa2;
        socklen_t l = salen;
        int cso;
        memset(&sa2, 0, sizeof sa2);
        
        ret = bind(sso, (struct sockaddr*)&sa, salen);
        if (ret==-1) { close(sso); perror("bind"); return -1; }
            
        if (listen(sso, listen_once?1:16)==-1) { close(sso); perror("listen"); return -1; }
    
        if (listen_once) {
            cso  = accept(sso, (struct sockaddr*)&sa2, &l);
            
            if (cso == -1) { close(sso); perror("accept"); return -1; }
            
            p->fd_send = cso;
            p->fd_recv = cso;
            close(sso);
            return 0;
        } else {
            signal(SIGCHLD, SIG_IGN);
            for(;;) {
                cso  = accept(sso, (struct sockaddr*)&sa2, &l);
                
                pid_t child = fork();
                if (child == -1) { close(cso); perror("fork"); usleep(100000); continue; }
                
                if (child == 0) {
                    close(sso);
                    p->fd_send = cso;
                    p->fd_recv = cso;
                    return 0;
                } else {
                    close(cso);
                    // go on accepting other connections
                }
            }
        }
    } else {
        fprintf(stderr, "Assertion failed 3\n");
        return -1;
    }
}

extern char ** environ;
static int init_part_subprocess(struct Part *p, struct Part *first_part) {
    int ret;
    
    if (first_part) {
        ret = dup2(first_part->fd_recv, 0); if(ret == -1) { perror("dup2"); return -1; }
        ret = dup2(first_part->fd_send, 1); if(ret == -1) { perror("dup2"); return -1; }
        execve(p->arguments[0], (char*const*)p->arguments+1, environ);
        perror("execve");
        return -1;
    }
    
    int sv[2];
    ret = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv);
    
    if (ret == -1) { perror("socketpair"); return -1; }
    
    p->fd_recv = sv[0];
    p->fd_send = sv[0];
    
    pid_t child = fork();
    if (child == -1) { perror("fork"); return -1; }
    if (child == 0) {
        close(sv[0]);
        ret = dup2(sv[1], 0); if(ret == -1) { perror("dup2"); exit(1); }
        ret = dup2(sv[1], 1); if(ret == -1) { perror("dup2"); exit(1); }
        close(sv[1]);
        execve(p->arguments[0], (char*const*)p->arguments+1, environ);
        perror("execve");
        exit(1);
    }
    close(sv[1]);
    
    return 0;
}
    
static int init_part(struct Part *p, int listen_once, struct Part *first_part) {
    switch(p->type) {
        case LISTEN:
        case CONNECT:
            return init_part_socket(p, listen_once);
        case START:
            return init_part_subprocess(p, first_part);
        case STDIO:
            p->fd_recv = 0;
            p->fd_send = 1;
            return 0;
        default:
            fprintf(stderr, "Assertion failed 2\n");
            return -1;
    }
}
    
static void shutdown_direction(struct Part* part1, struct Part* part2) {
    shutdown(part1->fd_recv, SHUT_RD);
    shutdown(part2->fd_send, SHUT_WR);
    part1->fd_recv = -1;
}

static void exchange_data(struct Part* part1, struct Part* part2, char* buffer, size_t bufsize, int shutdownonzero) {
    int ret;
    ret = read(part1->fd_recv, buffer, bufsize);
    if (ret == -1) {
        if (errno==EAGAIN || errno==EINTR) return;
        shutdown_direction(part1, part2);
    } else {
        int lengthtosend = ret;
        const char* bufpoint = buffer;
        do {
            int ret2;
            ret2 = write(part2->fd_send, bufpoint, lengthtosend);
            if (ret2 == -1 || (ret2 == 0 && lengthtosend != 0)) {
                if (ret2 == -1 && (errno==EAGAIN || errno==EINTR)) continue;
                shutdown_direction(part1, part2);
                return;
            }
            lengthtosend -= ret2;
            bufpoint += ret2;
        } while (lengthtosend>0);
        if (shutdownonzero && ret == 0) {
            shutdown_direction(part1, part2);
        }
    }
}


static int parse_part(const char*** argv_cursor, struct Part *p) {
    const char ***c = argv_cursor;
    if (!**c) {
        fprintf(stderr, "End of command line arguments: expected part type\n");
        return -1;
    }
    
    enum PartType t;
    int family = 0;
    int socktype = 0;
    int proto = 0;
    if (!strcmp(**c, "listen_unix") || !strcmp(**c, "unix_listen")) {
        t = LISTEN;
        family = AF_UNIX;
        socktype = SOCK_SEQPACKET;
    } else
    if (!strcmp(**c, "connect_unix") || !strcmp(**c, "unix_connect")) {
        t = CONNECT;
        family = AF_UNIX;
        socktype = SOCK_SEQPACKET;
    } else
    if (!strcmp(**c, "sctp_listen4") || !strcmp(**c, "sctp4_listen") || !strcmp(**c, "listen_sctp4")) {
        t = LISTEN;
        family = AF_INET;
        socktype = SOCK_STREAM;
        proto = IPPROTO_SCTP;
    } else
    if (!strcmp(**c, "sctp_connect4") || !strcmp(**c, "sctp4_connect") || !strcmp(**c, "connect_sctp4")) {
        t = CONNECT;
        family = AF_INET;
        socktype = SOCK_STREAM;
        proto = IPPROTO_SCTP;
    } else
    if (!strcmp(**c, "sctp_listen6") || !strcmp(**c, "sctp6_listen") || !strcmp(**c, "listen_sctp6")) {
        t = LISTEN;
        family = AF_INET6;
        socktype = SOCK_STREAM;
        proto = IPPROTO_SCTP;
    } else
    if (!strcmp(**c, "sctp_connect6") || !strcmp(**c, "sctp6_connect") || !strcmp(**c, "connect_sctp6")) {
        t = CONNECT;
        family = AF_INET6;
        socktype = SOCK_STREAM;
        proto = IPPROTO_SCTP;
    } else
    if (!strcmp(**c, "start") || !strcmp(**c, "exec")) {
        t = START;
    } else
    if (!strcmp(**c, "-")) {
        t = STDIO;
    } else {
        fprintf(stderr, "Part type %s is incorrect, look for --help message\n", **c);
        return -1;
    }
    ++*c;
    
    p->type = t;
    p->family = family;
    p->socktype = socktype;
    p->proto = proto;
    p->fd_recv = -1;
    p->fd_send = -1;
    
    if      (t==STDIO) { p->arguments = NULL; }
    else if (t==LISTEN || t==CONNECT) {
        if (family == AF_UNIX) {
            if (!**c) { fprintf(stderr, "After listen_unix or connect_unix one more argument is expected\n"); return -1; }
            p->arguments = (const char**) malloc(sizeof(const char*) * 1);
            p->arguments[0] = **c; ++*c;
        }
        else if (family == AF_INET || family == AF_INET6) {
            if (!**c || !*(*c+1) ) { fprintf(stderr, "After sctp_listen or connect two more arguments are expected\n"); return -1; }
            p->arguments = (const char**) malloc(sizeof(const char*) * 2);
            p->arguments[0] = **c; ++*c;
            p->arguments[1] = **c; ++*c;
        } else {
            fprintf(stderr, "Assertion failed 4\n"); return -1;
        }
    }
    else if (t==START) {
        if (!**c || !*(*c+1) || !*(*c+2) ) { fprintf(stderr, "After start more arguments are expected\n"); return -1; }
        const char* delim = **c; ++*c;
        // delim is supposed to be or more dashes, but let's be more flexible and just not check for the delimiter content at all
        int n;
        for(n=0; ;++n) {
            if (!*(*c+n)) { fprintf(stderr, "Premature end of command line arguments. Delimiter %s is expected.\n", delim); return -1; }
            if (!strcmp(*(*c+n), delim)) break;
        }
        if (n==0) { fprintf(stderr, "Empty list of child command line arguments\n"); return -1; }
        p->arguments = (const char**) malloc(sizeof(const char*) * (n + 1));
        
        int i;
        for (i=0; i<n; ++i) {
            p->arguments[i] = **c; ++*c;
        }
        p->arguments[n] = NULL;
        ++*c; // skip trailing delimiter
    } else {
        fprintf(stderr, "Assertion failed 1\n");
        return -1;
    }
    return 0;
}


int main(int argc, const char* argv[]) {
    if (argc==1 || !strcmp(argv[1], "--help")) {
        fprintf(stderr, "Usage: seqpackettool [options] part part\n");
        fprintf(stderr, "   part := listen_unix | connect_unix | listen_sctp | connect_sctp | startp | stdiop\n");
        fprintf(stderr, "   stdiop := '-' # use fd 0 for recv and fd 1 for send\n");
        fprintf(stderr, "   listen_unix := 'unix_listen' addressu\n");
        fprintf(stderr, "   connect_unix := 'connect' addressu\n");
        fprintf(stderr, "   listen_sctp := 'sctp_listen4' address4 port | 'sctp_listen6' address6 port\n");
        fprintf(stderr, "   connect_sctp := 'sctp_connect4' address4 port | 'sctp_connect6' address6 port\n");
        fprintf(stderr, "   startp := 'start' '--' full_path_to_program argv0 ... argvN '--'\n");
        fprintf(stderr, "   addressu - /path/to/unix/socket or @abstract_socket_address\n");
        fprintf(stderr, "   \n");
        fprintf(stderr, "   You may use more than two dashes in delimiters to allow '--' inside argv.\n");
        fprintf(stderr, "   BUFSIZE environment variable adjusts buffer size\n");
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "   --listen-once - don't fork, accept only one connection\n");
        fprintf(stderr, "   --unidirectional - only recv from first  and send to second part\n");
        fprintf(stderr, "   --allow-empty - don't shutdown connection on empty packets\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "    seqpackettool sctp4_listen 0.0.0.0 6655 sctp4_listen 127.0.0.1 5566\n");
        fprintf(stderr, "       emulate   socat sctp-listen:6655,fork,reuseaddr sctp-listen:5566,reuseaddr,bind=127.0.0.1\n");
        fprintf(stderr, "    seqpackettool unix_listen @myprog start -- /usr/bin/my_program my_program arg1 arg2 --\n");
        fprintf(stderr, "       for each incoming connection at abstract address 'myprog',\n");
        fprintf(stderr, "       start the program and provide the seqpacket socket as stdin/stdout\n");
        fprintf(stderr, "    seqpackettool - unix_connect /path/to/socket\n");
        fprintf(stderr, "       exchange data between stdin/stdout and AF_UNIX seqpacket socket\n");
        
        return 1;
    }
    
    size_t bufsize = 65536;
    if (getenv("BUFSIZE")) bufsize = atoi(getenv("BUFSIZE"));
    
    char *buffer;
    buffer =  (char*) malloc(bufsize);
    if (!buffer) { perror("malloc"); return 3; }
    
    struct Part part1, part2;

    int ret;
    
    int unidirectional = 0;
    int shutdownonzero = 1;
    int listen_once = 0;
    
    ++argv;
    for(;;) {
        if (!strcmp(*argv, "--listen-once")) { listen_once = 1; ++argv; continue; }
        if (!strcmp(*argv, "--unidirectional")) { unidirectional = 1; ++argv; continue; }
        if (!strcmp(*argv, "--allow-empty")) { shutdownonzero = 0; ++argv; continue; }
        break;
    }
    
    ret = parse_part (&argv, &part1);  if (ret != 0) return 1;
    ret = parse_part (&argv, &part2);  if (ret != 0) return 1;
    if (*argv != NULL) { fprintf(stderr, "Extra trailing command line arguments\n"); return 1; }
    
    ret = init_part (&part1, listen_once, NULL);    if (ret != 0) return 1;
    // At this point we may have been forked
    ret = init_part (&part2, 1          , &part1);  if (ret != 0) return 1;
    // We don't reach this point if part2's type is START
    
    if (unidirectional) {
        shutdown(part2.fd_recv, SHUT_RD);
        shutdown(part1.fd_send, SHUT_WR);
        part2.fd_recv = -1;
    }
    
    for(;;) {
        fd_set rfds;
        int retval;
        FD_ZERO(&rfds);
        int max = -1;
        if (part1.fd_recv != -1) { FD_SET(part1.fd_recv, &rfds); if(max<part1.fd_recv) max=part1.fd_recv; }
        if (part2.fd_recv != -1) { FD_SET(part2.fd_recv, &rfds); if(max<part2.fd_recv) max=part2.fd_recv; }
        
        if (max==-1) break;
        
        retval = select(max+1, &rfds, NULL, NULL, NULL);
        
        if (retval==-1) { perror("select"); return 2; }
        
        if (part1.fd_recv != -1 && FD_ISSET(part1.fd_recv, &rfds)) {
            exchange_data(&part1, &part2, buffer, bufsize, shutdownonzero);
        }
        if (part2.fd_recv != -1 && FD_ISSET(part2.fd_recv, &rfds)) {
            exchange_data(&part2, &part1, buffer, bufsize, shutdownonzero);
        }
        
    }
    
    free_part(&part1);
    free_part(&part2);
    
    return 0;
}
