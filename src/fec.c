#include "fec.h"

pthread_mutex_t decoder_list_mutex;
struct list *decoder_list = NULL;

pthread_mutex_t encoder_list_mutex;
struct list *encoder_list = NULL;

struct list *rx_list = NULL;
unsigned int rx_num = 0;
unsigned int rx_id = 0;
pthread_mutex_t rx_mutex;

unsigned int tx_id = 0;
pthread_mutex_t tx_mutex;

/**
 * @brief Serve incoming input packets from the TUN interface.
 *
 * @param args: Void pointer to input_param struct containing packet data and client information
 * @return void
 */
void *serve_input(void *args) // from server to client
{
    /* Get the input parameters */
    struct input_param *param = (struct input_param *)args;
    unsigned char *packet = param->packet;
    unsigned int packet_size = param->packet_size;
    unsigned long int tid = param->tid;
    int udp_fd = param->udp_fd;

    struct sockaddr_in *udp_addr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    udp_addr->sin_family = param->udp_addr.sin_family;
    udp_addr->sin_addr.s_addr = param->udp_addr.sin_addr.s_addr;
    udp_addr->sin_port = param->udp_addr.sin_port;
    free(param);

    socklen_t client_addr_len = sizeof(*udp_addr);
    int write_bytes = sendto(udp_fd, packet, packet_size, 0, (const struct sockaddr *)udp_addr, client_addr_len);
    printf("UDP %d send %d bytes to %s:%i\n", udp_fd, packet_size, inet_ntoa(udp_addr->sin_addr), ntohs(udp_addr->sin_port));
    if (write_bytes < 0)
    {
        perror("Error while sending to udp_fd!!!");
    }
    free(udp_addr);
    free(packet);
    return NULL;

    /* Get the target vpn address of the packet */
    struct sockaddr_in *vpn_addr = get_packet_addr(packet, INPUT);
    if (vpn_addr == NULL)
        return NULL;

    /* Get the encoder */
    pthread_mutex_lock(&encoder_list_mutex);
    struct encoder *enc = get_encoder(NULL, vpn_addr);
    if (enc == NULL)
    {
        pthread_mutex_unlock(&encoder_list_mutex);
        return NULL;
    }
    pthread_mutex_lock(&(enc->mutex));
    pthread_mutex_unlock(&encoder_list_mutex);

    /* Add the packet to the encoder buffer */
    memcpy(enc->packet_buf + enc->data_size, packet, packet_size);
    free(packet);
    enc->data_size += packet_size;
    enc->packet_num += 1;
    // printf("Encoded packet, packet_num: %d\n", enc->packet_num);
    if (enc->packet_num == 1)
    {
        struct enc_param *enc_p = (struct enc_param *)malloc(sizeof(struct enc_param));
        enc_p->enc = enc;
        enc_p->udp_fd = udp_fd;
        threadpool_add(pool, (void *)encode, (void *)enc_p, 0);
    }

    /* If packet reaches the maximum, encode the packets and send them over the network*/
    if (enc->packet_num >= config.max_TX_num || (packet_size + enc->data_size) > MAX_PACKET_BUF)
    {
        pthread_cond_signal(&(enc->cond));
    }
    pthread_mutex_unlock(&(enc->mutex));
}

/**
 * @brief Serve incoming output packets from the UDP socket.
 *
 * @param args: Void pointer to output_param struct containing packet data and client information
 * @return void
 */
void *serve_output(void *args)
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

    // int write_bytes = write(tun_fd, packet, packet_size);
    // if (write_bytes < 0)
    // {
    //     perror("Error while sending to tun_fd!!!");
    // }
    // return NULL;

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

    pthread_mutex_lock(&decoder_list_mutex);
    struct decoder *dec = get_decoder(groupID);
    if (dec == NULL)
    {
        dec = new_decoder(groupID, data_size, block_size, data_num, block_num);
    }
    pthread_mutex_lock(&(dec->mutex));
    pthread_mutex_unlock(&decoder_list_mutex);

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
    dec->udp_infos = update_udp_info_list(dec->udp_infos, udp_info);

    /* Check if the packet is received */
    if ((dec->marks[index]))
    {
        dec->receive_num++;
        dec->marks[index] = 0;
        memcpy(dec->data_blocks[index], packet + pos, dec->block_size);
        free(packet);

        /* Decode the data blocks if enough blocks are received */
        if (dec->receive_num == dec->data_num)
        {
            struct dec_param *dec_p = (struct dec_param *)malloc(sizeof(struct dec_param));
            dec_p->dec = dec;
            dec_p->tun_fd = tun_fd;
            threadpool_add(pool, (void *)decode, (void *)dec_p, 0);
        }
    }

    pthread_mutex_unlock(&(dec->mutex));

    return NULL;
}

