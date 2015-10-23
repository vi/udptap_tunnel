#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <arpa/inet.h>

#define BS 10240

unsigned char buf[BS];


// http://minirighi.sourceforge.net/html/udp_8c-source.html
//! \brief
//!     Calculate the UDP checksum (calculated with the whole
//!     packet).
//! \param buff The UDP packet.
//! \param len The UDP packet length.
//! \param src_addr The IP source address (in network format).
//! \param dest_addr The IP destination address (in network format).
//! \return The result of the checksum.
uint16_t udp_checksum(const void *buff, size_t len, in_addr_t src_addr, in_addr_t dest_addr)
{
        const uint16_t *buf=buff;
        uint16_t *ip_src=(void *)&src_addr, *ip_dst=(void *)&dest_addr;
        uint32_t sum;
        size_t length=len;

        // Calculate the sum                                            //
        sum = 0;
        while (len > 1)
        {
                sum += *buf++;
                if (sum & 0x80000000)
                        sum = (sum & 0xFFFF) + (sum >> 16);
                len -= 2;
        }

        if ( len & 1 )
                // Add the padding if the packet lenght is odd          //
                sum += *((uint8_t *)buf);

        // Add the pseudo-header                                        //
        sum += *(ip_src++);
        sum += *ip_src;

        sum += *(ip_dst++);
        sum += *ip_dst;

        sum += htons(IPPROTO_UDP);
        sum += htons(length);

        // Add the carries                                              //
        while (sum >> 16)
                sum = (sum & 0xFFFF) + (sum >> 16);

        // Return the one's complement of sum                           //
        return ( (uint16_t)(~sum)  );
}

int main(int argc, char* argv[]) {
    if (argc != 1) {
        fprintf(stderr, "Usage seqpackettool start -- ./udpnat -- start -- ./ipvampire tun0 -4 192.168.40.2/32 --\n");
        return 1;
    }
    
    int ret;
    for (;;) {
        
        ret = read(0, buf, BS);
        
        struct ip* ip = (struct ip*) buf;
        struct udphdr* udp = (struct udphdr*) (buf + ip->ip_hl * 4);
        unsigned char* data = ((unsigned char*)udp) + sizeof(struct udphdr);
        
        {
            char srcip[60];
            char dstip[60];
            inet_ntop(AF_INET, &ip->ip_src, srcip, sizeof srcip);
            inet_ntop(AF_INET, &ip->ip_dst, dstip, sizeof dstip);
        
            fprintf(stderr, "%s->%s %d %d->%d\n", srcip, dstip, ip->ip_p, ntohs(udp->uh_sport), ntohs(udp->uh_dport));
        }
        
        {
            struct in_addr tmp;
            tmp = ip->ip_src;
            ip->ip_src = ip->ip_dst;
            ip->ip_dst = tmp;
        }
        {
            uint16_t tmp;
            tmp = udp->uh_sport;
            udp->uh_sport = udp->uh_dport;
            udp->uh_dport = tmp;
        }
        udp->uh_sum = 0;//udp_checksum((const void*)udp, udp->uh_ulen, ip->ip_src.s_addr, ip->ip_dst.s_addr);
            
        write(1, buf, ret);
    }
    
    return 0;
}
