#include <stdio.h>
#include <sys/types.h>

#ifndef WIN32

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <netdb.h>

#else

#define WINVER 0x0501
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <fcntl.h>

#endif

#include <errno.h>

#undef NDEBUG
#include <assert.h>

#define MAXCLIENTS 64
#define MAXPORTS 16
#define BUFSIZE 4096

struct Client {
    struct sockaddr_storage addr;
};

enum Family {
    I4 = AF_INET,
    I6 = AF_INET6
};

int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: udpcast {-4|-6} address port1 ... portN\n");
        fprintf(stderr, " Send received packets on specified ports to all other seen clients\n");
        return 1;
    }
    
    #ifdef WIN32
        WSADATA wsaData;
        int iResult = WSAStartup( MAKEWORD(2,2), &wsaData );
        if ( iResult != NO_ERROR ) {
            fprintf(stderr,"Error at WSAStartup()\n");
            exit(1);
        }
        _setmode(_fileno(stdin), _O_BINARY);
    #endif

    
    const char* family_ = argv[1];
    const char* bindaddr = argv[2];
    const char** ports = (const char**)argv+3;
    int portcount = argc-3;
    int clientcount = 0;
    
    assert(portcount <= MAXPORTS);
    
    enum Family family;
    if      (!strcmp(family_, "-4")) family = I4;
    else if (!strcmp(family_, "-6")) family = I6;
    else assert(!"Expected -4 or -6");
    
    // FIXME: stack-hungry
    struct Client clients[MAXCLIENTS];
    int sockets[MAXPORTS];
    
    int i,j;
    
    #define i_for_all_ports \
    for (i=0; i<portcount; ++i)
        
    #define j_for_all_clients \
    for (j=0; j<clientcount; ++j)
        
    #define MATCH_FAMILIES(v4,v6) \
    switch (family) { \
        case I4: { \
            v4 \
        } break; \
        case I6: { \
            v6 \
        } break; \
    } \
        
    i_for_all_ports {
        struct addrinfo hints;
        struct addrinfo *result = NULL;
        
        memset(&hints, 0, sizeof(hints));
        hints.ai_flags = AI_PASSIVE;
        hints.ai_family = (int)family;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        
        assert(getaddrinfo(bindaddr, ports[i], &hints, &result) == 0);
        assert(result != NULL && result->ai_next == NULL);
        
        assert((sockets[i] = socket((int)family, SOCK_DGRAM, IPPROTO_UDP)) != -1);
        assert(bind(sockets[i], (const struct sockaddr*)result->ai_addr, result->ai_addrlen) != -1);
        
        freeaddrinfo(result);
    }
    
    for(;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = 0;
        i_for_all_ports {
            assert(sockets[i] != -1);
            FD_SET(sockets[i], &rfds);
            if (sockets[i] + 1 > maxfd) maxfd = sockets[i] + 1;
        }
        int retval;
        retval = select(maxfd, &rfds, NULL, NULL, NULL);
        
        if (retval == -1 && (errno == EINTR || errno == EAGAIN)) continue;
        assert(retval!=-1);
        
        i_for_all_ports {
            if (FD_ISSET(sockets[i], &rfds)) {
                char buffer[BUFSIZE];
                
                int ret;
                struct sockaddr_storage from;
                memset(&from, 0, sizeof(from));
                
                socklen_t addrsize = sizeof(from);
                
                ret = recvfrom(sockets[i], buffer, sizeof(buffer), 0, (struct sockaddr*)&from, &addrsize);
                if (ret == -1 && (errno == EINTR || errno == EAGAIN)) continue;
                assert(ret != -1);
                
                int client_number = -1;
                j_for_all_clients {
                    if (!memcmp(&clients[j].addr, &from, addrsize)) {
                        client_number = j;
                    } else {
                        sendto(sockets[i], buffer, ret, 0, (struct sockaddr*) &clients[j].addr, addrsize);
                    }
                }
                
                if (client_number == -1) {
                    if (clientcount < MAXCLIENTS) {
                        ++clientcount;
                        client_number = clientcount - 1;
                        memcpy(&clients[client_number].addr, &from, addrsize);
                    } else {
                        fprintf(stderr, "Client overflow\n");
                    }
                }
                
                {
                    char diagnostic_message[2];
                    diagnostic_message[0] = "ABCDEFGHIJKLMNOP"[i];
                    diagnostic_message[1] = "_0123456789abcdefghijklnmopqrstuvwxyz......................................................................."[client_number+1];
                    write(1, diagnostic_message, 2);
                }
            }
        }
    }
    return 1;
}
