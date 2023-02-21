#include "server.h"

/**
 * Create a TUN interface with the specified name, IP and MTU
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
 * Creates a UDP socket and binds it to the specified IP and port
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

/*
 * Sets up iptables
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

/*
 * Cleanup the iptables
 */
void cleanup_iptables()
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "iptables -t filter -D FORWARD -i %s -o %s -j ACCEPT", TUN_NAME, INTERFACE_NAME);
    run(cmd);

    snprintf(cmd, sizeof(cmd), "iptables -t nat -D POSTROUTING -o %s -j MASQUERADE", INTERFACE_NAME);
    run(cmd);
}

/*
 * Execute commands
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
 * Handle signals received by our program
 *
 * @param sig The received signal ID
 */
void signal_handler(int sig)
{
    printf("\n");
    cleanup_iptables();
    struct nat_record *next;
    while (nat_table)
    {
        next = nat_table->next;
        free(nat_table);
        nat_table = next;
    }

    while (dec_table)
    {
        next = nat_table->next;
        free_dec(dec_table);
        nat_table = next;
    }

    printf("Bye!!!\n");
    exit(0);
}

void *encode(void *args)
{
    int ret = 0;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    while (1)
    {
    }
}

int main(int argc, char *argv[])
{

    int tun_fd = allocate_tun(TUN_NAME, TUN_IP, MTU);
    int udp_fd = bind_udp(SERVER_IP, SERVER_PORT);

    setup_iptables();
    signal(SIGINT, signal_handler);

    char tun_buf[MTU], udp_buf[MTU];
    bzero(tun_buf, MTU);
    bzero(udp_buf, MTU);

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    fec_init();

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
                perror("Error while reading tun_fd!!!");
                break;
            }
            // printf("TUN %d recieved %d bytes\n", tun_fd, read_bytes);

            if (packet_nat(&client_addr, tun_buf, IN_NAT))
            {
                // TODO: Encode
                // TODO: Choose udp_fd
                int write_bytes = sendto(udp_fd, tun_buf, read_bytes, 0, (const struct sockaddr *)&client_addr, client_addr_len);
                printf("\nSend %d bytes to %s:%i\n", write_bytes, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                if (write_bytes < 0)
                {
                    perror("Error while sendding to udp_fd!!!");
                    break;
                }
            }
        }

        // Receive data from the client
        if (FD_ISSET(udp_fd, &readset))
        {

            int read_bytes = recvfrom(udp_fd, udp_buf, sizeof(udp_buf), 0, (struct sockaddr *)&client_addr, &client_addr_len);
            if (read_bytes < 0)
            {
                perror("Error while reading udp_fd!!!\n");
                break;
            }

            printf("\nReceive %d bytes from %s:%i\n", read_bytes, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

            int hash_code = be32toh(*((int *)(udp_buf)));
            int data_size = be32toh(*((int *)(udp_buf + 4)));
            int block_size = be32toh(*((int *)(udp_buf + 8)));
            int data_num = be32toh(*((int *)(udp_buf + 12)));
            int block_num = be32toh(*((int *)(udp_buf + 16)));
            int index = be32toh(*((int *)(udp_buf + 20)));
            printf("hash_code=%d, data_size=%d, block_size=%d, data_num=%d, block_num=%d, index=%d\n", hash_code, data_size, block_size, data_num, block_num, index);
            struct dec_record *dec = dec_get(hash_code, data_size, block_size, data_num, block_num);

            struct dec_param *dec_p = (struct dec_param *)malloc(sizeof(struct dec_param));

            if (dec_put(dec, index, udp_buf + 24))
            {
                dec_p->dec = dec;
                dec_p->tun_fd = tun_fd;
                dec_p->clinet_vpn_ip = client_addr.sin_addr.s_addr;
                dec_p->clinet_vpn_port = client_addr.sin_port;
                pthread_t dec_tid;
                pthread_create(&(dec_tid), NULL, decode, (void *)dec_p);
            }
        }
    }

    signal_handler(0);
    return 0;
}