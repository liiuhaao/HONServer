#include "fec.h"

pthread_mutex_t decoder_list_mutex;
struct list *decoder_list = NULL;

struct encoder *enc = NULL;

struct list *rx_list = NULL;
unsigned int rx_num = 0;
unsigned int rx_group_id = 0;
unsigned int rx_index = 0;
double rx_time = -1;
double rx_min = 1e18;
double rx_max = -1;
unsigned long long rx_count = 0;
unsigned long long rx_total = 1;
double rx_rate = 0;

unsigned long long rx_timeout = 0;
float rolling_time = 0.9;
pthread_mutex_t rx_mutex;
double timeout_rate = 0;

struct list *ack_head = NULL;
struct list *ack_tail = NULL;
pthread_mutex_t ack_mutex;

unsigned int tx_id = 0;
pthread_mutex_t tx_mutex;

double enc_time = -1;
double enc_min = 1e18;
double enc_max = -1;

double dec_time = -1;
double dec_min = 1e18;
double dec_max = -1;

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

    struct encoder *enc = param->enc;

    free(param);

    /* Get the target vpn address of the packet */
    struct sockaddr_in *vpn_addr = get_packet_addr(packet, INPUT);
    if (vpn_addr == NULL)
    {
        return NULL;
    }

    pthread_mutex_lock(&(enc->mutex));

    enc->index += 1;
    if (enc->index >= config.data_num)
    {
        printf("error!!! enc->index=%d data_num=%d parity_num=%d\n", enc->index, config.data_num, config.parity_num);
        enc->index = 0;
    }

    /* Add the packet to the encoder buffer */
    enc->packet_buffers[enc->index] = packet;
    enc->packet_sizes[enc->index] = packet_size;

    /* Get data_udp and parity_udp */
    if (enc->udp_infos == NULL)
    {
        pthread_mutex_unlock(&(enc->mutex));
        return NULL;
    }
    struct udp_info *data_udp = (struct udp_info *)(enc->udp_infos->data);
    struct udp_info *parity_udp = (struct udp_info *)(enc->udp_infos->data);
    struct list *udp_iter = enc->udp_infos;
    while (udp_iter != NULL)
    {
        struct udp_info *info = (struct udp_info *)(udp_iter->data);
        if (info->type == 1)
        {
            data_udp = info;
        }
        if (info->type == 0)
        {
            parity_udp = info;
        }
        udp_iter = udp_iter->next;
    }

    /* Send the data packet over the network */
    enc->packet_sizes[enc->index] = packet_size;
    input_send(udp_fd, packet, packet_size, enc->group_id, enc->index, DATA_TYPE, data_udp);

    /* If the mode is multiple sending, send the data packet with corresponding parity index. */
    if (config.mode == 2)
    {
        input_send(udp_fd, packet, packet_size, enc->group_id, enc->index + config.data_num, DATA_TYPE, parity_udp);
    }

    if (config.mode == 3)
    {
        ack_insert(packet, enc->group_id, enc->index, enc);
    }

    /* If packet reaches the config.data_num, encode the packets and send them over the network*/
    if (enc->index == config.data_num - 1)
    {
        if ((config.mode == 0 || config.mode == 1) && config.parity_num > 0)
        {
            struct enc_param *enc_p = (struct enc_param *)malloc(sizeof(struct enc_param));
            enc_p->enc = enc;
            enc_p->udp_fd = udp_fd;
            enc_p->udp = parity_udp;
            struct timespec before_enc;
            clock_gettime(CLOCK_REALTIME, &before_enc);
            encode((void *)enc_p);
            struct timespec after_enc;
            clock_gettime(CLOCK_REALTIME, &after_enc);
            long time_delta = (after_enc.tv_sec - before_enc.tv_sec) * (long)1e9 + (after_enc.tv_nsec - before_enc.tv_nsec);
            if (enc_time < 0)
            {
                enc_time = time_delta;
            }
            else
            {
                enc_time = rolling_time * enc_time + (1 - rolling_time) * time_delta;
            }
            enc_min = min(enc_min, time_delta);
            enc_max = max(enc_max, time_delta);
        }

        /* Update the encoder */
        enc->group_id += 1;
        enc->index = -1;
        // free_encoder_buffers(0);
        if (enc->packet_buffers != NULL)
        {
            for (int i = 0; i < config.data_num; i++)
            {
                if (enc->packet_buffers[i] != NULL)
                {
                    free(enc->packet_buffers[i]);
                }
                enc->packet_buffers[i] = NULL;
            }
        }
    }
    pthread_mutex_unlock(&(enc->mutex));
    // }
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
    unsigned int hon_size = param->hon_size;

    struct sockaddr_in *udp_addr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    udp_addr->sin_family = param->udp_addr.sin_family;
    udp_addr->sin_addr.s_addr = param->udp_addr.sin_addr.s_addr;
    udp_addr->sin_port = param->udp_addr.sin_port;

    int udp_fd = param->udp_fd;
    int tun_fd = param->tun_fd;

    struct encoder *enc = param->enc;

    free(param);

    /* Parse the HON Header */
    int pos = 0;
    unsigned int packet_type = be32toh(*((int *)(packet + pos)));
    pos += 4;
    unsigned int group_id = be32toh(*((int *)(packet + pos)));
    pos += 4;
    unsigned int index = be32toh(*((int *)(packet + pos)));
    pos += 4;
    long packet_sendtime = 0;
    packet_sendtime = be64toh(*((long *)(packet + pos)));
    pos += 8;

    if ((config.mode == 0 || config.mode == 1) && index >= config.data_num + config.parity_num)
    {
        return NULL;
    }
    if ((config.mode == 2 || config.mode == 3) && index >= 2 * config.data_num)
    {
        return NULL;
    }

    if (packet_type == ACK_TYPE)
    {
        printf("ACK group_id: %d, index: %d\n", group_id, index);
        // remove_ack(group_id, index);
        return NULL;
    }

    printf("OUTPUT group_id: %d, index: %d, data_num: %d, parity_num: %d, hon_size: %d , packet_sendtime: %ld udp_addr: %s:%i\n", group_id, index, config.data_num, config.parity_num, hon_size, packet_sendtime, inet_ntoa(udp_addr->sin_addr), ntohs(udp_addr->sin_port));

    /* Get the decoder */
    pthread_mutex_lock(&decoder_list_mutex);
    struct decoder *dec = get_decoder(group_id);
    if (dec == NULL)
    {
        dec = new_decoder(group_id);
    }
    pthread_mutex_lock(&(dec->mutex));
    pthread_mutex_unlock(&decoder_list_mutex);

    /* Get the udp_info */
    struct udp_info *udp_info = (struct udp_info *)malloc(sizeof(struct udp_info));
    udp_info->addr = udp_addr;
    udp_info->type = index < config.data_num ? 1 : 0;
    udp_info->time_head = (struct list *)malloc(sizeof(struct list));
    struct time_pair *tp = (struct time_pair *)malloc(sizeof(struct time_pair));
    tp->packet_send = packet_sendtime;
    clock_gettime(CLOCK_REALTIME, &(tp->packet_receive));
    udp_info->time_head->data = tp;
    udp_info->time_tail = udp_info->time_head;
    udp_info->time_tail->next = NULL;

    pthread_mutex_lock(&(enc->mutex));
    enc->udp_infos = update_udp_info_list(enc->udp_infos, udp_info);
    pthread_mutex_unlock(&(enc->mutex));

    /* If the mode is multiple sending, then change the index of parity packet to data packet directly */
    if (index >= config.data_num && (config.mode == 2 || config.mode == 3))
    {
        index -= config.data_num;
    }

    /* Check if the packet is received */
    if (dec->marks[index] == 1)
    {
        dec->packet_sizes[index] = hon_size - pos;
        dec->receive_num++;
        dec->marks[index] = 0;

        if (index >= config.data_num)
        {
            dec->block_size = dec->packet_sizes[index];
        }

        if (dec->receive_num <= config.data_num)
        {
            /* Copy the data block to the buffer */
            dec->data_blocks[index] = (unsigned char *)malloc(dec->packet_sizes[index] * sizeof(unsigned char));
            memcpy(dec->data_blocks[index], packet + pos, dec->packet_sizes[index]);
            free(packet);

            /* If the packet is data packet, send it to the tunnel directly */
            if (index < config.data_num)
            {
                rx_insert(tun_fd, udp_fd, enc, dec->data_blocks[index], group_id, index);
            }

            /* Decode the data blocks if enough blocks are received */
            if (dec->receive_num == config.data_num)
            {
                /* Check if there exist a data block has not been received */
                int decode_flag = 0;
                for (int i = 0; i < config.data_num; i++)
                {
                    if (dec->marks[i] == 1)
                    {
                        decode_flag = 1;
                        break;
                    }
                }

                if (decode_flag)
                {
                    /* Reallocate the data blocks */
                    for (int i = 0; i < config.data_num; i++)
                    {
                        if (dec->data_blocks[i] == NULL)
                        {
                            dec->data_blocks[i] = (unsigned char *)malloc(dec->block_size * sizeof(unsigned char));
                            memset(dec->data_blocks[i], 0, dec->block_size);
                        }
                        else
                        {
                            if (dec->packet_sizes[i] < dec->block_size)
                            {
                                dec->data_blocks[i] = realloc(dec->data_blocks[i], dec->block_size * sizeof(unsigned char));
                                memset(dec->data_blocks[i] + dec->packet_sizes[i], 0, dec->block_size - dec->packet_sizes[i]);
                            }
                        }
                    }

                    /* Decode the data blocks */
                    struct dec_param *dec_p = (struct dec_param *)malloc(sizeof(struct dec_param));
                    dec_p->dec = dec;
                    dec_p->tun_fd = tun_fd;
                    dec_p->udp_fd = udp_fd;

                    struct timespec before_dec;
                    clock_gettime(CLOCK_REALTIME, &before_dec);
                    decode((void *)dec_p);
                    struct timespec after_dec;
                    clock_gettime(CLOCK_REALTIME, &after_dec);
                    long time_delta = (after_dec.tv_sec - before_dec.tv_sec) * (long)1e9 + (after_dec.tv_nsec - before_dec.tv_nsec);
                    if (dec_time < 0)
                    {
                        dec_time = time_delta;
                    }
                    else
                    {
                        dec_time = rolling_time * dec_time + (1 - rolling_time) * time_delta;
                    }
                    dec_min = min(dec_min, time_delta);
                    dec_max = max(dec_max, time_delta);

                    for (int i = 0; i < config.data_num; i++)
                    {
                        /* Send the reconstructed data packet */
                        if (dec->marks[i] == 1)
                        {
                            rx_insert(tun_fd, udp_fd, enc, dec->data_blocks[i], group_id, i);
                        }
                    }
                }
            }
        }
    }
    pthread_mutex_unlock(&(dec->mutex));

    return NULL;
}

