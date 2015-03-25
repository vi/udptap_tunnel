#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#ifndef __NetBSD__
#include <linux/if.h>
#include <linux/if_tun.h>
#endif
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <stdio.h>
#include <errno.h>

#include <sys/select.h>

struct SocketWithAddresses {
    int s;
    struct sockaddr_storage sa;
    struct sockaddr_storage peer;
    socklen_t peeraddrlen;
};

int main(int argc, char* argv[])
{
    if (argc < 7) {
        fprintf(stderr,
          "Usage: udpjump {listen|connect|listen6|connect6} address post {listen|connect|listen6|connect6} multiport_address multiport_port1 multiport_port2 ... multiport_portN\n"
          "Environment variables:\n"
          "   MINISESSION_TIMEOUT_MS=3000 (milliseconds)\n"
          "Example:\n"
          "on server: udpjump connect 127.0.0.1 1194 listen 0.0.0.0 {4000.4039}\n"
          "on client: udpjump listen  127.0.0.1 1195 connect example.com {4000.4039}\n"
        );
        return 8;
    }
    
    const char* mainmode = argv[1];
    const char* mainaddr = argv[2];
    const char* mainport = argv[3];
    const char* multmode = argv[4];
    const char* multaddr = argv[5];
    int timeout_milliseconds = 3000;
    if (getenv("MINISESSION_TIMEOUT_MS")) timeout_milliseconds = atoi(getenv("MINISESSION_TIMEOUT_MS"));
    
    int mainfamily = ((!strcmp(mainmode,"listen6")) || (!strcmp(mainmode, "connect6"))) ? AF_INET6 : AF_INET;
    int multfamily = ((!strcmp(mainmode,"listen6")) || (!strcmp(mainmode, "connect6"))) ? AF_INET6 : AF_INET;
    
    int mainconnect_flag = (!strcmp(mainmode, "connect")) ||  (!strcmp(mainmode, "connect6"));
    int multconnect_flag = (!strcmp(multmode, "connect")) ||  (!strcmp(multmode, "connect6"));
    
    int n = argc - 6;
    const char ** portlist = (const char**) argv+6;
    
    struct SocketWithAddresses *a = (struct SocketWithAddresses*)malloc(sizeof (*a) * n);
    if (!a) { perror("malloc"); return 1; }
    
    struct SocketWithAddresses m;
    
    int ret, i;
    
    memset(&m.sa, 0, sizeof m.sa);
    memset(&m.peer, 0, sizeof m.peer);
    m.peeraddrlen = 0;
    m.sa.ss_family = mainfamily;
    if (mainfamily == AF_INET) {
        struct sockaddr_in *sa = (struct sockaddr_in*) &m.sa;
        sa->sin_port = htons(atoi(mainport));
        ret = inet_pton(AF_INET, mainaddr, &sa->sin_addr);
    } else {
        // AF_INET6
        struct sockaddr_in6 *sa = (struct sockaddr_in6*) &m.sa;
        sa->sin6_port = htons(atoi(mainport));
        ret = inet_pton(AF_INET6, mainaddr, &sa->sin6_addr);        
    }
    if (ret != 1) { perror("inet_pton"); return 2; }
    
    m.s = socket(mainfamily, SOCK_DGRAM, 0);
    if (m.s == -1) { perror("socket"); return 3; }
    if (mainconnect_flag) {
        ret = connect(m.s, (struct sockaddr*)&m.sa, (mainfamily==AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6));
        if (ret==-1) { perror("connect"); return 4; }
    } else {
        //listen or listen6
        ret = bind(m.s, (struct sockaddr*)&m.sa, (mainfamily==AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6));
        if (ret==-1) { perror("bind"); return 4; }
    }
    
    for (i=0; i<n; ++i) {
        struct SocketWithAddresses *e = &a[i];
        
        memset(&e->sa, 0, sizeof e->sa);
        memset(&e->peer, 0, sizeof e->peer);
        e->peeraddrlen = 0;
        e->sa.ss_family = multfamily;
        if (multfamily == AF_INET) {
            struct sockaddr_in *sa = (struct sockaddr_in*) &e->sa;
            sa->sin_port = htons(atoi(portlist[i]));
            ret = inet_pton(AF_INET, multaddr, &sa->sin_addr);
        } else {
            // AF_INET6
            struct sockaddr_in6 *sa = (struct sockaddr_in6*) &e->sa;
            sa->sin6_port = htons(atoi(portlist[i]));
            ret = inet_pton(AF_INET6, multaddr, &sa->sin6_addr);        
        }
        if (ret != 1) { perror("inet_pton"); return 7; }
    
        
        e->s = socket(multfamily, SOCK_DGRAM, 0);
        if (e->s == -1) { perror("socket"); return 5; }
        if (multconnect_flag) {
            ret = connect(e->s, (struct sockaddr*)&e->sa, (multfamily==AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6));
            if (ret==-1) { perror("connect"); return 6; }
        } else {
            //listen or listen6
            ret = bind(e->s, (struct sockaddr*)&e->sa, (multfamily==AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6));
            if (ret==-1) { perror("bind"); return 6; }
        }
    }
    
    char buffer[4096];
    struct timeval timeout = {0, timeout_milliseconds*1000};
    
    int current_send_mutlient = 0;
    
    for(;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        
        int maxfd = -1;
        if (m.s > maxfd) maxfd = m.s;
        FD_SET(m.s, &rfds);
        for(i=0; i<n; ++i) {
            if (a[i].s > maxfd) maxfd = a[i].s;
            FD_SET(a[i].s, &rfds);
        }
        
        struct timeval* timeoutptr = multconnect_flag ?  &timeout  :  NULL;
        
        ret = select(maxfd + 1, &rfds, NULL, NULL, timeoutptr);
        
        if (ret==-1) { perror("select"); return 9; }
        
        if (ret == 0) {
            if (multconnect_flag) {
                ++current_send_mutlient;
                if (current_send_mutlient >= n) current_send_mutlient = 0;
            }
            timeout.tv_sec = 0; timeout.tv_usec = timeout_milliseconds*1000;
            continue;
        }
        
        if (FD_ISSET(m.s, &rfds)) {
            if (mainconnect_flag) {
                ret = recv(m.s, buffer, sizeof buffer, 0);
            } else {
                m.peeraddrlen = sizeof(m.peer);
                ret = recvfrom(m.s, buffer, sizeof buffer, 0,   (struct sockaddr*)&m.peer, &m.peeraddrlen);
            }
            if (ret==-1) {perror("recv"); usleep(100000); }
            else {
                struct SocketWithAddresses *e = &a[current_send_mutlient];
                if (multconnect_flag) {
                    ret = send(e->s, buffer, ret, 0);
                } else {
                    ret = sendto(e->s, buffer, ret, 0,   (struct sockaddr*)&e->peer, e->peeraddrlen);
                }
            }
        }
        
        for (i=0; i<n; ++i) {
            struct SocketWithAddresses *e = &a[i];
            if (FD_ISSET(e->s, &rfds)) {
                if (multconnect_flag) {
                    ret = recv(e->s, buffer, sizeof buffer, 0);
                } else {
                    e->peeraddrlen = sizeof(m.peer);
                    ret = recvfrom(e->s, buffer, sizeof buffer, 0,   (struct sockaddr*) &e->peer, &e->peeraddrlen);
                    current_send_mutlient = i;
                }
                if (ret==-1) {  }
                else {
                    if (mainconnect_flag) {
                        ret = send(m.s, buffer, ret, 0);
                    } else {
                        ret = sendto(m.s, buffer, ret, 0,   (struct sockaddr*)&m.peer, m.peeraddrlen);
                    }
                }
            }
        }
        
    }
    
    return 0;
}
