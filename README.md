udptap_tunnel
---

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