/**
 * @brief Encode the packets and sends them over the network using the UDP file descriptor.
 *
 * @param args: Void pointer to the enc_param struct containing necessary encoding parameters
 * @return void
 */
void *encode(void *args)
{
    /* Get the encoding parameters */
    struct enc_param *enc_p = (struct enc_param *)args;
    // struct group *group = enc_p->group;
    struct encoder *enc = enc_p->enc;
    struct list *udp_infos = enc->udp_infos;
    int udp_fd = enc_p->udp_fd;
    free(enc_p);

    /* Wait for the packets */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += config.encode_timeout / 1000;
    ts.tv_nsec += (config.encode_timeout % 1000) * 1000;
    pthread_mutex_lock(&(enc->mutex));
    pthread_cond_timedwait(&enc->cond, &enc->mutex, &ts);

    printf("Encode %d packets\n", enc->packet_num);

    /* Determine the number of blocks and the size of each block */
    unsigned int data_size = enc->data_size;
    unsigned int data_num = enc->packet_num;
    unsigned int block_size = (data_size + data_num - 1) / data_num;

    /* Determine the number of parity blocks */
    unsigned int block_num = data_num;
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
        printf("UDP %d try to send bytes to %s:%i\n", udp_fd, inet_ntoa(udp_addr->sin_addr), ntohs(udp_addr->sin_port));
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
    pthread_mutex_unlock(&(enc->mutex));
}

/**
 * @brief Decode the received packets.
 *
 * @param args: Void pointer to dec_param struct containing decoder and tunnel fd
 * @return void
 */
void *decode(void *args)
{
    /* Get the decoder parameters */
    struct dec_param *dec_p = (struct dec_param *)args;
    // struct group *group = dec_p->group;
    struct decoder *dec = dec_p->dec;
    int tun_fd = dec_p->tun_fd;
    free(dec_p);

    print_udp_infos(dec->udp_infos);

    /* Decode the data blocks if necessary */
    if (dec->block_num > dec->data_num)
    {
        reed_solomon *rs = reed_solomon_new(dec->data_num, dec->block_num - dec->data_num);
        if (reed_solomon_reconstruct(rs, dec->data_blocks, dec->marks, dec->block_num, dec->block_size))
        {
            perror("Error while decoding!!!");
            return NULL;
        }
    }

    /* Combine the data blocks */
    unsigned char *buf = (unsigned char *)malloc((dec->data_num) * (dec->block_size) * sizeof(unsigned char));
    for (int i = 0; i < dec->data_num; i++)
    {
        memcpy(buf + i * (dec->block_size), dec->data_blocks[i], dec->block_size);
    }

    /* Get the encoder */
    pthread_mutex_lock(&encoder_list_mutex);
    struct encoder *enc = get_encoder(dec->udp_infos, NULL);
    if (enc == NULL)
    {
        enc = new_encoder(dec->udp_infos);
    }
    pthread_mutex_lock(&(enc->mutex));
    pthread_mutex_unlock(&encoder_list_mutex);

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
        enc->vpn_addrs = update_vpn_address_list(enc->vpn_addrs, vpn_addr);

        /* Send the packet to the tunnel */
        // int write_bytes = write(tun_fd, buf + pos, len);
        rx_insert(tun_fd, buf + pos, len, dec->groupID);

        pos += len;
    }
    pthread_mutex_unlock(&(enc->mutex));

    free(buf);

    print_rx();
    return NULL;
}

