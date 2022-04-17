all : udpserver tcpserver

udpserver: udp_server.c
	gcc udp_server.c -o udpserver
tcpserver: tcp_server.c
	gcc tcp_server.c -o tcpserver

clean:
	rm udpserver tcpserver