/* vethify - multiplex packets between two ethernet interfaces

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

#include <sys/socket.h>
#include <netpacket/packet.h>
#include <net/ethernet.h> /* the L2 protocols */

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

void printpacket(const char* msg, const unsigned char* p, size_t len) {
    int i;
    printf("%s len=%d ", msg, len);
    for(i=0; i<len; ++i) {
        printf("%02x", p[i]);
    }
    printf("\n");
}

int parseMac(char* mac, u_int8_t addr[])
{
    int i;
    for (i = 0; i < 6; i++) {
        long b = strtol(mac+(3*i), (char **) NULL, 16);
        addr[i] = (char)b;
    }
    return 0;
}


void init_MAC_addr(int pf, char *interface, char *addr, int *card_index, const char *env_var_for_overriding)
{
    struct ifreq card;

    strcpy(card.ifr_name, interface);

    if(!getenv(env_var_for_overriding)) {
        if (ioctl(pf, SIOCGIFHWADDR, &card) == -1) {
            fprintf(stderr, "Could not get MAC address for %s\n", card.ifr_name);
            perror("ioctl SIOCGIFHWADDR");
            exit(1);
        }

        memcpy(addr, card.ifr_hwaddr.sa_data, 6);
    } else {
        parseMac(getenv(env_var_for_overriding), (unsigned char*)addr);
    }

    if (ioctl(pf, SIOCGIFINDEX, &card) == -1) {
        fprintf(stderr, "Could not find device index number for %s\n", card.ifr_name);
        perror("ioctl SIOCGIFINDEX"); 
        exit(1);
    }
    *card_index = card.ifr_ifindex;
}


int main(int argc, char **argv)
{
	int cnt,sock;
	unsigned char buf_frame[1536+sizeof(struct ether_header)+20000];
    unsigned char *buf = buf_frame+sizeof(struct ether_header);
    struct ether_header *header = (struct ether_header*) buf_frame;
#ifndef __NetBSD__
	struct ifreq ifr;
#endif
    int debug=0;

    if(getenv("DEBUG")) { debug = atoi(getenv("DEBUG")); }

	if(argc<=2) {
		fprintf(stderr,
                "Usage: vethify interface1 interface2\n"
                "Example: vethify wlan0 veth0\n"
                "    Environment variables:\n"
                "    SOURCE_MAC_ADDRESS1 -- by default use interface's one\n"
                "    SOURCE_MAC_ADDRESS2 -- by default use interface's one\n"
                "    DEBUG=0,1,2 print send and recv packets\n"
                "    \n"
                "Primary use case: workaround inability to move wireless connections to other network namespaces on Linux\n"
                "    Setup Wi-Fi connection on wlan0, but don't configure any addresses on it\n"
                "    then create veth0/veth1 pair, use vethify to exchange packets between unmanagable wlan0 and manageable veth0\n"
                "    then move veth1 to your namespace and setup IPv4/IPv6 addresses there\n"
                "    If you can ping hosts, but TCP and UDP does not work, do something like this:\n"
                "       ethtool --offload veth1 rx off tx off && ethtool -K veth1 gso off\n"
                "Implemented by Vitaly \"_Vi\" Shukela based on Robert M Supnik's code\n"
                );
		exit(1);
	}

    char* interface1 = argv[1];
    char* interface2 = argv[2];

    if((sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL)))==-1) {
		perror("socket() failed");
		exit(4);
	}
	
    char source_mac1[6];
    char source_mac2[6];
    int card_index1;
    int card_index2;
    struct sockaddr_ll device1;
    struct sockaddr_ll device2;

    memset(&device1, 0, sizeof(device1));
    memset(&device2, 0, sizeof(device2));
    init_MAC_addr(sock, interface1, source_mac1, &card_index1, "SOURCE_MAC_ADDRESS1");
    init_MAC_addr(sock, interface2, source_mac2, &card_index2, "SOURCE_MAC_ADDRESS2");
    device1.sll_ifindex=card_index1;
    device2.sll_ifindex=card_index2;
    
    
    device1.sll_family = AF_PACKET;
    device2.sll_family = AF_PACKET;
    memcpy(device1.sll_addr, source_mac1, 6);
    memcpy(device2.sll_addr, source_mac2, 6);
    device1.sll_halen = htons(6);
    device2.sll_halen = htons(6);
	
		while(1) {
            struct sockaddr_ll device;
            size_t size = sizeof device;
			cnt=recvfrom(sock,buf_frame,sizeof(buf_frame),0,(struct sockaddr *)&device,&size);
            if(cnt<0) {
                if(debug==1) write(1,"-",1);
                continue;
            }
            if(device.sll_ifindex != card_index1 && device.sll_ifindex != card_index2) {
                if(debug==1) write(1, ".", 1);
                continue; /* Not our interface */
            }
            if(device.sll_ifindex == card_index1) {
                if(debug==2) printpacket("<", buf_frame, cnt);
			    sendto(sock, buf_frame, cnt,0,(struct sockaddr *)&device2, sizeof device2);
                if (debug==1) write(1, "<", 1);
            } else
            if(device.sll_ifindex == card_index2) {
                if(debug==2) printpacket(">", buf_frame, cnt);
			    sendto(sock, buf_frame, cnt,0,(struct sockaddr *)&device1, sizeof device1);
                if (debug==1) write(1, ">", 1);
            }
		}
}
