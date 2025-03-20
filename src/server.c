#include "server.h"

int tun_fd, udp_fd, tcp_fd;

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
    printf("TUN %d is allocated with name: %s, ip: %s, mtu: %d\n", tun_fd, tun_name, tun_ip, mtu);
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
        perror("Error while creating udp socket!!!\n");
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
 * Creates a TCP socket and binds it to the specified IP and port.
 *
 * @param server_ip IP address to bind the TCP socket to
 * @param server_port Port to bind the TCP socket to
 * @return The file descriptor of the bound TCP socket
 */
int bind_tcp(char *server_ip, int server_port)
{
    // Create a socket for UDP communication
    int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd < 0)
    {
        perror("Error while creating tcp socket!!!\n");
        exit(1);
    }

    // Set SO_REUSEADDR option
    int yes = 1;
    if (setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    {
        perror("Error while setting socket option SO_REUSEADDR!!!\n");
        exit(1);
    }

    // Set up the server address information for binding
    struct sockaddr_in server_addr;

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    // Bind the socket to the server address
    if (bind(tcp_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        fprintf(stderr, "Error while binding to %s:%d!!!\n", server_ip, server_port);
        exit(1);
    }

    // Listen for incoming packets
    printf("TCP %d is binded on: %s:%d\n", tcp_fd, server_ip, server_port);
    return tcp_fd;
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

    clean_all_rx();
    cleanup_iptables();

    close(tun_fd);
    close(udp_fd);
    close(tcp_fd);

    printf("Bye!!!\n");
    exit(0);
}

int main(int argc, char *argv[])
{

    tun_fd = allocate_tun(TUN_NAME, TUN_IP, MTU);
    udp_fd = bind_udp(SERVER_IP, SERVER_PORT);
    tcp_fd = bind_tcp(SERVER_IP, SYNC_PORT);

    if (listen(tcp_fd, 5) < 0)
    {
        perror("listen");
        return 1;
    }

    setup_iptables();
    signal(SIGINT, signal_handler);

    unsigned char tun_buf[MTU], udp_buf[MTU], tcp_buf[MTU];
    bzero(tun_buf, MTU);
    bzero(udp_buf, MTU);

    struct sockaddr_in udp_addr;
    socklen_t udp_addr_len = sizeof(udp_addr);

    struct sockaddr_in tcp_addr;
    socklen_t tcp_addr_len = sizeof(tcp_addr);

    struct encoder *enc = NULL;

    get_gf_table();
    fec_init();
    pthread_mutex_init(&decoder_list_mutex, NULL);
    pthread_mutex_init(&rx_mutex, NULL);
    pthread_mutex_init(&tx_mutex, NULL);
    pthread_mutex_init(&ack_mutex, NULL);
    pthread_mutex_init(&parity_status_mutex, NULL);

    assert((pool = threadpool_create(THREAD, QUEUE, 0)) != NULL);
    fprintf(stderr, "Pool started with %d threads and "
                    "queue size of %d\n",
            THREAD, QUEUE);

    config.drop_rate = 0;
    config.data_num = 8;
    config.parity_num = 0;
    config.rx_num = 100;
    config.encode_timeout = 1000000;
    config.decode_timeout = 1000000;
    config.rx_timeout = 500000;
    config.ack_timeout = 200000;
    config.parity_delay_thres = 100000;
    config.parity_duration = 1000000;
    config.primary_probability = 0.8;
    config.mode = 0;
    enc = new_encoder();

    // threadpool_add(pool, (void *)monitor_encoder, (void *)&udp_fd, 0);
    threadpool_add(pool, (void *)monitor_decoder, NULL, 0);
    threadpool_add(pool, (void *)monitor_rx, (void *)&tun_fd, 0);
    threadpool_add(pool, (void *)monitor_ack, (void *)&udp_fd, 0);

    while (1)
    {
        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(tun_fd, &readset);
        FD_SET(udp_fd, &readset);
        FD_SET(tcp_fd, &readset);
        int max_fd = max(max(tun_fd, udp_fd), tcp_fd) + 1;

        if (-1 == select(max_fd, &readset, NULL, NULL, NULL))
        {
            perror("Error while selecting!!!");
            break;
        }

        /* Receive data from the client */
        if (FD_ISSET(tcp_fd, &readset))
        {
            int client_fd = accept(tcp_fd, (struct sockaddr *)&tcp_addr, &tcp_addr_len);

            if (client_fd < 0)
            {
                perror("Accept failed");
            }
            else
            {
                // 读取操作类型
                unsigned char operation_type;
                int bytes_read = read(client_fd, &operation_type, sizeof(operation_type));
                if (bytes_read <= 0)
                {
                    perror("Read failed");
                }
                else
                {
                    if (operation_type == TYPE_SYNC_CONFIG)
                    {
                        rx_time = 0;
                        rx_count = 0;
                        rx_max = -1;
                        rx_min = 1e18;
                        rx_timeout = 0;
                        enc_time = -1;
                        enc_max = -1;
                        enc_min = 1e18;
                        dec_time = -1;
                        dec_max = -1;
                        dec_min = 1e18;
                        rx_group_id = 0;
                        rx_index = 0;

                        data_send_pacekt_num = 0;
                        parity_send_packet_num = 0;
                        data_receive_packet_num = 0;
                        parity_receive_packet_num = 0;
                        repeat_receive_packet_num = 0;

                        printf("Get syncing signal!!!\n");
                        clean_all();

                        bytes_read = read(client_fd, tcp_buf, sizeof(tcp_buf) - 1);
                        if (bytes_read < 0)
                        {
                            perror("Read failed");
                        }
                        else
                        {
                            tcp_buf[bytes_read] = '\0';
                            printf("Syncing config: %s\n", tcp_buf);
                            parse_config(tcp_buf, &config);

                            printf("------------------------------------------\n");

                            char config_str[1024];
                            serialize_config(&config, config_str);

                            /* Send response */
                            const char *response = "200";
                            write(client_fd, response, strlen(response));
                            printf("Synced Config: %s\n", config_str);

                            enc = new_encoder();
                        }
                    }
                    else if (operation_type == TYPE_REQUEST_DATA)
                    {
                        printf("Get request data signal!!!\n");
                        char result[1024];
                        snprintf(result, sizeof(result), "{\"rx_rate\":%f,\"rx_count\":%llu,\"rx_total\":%llu,\"timeout_rate\":%f,\"rx_time\":%f,\"rx_min\":%f,\"rx_max\":%f}", rx_rate, rx_count, rx_total, timeout_rate, rx_time, rx_min, rx_max);
                        write(client_fd, result, strlen(result));
                        printf("%s\n", result);
                    }
                    else
                    {
                        printf("Unknown operation type!!!\n");
                    }
                }
            }
        }

        /* Receive data from the remote */
        if (FD_ISSET(tun_fd, &readset))
        {
            int read_bytes = read(tun_fd, tun_buf, MTU);
            if (read_bytes < 0)
            {
                perror("Error while reading tun_fd!!!\n");
                break;
            }

            // printf("TUN %d receive %d bytes\n", tun_fd, read_bytes);

            struct input_param *input_p = (struct input_param *)malloc(sizeof(struct input_param));
            input_p->packet = (unsigned char *)malloc(read_bytes * sizeof(unsigned char));
            memcpy(input_p->packet, tun_buf, read_bytes);
            input_p->packet_size = read_bytes;
            input_p->udp_fd = udp_fd;
            input_p->udp_addr.sin_family = udp_addr.sin_family;
            input_p->udp_addr.sin_addr.s_addr = udp_addr.sin_addr.s_addr;
            input_p->udp_addr.sin_port = udp_addr.sin_port;
            input_p->enc = enc;
            serve_input(input_p);
            // threadpool_add(pool, (void *)serve_input, (void *)input_p, 0);
        }

        /* Receive data from the client */
        if (FD_ISSET(udp_fd, &readset))
        {
            int read_bytes = recvfrom(udp_fd, udp_buf, sizeof(udp_buf), 0, (struct sockaddr *)&udp_addr, &udp_addr_len);
            if (read_bytes < 0)
            {
                perror("Error while reading udp_fd!!!\n");
                break;
            }

            // printf("UDP %d receive %d bytes from %s:%i\n", udp_fd, read_bytes, inet_ntoa(udp_addr.sin_addr), ntohs(udp_addr.sin_port));

            struct output_param *output_p = (struct output_param *)malloc(sizeof(struct output_param));
            output_p->packet = (unsigned char *)malloc(read_bytes * sizeof(unsigned char));
            memcpy(output_p->packet, udp_buf, read_bytes);
            output_p->hon_size = read_bytes;
            output_p->tun_fd = tun_fd;
            output_p->udp_fd = udp_fd;
            output_p->udp_addr.sin_family = udp_addr.sin_family;
            output_p->udp_addr.sin_addr.s_addr = udp_addr.sin_addr.s_addr;
            output_p->udp_addr.sin_port = udp_addr.sin_port;
            output_p->enc = enc;
            serve_output(output_p);
            // threadpool_add(pool, (void *)serve_output, (void *)output_p, 0);
            // pthread_create(&(output_p->tid), NULL, serve_output, (void *)output_p);
        }
    }

    signal_handler(0);
    assert(threadpool_destroy(pool, 0) == 0);

    return 0;
}