/**
 * @brief print udp infos
 *
 * @param udp_infos: list of udp info
 * @return void
 */
void print_udp_infos(struct list *udp_infos)
{
    int udp_addr_num = 0;
    struct list *udp_info_iter = udp_infos;
    while (udp_info_iter != NULL)
    {
        udp_addr_num++;
        udp_info_iter = udp_info_iter->next;
    }
    udp_info_iter = udp_infos;
    printf("udp_addr_num: %d {", udp_addr_num);
    for (int i = 0; i < udp_addr_num; i++)
    {
        if (i != 0)
            printf(", ");
        struct sockaddr_in *udp_addr = ((struct udp_info *)(udp_info_iter->data))->addr;
        printf("udp_addr: %s:%i", inet_ntoa(udp_addr->sin_addr), ntohs(udp_addr->sin_port));
        udp_info_iter = udp_info_iter->next;
    }
    printf("}\n");
}

/**
 * @brief print rx
 *
 * @return void
 */
void print_rx()
{
    pthread_mutex_lock(&rx_mutex);
    if (rx_num > 0)
    {
        printf("rx_id= %d. left_id={", rx_id);
        struct list *rx_iter = rx_list;
        unsigned int left_id = rx_id;
        unsigned int left_num = rx_num;
        while (rx_iter != NULL)
        {
            struct rx_packet *rx = (struct rx_packet *)(rx_iter->data);
            if (rx->id != left_id)
            {
                if (left_id != rx_id)
                    printf("%u*%u,", left_id, left_num);
                left_id = rx->id;
                left_num = 1;
            }
            else
            {
                left_num++;
            }
            rx_iter = rx_iter->next;
        }
        if (rx_num > 0)
        {
            printf("%u*%u}\n", left_id, left_num);
        }
        else
        {
            printf("}\n");
        }
    }
    pthread_mutex_unlock(&rx_mutex);
}

/**
 * @brief and sort the received packets.
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
        long time_delta = (now.tv_sec - rx->touch.tv_sec) * (long)1e9 + (now.tv_nsec - rx->touch.tv_nsec);
        if (time_delta < config.rx_timeout * 1000 && rx_num < config.max_RX_num && rx_id != rx->id && rx_id < rx->id - 1)
            break;

        // printf("TUN %d send %d bytes. groupId=%u\n",tun_fd, rx->packet_len, rx->id);
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
 * @brief all rx packets.
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

struct encoder *get_encoder(struct list *udp_infos, struct sockaddr_in *vpn_addr)
{
    if (encoder_list == NULL)
    {
        return NULL;
    }
    struct list *encoder_iter = encoder_list;
    struct list *encoder_before = NULL;
    struct encoder *res_enc = NULL;

    while (encoder_iter->next != NULL)
    {
        /* Get the encoder */
        struct encoder *enc = (struct encoder *)(encoder_list->data);

        /* Check if the udp_infos match */
        if (udp_infos != NULL && res_enc == NULL)
        {
            struct list *enc_udp_iter = enc->udp_infos;
            while (enc_udp_iter != NULL)
            {
                struct sockaddr_in *enc_udp_addr = ((struct udp_info *)(enc_udp_iter->data))->addr;

                struct list *udp_iter = udp_infos;
                while (udp_iter != NULL)
                {
                    struct sockaddr_in *udp_addr = ((struct udp_info *)(udp_iter->data))->addr;
                    if (enc_udp_addr->sin_addr.s_addr == udp_addr->sin_addr.s_addr && enc_udp_addr->sin_port == udp_addr->sin_port)
                    {
                        clock_gettime(CLOCK_REALTIME, &(enc->touch));
                        res_enc = enc;
                        break;
                    }
                    udp_iter = udp_iter->next;
                }
                if (res_enc != NULL)
                {
                    break;
                }
                enc_udp_iter = enc_udp_iter->next;
            }
        }

        /* Check if the vpn_addr match */
        if (vpn_addr != NULL && res_enc == NULL)
        {
            struct list *enc_vpn_iter = enc->vpn_addrs;
            while (enc_vpn_iter != NULL)
            {
                struct sockaddr_in *enc_vpn_addr = ((struct sockaddr_in *)(enc_vpn_iter->data));

                if (enc_vpn_addr->sin_addr.s_addr == vpn_addr->sin_addr.s_addr && enc_vpn_addr->sin_port == vpn_addr->sin_port)
                {
                    clock_gettime(CLOCK_REALTIME, &(enc->touch));
                    res_enc = enc;
                    break;
                }
                enc_vpn_iter = enc_vpn_iter->next;
            }
        }

        /* Check if the udp is time to die */
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        long time_delta = (now.tv_sec - enc->touch.tv_sec) * (long)1e9 + (now.tv_nsec - enc->touch.tv_nsec);
        if (time_delta > UDP_TIMEOUT && pthread_mutex_trylock(&enc->mutex))
        {
            pthread_mutex_unlock(&enc->mutex);
            if (encoder_before == NULL)
            {
                encoder_list = encoder_iter->next;
                free(encoder_iter);
                encoder_iter = encoder_list;
                encoder_before = NULL;
            }
            else
            {
                encoder_before->next = encoder_iter->next;
                free(encoder_iter);
                encoder_iter = encoder_before->next;
            }
            continue;
        }
        encoder_before = encoder_iter;
        encoder_iter = encoder_iter->next;
    }

    /* If the encoder is found, update info */
    if (res_enc != NULL)
    {
        clock_gettime(CLOCK_REALTIME, &(res_enc->touch));
        if (udp_infos != NULL)
        {
            struct list *udp_iter = udp_infos;
            while (udp_iter != NULL)
            {
                res_enc->udp_infos = update_udp_info_list(res_enc->udp_infos, udp_iter->data);
                udp_iter = udp_iter->next;
            }
        }
    }

    return res_enc;
}

