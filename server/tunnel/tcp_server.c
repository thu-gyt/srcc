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

int tun_alloc(char *, int);


/**************************************************************************
 * read_n: ensures we read exactly n bytes, and puts them into "buf".     *
 *         (unless EOF, of course)                                        *
 **************************************************************************/
int read_n(int fd, char *buf, int n)
{

    int nread, left = n;

    while (left > 0)
    {
        if ((nread = read(fd, buf, left)) == 0)
        {
            return 0;
        }
        else
        {
            left -= nread;
            buf += nread;
        }
    }
    return n;
}


int main(int argc, char *argv[])
{
    char if_name[IFNAMSIZ] = "tun1";
    unsigned short int port = 55555;
    int flags = IFF_TUN;

    int tun_fd = tun_alloc(if_name, flags | IFF_NO_PI);

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    int optval = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, sizeof(optval));

    struct sockaddr_in local, remote;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);
    bind(sock_fd, (struct sockaddr *)&local, sizeof(local));

    listen(sock_fd, 5);
    socklen_t len1 = sizeof(remote);
    memset(&remote, 0, sizeof(remote));
    int net_fd = accept(sock_fd, (struct sockaddr *)&remote, &len1);
    // int nRecvBuf=2000000;//设置为32K
    // setsockopt(net_fd,SOL_SOCKET,SO_RCVBUF,(const char*)&nRecvBuf,sizeof(int));
    // //发送缓冲区
    // int nSendBuf=2000000;//设置为32K
    // setsockopt(net_fd,SOL_SOCKET,SO_SNDBUF,(const char*)&nSendBuf,sizeof(int));

    printf("SERVER: Client connected from %s\n", inet_ntoa(remote.sin_addr));

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
            /* data from tun/tap: just read it and write it to the network */

            nread = read(tun_fd, buffer, BUFSIZE);

            tap2net++;

            /* write length + packet */
            plength = htons(nread);
            nwrite = write(net_fd, (char *)&plength, sizeof(plength));
            nwrite = write(net_fd, buffer, nread);

            //do_debug("TAP2NET %lu: Written %d bytes to the network\n", tap2net, nwrite);
        }

        if (FD_ISSET(net_fd, &rd_set))
        {
            /* Read length */
            nread = read_n(net_fd, (char *)&plength, sizeof(plength));
            if (nread == 0)
            {
                /* ctrl-c at the other end */
                break;
            }

            nread = read_n(net_fd, buffer, ntohs(plength));
            nwrite = write(tun_fd, buffer, nread);
        }
    }

    return 0;
}

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