void send_ack(int udp_fd, struct encoder *enc, int group_id, int index)
{
    struct udp_info *data_udp = (struct udp_info *)(enc->udp_infos->data);
    struct udp_info *parity_udp = (struct udp_info *)(enc->udp_infos->data);
    struct list *udp_iter = enc->udp_infos;
    while (udp_iter != NULL)
    {
        struct udp_info *info = (struct udp_info *)(udp_iter->data);
        if (info->type == 1)
        {
            data_udp = info;
        }
        if (info->type == 0)
        {
            parity_udp = info;
        }
        udp_iter = udp_iter->next;
    }

    /* Send the data packet over the network */
    input_send(udp_fd, NULL, 0, group_id, index, ACK_TYPE, data_udp);
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
    struct encoder *enc = enc_p->enc;
    int udp_fd = enc_p->udp_fd;
    struct udp_info *udp = enc_p->udp;
    free(enc_p);

    int block_size = -1;
    int block_num = config.data_num + config.parity_num;
    for (int i = 0; i < config.data_num; i++)
    {
        block_size = max(block_size, (int)(enc->packet_sizes[i]));
    }
    if (config.mode == 0)
    {
        unsigned char **data_packets = (unsigned char **)malloc(config.data_num * sizeof(unsigned char *));
        for (int i = 0; i < config.data_num; i++)
        {
            data_packets[i] = (unsigned char *)malloc(block_size * sizeof(unsigned char));
            memset(data_packets[i], 0, block_size * sizeof(unsigned char));
            memcpy(data_packets[i], enc->packet_buffers[i], enc->packet_sizes[i]);
        }
        block_size += (4 - block_size % 4) % 4;
        unsigned char **parity_pacekts = fec_encode(8, 4, block_size, data_packets);
        for (int i = 0; i < config.parity_num; i++)
        {
            input_send(udp_fd, parity_pacekts[i], block_size, enc->group_id, i + config.data_num, DATA_TYPE, udp);
        }
        // free parity_packets?
        for (int i = 0; i < config.data_num; i++)
        {
            free(data_packets[i]);
        }
        free(data_packets);
    }
    else if (config.mode == 1)
    {
        /* Construct the data blocks */
        unsigned char arr[block_num][block_size];
        memset(arr, 0, sizeof(arr));
        unsigned char *data_blocks[block_num];

        for (int i = 0; i < block_num; i++)
        {
            if (i < config.data_num)
            {
                memcpy(arr[i], enc->packet_buffers[i], enc->packet_sizes[i]);
            }
            data_blocks[i] = arr[i];
        }

        /* Encode the data blocks */
        reed_solomon *rs = reed_solomon_new(config.data_num, config.parity_num);
        reed_solomon_encode2(rs, data_blocks, block_num, block_size);

        /* Send parity packets */
        for (int i = config.data_num; i < block_num; i++)
        {
            input_send(udp_fd, data_blocks[i], block_size, enc->group_id, i, DATA_TYPE, udp);
        }
    }

    return NULL;
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
    struct decoder *dec = dec_p->dec;
    int tun_fd = dec_p->tun_fd;
    int udp_fd = dec_p->udp_fd;
    free(dec_p);

    /* Decode the data blocks if necessary */
    if (config.parity_num > 0)
    {
        if (config.mode == 0)
        {
            dec->data_blocks = fec_decode(8, 4, dec->block_size, dec->data_blocks, dec->marks);
        }
        else if (config.mode == 1)
        {
            reed_solomon *rs = reed_solomon_new(config.data_num, config.parity_num);
            if (reed_solomon_reconstruct(rs, dec->data_blocks, dec->marks, config.data_num + config.parity_num, dec->block_size))
            {
                perror("Error while decoding!!!");
                return NULL;
            }
        }
    }

    return NULL;
}