struct encoder *new_encoder(struct list *udp_infos)
{
    struct encoder *enc = (struct encoder *)malloc(sizeof(struct encoder));
    enc->packet_buf = (unsigned char *)malloc(MAX_PACKET_BUF * sizeof(unsigned char));
    enc->data_size = 0;
    enc->packet_num = 0;

    enc->udp_infos = udp_infos;
    enc->vpn_addrs = NULL;

    pthread_cond_init(&(enc->cond), NULL);
    pthread_mutex_init(&(enc->mutex), NULL);

    clock_gettime(CLOCK_REALTIME, &(enc->touch));

    struct list *new_encoder_item = (struct list *)malloc(sizeof(struct list));
    new_encoder_item->data = enc;
    if (encoder_list == NULL)
    {
        new_encoder_item->next = NULL;
        encoder_list = new_encoder_item;
    }
    else
    {
        new_encoder_item->next = encoder_list;
        encoder_list = new_encoder_item;
    }

    return enc;
}

struct decoder *get_decoder(unsigned int groupID)
{
    if (decoder_list == NULL)
    {
        return NULL;
    }
    struct list *decoder_iter = decoder_list;
    struct list *decoder_before = NULL;
    struct decoder *res_dec = NULL;

    int i = 0;
    while (decoder_iter != NULL)
    {
        i++;
        struct decoder *dec = (struct decoder *)(decoder_iter->data);

        if (dec->groupID == groupID)
        {
            clock_gettime(CLOCK_REALTIME, &(dec->touch));
            res_dec = dec;
            // return res_dec;
        }

        /* Check if the decoder is time to die */
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        long time_delta = (now.tv_sec - dec->touch.tv_sec) * (long)1e9 + (now.tv_nsec - dec->touch.tv_nsec);
        if (time_delta > config.decode_timeout * 1000 && (pthread_mutex_trylock(&dec->mutex) == 0))
        {
            pthread_mutex_unlock(&dec->mutex);
            free_decoder(dec);
            if (decoder_before == NULL)
            {
                decoder_list = decoder_iter->next;
                free(decoder_iter);
                decoder_iter = decoder_list;
                decoder_before = NULL;
            }
            else
            {
                decoder_before->next = decoder_iter->next;
                free(decoder_iter);
                decoder_iter = decoder_before->next;
            }
            continue;
        }
        decoder_before = decoder_iter;
        decoder_iter = decoder_iter->next;
    }
    // printf("get_decoder %d times\n", i);

