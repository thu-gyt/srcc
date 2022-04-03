import socket# 客户端 发送一个数据，再接收一个数据
import time
import os
'''
sudo tc qdisc del dev enp5s0f1 root
sudo tc qdisc add dev enp5s0f1 root netem delay 50ms loss 3% rate 500Mbit limit 100000000
sudo tc qdisc ls dev enp5s0f1
'''


for rate in [1000]:
    for loss in [1, 3, 5, 10 ,15, 20]:
        os.system(f"sudo tc qdisc del dev enp5s0f1 root")
        os.system(f'sudo tc qdisc add dev enp5s0f1 root netem delay 50ms loss {loss}% rate {rate}Mbit limit 10000000000')
        os.system('sudo tc qdisc ls dev enp5s0f1')
        time.sleep(2)
        # client = socket.socket(socket.AF_INET,socket.SOCK_STREAM) #声明socket类型，同时生成链接对象
        # client.connect(('10.0.1.37',10001)) #建立一个链接，连接到本地的6969端口

        # num = int(input("set num:"))
        # str1 = ('a'*int(num*1e6)).encode('utf-8')
        # t1 = time.time()

        # client.send(str1)
        
        # print(f'time: {time.time() - t1} size: {8 * 1e9 / 1e6} Mb rate: {8 * num * 1e6  / (time.time() - t1) / 1e6}')
        # client.close() #关闭这个链接
        # print("-"*80)

        num = input("set num:")
        os.system(f"./../app/appclient 10.0.1.37 9000 {num}")
