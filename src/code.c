#include "code.h"

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
            record->touch = time(NULL);
            return record;
        }

        /* Obsolete record */
        if (record->touch + DEC_TIMEOUT < time(NULL))
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

    record->touch = time(NULL);

    record->next = dec_table ? dec_table : NULL;
    dec_table = record;

    return record;
}

int dec_put(struct dec_record *record, int index, unsigned char *block)
{
    if (!(record->marks[index]))
        return 0;
    record->receive_num++;
    record->marks[index] = 0;
    memcpy(record->data_blocks[index], block, record->block_size);
    return record->receive_num == record->data_num;
}

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

void *decode(void *args)
{
    struct dec_param *dec_p = (struct dec_param *)args;
    struct dec_record *dec = dec_p->dec;

    reed_solomon *rs = reed_solomon_new(dec->data_num, dec->block_num - dec->data_num);
    if (reed_solomon_reconstruct(rs, dec->data_blocks, dec->marks, dec->block_num, dec->block_size))
    {
        printf("Decode Error!!!");
        pthread_exit(NULL);
    }

    struct out_param *out_p = (struct out_param *)malloc(sizeof(struct out_param));
    out_p->buf = (char *)malloc(dec->data_num * dec->block_size * sizeof(char));
    for (int i = 0; i < dec->data_num; i++)
    {
        memcpy(out_p->buf + i * dec->block_size, dec->data_blocks[i], dec->block_size);
    }

    out_p->clinet_vpn_ip = dec_p->clinet_vpn_ip;
    out_p->clinet_vpn_port = dec_p->clinet_vpn_port;
    out_p->data_size = dec_p->dec->data_size;
    out_p->tun_fd = dec_p->tun_fd;
    pthread_t out_tid;

    pthread_create(&out_tid, NULL, output, (void *)out_p);

    free(dec_p);
}