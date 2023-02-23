#include "code.h"

/**
 * Serve incoming output packets from the UDP socket.
 * 
 * @param args: Void pointer to output_param struct containing packet data and client information
 */
void *serve_output(void *args)
{
    pthread_detach(pthread_self());
    struct output_param *param = (struct output_param *)args;
    char *packet = param->packet;
    unsigned int packet_size = param->packet_size;
    int tun_fd = param->tun_fd;
    in_addr_t client_vpn_ip = param->client_vpn_ip;
    in_port_t client_vpn_port = param->client_vpn_port;
    free(param);

    unsigned int hash_code = be32toh(*((int *)(packet)));
    unsigned int data_size = be32toh(*((int *)(packet + 4)));
    unsigned int block_size = be32toh(*((int *)(packet + 8)));
    unsigned int data_num = be32toh(*((int *)(packet + 12)));
    unsigned int block_num = be32toh(*((int *)(packet + 16)));
    unsigned int index = be32toh(*((int *)(packet + 20)));

    struct dec_record *dec = dec_get(hash_code, data_size, block_size, data_num, block_num);

    struct dec_param *dec_p = (struct dec_param *)malloc(sizeof(struct dec_param));

    if (dec_put(dec, index, packet + 24))
    {
        dec_p->dec = dec;
        dec_p->tun_fd = tun_fd;
        dec_p->client_vpn_ip = client_vpn_ip;
        dec_p->client_vpn_port = client_vpn_port;
        pthread_create(&(dec_p->tid), NULL, decode, (void *)dec_p);
    }

    pthread_exit(NULL);
}

/**
 * Search For an record in the dec_table with matching the specified parameters.
 *
 * @param hash_code The hash code for the data
 * @param data_size The size of the data in bytes
 * @param block_size The size of each block in bytes
 * @param data_num The number of data blocks
 * @param block_num the number of blocks in total (including data and parity blocks)
 * @return A pointer to the matching or newly created record.
 */
struct dec_record *dec_get(int hash_code, int data_size, int block_size, int data_num, int block_num)
{
    struct dec_record *record = dec_table;
    struct dec_record *before = NULL;
    while (record)
    {
        if (record->hash_code == hash_code &&
            record->data_size == data_size &&
            record->block_size == block_size &&
            record->data_num == data_num &&
            record->block_num == block_num)
        {
            clock_gettime(CLOCK_REALTIME, &(record->touch));
            return record;
        }

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);

        long time_delta = (now.tv_sec - record->touch.tv_sec) * (long)1e9 + (now.tv_sec - record->touch.tv_nsec);

        /* Obsolete record */
        if (time_delta > DEC_TIMEOUT)
        {
            if (before)
            {
                before->next = record->next;
            }
            if (record == dec_table)
            {
                dec_table = dec_table->next;
            }

            struct dec_record *tmp = record;
            record = record->next;
            free_dec(tmp);
            continue;
        }
        before = record;
        record = record->next;
    }

    /* Add new record */
    if ((record = (struct dec_record *)malloc(sizeof(struct dec_record))) == NULL)
    {
        perror("Unable to allocate a new dec record");
        return NULL;
    }
    record->hash_code = hash_code;
    record->data_size = data_size;
    record->block_size = block_size;

    record->data_num = data_num;
    record->block_num = block_num;
    record->receive_num = 0;

    record->data_blocks = (unsigned char **)malloc(block_num * sizeof(unsigned char *));
    for (int i = 0; i < record->block_num; i++)
    {
        record->data_blocks[i] = (unsigned char *)malloc(block_size * sizeof(unsigned char));
    }

    record->marks = (unsigned char *)malloc(block_num * sizeof(unsigned char));
    memset(record->marks, 1, block_num);

    clock_gettime(CLOCK_REALTIME, &(record->touch));

    record->next = dec_table ? dec_table : NULL;
    dec_table = record;

    return record;
}

/***
 * Stores a data block in a specific index of a decoder.
 * 
 * @param record: pointer to the dec_record to store the data block in
 * @param index: index to store the data block in
 * @param block: pointer to the data block to be stored
 * @return 0 if not enough data blocks have been received, 1 if enough data blocks have been received
 */
int dec_put(struct dec_record *record, int index, unsigned char *block)
{
    if (!(record->marks[index]))
        return 0;
    record->receive_num++;
    record->marks[index] = 0;
    clock_gettime(CLOCK_REALTIME, &(record->touch));
    memcpy(record->data_blocks[index], block, record->block_size);
    return record->receive_num == record->data_num;
}

/***
 * Decode the received packets and sends sends the reconstructed packets through the tun.
 * 
 * @param args: Void pointer to the dec_param struct containing necessary decoding parameters
 */
