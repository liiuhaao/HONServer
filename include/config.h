#ifndef CONFIG_H
#define CONFIG_H

struct HONConfig
{
    int drop_rate;
    int parity_rate;
    int max_RX_num;
    int max_TX_num;
    long encode_timeout;
    long decode_timeout;
    long rx_timeout;
    int primary_probability;
};

extern struct HONConfig config;

void parse_config(const char *json_string, struct HONConfig *config);

void serialize_config(struct HONConfig *config, char *buffer);


#endif