ip link del dev tun2 
ip tuntap add dev tun2 mod tun
ifconfig tun2 2.8.0.2 up
ifconfig tun2 txqueuelen 4000
route add -net 2.8.0.0 netmask 255.255.255.0 dev tun2