void *decode(void *args)
{
    pthread_detach(pthread_self());
    struct dec_param *dec_p = (struct dec_param *)args;
    struct dec_record *dec = dec_p->dec;

    reed_solomon *rs = reed_solomon_new(dec->data_num, dec->block_num - dec->data_num);
    if (reed_solomon_reconstruct(rs, dec->data_blocks, dec->marks, dec->block_num, dec->block_size))
    {
        printf("Decode Error!!!");
        pthread_exit(NULL);
    }

    char *buf = (char *)malloc(dec->data_num * dec->block_size * sizeof(char));
    for (int i = 0; i < dec->data_num; i++)
    {
        memcpy(buf + i * dec->block_size, dec->data_blocks[i], dec->block_size);
    }

    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = dec_p->client_vpn_ip;
    client_addr.sin_port = dec_p->client_vpn_port;

    int pos = 0;
    while (pos < dec->data_size)
    {
        int len = packet_nat(&client_addr, buf + pos, OUT_NAT);
        // printf("pos=%d data_size=%d len=%d\n", pos, dec->data_size, len);
        if (len <= 0)
        {
            perror("Error: packet len=0!!!");
            break;
        }
        printf("Send Packet len=%d\n", len);
        int write_bytes = write(dec_p->tun_fd, buf + pos, len);
        if (write_bytes < 0)
        {
            perror("Error while sending to tun_fd!!!");
        }
        pos += len;
    }
    free(buf);
    free(dec_p);
    pthread_exit(NULL);
}

/**
 * Free memory allocated for a dec_record structure.
 * 
 * @param record A pointer to the dec_record structure to be freed
 * @return None
 */
void free_dec(struct dec_record *record)
{
    for (int i = 0; i < record->block_num; i++)
    {
        free(record->data_blocks[i]);
    }
    free(record->data_blocks);

    free(record->marks);

    free(record);
}

/**
 * Serve incoming input packets from the TUN interface.
 * 
 * @param args: Void pointer to input_param struct containing packet data and client information
 */
void *serve_input(void *args)
{
    pthread_detach(pthread_self());
    struct input_param *param = (struct input_param *)args;
    char *packet = param->packet;
    unsigned int packet_size = param->packet_size;
    int udp_fd = param->udp_fd;
    in_addr_t client_vpn_ip = param->client_vpn_ip;
    in_port_t client_vpn_port = param->client_vpn_port;
    free(param);

    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = client_vpn_ip;
    client_addr.sin_port = client_vpn_port;

    if (packet_nat(&client_addr, packet, IN_NAT))
    {
        printf("\nTUN recieved %d bytes\n", packet_size);
        pthread_mutex_lock(&(enc_table_mutex));
        struct enc_record *enc = enc_get(&client_addr, udp_fd);
        pthread_mutex_lock(&(enc->mutex));

        clock_gettime(CLOCK_REALTIME, &(enc->touch));
        if ((packet_size + enc->data_size) < MAX_PACKET_BUF)
        {
            memcpy(enc->packet_buf + enc->data_size, packet, packet_size);
            free(packet);
            enc->data_size += packet_size;
            enc->packet_num += 1;
        }
        else
        {
            enc->extra_packet = (char *)malloc(packet_size);
            memcpy(enc->extra_packet, packet, packet_size);
            enc->extra_size = packet_size;
        }
        pthread_mutex_unlock(&(enc->mutex));
        pthread_mutex_unlock(&(enc_table_mutex));
    }
    pthread_exit(NULL);
}

/**
 * Search for an record in the enc_table with matching the specified parameters.
 *
 * @param client_addr Pointer to the client's socket address
 * @param udp_fd File descriptor of the UDP socket
 * @return A pointer to the matching or newly created record.
 */
struct enc_record *enc_get(struct sockaddr_in *client_addr, int udp_fd)
{
    struct enc_record *record = enc_table;

    while (record)
    {
        if (record->client_vpn_ip == client_addr->sin_addr.s_addr &&
            record->client_vpn_port == client_addr->sin_port)
        {
            return record;
        }
    }

    /* Add new record */
    if ((record = (struct enc_record *)malloc(sizeof(struct enc_record))) == NULL)
    {
        perror("Unable to allocate a new dec record");
        return NULL;
    }
    record->client_vpn_ip = client_addr->sin_addr.s_addr;
    record->client_vpn_port = client_addr->sin_port;

    record->packet_buf = (unsigned char *)malloc(MAX_PACKET_BUF);
    record->data_size = 0;
    record->packet_num = 0;

    record->extra_packet = NULL;
    record->extra_size = 0;

    pthread_mutex_init(&(record->mutex), NULL);

    clock_gettime(CLOCK_REALTIME, &(record->touch));

    record->next = enc_table ? enc_table : NULL;
    enc_table = record;

    struct enc_param *enc_p = (struct enc_param *)malloc(sizeof(struct enc_param));
    enc_p->enc = record;
    enc_p->udp_fd = udp_fd;
    enc_p->client_vpn_ip = client_addr->sin_addr.s_addr;
    enc_p->client_vpn_port = client_addr->sin_port;
    pthread_create(&(record->tid), NULL, encode, (void *)enc_p);
    return record;
}

