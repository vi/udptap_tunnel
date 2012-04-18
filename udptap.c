/* udptap - multiplex packets between a tap device and a udp socket

   Copyright (c) 2003, Hans Rosenfeld

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

#include <mcrypt.h>

int main(int argc, char **argv)
{
	int dev,cnt,sock,slen=sizeof(struct sockaddr_in);
	unsigned char buf[1536];
	struct sockaddr_in addr,from;
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
                "Usage: udptap_tunnel <localip> <localport> <remotehost> <remoteport>\n"
                "    Environment variables:\n"
                "    TUN_DEVICE  /dev/net/tun\n"
                "    DEV_NAME    name of the device, default tun%d\n"
                "    \n"
                "    MCRYPT_KEYFILE  -- turn on encryption, read key from this file\n"
                "    MCRYPT_KEYSIZE  -- key size in bits, default 256\n"
                "    MCRYPT_ALGO     -- algorithm, default is twofish. aes256 is called rijndael-256\n"
                "    MCRYPT_MODE     -- mode, default is CBC\n"
                );
		exit(1);
	}

    

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
	
	if((sock=socket(PF_INET, SOCK_DGRAM, 0))==-1) {
		perror("socket() failed");
		exit(4);
	}

	addr.sin_family=AF_INET;
	addr.sin_port=htons(atoi(argv[2]));
    inet_aton(argv[1],&addr.sin_addr);

	if(bind(sock,(struct sockaddr *)&addr,slen)) {
		fprintf(stderr,"bind() to port %d failed: %s\n",atoi(argv[2]),strerror(errno));
		exit(5);
	}

	addr.sin_port=htons(atoi(argv[4]));
	if(!inet_aton(argv[3],&addr.sin_addr)) {
		struct hostent *host;
		host=gethostbyname2(argv[3],AF_INET);
		if(host==NULL) {
			fprintf(stderr,"gethostbyname(%s) failed: %s\n",
				argv[3],hstrerror(h_errno));
			exit(6);
		}
		memcpy(&addr.sin_addr,host->h_addr,sizeof(struct in_addr));
	}
		

	if(fork())
		while(1) {
			cnt=read(dev,(void*)&buf,1518);
            if (blocksize) {
                cnt = ((cnt-1)/blocksize+1)*blocksize; // pad to block size
                mcrypt_generic (td, buf, cnt);
                mcrypt_enc_set_state (td, enc_state, enc_state_size);
            }
			sendto(sock,&buf,cnt,0,(struct sockaddr *)&addr,slen);
		}
	else
		while(1) {
			cnt=recvfrom(sock,&buf,1536,0,(struct sockaddr *)&from,&slen);
			if((from.sin_addr.s_addr==addr.sin_addr.s_addr) &&
			   (from.sin_port==addr.sin_port)) {
                if (blocksize) {
                    cnt = ((cnt-1)/blocksize+1)*blocksize; // pad to block size
                    mdecrypt_generic (td, buf, cnt);
                    mcrypt_enc_set_state (td, enc_state, enc_state_size);
                }
				write(dev,(void*)&buf,cnt);
            }
		}

    if (blocksize) {
        mcrypt_generic_deinit (td);
        mcrypt_module_close(td);
    }
}
