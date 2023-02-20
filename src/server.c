#include "server.h"
#include "table.h"

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
    run("iptables -t filter -A FORWARD -i tun0 -o eth0 -j ACCEPT");
    run("iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE");
}

/*
 * Cleanup the iptables
 */
void cleanup_iptables()
{
    run("iptables -t filter -D FORWARD -i tun0 -o eth0 -j ACCEPT");
    run("iptables -t nat -D POSTROUTING -o eth0 -j MASQUERADE");
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
        // printf("I am free!!!\n");
        next = nat_table->next;
        free(nat_table);
        nat_table = next;
    }
    printf("Bye!!!\n");
    exit(0);
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

            // int len = packet_nat(&client_addr, udp_buf, OUT_NAT);
            // if (len <= 0)
            // {
            //     perror("totallen<=0");
            //     signal_handler(0);
            // }
            // int write_bytes = write(tun_fd, udp_buf, read_bytes);
            // if (write_bytes < 0)
            // {
            //     perror("Error while sending to tun_fd!!!");
            //     signal_handler(0);
            // }
            // continue;

            int hash_code = be32toh(*((int *)(udp_buf)));
            int data_size = be32toh(*((int *)(udp_buf + 4)));
            int symbol_size = be32toh(*((int *)(udp_buf + 8)));
            int k = be32toh(*((int *)(udp_buf + 12)));
            int n = be32toh(*((int *)(udp_buf + 16)));
            int index = be32toh(*((int *)(udp_buf + 20)));
            printf("hash_code=%d, data_size=%d, symbol_size=%d, k=%d, n=%d, index=%d\n", hash_code, data_size, symbol_size, k, n, index);
            struct dec_record *dec = dec_get(hash_code, data_size, symbol_size, k, n);

            if (dec_put(dec, index, udp_buf + 24))
            {
                // for (int i = 0; i < dec->block_num; i++)
                // {
                //     printf("%d: ", dec->marks[i]);
                //     for (int j = 0; j < dec->block_size; j++)
                //     {
                //         printf("%d%c", dec->data_blocks[i][j], j == dec->block_size - 1 ? '\n' : ',');
                //     }
                // }

                reed_solomon *rs = reed_solomon_new(dec->data_num, dec->block_num - dec->data_num);
                if (reed_solomon_reconstruct(rs, dec->data_blocks, dec->marks, dec->block_num, dec->block_size))
                {
                    printf("Decode Error!!!");
                    dec_remove(dec);
                    continue;
                }

                char *buf = (char *)malloc(k * symbol_size * sizeof(char));
                for (int i = 0; i < k; i++)
                {
                    memcpy(buf + i * symbol_size, dec->data_blocks[i], symbol_size);
                }
                int pos = 0;
                while (pos < data_size)
                {
                    int len = packet_nat(&client_addr, buf + pos, OUT_NAT);
                    if (len <= 0)
                    {
                        perror("Error: packet total_len=0!!!");
                        dec_remove(dec);
                        break;
                    }
                    printf("pos=%d Send Packet len=%d\n", pos, len);
                    int write_bytes = write(tun_fd, buf + pos, len);
                    if (write_bytes < 0)
                    {
                        perror("Error while sending to tun_fd!!!");
                    }
                    pos += len;
                }
                dec_remove(dec);
            }
        }
    }

    signal_handler(0);
    return 0;
}