void input_send(int udp_fd, unsigned char *packet, int len, unsigned int group_id, unsigned int index, unsigned int packet_type, struct udp_info *udp)
{
    if (udp == NULL)
    {
        return;
    }
    unsigned char *buffer = (unsigned char *)malloc((36 + len) * sizeof(unsigned char));
    int pos = 0;

    /* Construct HON Header [group+id, index, packet_send]*/
    *((unsigned int *)(buffer + pos)) = htobe32(packet_type), pos += 4;
    *((unsigned int *)(buffer + pos)) = htobe32(group_id), pos += 4;
    *((unsigned int *)(buffer + pos)) = htobe32(index), pos += 4;
    long packet_send = ((struct time_pair *)(udp->time_head->data))->packet_send;
    struct timespec packet_receive = ((struct time_pair *)(udp->time_head->data))->packet_receive;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    long time_delta = (now.tv_sec - packet_receive.tv_sec) * (long)1e9 + (now.tv_nsec - packet_receive.tv_nsec);
    *((long *)(buffer + pos)) = htobe64(packet_send + time_delta / 1000000), pos += 8;

    if (udp->time_head->next != NULL)
    {
        struct list *next = udp->time_head->next;
        free(udp->time_head->data);
        free(udp->time_head);
        udp->time_head = next;
        if (udp->time_head == NULL)
        {
            udp->time_tail = NULL;
        }
    }

    /* Copy the packet to the buffer */
    if (len > 0)
    {
        memcpy(buffer + pos, packet, len);
    }

    struct sockaddr_in *udp_addr = udp->addr;
    socklen_t udp_addr_len = sizeof(*udp_addr);

    /* Send the data block over the network */
    int write_bytes = sendto(udp_fd, buffer, len + pos, 0, (const struct sockaddr *)udp_addr, udp_addr_len);
    printf("INPUT UDP %d send %d bytes to %s:%i [%d:%d/%d/%d]\n", udp_fd, write_bytes, inet_ntoa(udp_addr->sin_addr), ntohs(udp_addr->sin_port), group_id, index, config.data_num, config.data_num + config.parity_num);
    if (write_bytes < 0)
    {
        perror("Error while sendding to udp_fd!!!");
    }
    return;
}