/**
 * Encode the packets and sends them over the network using the UDP file descriptor.
 * 
 * @param args: Void pointer to the enc_param struct containing necessary encoding parameters
 */
void *encode(void *args)
{
    pthread_detach(pthread_self());
    struct enc_param *enc_p = (struct enc_param *)args;
    struct enc_record *enc = enc_p->enc;
    int udp_fd = enc_p->udp_fd;

    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = enc_p->client_vpn_ip;
    client_addr.sin_port = enc_p->client_vpn_port;
    socklen_t client_addr_len = sizeof(client_addr);

    free(enc_p);

    while (1)
    {
        pthread_mutex_lock(&(enc->mutex));
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);

        long time_delta = (now.tv_sec - enc->touch.tv_sec) * (long)1e9 + (now.tv_sec - enc->touch.tv_nsec);
        int timeout = time_delta > ENC_TIMEOUT;

        if (timeout && enc->packet_num == 0)
        {
            if (!(pthread_mutex_trylock(&enc_table_mutex)))
            {
                pthread_mutex_unlock(&(enc->mutex));
                enc_delete(enc->client_vpn_ip, enc->client_vpn_port);
                pthread_mutex_unlock(&enc_table_mutex);
                break;
            }
        }

        if ((timeout && enc->packet_num > 0) || (enc->extra_size > 0))
        {
            unsigned int data_size = enc->data_size;
            unsigned int block_size = data_size > MAX_BLOCK_SIZE ? MAX_BLOCK_SIZE : data_size;
            unsigned int data_num = (data_size + block_size - 1) / block_size;
            unsigned int block_num = max(data_num + 1, (unsigned int)(data_num * 0.2));

            enc->data_size = 0;
            enc->packet_num = 0;

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

            reed_solomon *rs = reed_solomon_new(data_num, block_num - data_num);
            reed_solomon_encode2(rs, data_blocks, block_num, block_size);

            char *buffer = (char *)malloc(24 + block_size);
            unsigned int hash_code = get_hash_code();

            printf("TIMEOUT: hash_code=%d, data_size=%d, block_size=%d, data_num=%d, block_num=%d\n", hash_code, data_size, block_size, data_num, block_num);

            for (unsigned int index = 0; index < block_num; index++)
            {
                *((unsigned int *)(buffer)) = htobe32(hash_code);
                *((unsigned int *)(buffer + 4)) = htobe32(data_size);
                *((unsigned int *)(buffer + 8)) = htobe32(block_size);
                *((unsigned int *)(buffer + 12)) = htobe32(data_num);
                *((unsigned int *)(buffer + 16)) = htobe32(block_num);
                *((unsigned int *)(buffer + 20)) = htobe32(index);

                memcpy(buffer + 24, data_blocks[index], block_size);

                int write_bytes = sendto(udp_fd, buffer, block_size + 24, 0, (const struct sockaddr *)&client_addr, client_addr_len);
                // printf("Send %d bytes to %s:%i\n", write_bytes, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                if (write_bytes < 0)
                {
                    perror("Error while sendding to udp_fd!!!");
                    break;
                }
            }
        }

        if (enc->extra_size)
        {
            memcpy(enc->packet_buf, enc->extra_packet, enc->extra_size);
            enc->data_size = enc->extra_size;
            enc->packet_num = 1;
            free(enc->extra_packet);
            enc->extra_size = 0;
        }
        pthread_mutex_unlock(&(enc->mutex));
    }
    pthread_exit(NULL);
}

/**
 * Obsolete record encoder record.
 * 
 * @param client_vpn_ip Client ip address
 * @param client_vpn_port Client port
 */
void enc_delete(in_addr_t client_vpn_ip, in_port_t client_vpn_port)
{
    struct enc_record *record = enc_table;
    struct enc_record *before = NULL;
    while (record)
    {
        if (record->client_vpn_ip == client_vpn_ip &&
            record->client_vpn_port == client_vpn_port)
        {
            if (before)
            {
                before->next = record->next;
            }
            if (record == enc_table)
            {
                enc_table = enc_table->next;
            }
            free_enc(record);
            break;
        }
        record = record->next;
    }
}

/**
 * Free memory allocated for a enc_record structure.
 * 
 * @param record A pointer to the enc_record structure to be freed
 */
void free_enc(struct enc_record *record)
{
    free(record->packet_buf);
    free(record);
}

/**
 * Get a random hash_code.
 * 
 */
unsigned int get_hash_code()
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    srand((unsigned)(now.tv_sec));
    unsigned int hash_code = rand();
    srand((unsigned)(now.tv_nsec));
    hash_code = hash_code * rand();
    return hash_code;
}
