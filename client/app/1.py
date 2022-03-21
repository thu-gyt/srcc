import os
import time
import sys
os.system("sudo tc qdisc del dev eth0 root")
os.system("sudo tc qdisc add dev eth0 root handle 1:0 netem delay 50ms loss 9%")
os.system("sudo tc qdisc add dev eth0 parent 1:1 handle 100: tbf rate 25Mbit buffer 1000000 limit 1000000")
os.system("./appclient 10.211.55.5 9000 >> data/50_loss_9.txt &")
time.sleep(101)

# os.system("sudo tc qdisc replace dev eth0 parent 1:1 handle 100: tbf rate 50Mbit buffer 10000000 limit 10000000")
# time.sleep(30)

# os.system("sudo tc qdisc replace dev eth0 parent 1:1 handle 100: tbf rate 100Mbit buffer 10000000 limit 10000000")
# time.sleep(30)

# os.system("sudo tc qdisc replace dev eth0 parent 1:1 handle 100: tbf rate 50Mbit buffer 10000000 limit 10000000")
# time.sleep(30)

# os.system("sudo tc qdisc replace dev eth0 parent 1:1 handle 100: tbf rate 25Mbit buffer 10000000 limit 10000000")
# time.sleep(30)

str1 = os.popen("ps -aux | grep appclient").read()
num = str1.split()[1]
os.system(f"sudo kill -9 {num}")