struct decoder *get_decoder(unsigned int group_id)
{
    if (decoder_list == NULL)
    {
        return NULL;
    }
    struct list *decoder_iter = decoder_list;
    struct list *decoder_before = NULL;

    while (decoder_iter != NULL)
    {
        struct decoder *dec = (struct decoder *)(decoder_iter->data);

        if (dec->group_id == group_id)
        {
            clock_gettime(CLOCK_REALTIME, &(dec->touch));
            return dec;
        }
        decoder_before = decoder_iter;
        decoder_iter = decoder_iter->next;
    }
    return NULL;
}

struct decoder *new_decoder(unsigned int groupId)
{
    struct decoder *dec = (struct decoder *)malloc(sizeof(struct decoder));
    dec->group_id = groupId;
    dec->receive_num = 0;
    dec->block_size = 0;

    unsigned int block_num = config.data_num + config.parity_num;
    if (config.mode == 0)
    {
        block_num = 12;
    }
    dec->data_blocks = (unsigned char **)malloc(block_num * sizeof(unsigned char *));
    dec->marks = (char *)malloc(block_num * sizeof(char));
    dec->packet_sizes = (unsigned int *)malloc(block_num * sizeof(unsigned int));
    for (int i = 0; i < block_num; i++)
    {
        dec->data_blocks[i] = NULL;
        dec->marks[i] = 1;
        dec->packet_sizes[i] = 0;
    }

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

void *monitor_decoder(void *arg)
{
    while (1)
    {
        int rand_num = rand();
        usleep(config.decode_timeout / 10);
        pthread_mutex_lock(&decoder_list_mutex);
        struct list *decoder_iter = decoder_list;
        struct list *decoder_before = NULL;

        while (decoder_iter != NULL)
        {
            struct decoder *dec = (struct decoder *)(decoder_iter->data);

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
        pthread_mutex_unlock(&decoder_list_mutex);
    }
}

void rx_insert(int tun_fd, int udp_fd, struct encoder *enc, unsigned char *buf, unsigned int group_id, unsigned int index)
{
    int len = get_packet_len(buf);

    pthread_mutex_lock(&rx_mutex);
    struct rx_packet *rx = (struct rx_packet *)malloc(sizeof(struct rx_packet));
    rx->group_id = group_id;
    rx->index = index;
    rx->packet = (unsigned char *)malloc(len * sizeof(unsigned char));
    rx->packet_len = len;
    clock_gettime(CLOCK_REALTIME, &(rx->touch));
    memcpy(rx->packet, buf, len);

    if (rx_group_id > group_id || (rx_group_id == group_id && rx_index > index))
    {
        pthread_mutex_unlock(&rx_mutex);
        return;
    }

    /* Send Ack*/
    if (config.mode == 3)
    {
        send_ack(udp_fd, enc, group_id, index);
    }

    struct list *new_rx_item = (struct list *)malloc(sizeof(struct list));
    new_rx_item->data = rx;

    if (rx_list == NULL)
    {
        new_rx_item->next = NULL;
        rx_list = new_rx_item;
    }
    else
    {
        struct list *rx_iter = rx_list;
        struct list *rx_before = NULL;
        int num = 0;
        while (rx_iter != NULL)
        {
            num += 1;
            struct rx_packet *rx = (struct rx_packet *)(rx_iter->data);
            if (rx->group_id > group_id || (rx->group_id == group_id && rx->index > index))
                break;
            rx_before = rx_iter;
            rx_iter = rx_iter->next;
        }
        if (rx_before == NULL)
        {
            new_rx_item->next = rx_list;
            rx_list = new_rx_item;
        }
        else
        {
            new_rx_item->next = rx_iter;
            rx_before->next = new_rx_item;
        }
    }

    rx_num++;

    pthread_mutex_unlock(&rx_mutex);

    // print_rx();

    rx_send(tun_fd);
}

void rx_send(int tun_fd)
{
    pthread_mutex_lock(&rx_mutex);

    /* Check if the packet at the head of the list can be sent to the tunnel or the rx_list is full */
    while (rx_list != NULL || rx_num > config.rx_num)
    {
        struct rx_packet *rx = (struct rx_packet *)(rx_list->data);

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        long time_delta = (now.tv_sec - rx->touch.tv_sec) * (long)1e9 + (now.tv_nsec - rx->touch.tv_nsec);
        if (time_delta < config.rx_timeout * 1000 && rx_num < config.rx_num && ((rx_group_id != rx->group_id) || (rx_index != rx->index)))
            break;

        if (time_delta >= config.rx_timeout * 1000)
        {
            rx_timeout++;
        }

        rx_min = min(rx_min, time_delta);
        rx_max = max(rx_max, time_delta);
        if (rx_count == 0)
        {
            rx_time = time_delta;
            rx_count = 1;
        }
        else
        {
            rx_time += time_delta;
            rx_count += 1;
        }
        timeout_rate = 1.0 * rx_timeout / rx_count;
        rx_total = rx->group_id * config.data_num + rx->index + 1;
        rx_rate = 1.0 * rx_count / rx_total;

        // printf("TUN %d send %d bytes. groupId=%u index=%u time_delta=%ld rx_time=%f rx_min=%f rx_max=%f timeout_rate=%f rx_rate=%f[%llu/%llu]\n", tun_fd, rx->packet_len, rx->group_id, rx->index, time_delta, rx_time / rx_count, rx_min, rx_max, 1.0 * rx_timeout / rx_count, rx_rate, rx_count, rx_total);
        int write_bytes = write(tun_fd, rx->packet, rx->packet_len);
        if (write_bytes < 0)
        {
            perror("Error while sending to tun_fd!!!");
        }
        if (rx->index < config.data_num - 1)
        {
            rx_group_id = rx->group_id;
            rx_index = rx->index + 1;
        }
        else
        {
            rx_group_id = rx->group_id + 1;
            rx_index = 0;
        }
        struct list *next = rx_list->next;
        free(rx->packet);
        free(rx);
        free(rx_list);
        rx_list = next;
        rx_num--;
    }
    pthread_mutex_unlock(&rx_mutex);
}

void monitor_rx(void *arg)
{
    int tun_fd = *((int *)arg);
    while (1)
    {
        usleep(config.rx_timeout / 10);
        rx_send(tun_fd);
    }
}

void ack_insert(unsigned char *buf, unsigned int group_id, unsigned int index, struct encoder *enc)
{
    int len = get_packet_len(buf);
    pthread_mutex_lock(&ack_mutex);
    struct ack_packet *ack = (struct ack_packet *)malloc(sizeof(struct ack_packet));
    ack->group_id = group_id;
    ack->index = index;
    ack->packet = (unsigned char *)malloc(len * sizeof(unsigned char));
    ack->packet_len = len;
    ack->enc = enc;
    clock_gettime(CLOCK_REALTIME, &(ack->touch));
    memcpy(ack->packet, buf, len);

    struct list *new_ack_item = (struct list *)malloc(sizeof(struct list));
    new_ack_item->data = ack;
    new_ack_item->next = NULL;
    if (ack_head == NULL)
    {
        ack_head = new_ack_item;
        ack_tail = new_ack_item;
    }
    else
    {
        ack_tail->next = new_ack_item;
        ack_tail = new_ack_item;
    }
    pthread_mutex_unlock(&ack_mutex);
}

void remove_ack(int group_id, int index)
{
    struct list *ack_iter = ack_head;
    struct list *ack_before = NULL;
    pthread_mutex_lock(&ack_mutex);
    while (ack_iter != NULL)
    {
        struct list *next = ack_iter->next;
        struct ack_packet *ack = (struct ack_packet *)(ack_iter->data);
        if (ack->group_id == group_id && ack->index == index)
        {
            if (ack_before == NULL)
            {
                ack_head = next;
                if (next == NULL)
                {
                    ack_tail = NULL;
                }
            }
            else
            {
                ack_before->next = next;
                if (next == NULL)
                {
                    ack_tail = ack_before;
                }
            }
            free(ack->packet);
            free(ack);
            free(ack_iter);
            break;
        }
        ack_before = ack_iter;
        ack_iter = next;
    }
    pthread_mutex_unlock(&ack_mutex);
}

void monitor_ack(void *arg)
{
    int udp_fd = *((int *)arg);
    while (1)
    {
        usleep(config.ack_timeout / 10);
        pthread_mutex_lock(&ack_mutex);
        while (ack_head != NULL)
        {
            struct ack_packet *ack = (struct ack_packet *)(ack_head->data);

            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            long time_delta = (now.tv_sec - ack->touch.tv_sec) * (long)1e9 + (now.tv_nsec - ack->touch.tv_nsec);

            if (time_delta < config.ack_timeout * 1000)
                break;

            // printf("ack is NULL!%d\n", ack->group_id);
            enc = ack->enc;
            if (enc->udp_infos == NULL)
            {
                break;
            }
            struct udp_info *data_udp = (struct udp_info *)(enc->udp_infos->data);
            struct udp_info *parity_udp = (struct udp_info *)(enc->udp_infos->data);
            struct list *udp_iter = enc->udp_infos;
            while (udp_iter != NULL)
            {
                struct udp_info *info = (struct udp_info *)(udp_iter->data);
                if (info->type == 1)
                {
                    data_udp = info;
                }
                if (info->type == 0)
                {
                    parity_udp = info;
                }
                udp_iter = udp_iter->next;
            }

            input_send(udp_fd, ack->packet, ack->packet_len, ack->group_id, ack->index, DATA_TYPE, data_udp);
            struct list *next = ack_head->next;
            free(ack->packet);
            free(ack);
            free(ack_head);
            ack_head = next;
            if (next == NULL)
            {
                ack_tail = NULL;
            }
        }
        pthread_mutex_unlock(&ack_mutex);
    }
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
        printf("rx_group=%d. rx_index=%d left_id={", rx_group_id, rx_index);
        struct list *rx_iter = rx_list;
        while (rx_iter != NULL)
        {
            struct rx_packet *rx = (struct rx_packet *)(rx_iter->data);
            printf("%u:%u,", rx->group_id, rx->index);
            rx_iter = rx_iter->next;
        }
        printf("}\n");
    }
    pthread_mutex_unlock(&rx_mutex);
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
        printf("Unknown protocol %d\n", ip_hdr->protocol);
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
 * Get group_id.
 *
 */
unsigned int get_groupId()
{
    pthread_mutex_lock(&tx_mutex);
    unsigned int group_id = tx_id;
    tx_id++;
    pthread_mutex_unlock(&tx_mutex);
    return group_id;
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

    while (udp_info_iter != NULL)
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

struct encoder *new_encoder()
{
    /* Initialize the new encoder */
    struct encoder *enc = (struct encoder *)malloc(sizeof(struct encoder));
    enc->group_id = 0;
    enc->index = -1;

    enc->packet_buffers = (unsigned char **)malloc((config.data_num) * sizeof(unsigned char *));
    enc->packet_sizes = (unsigned int *)malloc((config.data_num) * sizeof(unsigned int));

    enc->udp_infos = NULL;

    pthread_mutex_init(&(enc->mutex), NULL);
    return enc;
}

void free_encoder_buffers(int free_self)
{
    if (enc->packet_buffers != NULL)
    {
        for (int i = 0; i < config.data_num; i++)
        {
            if (enc->packet_buffers[i] != NULL)
            {
                free(enc->packet_buffers[i]);
            }
            enc->packet_buffers[i] = NULL;
        }
        // if (free_self)
        // {
        //     free(enc->packet_buffers);
        // }
    }
}

void free_encoder_sizes()
{
    if (enc->packet_sizes != NULL)
    {
        free(enc->packet_sizes);
    }
}

void free_encoder_udp_infos()
{
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
}

void free_encoder()
{
    if (enc != NULL)
    {
        free_encoder_buffers(1);
        free_encoder_sizes(enc);
        free_encoder_udp_infos(enc);
        free(enc);
    }
}

void free_decoder(struct decoder *dec)
{
    if (dec->data_blocks != NULL)
    {
        for (int i = 0; i < config.data_num + config.parity_num; i++)
        {
            if (dec->marks[i] == 0)
            {
                free(dec->data_blocks[i]);
            }
        }
        free(dec->data_blocks);
        free(dec->marks);
    }
    if (dec->packet_sizes != NULL)
    {
        free(dec->packet_sizes);
    }
    free(dec);
}

void clean_all_decoder()
{
    pthread_mutex_lock(&decoder_list_mutex);
    while (decoder_list != NULL)
    {
        while (pthread_mutex_trylock(&((struct decoder *)(decoder_list->data))->mutex) != 0)
        {
            usleep(1000);
        }
        pthread_mutex_unlock(&((struct decoder *)(decoder_list->data))->mutex);
        struct decoder *dec = (struct decoder *)(decoder_list->data);
        free_decoder(dec);
        struct list *next = decoder_list->next;
        free(decoder_list);
        decoder_list = next;
    }
    pthread_mutex_unlock(&decoder_list_mutex);
    printf("clean_all_decoder done!!!\n");
}

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
    printf("clean_all_rx done!!!\n");
}

void clean_all_ack()
{
    pthread_mutex_lock(&ack_mutex);
    while (ack_head != NULL)
    {
        struct ack_packet *ack = (struct ack_packet *)(ack_head->data);
        struct list *next = ack_head->next;
        free(ack->packet);
        free(ack);
        free(ack_head);
        ack_head = next;
    }
    ack_tail = NULL;
    pthread_mutex_unlock(&ack_mutex);
    printf("clean_all_ack_done!!!\n");
}

void clean_all()
{

    free_encoder();
    clean_all_decoder();
    clean_all_rx();
    clean_all_ack();
}
