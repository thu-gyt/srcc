#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_tun.h>
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

#define BUFSIZE 20000
#define PORT 55555

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

int main(int argc, char *argv[])
{
    char if_name[IFNAMSIZ] = "tun2";
    int flags = IFF_TUN;
    int tun_fd = tun_alloc(if_name, flags | IFF_NO_PI);

    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    int nRecvBuf=40000000;
    setsockopt(sock_fd,SOL_SOCKET,SO_RCVBUF,(const char*)&nRecvBuf,sizeof(int));
    // //发送缓冲区
    int nSendBuf=40000000;
    setsockopt(sock_fd,SOL_SOCKET,SO_SNDBUF,(const char*)&nSendBuf,sizeof(int));
    int net_fd = sock_fd;
    //创建网络通信对象
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    unsigned short int port = 55555;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("202.112.237.37");


    int maxfd = (tun_fd > net_fd) ? tun_fd : net_fd;
    uint16_t nread, nwrite, plength;
    unsigned long int tap2net = 0, net2tap = 0;
    char buffer[BUFSIZE];

    while (1)
    {
        int ret;
        fd_set rd_set;

        FD_ZERO(&rd_set);
        FD_SET(tun_fd, &rd_set);
        FD_SET(net_fd, &rd_set);

        ret = select(maxfd + 1, &rd_set, NULL, NULL, NULL);

        if (ret < 0 && errno == EINTR)
        {
            continue;
        }

        if (ret < 0)

        {
            perror("select()");
            exit(1);
        }

        if (FD_ISSET(tun_fd, &rd_set))
        {
            // tun ------> socket

            int read_bytes = read(tun_fd, buffer, BUFSIZE);
            if (read_bytes < 0)
				printf("tun_io - read() failed");
            int wrote_bytes = sendto(sock_fd, buffer, read_bytes, 0, (struct sockaddr *)&addr, sizeof(addr));
            if (wrote_bytes < read_bytes)
				printf("write on socket failed");	
			printf("Read %d bytes on tun and wrote %d on scoket\n", read_bytes, wrote_bytes);

        }

        if (FD_ISSET(net_fd, &rd_set))
        {
            // socket ----> tun
            int read_bytes = recvfrom(sock_fd, buffer, BUFSIZE, 0, NULL, NULL);
            int wrote_bytes = write(tun_fd, buffer, read_bytes);
			if (wrote_bytes < read_bytes)
				printf("write() failed on tun");
			printf("Read %d bytes on socket and wrote %d on tun\n",read_bytes, wrote_bytes);
        }
    }
    return 0;
}