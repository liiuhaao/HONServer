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
    run("iptables -t filter -A FORWARD -i tun0 -o eno1 -j ACCEPT");
    run("iptables -t nat -A POSTROUTING -o eno1 -j MASQUERADE");
}

/*
 * Cleanup the iptables
 */
void cleanup_iptables()
{
    run("iptables -t filter -D FORWARD -i tun0 -o eno1 -j ACCEPT");
    run("iptables -t nat -D POSTROUTING -o eno1 -j MASQUERADE");
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
    cleanup_iptables();
    printf("\n");
    struct table_record *next;
    while (table)
    {
        // printf("I am free!!!\n");
        next = table->next;
        free(table);
        table = next;
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

            if (packet_nat(&client_addr, tun_buf, IN_TABLE))
            {
                // TODO: Encode
                // TODO: Choose udp_fd
                int write_bytes = sendto(udp_fd, tun_buf, read_bytes, 0, (const struct sockaddr *)&client_addr, client_addr_len);
                printf("Send %d bytes to %s:%i\n", write_bytes, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
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

            printf("Receive %d bytes from %s:%i\n", read_bytes,
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

            // TODO: Decode
            // TODO: Aggregate
            if (packet_nat(&client_addr, udp_buf, OUT_TABLE))
            {
                int write_bytes = write(tun_fd, udp_buf, read_bytes);
                if (write_bytes < 0)
                {
                    perror("Error while sending to tun_fd!!!");
                    break;
                }
            }
        }
    }

    close(tun_fd);
    close(udp_fd);

    return 0;
}