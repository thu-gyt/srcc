sudo tc qdisc del dev enp5s0f1 root
sudo tc qdisc add dev enp5s0f1 root handle 1:0 netem delay 50ms loss 00%
sudo tc qdisc add dev enp5s0f1 parent 1:1 handle 100: tbf rate 50Mbit buffer 1000000 limit 1000000
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