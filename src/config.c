#include <stdio.h>

#include "config.h"

struct HONConfig config;

void parse_config(const char *json_str, struct HONConfig *config)
{
    sscanf(json_str, "{\n\"dropRate\": %d,\n\"dataNum\": %d,\n\"parityNum\": %d,\n\"rxNum\": %d,\n\"encodeTimeout\": %ld,\n\"decodeTimeout\": %ld,\n\"rxTimeout\": %ld,\n\"ackTimeout\": %ld,\n\"parityDelayThres\": %ld,\n\"parityDuration\": %ld,\n\"primaryProbability\": %d,\n\"mode\": %d\n",
           &config->drop_rate, &config->data_num, &config->parity_num, &config->rx_num, &config->encode_timeout, &config->decode_timeout, &config->rx_timeout, &config->ack_timeout, &config->parity_delay_thres, &config->parity_duration, &config->primary_probability, &config->mode);
    if (config->data_num <= 0)
    {
        config->data_num = 1;
    }
}

void serialize_config(struct HONConfig *config, char *buffer)
{
    sprintf(buffer, "{\n\"drop_rate\": %d,\n\"data_num\": %d,\n\"parity_num\": %d,\n\"rx_num\": %d,\n\"encode_timeout\": %ld,\n\"decode_timeout\": %ld,\n\"rx_timeout\": %ld,\n\"ack_timeout\": %ld,\n\"parity_delay_thres\": %ld,\n\"parity_duration\": %ld,\n\"primary_probability\": %d\n\"mode\": %d\n}\n",
            config->drop_rate, config->data_num, config->parity_num, config->rx_num, config->encode_timeout, config->decode_timeout, config->rx_timeout, config->ack_timeout, config->parity_delay_thres, config->parity_duration, config->primary_probability, config->mode);
}
