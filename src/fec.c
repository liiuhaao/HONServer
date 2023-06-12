#include "fec.h"

struct list *group_list = NULL;
pthread_mutex_t group_list_mutex;
struct list *group_iter = NULL;

/**
 * Serve incoming input packets from the TUN interface.
 *
 * @param args: Void pointer to input_param struct containing packet data and client information
 */
void *serve_input(void *args)
{
    /* Get the input parameters */
    struct input_param *param = (struct input_param *)args;
    unsigned char *packet = param->packet;
    unsigned int packet_size = param->packet_size;
    int udp_fd = param->udp_fd;
    free(param);

    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;

    /* Get the target address of the packet */
    struct address *target_addr = get_packet_addr(packet, INPUT);

    /* Get the group of the target address */
    pthread_mutex_lock(&group_list_mutex);
    struct group *group = get_group(NULL, target_addr, udp_fd);
    /* Get the encoder of the group */
    struct encoder *enc = group->enc;
    pthread_mutex_lock(&(enc->mutex));
    pthread_mutex_unlock(&group_list_mutex);

    /* If the encoder buffer is not full, add the packet to the buffer */
    if ((packet_size + enc->data_size) < MAX_PACKET_BUF)
    {
        /* Add the packet to the encoder buffer */
        memcpy(enc->packet_buf + enc->data_size, packet, packet_size);
        free(packet);
        enc->data_size += packet_size;
        enc->packet_num += 1;

        /* If packet number reaches the maximum, encode the packets and send them over the network*/
        if (enc->packet_num >= MAX_PACKET_NUM)
        {
            struct enc_param *enc_p = (struct enc_param *)malloc(sizeof(struct enc_param));
            enc_p->enc = enc;
            enc_p->udp_fd = udp_fd;
            enc_p->udp_addrs = group->udp_addrs;
            encode((void *)enc_p);
        }
    }
    else
    {
        /* The encoder buffer is full, encode the packets and send them over the network */
        struct enc_param *enc_p = (struct enc_param *)malloc(sizeof(struct enc_param));
        enc_p->enc = enc;
        enc_p->udp_fd = udp_fd;
        enc_p->udp_addrs = group->udp_addrs;
        encode((void *)enc_p);

        /* Add the packet to the encoder buffer */
        memcpy(enc->packet_buf, packet, packet_size);
        free(packet);
        enc->data_size += packet_size;
        enc->packet_num += 1;
    }
    pthread_mutex_unlock(&(enc->mutex));
}

/**
 * Serve incoming output packets from the UDP socket.
 *
 * @param args: Void pointer to output_param struct containing packet data and client information
 */
void *serve_output(void *args) // client to server
{
    /* Get the output parameters */
    struct output_param *param = (struct output_param *)args;
    unsigned char *packet = param->packet;
    unsigned int packet_size = param->packet_size;
    int tun_fd = param->tun_fd;
    in_addr_t udp_ip = param->udp_ip;
    in_port_t udp_port = param->udp_port;
    free(param);

    /* Parse the HON Header */
    unsigned int groupID = be32toh(*((int *)(packet)));
    unsigned int data_size = be32toh(*((int *)(packet + 4)));
    unsigned int block_size = be32toh(*((int *)(packet + 8)));
    unsigned int data_num = be32toh(*((int *)(packet + 12)));
    unsigned int block_num = be32toh(*((int *)(packet + 16)));
    unsigned int index = be32toh(*((int *)(packet + 20)));

    /* Get the source address */
    struct address *source_addr = get_packet_addr(packet + 24, OUTPUT);

    /* Get the group by groupID */
    pthread_mutex_lock(&group_list_mutex);
    struct group *group = get_group(groupID, NULL, -1);
    /* Create a new group if it does not exist */
    if (group == NULL)
    {
        group = new_group(groupID, data_size, block_size, data_num, block_num, udp_ip, udp_port, source_addr);
    }
    /* Get the decoder */
    struct decoder *dec = group->dec;
    pthread_mutex_lock(&(dec->mutex));
    pthread_mutex_unlock(&group_list_mutex);

    /* Check if the packet is received */
    if ((dec->marks[index]))
    {
        dec->receive_num++;
        dec->marks[index] = 0;
        clock_gettime(CLOCK_REALTIME, &(dec->touch));
        memcpy(dec->data_blocks[index], packet + 24, dec->block_size);
        free(packet);

        /* Decode the data blocks if enough blocks are received */
        if (dec->receive_num == dec->data_num)
        {
            struct dec_param *dec_p = (struct dec_param *)malloc(sizeof(struct dec_param));
            dec_p->dec = dec;
            dec_p->tun_fd = tun_fd;
            decode(dec_p);
        }
    }
    pthread_mutex_unlock(&(dec->mutex));
}

