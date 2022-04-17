#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <iostream>
#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <stdarg.h>
#include <pthread.h>

#include <udt.h>
#include "bbr.h"


using namespace std;
#define BUFSIZE 20000
#define PORT 55555

void* tun_sock(void* args);
void* sock_tun(void* args);
struct thread_args {
	int tun_fd;								/* Device file descriptor */
	int sock_fd;
};


struct UDTUpDown {
    UDTUpDown() {
        UDT::startup();
    }
    ~UDTUpDown() {
        UDT::cleanup();
    }
};

int tun_alloc(char *dev, int flags) //dev:网卡名字  flags:tun/tap
{
    struct ifreq ifr;
    int fd, err;
    char *clonedev = "/dev/net/tun";

    if ((fd = open(clonedev, O_RDWR)) < 0)
    {
        perror("Opening /dev/net/tun");
        return fd;
    }

    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_flags = flags;

    if (*dev)
    {
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    }

    if ((err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0)
    {
        perror("ioctl(TUNSETIFF)");
        close(fd);
        return err;
    }
    strcpy(dev, ifr.ifr_name);
    return fd; //返回tun的文件描述符
}


int main(){

    char if_name[IFNAMSIZ] = "tun1";
    int flags = IFF_TUN;
    int tun_fd = tun_alloc(if_name, flags | IFF_NO_PI);

    UDTUpDown _udt_;
    addrinfo hints;
    addrinfo* res;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    string service("55555");
    getaddrinfo(NULL, service.c_str(), &hints, &res);
    UDTSOCKET serv = UDT::socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    //UDT::setsockopt(serv, 0, UDT_CC, new CCCFactory<CUDPBlast>, sizeof(CCCFactory<CUDPBlast>));
    //UDT::setsockopt(serv, 0, UDT_MSS, new int(9000), sizeof(int)); 
    UDT::setsockopt(serv, 0, UDT_CC, new CCCFactory<CBBR>, sizeof(CCCFactory<CBBR>));

    UDT::bind(serv, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    UDT::listen(serv, 10);
    sockaddr_storage clientaddr;
    int addrlen = sizeof(clientaddr);
    UDTSOCKET net_fd = UDT::accept(serv, (sockaddr*) &clientaddr, &addrlen);
    char clienthost[NI_MAXHOST];
    char clientservice[NI_MAXSERV];
    getnameinfo((sockaddr *) &clientaddr, addrlen, clienthost, sizeof(clienthost), clientservice,
                sizeof(clientservice), NI_NUMERICHOST | NI_NUMERICSERV);
    cout << "new connection: " << clienthost << ":" << clientservice << endl;
    


    pthread_t t2n, n2t;
	int ret1, ret2;

    struct thread_args tun_to_net, net_to_tun;
	
	net_to_tun.tun_fd = tun_fd;
	net_to_tun.sock_fd = net_fd;

	tun_to_net.tun_fd = tun_fd;
	tun_to_net.sock_fd = net_fd;

	printf("Starting the tunnelling threads\n");
	/* spawn the two threads */
	ret1 = pthread_create( &t2n, NULL, tun_sock, (void *) &tun_to_net);
	ret2 = pthread_create( &n2t, NULL, sock_tun, (void *) &net_to_tun);
    pthread_join(t2n, NULL);
	printf("Thread tun-to-network returned %d\n",ret1);
    pthread_join(n2t, NULL);
	printf("Thread network-to-tun returned %d\n",ret2);

    UDT::close(net_fd);
    UDT::close(serv);

    return 0;
}



void* tun_sock(void* ptr){
    
    //  tun ----------> socket
	struct thread_args * args;
	args = (struct thread_args *) ptr;

	/* Extract the arguments from structure */
	int tun_fd = args->tun_fd;
	int sock_fd = args->sock_fd;
	char buff[BUFSIZE];
	int stat;
	int len = 0;

	fd_set r_set;

	printf("Thread %ld: Starting operations on tun\n",pthread_self());
	while (1) {
		
		/* Wait till tun device is ready to be read, if it is, read the data
		into a buffer */
		FD_ZERO(&r_set);
		FD_SET(tun_fd, &r_set);
		stat = select(tun_fd+1, &r_set, NULL, NULL, NULL);

		if (stat < 0 && errno == EINTR)
			continue;

		if (stat < 0)
			printf("select() failed");

		if (FD_ISSET(tun_fd, &r_set)) {
            char buffer1[BUFSIZE];
            bzero(buffer1, BUFSIZE);
            int read_bytes = read(tun_fd, buffer1, BUFSIZE);
            if (read_bytes < 0)
				printf("tun_io - read() failed");
            int wrote_bytes = UDT::sendmsg(sock_fd, buffer1, read_bytes);
			//printf("Read %d bytes on tun and wrote %d on scoket\n", read_bytes, wrote_bytes);
        }
    }

}
void* sock_tun(void* ptr){
	struct thread_args * args;
	args = (struct thread_args *) ptr;

	/* Extract the arguments from the structure */
	int tun_fd = args->tun_fd;
	int sock_fd = args->sock_fd;
	char buff[BUFSIZE];
	printf("Thread %ld: Starting operations on socket\n",pthread_self());
    while(1){
        int ret;
        ud_set rd_set;
        rd_set.clear();
        rd_set.insert(sock_fd);
        ret = UDT::select(sock_fd+1,&rd_set,NULL,NULL,NULL);
        if(ret < 0){
            continue;
        }
        if (rd_set.count(sock_fd))
        {
            // socket ----> tun
            char buffer2[BUFSIZE];
            bzero(buffer2, BUFSIZE);

            int rcv_size;
            int var_size = sizeof(int);
            UDT::getsockopt(sock_fd, 0, UDT_RCVDATA, &rcv_size, &var_size);
            int read_bytes = UDT::recvmsg(sock_fd, buffer2, BUFSIZE);
            if (read_bytes < 0)
                printf("recvfrom() failed");
			int wrote_bytes = write(tun_fd, buffer2, read_bytes);
            //printf("Read %d bytes on socket and wrote %d on tun\n",read_bytes, wrote_bytes);
        }

    }
}