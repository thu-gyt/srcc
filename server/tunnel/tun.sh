ip link del dev tun1 
ip tuntap add dev tun1 mod tun
ifconfig tun1 2.8.0.1 up
ifconfig tun1 txqueuelen 4000
route add -net 2.8.0.0 netmask 255.255.255.0 dev tun1