/**
 * Encode the packets and sends them over the network using the UDP file descriptor.
 *
 * @param args: Void pointer to the enc_param struct containing necessary encoding parameters
 */
void *encode(void *args)
{
    /* Get the encoding parameters */
    struct enc_param *enc_p = (struct enc_param *)args;
    struct encoder *enc = enc_p->enc;
    struct list *udp_addrs = enc_p->udp_addrs;
    int udp_fd = enc_p->udp_fd;
    free(enc_p);

    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    socklen_t client_addr_len = sizeof(client_addr);

    /* Determine the number of blocks and the size of each block */
    unsigned int data_size = enc->data_size;
    unsigned int data_num = enc->packet_num;
    unsigned int block_size = (data_size + data_num - 1) / data_num;

    /* Determine the number of parity blocks */
    unsigned int block_num = data_num;
    srand(enc->touch.tv_nsec);
    for (unsigned int i = 0; i < data_num; i++)
    {
        if (rand() % 100 + 1 < PARITY_RATE)
        {
            block_num++;
        }
    }

    /* Construct the data blocks */
    unsigned char arr[block_num][block_size];
    memset(arr, 0, sizeof(arr));
    unsigned char *data_blocks[block_num];
    for (int i = 0; i < block_num; i++)
    {
        if (i < data_num)
        {
            memcpy(arr[i], enc->packet_buf + i * block_size, block_size);
        }
        data_blocks[i] = arr[i];
    }

    /* Encode the data blocks */
    if (block_num > data_num)
    {
        reed_solomon *rs = reed_solomon_new(data_num, block_num - data_num);
        reed_solomon_encode2(rs, data_blocks, block_num, block_size);
    }

    unsigned char *buffer = (unsigned char *)malloc((24 + block_size) * sizeof(unsigned char));
    unsigned int groupID = get_random_groupID();
    // printf("TIMEOUT: groupID=%d, data_size=%d, block_size=%d, data_num=%d, block_num=%d paraty_rate=%f\n", groupID, data_size, block_size, data_num, block_num, rate);

    /* Select UDP address for sending */ /* TODO: Update the algorithm */
    struct list *udp_record = udp_addrs;

    /* Send all data blocks over the network */
    for (unsigned int index = 0; index < block_num; index++)
    {
        if (udp_record == NULL)
        {
            udp_record = udp_addrs;
        }

        /* Construct the HON Header */
        *((unsigned int *)(buffer)) = htobe32(groupID);
        *((unsigned int *)(buffer + 4)) = htobe32(data_size);
        *((unsigned int *)(buffer + 8)) = htobe32(block_size);
        *((unsigned int *)(buffer + 12)) = htobe32(data_num);
        *((unsigned int *)(buffer + 16)) = htobe32(block_num);
        *((unsigned int *)(buffer + 20)) = htobe32(index);

        /* Copy the data block to the buffer */
        memcpy(buffer + 24, data_blocks[index], block_size);

        /* Select UDP address for sending */
        struct address *addr = (struct address *)(udp_record->data);
        client_addr.sin_addr.s_addr = addr->ip;
        client_addr.sin_port = addr->port;

        /* Send the data block over the network */
        int write_bytes = sendto(udp_fd, buffer, block_size + 24, 0, (const struct sockaddr *)&client_addr, client_addr_len);
        // printf("Send %d bytes to %s:%i\n", write_bytes, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        if (write_bytes < 0)
        {
            perror("Error while sendding to udp_fd!!!");
            break;
        }
        udp_record = udp_record->next;
    }

    /* Reset the encoder */
    enc->packet_num = 0;
    enc->data_size = 0;
}

/**
 * Decode the received packets.
 *
 * @param args: Void pointer to dec_param struct containing decoder and tunnel fd
 */
void *decode(void *args)
{
    /* Get the decoder parameters */
    struct dec_param *dec_p = (struct dec_param *)args;
    struct decoder *dec = dec_p->dec;
    int tun_fd = dec_p->tun_fd;
    free(dec_p);

    /* Decode the data blocks if necessary */
    if (dec->block_num > dec->data_num)
    {
        reed_solomon *rs = reed_solomon_new(dec->data_num, dec->block_num - dec->data_num);
        if (reed_solomon_reconstruct(rs, dec->data_blocks, dec->marks, dec->block_num, dec->block_size))
        {
            printf("Decode Error!!!");
            pthread_exit(NULL);
        }
    }

    /* Combine the data blocks */
    unsigned char *buf = (unsigned char *)malloc((dec->data_num) * (dec->block_size) * sizeof(unsigned char));
    for (int i = 0; i < dec->data_num; i++)
    {
        memcpy(buf + i * (dec->block_size), dec->data_blocks[i], dec->block_size);
    }

    /* Send the data to the tunnel */
    int pos = 0;
    while (pos < dec->data_size)
    {
        /* Get the packet length */
        int len = get_packet_len(buf + pos);
        if (len <= 0)
        {
            perror("Error: packet len=0!!!");
            break;
        }
        // printf("Send Packet len=%d\n", len);

        /* Send the packet to the tunnel */
        int write_bytes = write(tun_fd, buf + pos, len);
        if (write_bytes < 0)
        {
            perror("Error while sending to tun_fd!!!");
        }
        pos += len;
    }
    free(buf);
}

