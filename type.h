typedef struct header_t {
        char type;
        char error;
        unsigned int room;
        char room_name[64];
        unsigned int payload_length;
} header;

typedef struct packet_t {
        struct header_t header;
        char payload[512];
} packet;

