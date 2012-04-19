/* tap_mcrypt - multiplex packets between a tap device and a ethernet interface,
        encrypting everything with mcrypt

   Copyright (c) 2003, Hans Rosenfeld - udptap
                 2012, Vitaly "_Vi" Shukela;
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

#include <mcrypt.h>

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


void init_MAC_addr(int pf, char *interface, char *addr, int *card_index)
{
    struct ifreq card;

    strcpy(card.ifr_name, interface);

    if(!getenv("SOURCE_MAC_ADDRESS")) {
        if (ioctl(pf, SIOCGIFHWADDR, &card) == -1) {
            fprintf(stderr, "Could not get MAC address for %s\n", card.ifr_name);
            perror("ioctl SIOCGIFHWADDR");
            exit(1);
        }

        memcpy(addr, card.ifr_hwaddr.sa_data, 6);
    } else {
        parseMac(getenv("SOURCE_MAC_ADDRESS"), (unsigned char*)addr);
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
	int dev,cnt,sock;
	unsigned char buf_frame[1536+sizeof(struct ether_header)];
    unsigned char *buf = buf_frame+sizeof(struct ether_header);
    struct ether_header *header = (struct ether_header*) buf_frame;
#ifndef __NetBSD__
	struct ifreq ifr;
#endif

    MCRYPT td;
    char *key; 
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

	if(argc<=2) {
		fprintf(stderr,
                "Usage: tap_mcrypt plaintext_interface destination_mac_address\n"
                "Example: tap_mcrypt wlan0 ff:ff:ff:ff:ff:ff\n"
                "    Environment variables:\n"
                "    TUN_DEVICE  /dev/net/tun\n"
                "    DEV_NAME    name of the device, default tun%%d\n"
                "    SOURCE_MAC_ADDRESS -- by default use interface's one\n"
                "    \n"
                "    MCRYPT_KEYFILE  -- turn on encryption, read key from this file\n"
                "    MCRYPT_KEYSIZE  -- key size in bits, default 256\n"
                "    MCRYPT_ALGO     -- algorithm, default is twofish. aes256 is called rijndael-256\n"
                "    MCRYPT_MODE     -- mode, default is CBC\n"
                );
		exit(1);
	}

    char* interface = argv[1];
    char* dest_mac = argv[2];

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

    {
	    struct ifreq ifr_tun;
	    strncpy(ifr_tun.ifr_name, ifr.ifr_name, IFNAMSIZ);
        if((sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL)))==-1) {
            perror("socket() failed");
            exit(4);
        }
        
        if (ioctl(sock, SIOCGIFFLAGS, &ifr_tun) < 0) { perror("ioctl SIOCGIFFLAGS"); }
        ifr_tun.ifr_mtu=1408;
        if(ioctl(sock, SIOCSIFMTU, (void*) &ifr_tun) < 0) {
            perror("ioctl(SIOCSIFMTU) failed");
        }
        
        close(sock);
    } 
#endif
	

    if((sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL)))==-1) {
		perror("socket() failed");
		exit(4);
	}

    char source_mac[6];
    int card_index;
    struct sockaddr_ll device;

    memset(&device, 0, sizeof(device));
    init_MAC_addr(sock, interface, source_mac, &card_index);
    device.sll_ifindex=card_index;
    
    
    device.sll_family = AF_PACKET;
    memcpy(device.sll_addr, source_mac, 6);
    device.sll_halen = htons(6);
    
    parseMac(dest_mac, header->ether_dhost);
    memcpy(header->ether_shost, source_mac, 6);
    header->ether_type = htons(0x08F4);  
		

	if(fork())
		while(1) {
			cnt=read(dev,(void*)buf,1518);
            //printpacket("sent", buf, cnt);
            if (blocksize) {
                cnt = ((cnt-1)/blocksize+1)*blocksize; // pad to block size
                mcrypt_generic (td, buf, cnt);
                mcrypt_enc_set_state (td, enc_state, enc_state_size);
            }
            //printpacket("encr", buf, cnt);
			sendto(sock, buf_frame, cnt+sizeof(struct ether_header),0,(struct sockaddr *)&device, sizeof device);
		}
	else
		while(1) {
            size_t size = sizeof device;
			cnt=recvfrom(sock,buf_frame,1536,0,(struct sockaddr *)&device,&size);
            if(device.sll_ifindex != card_index) {
                continue; /* Not our interface */
            }
            if(header->ether_type != htons(0x08F4)) {
                continue; /* Not our protocol type */
            }
            cnt-=sizeof(struct ether_header);
            //printpacket("recv", buf, cnt);
            if (blocksize) {
                cnt = ((cnt-1)/blocksize+1)*blocksize; // pad to block size
                mdecrypt_generic (td, buf, cnt);
                mcrypt_enc_set_state (td, enc_state, enc_state_size);
            }
            //printpacket("decr", buf, cnt);
            write(dev,(void*)buf,cnt);
		}

    if (blocksize) {
        mcrypt_generic_deinit (td);
        mcrypt_module_close(td);
    }
}
