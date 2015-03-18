#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

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
    int fd;
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
        p->arguments = (const char**) malloc(sizeof(const char*) * n);
        
        int i;
        for (i=0; i<n; ++i) {
            p->arguments[i] = **c; ++*c;
        }
        ++*c; // skip trailing delimiter
    } else {
        fprintf(stderr, "Assertion failed 1\n");
        return -1;
    }
    return 0;
}

static void free_part(struct Part *p) {
    close(p->fd);    
    free(p->arguments);
}
    
    
int main(int argc, const char* argv[]) {
    if (argc==1 || !strcmp(argv[1], "--help")) {
        fprintf(stderr, "Usage: seqpackettool [--listen-once] part part\n");
        fprintf(stderr, "   part := listen_unix | connect_unix | listen_sctp | connect_sctp | startp | stdiop\n");
        fprintf(stderr, "   stdiop := '-' # use fd 0 for recv and fd 1 for send\n");
        fprintf(stderr, "   listen_unix := 'unix_listen' addressu\n");
        fprintf(stderr, "   connect_unix := 'connect' addressu\n");
        fprintf(stderr, "   listen_sctp := 'sctp_listen4' address4 port | 'sctp_listen6' address6 port\n");
        fprintf(stderr, "   connect_sctp := 'sctp_connect4' address4 port | 'sctp_connect6' address6 port\n");
        fprintf(stderr, "   startp := 'start' '--' argv '--'\n");
        fprintf(stderr, "   argv - one or more command line arguments\n");
        fprintf(stderr, "   addressu - /path/to/unix/socket or @abstract_socket_address\n");
        fprintf(stderr, "   You may use more than two dashes delimiters to allow '--' inside argv.\n");
        
        return 1;
    }
    
    struct Part part1, part2;

    int ret;
    
    ++argv;
    ret = parse_part (&argv, &part1);
    if (ret != 0) return 1;
    ret = parse_part (&argv, &part2);
    if (ret != 0) return 1;
    
    if (*argv != NULL) { fprintf(stderr, "Extra trailing command line arguments\n"); return 1; }
    
    free_part(&part1);
    free_part(&part2);
    
    return 0;
}
