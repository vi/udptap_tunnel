udptap_tunnel
---

Poor man's OpenVPN.

```
Usage: udptap_tunnel [-6] <localip> <localport> <remotehost> <remoteport>
    Environment variables:
    TUN_DEVICE  /dev/net/tun
    DEV_NAME    name of the device, default tun0
    IFF_TUN     if set, uses point-to-point instead ot TAP.
    
    MCRYPT_KEYFILE  -- turn on encryption, read key from this file
    MCRYPT_KEYSIZE  -- key size in bits, default 256
    MCRYPT_ALGO     -- algorithm, default is twofish. aes256 is called rijndael-256
    MCRYPT_MODE     -- mode, default is CBC
```

IPv6 mode requires "-6" option as argv[1].


Note that each packet is encrypted separately and there is no filtering of invalid packets. Replay-based attacks will work.
Transferring some file using this tunnel's encryption feature is less secure than encrypting the file and then transferring it.

The project is named "udptap_tunnel" because of there is already other udptap on Github.

Usage example:

on server:

    IFF_TUN=1 DEV_NAME=udp0 MCRYPT_KEYFILE=/root/my.key udptap_tunnel 0.0.0.0 3443
    ip addr add 192.168.90.1/30 dev udp0
    ip link set udp0 up
    ip link set udp0 mtu 1280

on client:

    IFF_TUN=1 MCRYPT_KEYFILE=/root/my.key DEV_NAME=udp0 udptap_tunnel 0.0.0.0 0 1.2.3.4 3443
    ip link set udp0  up
    ip addr add 192.168.90.2/30 dev udp0
    ip link set ppp8 mtu 1280


tap_mcrypt
---

tap_mcrypt - create TAP device that sends/receives encrypted packets to interface
It uses EtherType 0x08F4.

```
Usage: tap_mcrypt plaintext_interface {auto|destination_mac_address}
Example: tap_mcrypt wlan0 auto
                (note that ff:ff:ff:ff:ff:ff may work bad in Wi-Fi)
    Environment variables:
    TUN_DEVICE  /dev/net/tun
    DEV_NAME    name of the device, default tun%d
    SOURCE_MAC_ADDRESS -- by default use interface's one
    IFF_TUN     if set, uses point-to-point instead ot TAP.
    
    MCRYPT_KEYFILE  -- turn on encryption, read key from this file
    MCRYPT_KEYSIZE  -- key size in bits, default 256
    MCRYPT_ALGO     -- algorithm, default is twofish. aes256 is called rijndael-256
    MCRYPT_MODE     -- mode, default is CBC
```


Note that each packet is encrypted separately and there is no filtering of invalid packets. Replay-based attacks will work.
Transferring some file using this tunnel's encryption feature is less secure than encrypting the file and then transferring it.

Networking with more than two peers (without locking to broadcast mode) is not implemented. 
Use https://github.com/vi/udptap_tunnel/issues/new if you want tap_mcrypt to have it.

vethify
---
```
Usage: vethify interface1 interface2
Example: vethify wlan0 veth0
    Environment variables:
    SOURCE_MAC_ADDRESS1 -- by default use interface's one
    SOURCE_MAC_ADDRESS2 -- by default use interface's one
    DEBUG=0,1,2 print send and recv packets
    
Primary use case: workaround inability to move wireless connections to other network namespaces on Linux
    Setup Wi-Fi connection on wlan0, but don't configure any addresses on it
    then create veth0/veth1 pair, use vethify to exchange packets between unmanagable wlan0 and manageable veth0
    then move veth1 to your namespace and setup IPv4/IPv6 addresses there
```

tap_copy
---
```
Usage: tap_copy interface
Example: tap_copy eth0
    Environment variables:
    TUN_DEVICE  /dev/net/tun
    DEV_NAME    name of the device, default tun%d
    SOURCE_MAC_ADDRESS -- by default use interface's one
    DEBUG=0,1,2 print send and recv packets
```

I actually don't remember why I needed this one.


seqpackettool
---

[socat][1] seems to lack support of AF_UNIX SOCK_SEQPACKET. So I implemented poor man's socat that allows you to connect and listen AF_UNIX SOCK_SEQPACKET sockets, exchanging data between them and SCTP or started processes. It can also start two processes, mutually (circularly) connected by a seqpacket socket pairt.

