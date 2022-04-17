# server: 
- 安装虚拟网卡:  
``cd server/tunnel && sudo sh tun.sh``   
- 编译:   
    ``cd server && make``  
    ``cd tunnel && make``
- 运行:  
    ``tcptunnel: cd server/tunnel && ./tcpserver``  
    ``udptunnel: cd server/tunnel && ./udpserver``  
    ``udttunnel: cd server/app && ./udtserver``   
- iperf3测试:  
    ``iperf3 -s --bind 2.8.0.1``  


# client: 
- 安装虚拟网卡:  
``cd client/tunnel && sudo sh tun.sh``   
- 修改以下代码中的ip地址为server绑定的ip:    
    ``client/tunnel/tcp_client.c``  
    ``client/tunnel/udp_client.c``  
    ``client/app/udtclient.c`` 
- 编译:   
    ``cd client && make``  
    ``cd tunnel && make``
- 运行:  
    ``tcptunnel: cd client/tunnel && ./tcpclient``  
    ``udptunnel: cd client/tunnel && ./udpclient``  
    ``udttunnel: cd client/app && ./udtclient``   
- iperf3测试:  
    ``iperf3 -c 2.8.0.1``  