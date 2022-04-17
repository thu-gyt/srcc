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
#include "bbr2.h"

using namespace std;
#define BUFSIZE 20000
#define PORT 55555

void *monitor(void *s)
{
    UDTSOCKET u = *(UDTSOCKET *)s;
    UDT::TRACEINFO perf;
    freopen("out.txt", "a", stdout); 
    cout << "SendRate(Mb/s)\tRTT(ms)\tCWnd\tPktSndPeriod(us)\tRecvACK\tRecvNAK" << endl;
    while (true)
    {
        sleep(1);
        if (UDT::ERROR == UDT::perfmon(u, &perf))
        {
            cout << "perfmon: " << UDT::getlasterror().getErrorMessage() << endl;
            break;
        }
        cout << perf.mbpsSendRate << "\t\t"
             << perf.msRTT << "\t"
             << perf.pktCongestionWindow << "\t"
             << perf.usPktSndPeriod << "\t\t\t"
             << perf.pktRecvACK << "\t"
             << perf.pktRecvNAK << endl;
    }
    return NULL;
}

void *tun_sock(void *args);
void *sock_tun(void *args);
struct thread_args
{
    int tun_fd; /* Device file descriptor */
    int sock_fd;
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

int main()
{
    int tun_fd;
    UDTSOCKET net_fd;
    char if_name[IFNAMSIZ] = "tun2";
    unsigned short int port = 55555;
    int flags = IFF_TUN;
    tun_fd = tun_alloc(if_name, flags | IFF_NO_PI);
    cout << "tun_alloc success" << endl;

    UDT::startup();
    struct addrinfo hints, *local, *peer;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (0 != getaddrinfo(NULL, "9000", &hints, &local))
    {
        cout << "incorrect network address.\n"
             << endl;
        return 0;
    }
    UDTSOCKET client = UDT::socket(local->ai_family, local->ai_socktype, local->ai_protocol);

    UDT::setsockopt(client, 0, UDT_CC, new CCCFactory<CBBR2>, sizeof(CCCFactory<CBBR2>));
    //UDT::setsockopt(client, 0, UDT_MSS, new int(9000), sizeof(int));
    UDT::setsockopt(client, 0, UDT_SNDBUF, new int(400000000), sizeof(int));
    UDT::setsockopt(client, 0, UDP_SNDBUF, new int(400000000), sizeof(int));

    freeaddrinfo(local);
    getaddrinfo("202.112.237.37", "55555", &hints, &peer);
    // connect to the server, implict bind
    UDT::connect(client, peer->ai_addr, peer->ai_addrlen);
    freeaddrinfo(peer);

    net_fd = client;
    int maxfd = (tun_fd > net_fd) ? tun_fd : net_fd;
    cout << tun_fd << " " << net_fd << " " << maxfd << endl;

    pthread_t t2n, n2t;
    int ret1, ret2;

    struct thread_args tun_to_net, net_to_tun;

    net_to_tun.tun_fd = tun_fd;
    net_to_tun.sock_fd = net_fd;

    tun_to_net.tun_fd = tun_fd;
    tun_to_net.sock_fd = net_fd;

    printf("Starting the tunnelling threads\n");
    /* spawn the two threads */
    pthread_create(new pthread_t, NULL, monitor, &client);
    ret1 = pthread_create(&t2n, NULL, tun_sock, (void *)&tun_to_net);
    ret2 = pthread_create(&n2t, NULL, sock_tun, (void *)&net_to_tun);
    pthread_join(t2n, NULL);
    printf("Thread tun-to-network returned %d\n", ret1);
    pthread_join(n2t, NULL);
    printf("Thread network-to-tun returned %d\n", ret2);

    return 0;
}

void *tun_sock(void *ptr)
{

    //  tun ----------> socket
    struct thread_args *args;
    args = (struct thread_args *)ptr;

    /* Extract the arguments from structure */
    int tun_fd = args->tun_fd;
    int sock_fd = args->sock_fd;
    char buff[BUFSIZE];
    int stat;
    int len = 0;

    fd_set r_set;

    printf("Thread %ld: Starting operations on tun\n", pthread_self());
    while (1)
    {

        /* Wait till tun device is ready to be read, if it is, read the data
		into a buffer */
        FD_ZERO(&r_set);
        FD_SET(tun_fd, &r_set);
        stat = select(tun_fd + 1, &r_set, NULL, NULL, NULL);

        if (stat < 0 && errno == EINTR)
            continue;

        if (stat < 0)
            printf("select() failed");

        if (FD_ISSET(tun_fd, &r_set))
        {
            char buffer[BUFSIZE];
            bzero(buffer, BUFSIZE);
            int read_bytes = read(tun_fd, buffer, BUFSIZE);
            if (read_bytes < 0)
                printf("tun_io - read() failed");
            int wrote_bytes = UDT::sendmsg(sock_fd, buffer, read_bytes);
            //printf("Read %d bytes on tun and wrote %d on scoket\n", read_bytes, wrote_bytes);
        }
    }
}
void *sock_tun(void *ptr)
{
    struct thread_args *args;
    args = (struct thread_args *)ptr;

    /* Extract the arguments from the structure */
    int tun_fd = args->tun_fd;
    int sock_fd = args->sock_fd;
    char buff[BUFSIZE];
    printf("Thread %ld: Starting operations on socket\n", pthread_self());
    while (1)
    {
        int ret;
        ud_set rd_set;
        rd_set.clear();
        rd_set.insert(sock_fd);
        ret = UDT::select(sock_fd + 1, &rd_set, NULL, NULL, NULL);
        if (ret < 0)
        {
            continue;
        }
        if (rd_set.count(sock_fd))
        {
            // socket ----> tun
            char buffer[BUFSIZE];
            bzero(buffer, BUFSIZE);
            int rcv_size;
            int var_size = sizeof(int);
            UDT::getsockopt(sock_fd, 0, UDT_RCVDATA, &rcv_size, &var_size);
            int read_bytes = UDT::recvmsg(sock_fd, buffer, BUFSIZE);
            int wrote_bytes = write(tun_fd, buffer, read_bytes);
            if (wrote_bytes < read_bytes)
                printf("write() failed on tun");
            //printf("Read %d bytes on socket and wrote %d on tun\n",read_bytes, wrote_bytes);
        }
    }
}