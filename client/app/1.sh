sudo tc qdisc del dev enp5s0f1 root
sudo tc qdisc add dev enp5s0f1 root netem delay 50ms loss 0% rate 900Mbit limit 10000000000
# sudo tc qdisc add dev enp5s0f1 parent 1:1 handle 100: tbf rate 400Mbit burst 1000000000 latency 10000ms
sudo tc qdisc ls dev enp5s0f1
# iperf3 -c 10.211.55.5 -t 60 &
# sleep 10
# sudo tc qdisc replace dev eth0 parent 1:1 handle 100: tbf rate 50Mbit buffer 1000000 limit 1000000
# sleep 10
# sudo tc qdisc replace dev eth0 parent 1:1 handle 100: tbf rate 100Mbit buffer 1000000 limit 1000000
# sleep 20
# sudo tc qdisc replace dev eth0 parent 1:1 handle 100: tbf rate 50Mbit buffer 1000000 limit 1000000
# sleep 10
# sudo tc qdisc replace dev eth0 parent 1:1 handle 100: tbf rate 25Mbit buffer 1000000 limit 1000000


# sudo wondershaper clear enp5s0f1
# sudo wondershaper enp5s0f1 300000 300000


# tc qdisc add dev enp5s0f1 root tbf rate 50Mbit latency 50ms burst 1000
# sudo 