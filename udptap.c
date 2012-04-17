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

int main(int argc, char **argv)
{
	int dev,cnt,sock,slen=sizeof(struct sockaddr_in);
	unsigned char buf[1518];
	struct sockaddr_in addr,from;
#ifndef __NetBSD__
	struct ifreq ifr;
#endif

	if(argc<=5) {
		fprintf(stderr,"usage: udptap <tapdevice> <localip> <localport> <remotehost> <remoteport>\n");
		exit(1);
	}

	if((dev = open(argv[1], O_RDWR)) < 0) {
		fprintf(stderr,"open(%s) failed: %s\n", argv[1], strerror(errno));
		exit(2);
	}

#ifndef __NetBSD__
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	strncpy(ifr.ifr_name, "tun%d", IFNAMSIZ);
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
	addr.sin_port=htons(atoi(argv[3]));
    inet_aton(argv[2],&addr.sin_addr);

	if(bind(sock,(struct sockaddr *)&addr,slen)) {
		fprintf(stderr,"bind() to port %d failed: %s\n",atoi(argv[3]),strerror(errno));
		exit(5);
	}

	addr.sin_port=htons(atoi(argv[5]));
	if(!inet_aton(argv[4],&addr.sin_addr)) {
		struct hostent *host;
		host=gethostbyname2(argv[4],AF_INET);
		if(host==NULL) {
			fprintf(stderr,"gethostbyname(%s) failed: %s\n",
				argv[4],hstrerror(h_errno));
			exit(6);
		}
		memcpy(&addr.sin_addr,host->h_addr,sizeof(struct in_addr));
	}
		

	if(fork())
		while(1) {
			cnt=read(dev,(void*)&buf,1518);
			sendto(sock,&buf,cnt,0,(struct sockaddr *)&addr,slen);
		}
	else
		while(1) {
			cnt=recvfrom(sock,&buf,1518,0,(struct sockaddr *)&from,&slen);
			if((from.sin_addr.s_addr==addr.sin_addr.s_addr) &&
			   (from.sin_port==addr.sin_port))
				write(dev,(void*)&buf,cnt);
					
		}
}
