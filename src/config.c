#include <stdio.h>

#include "config.h"

struct HONConfig config;

void parse_config(const char *json_str, struct HONConfig *config)
{
    sscanf(json_str, "{\n\"dropRate\": %d,\n\"parityRate\": %d,\n\"maxRXNum\": %d,\n\"maxTXNum\": %d,\n\"encodeTimeout\": %ld,\n\"decodeTimeout\": %ld,\n\"rxTimeout\": %ld,\n\"primaryProbability\": %d\n}\n",
           &config->drop_rate, &config->parity_rate, &config->max_RX_num, &config->max_TX_num, &config->encode_timeout, &config->decode_timeout, &config->rx_timeout, &config->primary_probability);
}

void serialize_config(struct HONConfig *config, char *buffer)
{
    sprintf(buffer, "{\n\"drop_rate\": %d,\n\"parity_rate\": %d,\n\"max_RX_num\": %d,\n\"max_TX_num\": %d,\n\"encode_timeout\": %ld,\n\"decode_timeout\": %ld,\n\"rx_timeout\": %ld,\n\"primary_probability\": %d\n}\n",
            config->drop_rate, config->parity_rate, config->max_RX_num, config->max_TX_num, config->encode_timeout, config->decode_timeout, config->rx_timeout, config->primary_probability);
}
