#include "fec.h"

pthread_mutex_t group_list_mutex;
struct list *group_iter = NULL;
struct list *group_before = NULL;

struct list *rx_list = NULL;
unsigned int rx_num = 0;
unsigned int rx_id = 0;
pthread_mutex_t rx_mutex;

unsigned int tx_id = 0;
pthread_mutex_t tx_mutex;

/**
 * Serve incoming input packets from the TUN interface.
 *
 * @param args: Void pointer to input_param struct containing packet data and client information
 */
void *serve_input(void *args) // from server to client
{
    /* Get the input parameters */
    struct input_param *param = (struct input_param *)args;
    unsigned char *packet = param->packet;
    unsigned int packet_size = param->packet_size;
    unsigned long int tid = param->tid;
    int udp_fd = param->udp_fd;
    free(param);

    /* Get the target vpn address of the packet */
    struct sockaddr_in *vpn_addr = get_packet_addr(packet, INPUT);
    if (vpn_addr == NULL)
        return NULL;

    /* Get the group of the target address by vpn address*/
    pthread_mutex_lock(&group_list_mutex);
    struct group *group = get_group(-1, vpn_addr, udp_fd);
    if (group == NULL)
    {
        pthread_mutex_unlock(&group_list_mutex);
        return NULL;
    }
    pthread_mutex_lock(&(group->mutex));
    pthread_mutex_unlock(&group_list_mutex);

    /* Get the encoder of the group */
    struct encoder *enc = group->enc;

    /* If the encoder buffer is not full, add the packet to the buffer */
    if ((packet_size + enc->data_size) < MAX_PACKET_BUF)
    {
        /* Add the packet to the encoder buffer */
        memcpy(enc->packet_buf + enc->data_size, packet, packet_size);
        free(packet);
        enc->data_size += packet_size;
        enc->packet_num += 1;

        /* If packet number reaches the maximum, encode the packets and send them over the network*/
        if (enc->packet_num >= config.max_TX_num)
        {
            struct enc_param *enc_p = (struct enc_param *)malloc(sizeof(struct enc_param));
            enc_p->enc = enc;
            enc_p->udp_fd = udp_fd;
            enc_p->udp_infos = group->udp_infos;
            encode((void *)enc_p);
        }
    }
    else
    {
        /* The encoder buffer is full, encode the packets and send them over the network */
        struct enc_param *enc_p = (struct enc_param *)malloc(sizeof(struct enc_param));
        enc_p->enc = enc;
        enc_p->udp_fd = udp_fd;
        enc_p->udp_infos = group->udp_infos;
        encode((void *)enc_p);

        /* Add the packet to the encoder buffer */
        memcpy(enc->packet_buf, packet, packet_size);
        free(packet);
        enc->data_size += packet_size;
        enc->packet_num += 1;
    }
    pthread_mutex_unlock(&(group->mutex));
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

    struct sockaddr_in *udp_addr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    udp_addr->sin_family = param->udp_addr.sin_family;
    udp_addr->sin_addr.s_addr = param->udp_addr.sin_addr.s_addr;
    udp_addr->sin_port = param->udp_addr.sin_port;

    int udp_fd = param->udp_fd;
    int tun_fd = param->tun_fd;
    free(param);

    /* Parse the HON Header */
    int pos = 0;
    unsigned int groupID = be32toh(*((int *)(packet + pos)));
    pos += 4;
    unsigned int data_size = be32toh(*((int *)(packet + pos)));
    pos += 4;
    unsigned int block_size = be32toh(*((int *)(packet + pos)));
    pos += 4;
    unsigned int data_num = be32toh(*((int *)(packet + pos)));
    pos += 4;
    unsigned int block_num = be32toh(*((int *)(packet + pos)));
    pos += 4;
    unsigned int index = be32toh(*((int *)(packet + pos)));
    pos += 4;
    unsigned int send_time = be32toh(*((int *)(packet + pos)));
    pos += 4;
    long packet_sendtime = 0;
    if (send_time > 0)
    {
        packet_sendtime = be64toh(*((long *)(packet + pos)));
        pos += 8;
    }

    /* Get the group by groupID */
    pthread_mutex_lock(&group_list_mutex);
    struct group *group = get_group(groupID, NULL, udp_fd);

    /* Create a new group if it does not exist */
    if (group == NULL)
    {
        group = new_group(groupID, data_size, block_size, data_num, block_num);
    }
    pthread_mutex_lock(&(group->mutex));
    pthread_mutex_unlock(&group_list_mutex);

    /* Update the UDP info */
    struct udp_info *udp_info = (struct udp_info *)malloc(sizeof(struct udp_info));
    udp_info->addr = udp_addr;
    if (send_time <= 0)
    {
        udp_info->time_head = NULL;
    }
    else
    {
        udp_info->time_head = (struct list *)malloc(sizeof(struct list));
        struct time_pair *tp = (struct time_pair *)malloc(sizeof(struct time_pair));
        tp->packet_send = packet_sendtime;
        clock_gettime(CLOCK_REALTIME, &(tp->packet_receive));
        udp_info->time_head->data = tp;
        udp_info->time_tail = udp_info->time_head;
        udp_info->time_tail->next = NULL;
    }
    group->udp_infos = update_udp_info_list(group->udp_infos, udp_info);

    /* Get udp_addr nums, and print every addr*/
    // int udp_addr_num = 0;
    // struct list *udp_info_iter = group->udp_infos;
    // while (udp_info_iter != NULL)
    // {
    //     udp_addr_num++;
    //     udp_info_iter = udp_info_iter->next;
    // }
    // udp_info_iter = group->udp_infos;
    // printf("udp_addr_num: %d {", udp_addr_num);
    // for (int i = 0; i < udp_addr_num; i++)
    // {
    //     if (i != 0)
    //         printf(", ");
    //     struct sockaddr_in *udp_addr = ((struct udp_info *)(udp_info_iter->data))->addr;
    //     printf("udp_addr: %s:%i", inet_ntoa(udp_addr->sin_addr), ntohs(udp_addr->sin_port));
    //     udp_info_iter = udp_info_iter->next;
    // }
    // printf("}\n");

    /* Get the decoder */
    struct decoder *dec = group->dec;

    /* Check if the packet is received */
    if ((dec->marks[index]))
    {
        dec->receive_num++;
        dec->marks[index] = 0;
        clock_gettime(CLOCK_REALTIME, &(dec->touch));
        memcpy(dec->data_blocks[index], packet + pos, dec->block_size);
        free(packet);

        /* Decode the data blocks if enough blocks are received */
        if (dec->receive_num == dec->data_num)
        {
            struct dec_param *dec_p = (struct dec_param *)malloc(sizeof(struct dec_param));
            dec_p->group = group;
            dec_p->tun_fd = tun_fd;
            decode(dec_p);
        }
    }
    pthread_mutex_unlock(&(group->mutex));
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
    struct list *udp_infos = enc_p->udp_infos;
    int udp_fd = enc_p->udp_fd;
    free(enc_p);

    /* Determine the number of blocks and the size of each block */
    unsigned int data_size = enc->data_size;
    unsigned int data_num = enc->packet_num;
    unsigned int block_size = (data_size + data_num - 1) / data_num;

    /* Determine the number of parity blocks */
    unsigned int block_num = data_num;
    srand(enc->touch.tv_nsec);
    for (unsigned int i = 0; i < data_num; i++)
    {
        if (rand() % 100 + 1 < config.parity_rate)
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

    unsigned char *buffer = (unsigned char *)malloc((36 + block_size) * sizeof(unsigned char));
    unsigned int groupID = get_groupId();

    /* Send all data blocks over the network */
    for (unsigned int index = 0; index < block_num; index++)
    {
        /* Select UDP address for sending */
        struct list *udp_select = udp_infos;
        struct list *udp_iter = udp_infos;
        long first_time = 2147483647;
        while (udp_iter != NULL)
        {
            struct udp_info *info = (struct udp_info *)(udp_iter->data);
            if (info->time_head != NULL)
            {
                long udp_time = ((struct time_pair *)(info->time_head->data))->packet_send;
                if (udp_time < first_time)
                {
                    first_time = udp_time;
                    udp_select = udp_iter;
                }
            }
            udp_iter = udp_iter->next;
        }

        if (udp_select == NULL)
        {
            perror("Error: udp_infos is NULL!!!");
        }
        struct udp_info *udp_info = (struct udp_info *)(udp_select->data);

        /* Construct the HON Header */
        int pos = 0;
        *((unsigned int *)(buffer)) = htobe32(groupID), pos += 4;
        *((unsigned int *)(buffer + pos)) = htobe32(data_size), pos += 4;
        *((unsigned int *)(buffer + pos)) = htobe32(block_size), pos += 4;
        *((unsigned int *)(buffer + pos)) = htobe32(data_num), pos += 4;
        *((unsigned int *)(buffer + pos)) = htobe32(block_num), pos += 4;
        if (udp_info->time_head == NULL)
        {
            *((unsigned int *)(buffer + pos)) = htobe32(0), pos += 4;
        }
        else
        {
            *((unsigned int *)(buffer + pos)) = htobe32(1), pos += 4;
            long packet_send = ((struct time_pair *)(udp_info->time_head->data))->packet_send;
            struct timespec packet_receive = ((struct time_pair *)(udp_info->time_head->data))->packet_receive;
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            long time_delta = (now.tv_sec - packet_receive.tv_sec) * (long)1e9 + (now.tv_nsec - packet_receive.tv_nsec);
            packet_send += time_delta / 1000000;
            *((long *)(buffer + pos)) = htobe64(packet_send), pos += 8;

            free(udp_info->time_head->data);
            udp_info->time_head = udp_info->time_head->next;
            if (udp_info->time_head == NULL)
            {
                udp_info->time_tail = NULL;
            }
        }
        *((unsigned int *)(buffer + pos)) = htobe32(index), pos += 4;

        /* Copy the data block to the buffer */
        memcpy(buffer + pos, data_blocks[index], block_size);

        struct sockaddr_in *udp_addr = udp_info->addr;
        socklen_t udp_addr_len = sizeof(*udp_addr);

        /* Send the data block over the network */
        int write_bytes = sendto(udp_fd, buffer, block_size + pos, 0, (const struct sockaddr *)udp_addr, udp_addr_len);
        printf("UDP %d Send %d bytes to %s:%i\n", udp_fd, write_bytes, inet_ntoa(udp_addr->sin_addr), ntohs(udp_addr->sin_port));
        if (write_bytes < 0)
        {
            perror("Error while sendding to udp_fd!!!");
            break;
        }
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
    struct group *group = dec_p->group;
    struct decoder *dec = group->dec;
    int tun_fd = dec_p->tun_fd;
    free(dec_p);

    /* Decode the data blocks if necessary */
    if (dec->block_num > dec->data_num)
    {
        reed_solomon *rs = reed_solomon_new(dec->data_num, dec->block_num - dec->data_num);
        if (reed_solomon_reconstruct(rs, dec->data_blocks, dec->marks, dec->block_num, dec->block_size))
        {
            // printf("Decode Error!!!");
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

        /* Get the source vpn address */
        struct sockaddr_in *vpn_addr = get_packet_addr(buf + pos, OUTPUT);

        /* Update the VPN address */
        group->vpn_addrs = update_vpn_address_list(group->vpn_addrs, vpn_addr);

        /* Send the packet to the tunnel */
        rx_insert(tun_fd, buf + pos, len, group->groupID);

        pos += len;
    }

    // pthread_mutex_lock(&rx_mutex);
    // if (rx_num > 0)
    // {
    //     printf("Left %d packets in rx_list: ", rx_num);
    //     printf("rx_id= %d. left_id={", rx_id);
    //     struct list *rx_iter = rx_list;
    //     unsigned int left_id = rx_id;
    //     unsigned int left_num = rx_num;
    //     while (rx_iter != NULL)
    //     {
    //         struct rx_packet *rx = (struct rx_packet *)(rx_iter->data);
    //         if (rx->id != left_id)
    //         {
    //             if (left_id != rx_id)
    //                 printf("%u*%u,", left_id, left_num);
    //             left_id = rx->id;
    //             left_num = 1;
    //         }
    //         else
    //         {
    //             left_num++;
    //         }
    //         rx_iter = rx_iter->next;
    //     }
    //     if (rx_num > 0)
    //     {
    //         printf("%u*%u}\n", left_id, left_num);
    //     }
    //     else
    //     {
    //         printf("}\n");
    //     }
    // }
    // pthread_mutex_unlock(&rx_mutex);
    free(buf);
}

/**
 * Insert and sort the received packets.
 *
 * @param tun_fd: The tunnel file descriptor
 * @param buf: The packet buffer
 * @param len: The length of the packet
 * @param groupId: The groupID
 */
void rx_insert(int tun_fd, unsigned char *buf, unsigned int len, unsigned int groupId)
{
    pthread_mutex_lock(&rx_mutex);
    struct rx_packet *rx = (struct rx_packet *)malloc(sizeof(struct rx_packet));
    rx->id = groupId;
    rx->packet = (unsigned char *)malloc(len * sizeof(unsigned char));
    rx->packet_len = len;
    clock_gettime(CLOCK_REALTIME, &(rx->touch));
    memcpy(rx->packet, buf, len);

    struct list *new_rx_item = (struct list *)malloc(sizeof(struct list));
    new_rx_item->data = rx;

    if (rx_list == NULL)
    {
        new_rx_item->next = NULL;
        rx_list = new_rx_item;
    }
    else
    {
        if (((struct rx_packet *)(rx_list->data))->id > groupId)
        {
            new_rx_item->next = rx_list;
            rx_list = new_rx_item;
        }
        else
        {
            struct list *rx_iter = rx_list;
            while (rx_iter->next != NULL)
            {
                if (((struct rx_packet *)(rx_iter->next->data))->id > groupId)
                    break;
                rx_iter = rx_iter->next;
            }
            new_rx_item->next = rx_iter->next;
            rx_iter->next = new_rx_item;
        }
    }

    rx_num++;

    /* Check if the packet at the head of the list can be sent to the tunnel or the rx_list is full */
    while (rx_list != NULL || rx_num > config.max_RX_num)
    {
        struct rx_packet *rx = (struct rx_packet *)(rx_list->data);

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        long time_delta = (now.tv_sec - rx->touch.tv_sec) * (long)1e9 + (now.tv_sec - rx->touch.tv_nsec);
        if (time_delta < config.rx_timeout && rx_num < config.max_RX_num && rx_id != rx->id && rx_id < rx->id - 1)
            break;

        // printf("TUN send %d bytes. groupId=%u\n", rx->packet_len, rx->id);
        int write_bytes = write(tun_fd, rx->packet, rx->packet_len);
        if (write_bytes < 0)
        {
            perror("Error while sending to tun_fd!!!");
        }
        rx_id = rx->id;
        struct list *next = rx_list->next;
        free(rx->packet);
        free(rx);
        free(rx_list);
        rx_list = next;
        rx_num--;
    }
    pthread_mutex_unlock(&rx_mutex);
}

/**
 * Clean all rx packets.
 *
 */
void clean_all_rx()
{
    pthread_mutex_lock(&rx_mutex);
    while (rx_list != NULL)
    {
        struct rx_packet *rx = (struct rx_packet *)(rx_list->data);
        struct list *next = rx_list->next;
        free(rx->packet);
        free(rx);
        free(rx_list);
        rx_list = next;
        rx_num--;
    }
    pthread_mutex_unlock(&rx_mutex);
}

/**
 * Get the group by groupID or VPN address.
 *
 * @param groupID: The groupID
 * @param sockaddr_in: The VPN address
 * @param udp_fd: The UDP socket file descriptor
 * @return: The group
 */
struct group *get_group(unsigned int groupID, struct sockaddr_in *addr, int udp_fd)
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
        struct decoder *dec = group->dec;

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        long time_delta_enc = (now.tv_sec - enc->touch.tv_sec) * (long)1e9 + (now.tv_sec - enc->touch.tv_nsec);
        long time_delta_dec = (now.tv_sec - dec->touch.tv_sec) * (long)1e9 + (now.tv_sec - dec->touch.tv_nsec);
        long time_delta_group = (now.tv_sec - group->touch.tv_sec) * (long)1e9 + (now.tv_sec - group->touch.tv_nsec);

        /* If the encoder has not been used for a while, encode the packets and send them over the network. */
        if (time_delta_enc > config.encode_timeout && !pthread_mutex_trylock(&(group->mutex)))
        {
            if (enc->data_size > 0)
            {
                struct enc_param *enc_p = (struct enc_param *)malloc(sizeof(struct enc_param));
                enc_p->enc = enc;
                enc_p->udp_fd = udp_fd;
                enc_p->udp_infos = group->udp_infos;
                // printf("Encoder TimeOut: %d\n", enc->packet_num);
                encode((void *)enc_p);
            }
            pthread_mutex_unlock(&(group->mutex));
        }

        /* If the group has not been used for a while, free the group. */
        if (time_delta_group > GROUP_TIMEOUT)
        {
            struct list *next = group_iter->next;
            free_group(group);
            free(group_iter);

            if (group_before == group_iter)
            {
                group_iter = NULL;
                group_before = NULL;
            }
            else
            {
                group_before->next = next;
                group_iter = next;
            }
            continue;
        }

        /* Find the group by VPN address */
        if (addr != NULL)
        {
            struct list *vpn_iter = (struct list *)group->vpn_addrs;
            while (vpn_iter != NULL)
            {
                struct sockaddr_in *vpn_addr = (struct sockaddr_in *)(vpn_iter->data);
                if (vpn_addr->sin_addr.s_addr == addr->sin_addr.s_addr && vpn_addr->sin_port == addr->sin_port)
                {
                    clock_gettime(CLOCK_REALTIME, &(group->touch));
                    clock_gettime(CLOCK_REALTIME, &(group->enc->touch));
                    return group;
                }
                vpn_iter = vpn_iter->next;
            }
        }

        /* Find the group by groupID */
        else if ((group->groupID == groupID))
        {

            clock_gettime(CLOCK_REALTIME, &(group->touch));
            clock_gettime(CLOCK_REALTIME, &(group->dec->touch));
            return group;
        }

        group_before = group_iter;
        group_iter = group_iter->next;

    } while (group_iter != end_iter && group_iter != NULL);

    return NULL;
}

/**
 * Create a new group.
 *
 * @param groupID: The groupID
 * @param data_size: The size of the data
 * @param block_size: The size of the block
 * @param data_num: The number of data blocks
 * @param block_num: The number of blocks
 * @return: The new group
 */
struct group *new_group(unsigned int groupID, unsigned int data_size, unsigned int block_size, unsigned int data_num, unsigned int block_num)
{
    /* Create a new group */
    struct group *group = (struct group *)malloc(sizeof(struct group));

    /* Set the groupID */
    group->groupID = groupID;

    /* Init the UDP address */
    group->udp_infos = (struct list *)malloc(sizeof(struct list));
    group->udp_infos = NULL;

    /* Init the VPN address */
    group->vpn_addrs = (struct list *)malloc(sizeof(struct list));
    group->vpn_addrs = NULL;

    /* Init the encoder */
    group->enc = (struct encoder *)malloc(sizeof(struct encoder));
    group->enc->packet_buf = (unsigned char *)malloc(MAX_PACKET_BUF * sizeof(unsigned char));
    group->enc->data_size = 0;
    group->enc->packet_num = 0;
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
    group->dec->marks = (unsigned char *)malloc(block_num * sizeof(unsigned char));
    memset(group->dec->marks, 1, block_num * sizeof(unsigned char));
    clock_gettime(CLOCK_REALTIME, &(group->dec->touch));

    /* Init the mutex */
    pthread_mutex_init(&(group->mutex), NULL);

    /* Set the touch time */
    clock_gettime(CLOCK_REALTIME, &(group->touch));

    /* Add the group to the group list */
    struct list *new_group_item = (struct list *)malloc(sizeof(struct list));
    new_group_item->data = group;
    if (group_iter == NULL)
    {
        new_group_item->next = new_group_item;
        group_iter = new_group_item;
        group_before = group_iter;
    }
    else
    {
        new_group_item->next = group_iter->next;
        group_iter->next = new_group_item;
    }
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
    struct list *udp_iter = group->udp_infos;
    while (udp_iter != NULL)
    {
        struct list *next = udp_iter->next;
        free(((struct udp_info *)(udp_iter->data))->addr);
        struct list *time_iter = ((struct udp_info *)(udp_iter->data))->time_head;
        while (time_iter != NULL)
        {
            struct list *next = time_iter->next;
            free(time_iter->data);
            free(time_iter);
            time_iter = next;
        }
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
    free(group->enc->packet_buf);
    free(group->enc);

    /* Free the decoder */
    for (int i = 0; i < group->dec->block_num; i++)
    {
        free(group->dec->data_blocks[i]);
    }
    free(group->dec->data_blocks);
    free(group->dec->marks);
    free(group->dec);

    /* Destroy the mutex */
    pthread_mutex_destroy(&(group->mutex));

    /* Free the group */
    free(group);
}

/**
 * Clean all groups.
 *
 */
void clean_all_group()
{
    struct list *end_iter = group_iter;
    if (group_iter == NULL)
        return;
    do
    {
        struct group *group = (struct group *)(group_iter->data);
        struct list *next = group_iter->next;
        free_group(group);
        free(group_iter);
        group_iter = next;
    } while (group_iter != end_iter && group_iter != NULL);
}

/**
 * Get the address of the packet.
 *
 * @param buf: The packet buffer
 * @param in_or_out: INPUT or OUTPUT
 * @return: The address of the packet
 */
struct sockaddr_in *get_packet_addr(unsigned char *buf, int in_or_out)
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
        // printf("Unknown protocol\n");
        return NULL;
    }

    struct sockaddr_in *addr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));

    if (in_or_out == OUTPUT)
    {
        addr->sin_family = AF_INET;
        addr->sin_addr.s_addr = ip_hdr->saddr;
        addr->sin_port = *source;
        addr->sin_port = htons(addr->sin_port);
    }
    else
    {
        addr->sin_family = AF_INET;
        addr->sin_addr.s_addr = ip_hdr->daddr;
        addr->sin_port = *dest;
        addr->sin_port = htons(addr->sin_port);
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
 * Get groupID.
 *
 */
unsigned int get_groupId()
{
    pthread_mutex_lock(&tx_mutex);
    unsigned int groupID = tx_id;
    tx_id++;
    pthread_mutex_unlock(&tx_mutex);
    return groupID;
}

/**
 * Update the UDP info list.
 *
 * @param udp_info_list: The UDP info list
 * @param udp_info: The UDP info
 */
struct list *update_udp_info_list(struct list *udp_info_list, struct udp_info *udp_info)
{
    struct list *udp_info_item = udp_info_list;

    while (udp_info_item) // != NULL
    {
        struct udp_info *info = (struct udp_info *)(udp_info_item->data);
        if (info->addr->sin_addr.s_addr == udp_info->addr->sin_addr.s_addr && info->addr->sin_port == udp_info->addr->sin_port)
        {
            if (info->time_head == NULL)
            {
                info->time_head = udp_info->time_head;
                info->time_tail = udp_info->time_tail;
            }
            else
            {
                info->time_tail->next = udp_info->time_head;
                info->time_tail = udp_info->time_tail;
            }
            free(udp_info->addr);
            free(udp_info);
            return udp_info_list;
        }
        udp_info_item = udp_info_item->next;
    }

    struct list *new_udp_info_item = (struct list *)malloc(sizeof(struct list));
    new_udp_info_item->data = udp_info;
    new_udp_info_item->next = udp_info_list;
    return new_udp_info_item;
}

/**
 * Update the vpn address list if the address is not in the list.
 *
 * @param vpn_addr_list: The vpn address list
 * @param vpn_addr: The vpn address
 */
struct list *update_vpn_address_list(struct list *vpn_addr_list, struct sockaddr_in *vpn_addr)
{
    struct list *vpn_addr_item = vpn_addr_list;

    while (vpn_addr_item) // != NULL
    {
        struct sockaddr_in *addr = (struct sockaddr_in *)(vpn_addr_item->data);
        if (addr->sin_addr.s_addr == vpn_addr->sin_addr.s_addr && addr->sin_port == vpn_addr->sin_port)
        {
            free(vpn_addr);
            return vpn_addr_list;
        }
        vpn_addr_item = vpn_addr_item->next;
    }

    struct list *new_addr_item = (struct list *)malloc(sizeof(struct list));
    new_addr_item->data = vpn_addr;
    new_addr_item->next = vpn_addr_list;
    return new_addr_item;
}