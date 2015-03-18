/* ipvampire - Attach to a network interface and sniff/inject packets for some IP addresses

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
    fprintf(stderr, "%s len=%d ", msg, len);
    for(i=0; i<len; ++i) {
        fprintf(stderr, "%02x", p[i]);
    }
    fprintf(stderr, "\n");
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

	if(argc!=4) {
		fprintf(stderr,
                "Usage: ipvampire interface {-6|-4} ip_address/netmask\n"
                "Example: ipvampire venet0 -6 2a01:4f8:162:732f:419::acbd/84\n"
                "    Environment variables:\n"
                "    SOURCE_MAC_ADDRESS1 -- by default use interface's one\n"
                "    DEBUG=0,1,2 print send and recv packets\n"
                "    \n"
                "Implemented by Vitaly \"_Vi\" Shukela based on Robert M Supnik's code\n"
                "Example:\n"
                "    ./seqpackettool start -- ./openvpn openvpn --dev stdout --log /dev/stderr -- start -- ./ipvampire ./ipvampire venet0 -6 2a01:4f8:162:732f:419:8000::/88 --\n"
                );
		exit(1);
	}

    char* interface1  = argv[1];
    char* ipmode      = argv[2];
    char* addrandmask = argv[3];
    
    int proto;
    
    if (!strcmp(ipmode, "-4")) {
        proto = AF_INET;
    } else
    if (!strcmp(ipmode, "-6")) {
        proto = AF_INET6;
    } else {
        fprintf(stderr, "Protocol should be -4 or -6\n");
        return 1;
    }
    
    unsigned char filter_addr     [16]; // to fit both ipv4 and ipv6
    unsigned char filter_addr_mask[16];
    
    {
        char* r1 = strtok(addrandmask, "/");
        char* r2 = strtok(NULL       , "/");
        if (r1 == NULL || r2 == NULL) {
            fprintf(stderr, "ip_address/netmask must contain slash (/)\n");
            return 1;
        }
        if (1!=inet_pton(proto, r1, filter_addr)) {
            perror("inet_pton");
            return 1;
        }
        int masked_bits = atoi(r2);
        int i;
        for(i=0; i < ((proto==AF_INET)?4:16); ++i) {
            if (masked_bits == 0) {
                filter_addr_mask[i] = 0x00;
            } else
            if (masked_bits>=8) {
                filter_addr_mask[i] = 0xFF;
                masked_bits -= 8;
            } else {
                filter_addr_mask[i] = 0xFF << (8-masked_bits);
                masked_bits = 0;
            }
            filter_addr[i] &= filter_addr_mask[i];
        }
    }
    
    sock = socket(AF_PACKET, SOCK_DGRAM, htons((proto==AF_INET6) ? ETH_P_IPV6 : ETH_P_IP));
    if (sock == -1) {
		perror("socket() failed");
		exit(4);
	}
	
	
    char source_mac1[6];
    int card_index1;
    struct sockaddr_ll device1;

    memset(&device1, 0, sizeof(device1));
    init_MAC_addr(sock, interface1, source_mac1, &card_index1, "SOURCE_MAC_ADDRESS1");
    device1.sll_ifindex=card_index1;
    device1.sll_protocol = 0;
    device1.sll_family = AF_PACKET;
    memcpy(device1.sll_addr, source_mac1, 6);
    device1.sll_halen = htons(6);
    
    if (bind(sock, (struct sockaddr*)&device1, sizeof device1) == -1) {
        perror("bind");
    }
	
    for(;;) {
        fd_set rfds;
        int retval;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        FD_SET(0, &rfds);
        
        retval = select(sock+1, &rfds, NULL, NULL, NULL);
    
        if (retval==-1) { perror("select"); return 2; }
        
        if (FD_ISSET(sock, &rfds)) {
            
            struct sockaddr_ll device;
            size_t size = sizeof device;
			cnt=recvfrom(sock,buf_frame,sizeof(buf_frame),0,(struct sockaddr *)&device,&size);
            if(cnt<0) {
                if(debug==1) write(2,"-",1);
                continue;
            }
            if(device.sll_ifindex != card_index1) {
                if(debug==1) write(2, ".", 1);
                // Should not happen because of we use "bind"
                continue; /* Not our interface */
            }
            if(device.sll_ifindex == card_index1) {
                int should_we_handle_this_packet = 1;
                unsigned char *dstip;
                int number_of_bytes_in_ip_address;
                
                if (proto == AF_INET) {
                    //unsigned char *srcip = buf_frame + 12;
                    dstip = buf_frame + 16;
                    number_of_bytes_in_ip_address = 4;
                } else {
                    number_of_bytes_in_ip_address = 16;
                    dstip = buf_frame + 24;
                }
                
                {
                    int i;
                    for (i=0; i<number_of_bytes_in_ip_address; ++i) {
                        if ((dstip[i]&filter_addr_mask[i]) != filter_addr[i]) should_we_handle_this_packet = 0;
                    }
                }
                
                if (!should_we_handle_this_packet) {
                    if (debug==2) printpacket("_", buf_frame, cnt);
                    if (debug==1) write(2, "_", 1);
                } else {
                    if(debug==2) printpacket("<", buf_frame, cnt);
			        
			        int ret;
                    ret = write(1, buf_frame, cnt);
                    if (ret != cnt) return 0;
                    if (debug==1) write(2, "<", 1);
                }
            }
        } // FD_ISSET sock
        if (FD_ISSET(0, &rfds)) {
            cnt = read(0, buf_frame, sizeof(buf_frame));
            if (cnt < 1) return 0;
            
            if(debug==2) printpacket(">", buf_frame, cnt);
            
            int ret;
            device1.sll_protocol = htons((proto==AF_INET6) ? ETH_P_IPV6 : ETH_P_IP);
            ret = sendto(sock, buf_frame, cnt,0,(struct sockaddr *)&device1, sizeof device1);
            if (ret==-1) perror("sendto");
            
            if (debug==1) write(2, ">", 1);
            
        }
	}
}