/**
 * Get the group by groupID or VPN address.
 *
 * @param groupID: The groupID
 * @param addr: The VPN address
 * @param udp_fd: The UDP socket file descriptor
 * @return: The group
 */
struct group *get_group(unsigned int groupID, struct address *addr, int udp_fd)
{
    if (group_iter == NULL)
    {
        return NULL;
    }
    struct list *end_iter = group_iter;
    do
    {
        struct group *group = (struct group *)(group_iter->data);
        struct encoder *enc = group->enc;

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        long time_delta = (now.tv_sec - enc->touch.tv_sec) * (long)1e9 + (now.tv_sec - enc->touch.tv_nsec);

        /* If the encoder has not been used for a while, encode the packets and send them over the network. */
        if (time_delta > ENC_TIMEOUT && !pthread_mutex_trylock(&(enc->mutex)))
        {
            if (enc->data_size > 0)
            {
                struct enc_param *enc_p = (struct enc_param *)malloc(sizeof(struct enc_param));
                enc_p->enc = enc;
                enc_p->udp_fd = udp_fd;
                enc_p->udp_addrs = group->udp_addrs;

                encode((void *)enc_p);
            }
            pthread_mutex_unlock(&(enc->mutex));
        }

        /* If the group has not been used for a while, free the group. */
        if (time_delta > GROUP_TIMEOUT)
        {
            struct list *next = group_iter->next;
            free_group(group);
            group_iter = next;
            continue;
        }

        /* Find the group by VPN address */
        if (addr != NULL)
        {
            struct list *vpn_iter = group->vpn_addrs;
            while (vpn_iter != NULL)
            {

                struct address *vpn_addr = (struct address *)(vpn_iter->data);
                if (vpn_addr->ip == addr->ip && vpn_addr->port == addr->port)
                {
                    clock_gettime(CLOCK_REALTIME, &(group->touch));
                    clock_gettime(CLOCK_REALTIME, &(group->enc->touch));
                    return group;
                }
                vpn_iter = vpn_iter->next;
            }
        }

        /* Find the group by groupID */
        if (groupID != NULL && group->groupID == groupID)
        {
            clock_gettime(CLOCK_REALTIME, &(group->touch));
            clock_gettime(CLOCK_REALTIME, &(group->dec->touch));
            return group;
        }

        group_iter = group_iter->next;
        if (group_iter == NULL)
            group_iter = group_list;

    } while (group_iter != end_iter);

    return NULL;
}

/**
 * Create a new group.
 *
 * @param groupID: The groupID
 * @param data_size: The size of the data
 * @param block_size: The size of the block
 * @param block_num: The number of the blocks
 * @param udp_ip: The UDP IP address
 * @param udp_port: The UDP port
 * @param addr: The VPN address
 * @return: The new group
 */
struct group *new_group(unsigned int groupID, unsigned int data_size, unsigned int block_size, unsigned int data_num, unsigned int block_num, in_addr_t udp_ip, in_port_t udp_port, struct address *addr)
{
    /* Create a new group */
    struct group *group = (struct group *)malloc(sizeof(struct group));

    /* Set the groupID */
    group->groupID = groupID;

    /* Set the VPN address */
    struct address *addr = (struct address *)malloc(sizeof(struct address));
    addr->ip = udp_ip;
    addr->port = udp_port;

    /* Set the UDP address */
    group->udp_addrs = (struct list *)malloc(sizeof(struct list));
    group->udp_addrs->data = addr;
    group->udp_addrs->next = NULL;

    /* Set the VPN address */
    group->vpn_addrs = (struct list *)malloc(sizeof(struct list));
    group->vpn_addrs->data = addr;
    group->vpn_addrs->next = NULL;

    /* Init the encoder */
    group->enc = (struct encoder *)malloc(sizeof(struct encoder));
    group->enc->packet_buf = (unsigned char *)malloc(MAX_PACKET_BUF * sizeof(unsigned char));
    group->enc->data_size = 0;
    group->enc->packet_num = 0;
    pthread_mutex_init(&(group->enc->mutex), NULL);
    clock_gettime(CLOCK_REALTIME, &(group->enc->touch));