```
Usage: seqpackettool [options] part part
   part := listen_unix | connect_unix | listen_sctp | connect_sctp | startp | stdiop
   stdiop := '-' # use fd 0 for recv and fd 1 for send
   listen_unix := 'unix_listen' addressu
   connect_unix := 'connect' addressu
   listen_sctp := 'sctp_listen4' address4 port | 'sctp_listen6' address6 port
   connect_sctp := 'sctp_connect4' address4 port | 'sctp_connect6' address6 port
   startp := 'start' '--' full_path_to_program argv0 ... argvN '--'
   addressu - /path/to/unix/socket or @abstract_socket_address
   
   You may use more than two dashes in delimiters to allow '--' inside argv.
   BUFSIZE environment variable adjusts buffer size
Options:
   --listen-once - don't fork, accept only one connection
   --unidirectional - only recv from first  and send to second part
   --allow-empty - don't shutdown connection on empty packets
Examples:
    seqpackettool sctp4_listen 0.0.0.0 6655 sctp4_listen 127.0.0.1 5566
       emulate   socat sctp-listen:6655,fork,reuseaddr sctp-listen:5566,reuseaddr,bind=127.0.0.1
    seqpackettool unix_listen @myprog start -- /usr/bin/my_program my_program arg1 arg2 --
       for each incoming connection at abstract address 'myprog',
       start the program and provide the seqpacket socket as stdin/stdout
    seqpackettool - unix_connect /path/to/socket
       exchange data between stdin/stdout and AF_UNIX seqpacket socket
```
[1]:http://www.dest-unreach.org/socat/

ipvampire
---

Sniff the given interface for IPv4 or IPv6 packets belonging to the given subnet and copy them to stdout.
Inject packets appearing on stdin to the interface back.

Use case: OpenVPN-ing a part of networking out of restrictive VPN (such as OpenVZ without tun/tap enabled).

```
Usage: ipvampire interface {-6|-4} ip_address/netmask
Example: ipvampire venet0 -6 2a01:4f8:162:732f:419::acbd/84
    Environment variables:
    SOURCE_MAC_ADDRESS1 -- by default use interface's one
    DEBUG=0,1,2 print send and recv packets
    
Implemented by Vitaly "_Vi" Shukela based on Robert M Supnik's code
Example:
    ./seqpackettool start -- ./openvpn openvpn --dev stdout --log /dev/stderr -- start -- ./ipvampire ./ipvampire venet0 -6 2a01:4f8:162:732f:419:8000::/88 --
```

You can find the patched OpenVPN at https://github.com/vi/openvpn/tree/stdout


udpjump
---

Allow OpenVPN to jump from UDP socket to UDP socket, avoiding long-lived associations.
A workaround against problems in a particular network.

```
Usage: udpjump {listen|connect|listen6|connect6} address post {listen|connect|listen6|connect6} multiport_address multiport_port1 multiport_port2 ... multiport_portN
Environment variables:
   MINISESSION_TIMEOUT_MS=3000 (milliseconds)
Example:
on server: udpjump connect 127.0.0.1 1194 listen 0.0.0.0 {4000.4039}
on client: udpjump listen  127.0.0.1 1195 connect 1.2.3.4 {4000.4039}
```

mapopentounixsocket
---

Sometimes usual pipelines are not enough. 

```
source | sink
```

For example, you may want to pre-buffer some content, then start the sink. Or you may want to start the sink later.

```
# echo 100000000 >  /proc/sys/net/core/wmem_max
$ socketpair_dispenser $((1000*1000*50)) /tmp/123
$ LD_PRELOAD=libmapopentounixsocket.so MAPOPENTOUNIXSOCKET=/tmp/123 sh -c 'source > /tmp/somefile.sock'
$ LD_PRELOAD=libmapopentounixsocket.so MAPOPENTOUNIXSOCKET=/tmp/123 sink /tmp/somefile.sock
```

The source can start filling the 50-megabyte buffer even before sink is started.


buffered_pipeline
---

Like Shell's pipeline, but with AF_UNIX socketpair instead of pipe2's FIFO, with adjustable buffer size.


```
$ ./buffered_pipeline ! pv -i 10 -c -N 1 /dev/zero ! $((20*1000*1000)) ! pv -i 10 -L 100k -c -N 2 ! > /dev/zero
        1: 13.4MB 0:00:40 [ 103kB/s] [         <=>          ]
        2: 3.91MB 0:00:40 [ 100kB/s] [         <=>          ]

```
