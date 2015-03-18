#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <errno.h>

enum PartType {
    UNIX_LISTEN = 1,
    UNIX_CONNECT = 2,
    SCTP4_LISTEN = 3,
    SCTP4_CONNECT = 4,
    SCTP6_LISTEN = 5,
    SCTP6_CONNECT = 6,
    STDIO = 100,
    START = 101
};

struct Part {
    enum PartType type;
    const char** arguments;
    int fd_recv;
    int fd_send;
};

static int parse_part(const char*** argv_cursor, struct Part *p) {
    const char ***c = argv_cursor;
    if (!**c) {
        fprintf(stderr, "End of command line arguments: expected part type\n");
        return -1;
    }
    
    enum PartType t;
    if (!strcmp(**c, "listen_unix") || !strcmp(**c, "unix_listen")) {
        t = UNIX_LISTEN;
    } else
    if (!strcmp(**c, "connect_unix") || !strcmp(**c, "unix_connect")) {
        t = UNIX_CONNECT;
    } else
    if (!strcmp(**c, "sctp_listen4") || !strcmp(**c, "sctp4_listen") || !strcmp(**c, "listen_sctp4")) {
        t = SCTP4_LISTEN;
    } else
    if (!strcmp(**c, "sctp_connect4") || !strcmp(**c, "sctp4_connect") || !strcmp(**c, "connect_sctp4")) {
        t = SCTP4_CONNECT;
    } else
    if (!strcmp(**c, "sctp_listen6") || !strcmp(**c, "sctp6_listen") || !strcmp(**c, "listen_sctp6")) {
       t = SCTP6_LISTEN;
    } else
    if (!strcmp(**c, "sctp_connect6") || !strcmp(**c, "sctp6_connect") || !strcmp(**c, "connect_sctp6")) {
        t = SCTP6_CONNECT;
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
    p->fd_recv = -1;
    p->fd_send = -1;
    
    if      (t==STDIO) { p->arguments = NULL; }
    else if (t==UNIX_LISTEN || t==UNIX_CONNECT) {
        if (!**c) { fprintf(stderr, "After listen_unix or connect_unix one more argument is expected\n"); return -1; }
        p->arguments = (const char**) malloc(sizeof(const char*) * 1);
        p->arguments[0] = **c; ++*c;
    }
    else if (t==SCTP4_LISTEN || t==SCTP4_CONNECT || t==SCTP6_LISTEN || t==SCTP6_CONNECT) {
        if (!**c || !*(*c+1) ) { fprintf(stderr, "After sctp_listen or connect two more arguments are expected\n"); return -1; }
        p->arguments = (const char**) malloc(sizeof(const char*) * 2);
        p->arguments[0] = **c; ++*c;
        p->arguments[1] = **c; ++*c;
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

static void free_part(struct Part *p) {
    if (p->fd_recv == p->fd_send) {
        close(p->fd_recv);
    } else {
        close(p->fd_send);
        close(p->fd_recv);
    }
    free(p->arguments);
}


static int init_part_unix(struct Part *p, int listen_once) {
    fprintf(stderr, "AF_UNIX is not implemented\n");
    return -1;
}
static int init_part_sctp(struct Part *p, int listen_once) {
    fprintf(stderr, "SCTP is not implemented\n");
    return -1;
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
        case UNIX_LISTEN:
        case UNIX_CONNECT:
            return init_part_unix(p, listen_once);
        case SCTP4_LISTEN:
        case SCTP4_CONNECT:
        case SCTP6_LISTEN:
        case SCTP6_CONNECT:
            return init_part_sctp(p, listen_once);
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
            }
            lengthtosend -= ret2;
            bufpoint += ret2;
        } while (lengthtosend>0);
        if (shutdownonzero && ret == 0) {
            shutdown_direction(part1, part2);
        }
    }
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
        fprintf(stderr, "   --shutdown-on-zero - interpret zero length packet as a shutdown signal\n");
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
    int shutdownonzero = 0;
    int listen_once = 0;
    
    ++argv;
    for(;;) {
        if (!strcmp(*argv, "--listen-once")) { listen_once = 1; ++argv; continue; }
        if (!strcmp(*argv, "--unidirectional")) { unidirectional = 1; ++argv; continue; }
        if (!strcmp(*argv, "--shutdown-on-zero")) { shutdownonzero = 1; ++argv; continue; }
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
        if (part2.fd_recv != -1) { FD_SET(part2.fd_recv, &rfds); if(max<part1.fd_recv) max=part2.fd_recv; }
        
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