    /* Init the decoder */
    group->dec = (struct decoder *)malloc(sizeof(struct decoder));
    group->dec->data_size = data_size;
    group->dec->block_size = block_size;
    group->dec->data_num = data_num;
    group->dec->block_num = block_num;
    group->dec->receive_num = 0;
    group->dec->data_blocks = (unsigned char **)malloc(block_num * sizeof(unsigned char *));
    for (int i = 0; i < block_num; i++)
    {
        group->dec->data_blocks[i] = (unsigned char *)malloc(block_size * sizeof(unsigned char));
    }
    group->dec->marks = (int *)malloc(block_num * sizeof(int));
    memset(group->dec->marks, 0, block_num * sizeof(int));
    pthread_mutex_init(&(group->dec->mutex), NULL);
    clock_gettime(CLOCK_REALTIME, &(group->dec->touch));

    /* Add the group to the group list */
    struct list *new_group_item = (struct list *)malloc(sizeof(struct list));
    new_group_item->data = group;
    new_group_item->next = group_list;
    group_list = new_group_item;
    if (group_iter == NULL)
        group_iter = group_list;

    return group;
}

/**
 * Free a group.
 *
 * @param group: The group
 */
void free_group(struct group *group)
{
    /* Free the UDP address */
    struct list *udp_iter = group->udp_addrs;
    while (udp_iter != NULL)
    {
        struct list *next = udp_iter->next;
        free(udp_iter->data);
        free(udp_iter);
        udp_iter = next;
    }

    /* Free the VPN address */
    struct list *vpn_iter = group->vpn_addrs;
    while (vpn_iter != NULL)
    {
        struct list *next = vpn_iter->next;
        free(vpn_iter->data);
        free(vpn_iter);
        vpn_iter = next;
    }

    /* Free the encoder */
    pthread_mutex_destroy(&(group->enc->mutex));
    free(group->enc->packet_buf);
    free(group->enc);

    /* Free the decoder */
    pthread_mutex_destroy(&(group->dec->mutex));
    for (int i = 0; i < group->dec->block_num; i++)
    {
        free(group->dec->data_blocks[i]);
    }
    free(group->dec->data_blocks);
    free(group->dec->marks);
    free(group->dec);

    /* Free the group */
    free(group);
}

/**
 * Get the address of the packet.
 *
 * @param buf: The packet buffer
 * @param in_or_out: INPUT or OUTPUT
 * @return: The address of the packet
 */
struct address *get_packet_addr(unsigned char *buf, int in_or_out)
{
    /* IP Header*/
    struct iphdr *ip_hdr = (struct iphdr *)(buf);
    int ip_hdr_len = ip_hdr->ihl * 4;

    /* Protocol Header */
    struct tcphdr *tcp_hdr;
    struct udphdr *udp_hdr;
    struct icmphdr *icmp_hdr;
    __be16 *source;
    __be16 *dest;
    if (ip_hdr->protocol == IPPROTO_TCP)
    {
        tcp_hdr = (struct tcphdr *)(buf + ip_hdr_len);
        source = &(tcp_hdr->source);
        dest = &(tcp_hdr->dest);
    }
    else if (ip_hdr->protocol == IPPROTO_UDP)
    {
        udp_hdr = (struct udphdr *)(buf + ip_hdr_len);
        source = &(udp_hdr->source);
        dest = &(udp_hdr->dest);
    }
    else if (ip_hdr->protocol == IPPROTO_ICMP)
    {
        icmp_hdr = (struct icmphdr *)(buf + ip_hdr_len);
        source = dest = &(icmp_hdr->un.echo.id);
    }
    else
    {
        return NULL;
    }

    struct address *addr = (struct address *)malloc(sizeof(struct address));
    if (in_or_out == INPUT)
    {
        addr->ip = ip_hdr->saddr;
        addr->port = *source;
        addr->port = htons(addr->port);
    }
    else
    {
        addr->ip = ip_hdr->daddr;
        addr->port = *dest;
        addr->port = htons(addr->port);
    }
    return addr;
}

/**
 * Get the length of the packet.
 *
 * @param buf: The packet buffer
 * @return: The length of the packet
 */
unsigned int get_packet_len(unsigned char *buf)
{
    struct iphdr *ip_hdr = (struct iphdr *)(buf);
    return ntohs(ip_hdr->tot_len);
}

/**
 * Get a random groupID.
 *
 */
unsigned int get_random_groupID()
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    srand((unsigned)(now.tv_sec));
    unsigned int groupID = rand();
    srand((unsigned)(now.tv_nsec));
    groupID = groupID * rand();
    return groupID;
}