    return res_dec;
}

struct decoder *new_decoder(unsigned int groupId, unsigned int data_size, unsigned int block_size, unsigned int data_num, unsigned int block_num)
{
    struct decoder *dec = (struct decoder *)malloc(sizeof(struct decoder));
    dec->groupID = groupId;
    dec->data_size = data_size;
    dec->block_size = block_size;
    dec->data_num = data_num;
    dec->block_num = block_num;
    dec->receive_num = 0;
    dec->marks = (char *)malloc(block_num * sizeof(char));
    dec->data_blocks = (unsigned char **)malloc(block_num * sizeof(unsigned char *));
    for (int i = 0; i < block_num; i++)
    {
        dec->marks[i] = 1;
        dec->data_blocks[i] = (unsigned char *)malloc(block_size * sizeof(unsigned char));
    }
    dec->udp_infos = NULL;

    pthread_mutex_init(&(dec->mutex), NULL);

    clock_gettime(CLOCK_REALTIME, &(dec->touch));

    struct list *new_decoder_item = (struct list *)malloc(sizeof(struct list));
    new_decoder_item->data = dec;
    if (decoder_list == NULL)
    {
        new_decoder_item->next = NULL;
        decoder_list = new_decoder_item;
    }
    else
    {
        new_decoder_item->next = decoder_list;
        decoder_list = new_decoder_item;
    }

    return dec;
}

/**
 * @brief Free the encoder
 *
 * @param enc: The encoder
 * @return void
 */
void free_encoder(struct encoder *enc)
{
    /* Free the packet_buf */
    free(enc->packet_buf);

    /* Free the udp_infos */
    struct list *udp_iter = enc->udp_infos;
    while (udp_iter != NULL)
    {
        struct udp_info *udp_info = (struct udp_info *)(udp_iter->data);
        free(udp_info->addr);

        /* Free the time_pairs */
        struct list *time_iter = udp_info->time_head;
        while (time_iter != NULL)
        {
            struct time_pair *tp = (struct time_pair *)(time_iter->data);
            free(tp);
            struct list *next = time_iter->next;
            free(time_iter);
            time_iter = next;
        }

        struct list *next = udp_iter->next;
        free(udp_info);
        free(udp_iter);
        udp_iter = next;
    }

    /* Free the vpn_addrs */
    struct list *vpn_iter = enc->vpn_addrs;
    while (vpn_iter != NULL)
    {
        struct sockaddr_in *vpn_addr = (struct sockaddr_in *)(vpn_iter->data);
        free(vpn_addr);
        struct list *next = vpn_iter->next;
        free(vpn_iter);
        vpn_iter = next;
    }

    /* Free the encoder */
    free(enc);
}

/**
 * Free the decoder
 *
 * @param dec: The decoder
 * @return void
 */
void free_decoder(struct decoder *dec)
{
    for (int i = 0; i < dec->block_num; i++)
    {
        free(dec->data_blocks[i]);
    }
    free(dec->data_blocks);
    free(dec->marks);
    free(dec);
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
 * @param new_udp_info: The UDP info
 */
struct list *update_udp_info_list(struct list *udp_info_list, struct udp_info *new_udp_info)
{
    struct list *udp_info_iter = udp_info_list;

    while (udp_info_iter) // != NULL
    {
        struct udp_info *info = (struct udp_info *)(udp_info_iter->data);
        if (info->addr->sin_addr.s_addr == new_udp_info->addr->sin_addr.s_addr && info->addr->sin_port == new_udp_info->addr->sin_port)
        {
            if (info->time_head == NULL)
            {
                info->time_head = new_udp_info->time_head;
                info->time_tail = new_udp_info->time_tail;
            }
            else
            {
                info->time_tail->next = new_udp_info->time_head;
                info->time_tail = new_udp_info->time_tail;
            }
            free(new_udp_info->addr);
            free(new_udp_info);
            return udp_info_list;
        }
        udp_info_iter = udp_info_iter->next;
    }

    struct list *new_udp_info_item = (struct list *)malloc(sizeof(struct list));
    new_udp_info_item->data = new_udp_info;
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
