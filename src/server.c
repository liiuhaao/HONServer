#include "server.h"

/**
 * Create a TUN interface with the specified name, IP and MTU.
 *
 * @param tun_name Name of the TUN interface
 * @param tun_ip IP address to assign to the TUN interface
 * @param mtu MTU value for the TUN interface
 * @return File descriptor of the TUN interface
 */
int allocate_tun(char *tun_name, char *tun_ip, int mtu)
{
    // Create a tun
    int tun_fd = open("/dev/net/tun", O_RDWR);
    if (tun_fd < 0)
    {
        perror("Error while opening /dev/net/tun!!!");
        exit(1);
    }

    // Sets the specified information to the TUN interface
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;

    strncpy(ifr.ifr_name, TUN_NAME, IFNAMSIZ);

    if (ioctl(tun_fd, TUNSETIFF, &ifr) < 0)
    {
        close(tun_fd);
        fprintf(stderr, "Error while creating tun with TUN_NAME: %s!!!\n", tun_name);
        exit(1);
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "ifconfig tun0 %s/16 mtu %d up", tun_ip, mtu);
    run(cmd);
    return tun_fd;
}

/**
 * Creates a UDP socket and binds it to the specified IP and port.
 *
 * @param server_ip IP address to bind the UDP socket to
 * @param server_port Port to bind the UDP socket to
 * @return The file descriptor of the bound UDP socket
 */
int bind_udp(char *server_ip, int server_port)
{
    // Create a socket for UDP communication
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0)
    {
        perror("Error while creating socket!!!\n");
        exit(1);
    }

    // Set up the server address information for binding
    struct sockaddr_in server_addr;

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    // Bind the socket to the server address
    if (bind(udp_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        fprintf(stderr, "Error while binding to %s:%d!!!\n", server_ip, server_port);
        exit(1);
    }

    // Listen for incoming packets
    printf("UDP %d is binded on: %s:%d\n", udp_fd, server_ip, server_port);
    return udp_fd;
}

/**
 * Sets up iptables.
 *
 */
void setup_iptables()
{
    run("sysctl -w net.ipv4.ip_forward=1");

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "iptables -t filter -A FORWARD -i %s -o %s -j ACCEPT", TUN_NAME, INTERFACE_NAME);
    run(cmd);

    snprintf(cmd, sizeof(cmd), "iptables -t nat -A POSTROUTING -o %s -j MASQUERADE", INTERFACE_NAME);
    run(cmd);
}

/**
 * Cleanup the iptables.
 *
 */
void cleanup_iptables()
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "iptables -t filter -D FORWARD -i %s -o %s -j ACCEPT", TUN_NAME, INTERFACE_NAME);
    run(cmd);

    snprintf(cmd, sizeof(cmd), "iptables -t nat -D POSTROUTING -o %s -j MASQUERADE", INTERFACE_NAME);
    run(cmd);
}

/**
 * Execute commands
 *
 */
void run(char *cmd)
{
    printf("Execute `%s`\n", cmd);
    if (system(cmd))
    {
        perror(cmd);
        exit(1);
    }
}

/**
 * Handle signals received by our program.
 *
 * @param sig The received signal ID
 */
void signal_handler(int sig)
{
    printf("\n");
    cleanup_iptables();

    printf("Bye!!!\n");
    exit(0);
}

int main(int argc, char *argv[])
{

    int tun_fd = allocate_tun(TUN_NAME, TUN_IP, MTU);
    int udp_fd = bind_udp(SERVER_IP, SERVER_PORT);

    setup_iptables();
    signal(SIGINT, signal_handler);

    unsigned char tun_buf[MTU], udp_buf[MTU];
    bzero(tun_buf, MTU);
    bzero(udp_buf, MTU);

    struct sockaddr_in udp_addr;
    socklen_t client_addr_len = sizeof(udp_addr);

    fec_init();
    pthread_mutex_init(&group_list_mutex, NULL);
    pthread_mutex_init(&rx_mutex, NULL);
    pthread_mutex_init(&tx_mutex, NULL);

    threadpool_t *pool;
    assert((pool = threadpool_create(THREAD, QUEUE, 0)) != NULL);
    fprintf(stderr, "Pool started with %d threads and "
                    "queue size of %d\n",
            THREAD, QUEUE);

    while (1)
    {
        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(tun_fd, &readset);
        FD_SET(udp_fd, &readset);
        int max_fd = max(tun_fd, udp_fd) + 1;

        if (-1 == select(max_fd, &readset, NULL, NULL, NULL))
        {
            perror("Error while selecting!!!");
            break;
        }

        // Receive data from the remote
        if (FD_ISSET(tun_fd, &readset))
        {
            int read_bytes = read(tun_fd, tun_buf, MTU);
            if (read_bytes < 0)
            {
                perror("Error while reading tun_fd!!!\n");
                break;
            }

            // printf("TUN received %d bytes\n", read_bytes);

            struct input_param *input_p = (struct input_param *)malloc(sizeof(struct input_param));
            input_p->packet = (unsigned char *)malloc(read_bytes * sizeof(unsigned char));
            memcpy(input_p->packet, tun_buf, read_bytes);
            input_p->packet_size = read_bytes;
            input_p->udp_fd = udp_fd;
            threadpool_add(pool, (void *)serve_input, (void *)input_p, 0);
        }

        // Receive data from the client
        if (FD_ISSET(udp_fd, &readset))
        {
            int read_bytes = recvfrom(udp_fd, udp_buf, sizeof(udp_buf), 0, (struct sockaddr *)&udp_addr, &client_addr_len);
            if (read_bytes < 0)
            {
                perror("Error while reading udp_fd!!!\n");
                break;
            }

            printf("UDP received %d bytes from %s:%i\n", read_bytes, inet_ntoa(udp_addr.sin_addr), ntohs(udp_addr.sin_port));
            
            struct output_param *output_p = (struct output_param *)malloc(sizeof(struct output_param));
            output_p->packet = (unsigned char *)malloc(read_bytes * sizeof(unsigned char));
            memcpy(output_p->packet, udp_buf, read_bytes);
            output_p->packet_size = read_bytes;
            output_p->tun_fd = tun_fd;
            output_p->udp_fd = udp_fd;
            output_p->udp_addr.sin_family = udp_addr.sin_family;
            output_p->udp_addr.sin_addr.s_addr = udp_addr.sin_addr.s_addr;
            output_p->udp_addr.sin_port = udp_addr.sin_port;

            threadpool_add(pool, (void *)serve_output, (void *)output_p, 0);
            // pthread_create(&(output_p->tid), NULL, serve_output, (void *)output_p);
        }
    }

    signal_handler(0);
    assert(threadpool_destroy(pool, 0) == 0);

    return 0;
}