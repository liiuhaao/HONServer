#ifndef CONFIG_H
#define CONFIG_H

struct HONConfig
{
    int drop_rate;
    int data_num;
    int parity_num;
    int rx_num;
    long encode_timeout;
    long decode_timeout;
    long rx_timeout;
    long ack_timeout;
    int primary_probability;
    int mode;
};

extern struct HONConfig config;

void parse_config(const char *json_string, struct HONConfig *config);

void serialize_config(struct HONConfig *config, char *buffer);


#endif