/* udptap - multiplex packets between a tap device and a udp socket

   Copyright (c) 2003, Hans Rosenfeld
   Added mcrypt encryption - Vitaly "_Vi" Shukela; 2012
     Note: mcrypt is GPLv3+

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   HANS ROSENFELD BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Hans Rosenfeld shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Hans Rosenfeld.

   (This Copyright notice / Disclaimer was copied from SIMH (c) Robert M Supnik)

 */

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

#include <mcrypt.h>

union sockaddr_4or6 {
    struct sockaddr_in a4;
    struct sockaddr_in6 a6;
    struct sockaddr a;
};

int main(int argc, char **argv)
{
    
    struct addrinfo hints;
    struct addrinfo *result, *rp;
        
    
    

    
	int dev,cnt,sock,slen;
	unsigned char buf[1536];
	union sockaddr_4or6 addr,from;
#ifndef __NetBSD__
	struct ifreq ifr;
#endif
    
    

    MCRYPT td;
    int i;
    char *key; 
    char *block_buffer;
    int blocksize=0;
    int keysize = 32; /* 256 bits == 32 bytes */
    char enc_state[1024];
    int enc_state_size;
    char* tun_device = "/dev/net/tun";
    char* dev_name="tun%d";

    if(getenv("TUN_DEVICE")) { tun_device = getenv("TUN_DEVICE"); }
    if(getenv("DEV_NAME")) { dev_name = getenv("DEV_NAME"); }

    if(getenv("MCRYPT_KEYFILE")) {
        if (getenv("MCRYPT_KEYSIZE")) { keysize=atoi(getenv("MCRYPT_KEYSIZE"))/8; }
        key = calloc(1, keysize);
        FILE* keyf = fopen(getenv("MCRYPT_KEYFILE"), "r");
        if (!keyf) { perror("fopen keyfile"); return 10; }
        memset(key, 0, keysize);
        fread(key, 1, keysize, keyf);
        fclose(keyf);

        char* algo="twofish";
        char* mode="cbc";

        if (getenv("MCRYPT_ALGO")) { algo = getenv("MCRYPT_ALGO"); }
        if (getenv("MCRYPT_MODE")) { mode = getenv("MCRYPT_MODE"); }


        td = mcrypt_module_open(algo, NULL, mode, NULL);
        if (td==MCRYPT_FAILED) {
            fprintf(stderr, "mcrypt_module_open failed algo=%s mode=%s keysize=%d\n", algo, mode, keysize);
            return 11;
        }
        blocksize = mcrypt_enc_get_block_size(td);
        //block_buffer = malloc(blocksize);

        mcrypt_generic_init( td, key, keysize, NULL);

        enc_state_size = sizeof enc_state;
        mcrypt_enc_get_state(td, enc_state, &enc_state_size);
    }

	if(argc<=4) {
		fprintf(stderr,
                "Usage: udptap_tunnel [-6] <localip> <localport> <remotehost> <remoteport>\n"
                "    Environment variables:\n"
                "    TUN_DEVICE  /dev/net/tun\n"
                "    DEV_NAME    name of the device, default tun%%d\n"
                "    \n"
                "    MCRYPT_KEYFILE  -- turn on encryption, read key from this file\n"
                "    MCRYPT_KEYSIZE  -- key size in bits, default 256\n"
                "    MCRYPT_ALGO     -- algorithm, default is twofish. aes256 is called rijndael-256\n"
                "    MCRYPT_MODE     -- mode, default is CBC\n"
                );
		exit(1);
	}

    int ip_family;
    if (!strcmp(argv[1], "-6")) {
        ++argv;
        ip_family = AF_INET6;
        slen = sizeof(struct sockaddr_in6);
    } else {
        ip_family = AF_INET;
        slen = sizeof(struct sockaddr_in);
    }
    char* laddr = argv[1];
    char* lport = argv[2];
    char* rhost = argv[3];
    char* rport = argv[4];

	if((dev = open(tun_device, O_RDWR)) < 0) {
		fprintf(stderr,"open(%s) failed: %s\n", tun_device, strerror(errno));
		exit(2);
	}

#ifndef __NetBSD__
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	strncpy(ifr.ifr_name, dev_name, IFNAMSIZ);
	if(ioctl(dev, TUNSETIFF, (void*) &ifr) < 0) {
		perror("ioctl(TUNSETIFF) failed");
		exit(3);
	}
#endif
	
	if((sock=socket(ip_family, SOCK_DGRAM, 0))==-1) {
		perror("socket() failed");
		exit(4);
	}
    
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    hints.ai_family = ip_family;
    
    if (getaddrinfo(laddr, lport, &hints, &result)) {
        perror("getaddrinfo for local address");
        exit(5);
    }
    if (result->ai_next) {
        fprintf(stderr, "getaddrinfo for local returned multiple addresses\n");
    }
    if (!result) {
        fprintf(stderr, "getaddrinfo for remote returned no addresses\n");        
        exit(6);
    }
    memcpy(&addr.a, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);
    
	if(bind(sock, (struct sockaddr *)&addr.a, slen)) {
		fprintf(stderr,"bind() to port %d failed: %s\n",lport,strerror(errno));
		exit(5);
	}
    
    if (getaddrinfo(rhost, rport, &hints, &result)) {
        perror("getaddrinfo for local address");
        exit(5);
    }
    if (result->ai_next) {
        fprintf(stderr, "getaddrinfo for remote returned multiple addresses\n");
    }
    if (!result) {
        fprintf(stderr, "getaddrinfo for remote returned no addresses\n");        
        exit(6);
    }
    memcpy(&addr.a, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);

    fcntl(sock, F_SETFL, O_NONBLOCK);
    fcntl(dev, F_SETFL, O_NONBLOCK);
    int maxfd = (sock>dev)?sock:dev;
    for(;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        FD_SET(dev, &rfds);
        int ret = select(maxfd+1, &rfds, NULL, NULL, NULL);
        
        if (ret<0) continue;
            
        if (FD_ISSET(dev, &rfds)) {
			cnt=read(dev,(void*)&buf,1518);
            if (blocksize) {
                cnt = ((cnt-1)/blocksize+1)*blocksize; // pad to block size
                mcrypt_generic (td, buf, cnt);
                mcrypt_enc_set_state (td, enc_state, enc_state_size);
            }
			sendto(sock,&buf,cnt,0, &addr.a, slen);
		}
        
        if (FD_ISSET(sock, &rfds)) {
			cnt=recvfrom(sock,&buf,1536,0, &from.a, &slen);
            
            int address_ok = 0;
            
            if (ip_family == AF_INET) {
                if ((from.a4.sin_addr.s_addr==addr.a4.sin_addr.s_addr) && (from.a4.sin_port==addr.a4.sin_port)) {
                   address_ok = 1; 
                }
            } else {
                if ((!memcmp(
                        from.a6.sin6_addr.s6_addr,
                        addr.a6.sin6_addr.s6_addr, 
                        sizeof(addr.a6.sin6_addr.s6_addr))
                    ) && (from.a6.sin6_port==addr.a6.sin6_port)) {
                   address_ok = 1; 
                }
            }
            
			if (address_ok) {
                if (blocksize) {
                    cnt = ((cnt-1)/blocksize+1)*blocksize; // pad to block size
                    mdecrypt_generic (td, buf, cnt);
                    mcrypt_enc_set_state (td, enc_state, enc_state_size);
                }
				write(dev,(void*)&buf,cnt);
            }
		}
    }

    if (blocksize) {
        mcrypt_generic_deinit (td);
        mcrypt_module_close(td);
    }
}
