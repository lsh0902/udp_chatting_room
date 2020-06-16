#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<fcntl.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/stat.h>
#include<pthread.h>
#include<netdb.h>
#include"type.h"

#define ROOM_NAME_LEN 64

int sock;
struct sockaddr_in server_addr;
struct sockaddr_in self_addr;
struct sockaddr_in peer_list[100];
unsigned int room_num = 0;
int peer_num = 0;
char room_name[ROOM_NAME_LEN];
pthread_mutex_t stdout_lock;
pthread_mutex_t peer_list_lock;

//=========function prototype========//
void parse_args(int argc, char **argv);
void *read_input(void *ptr);
void receive_pkt();

//send request
void create_room_request(char * msg);
void enter_room_request(int new_room_num);
void leave_room_request();
void request_available_rooms();
void room_info_request();

//server reply
void create_room_reply(packet *pkt);
void enter_room_reply(packet *pkt);
void leave_room_reply(packet *pkt);
void receive_available_rooms(packet *pkt);

//message
void send_message(char *msg);
void receive_message(struct sockaddr_in *sender_addr, packet *pkt);

//manage connection
void reply_to_ping(struct sockaddr_in *sender_addr);
void user_connection_updates(packet *pkt);

int main(int argc, char ** argv){
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0){
        	fprintf(stderr, "%s\n", "error - error creating socket.");
		    return -1;
    }
    parse_args(argc, argv);

    if (bind(sock, (struct sockaddr *)&self_addr, sizeof(self_addr))) {
		fprintf(stderr, "%s\n", "error - error binding.");
		return -1;
	}

    pthread_t input_thread;
    pthread_create(&input_thread, NULL, read_input, NULL);
	pthread_detach(input_thread);

	receive_pkt();
}


void parse_args(int argc, char **argv){
    if (argc != 4) {
		fprintf(stderr, "More Argument needed\n");
	}
    short server_port = atoi(argv[2]);
    short self_port = atoi(argv[3]);
    char server_ip[20];
    memcpy(server_ip, argv[1], (strlen(argv[1]) + 1 > sizeof(server_ip)) ? sizeof(server_ip) : strlen(argv[1]));

    self_addr.sin_family = AF_INET;
    server_addr.sin_family = AF_INET; 
	if (inet_aton(server_ip, &server_addr.sin_addr) == 0) {
		fprintf(stderr, "error in parsing server ip.");
		abort();
	}
    server_addr.sin_port = htons(server_port);
}

void *read_input(void *ptr){
    char line[1000];
    char *p;

    while(1){
        memset(line, 0, sizeof(line));
        p = fgets(line, sizeof(line), stdin);
        if(p == NULL){
            pthread_mutex_lock(&stdout_lock);
            fprintf(stderr, "read input error!\n");
            pthread_mutex_unlock(&stdout_lock);
            continue;
        }
        if (line[strlen(line) - 1] != '\n') {
            //flush
			scanf ("%*[^\n]"); 
			(void) getchar ();
		}
        line[strlen(line) - 1] = '\0';

        if (line[0] != '-') {
			pthread_mutex_lock(&stdout_lock);
			fprintf(stderr, "input format is not correct. ex) -r\n");
			pthread_mutex_unlock(&stdout_lock);
			continue;
		}
        int new_room_num;
        switch (line[1]) {
			case 'c': 
				create_room_request(line + 3);
				break;
			case 'j':
				new_room_num = atoi(line + 3);
				if (new_room_num < 0) {
					pthread_mutex_lock(&stdout_lock);
					fprintf(stderr, "%s\n", "error - room number invalid.");
					pthread_mutex_unlock(&stdout_lock);
				}
				else {
					enter_room_request(new_room_num);
				}
				break;
			case 'l':
				leave_room_request();
				break;
			case 'm':
				send_message(line + 3);
				break;
			case 'r':
				request_available_rooms();
				break;
			case 'i':
				room_info_request();
				break;
			default:
				pthread_mutex_lock(&stdout_lock);
				fprintf(stderr, "%s\n", "error - request type unknown.");
				pthread_mutex_unlock(&stdout_lock);
				break;
		}
    }
    return NULL;
}

void receive_pkt(){
    struct sockaddr_in sender_addr;
    socklen_t addrlen = 10;
    packet pkt;
    int status;

    while(1){
        status = recvfrom(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&sender_addr, &addrlen);
        if(status == -1){
            pthread_mutex_lock(&stdout_lock);
			fprintf(stderr, "error in receiving a packet");
			pthread_mutex_unlock(&stdout_lock);
			continue;
        }

        switch(pkt.header.type){
            case 'c': 
				create_room_reply(&pkt);
				break;
			case 'j':
				enter_room_reply(&pkt);
				break;
			case 'l':
				leave_room_reply(&pkt);
				break;
			case 'u': 
				user_connection_updates(&pkt);
				break;
			case 'r':
				receive_available_rooms(&pkt);
				break;
			case 'm':
				receive_message(&sender_addr, &pkt);
				break;
			case 'p':
				reply_to_ping(&sender_addr);
				break;
			default:
				pthread_mutex_lock(&stdout_lock);
				fprintf(stderr, "received packet type error!\n");
				pthread_mutex_unlock(&stdout_lock);
				break;
        }
    }
}

//send request
void create_room_request(char * msg){
    //check room name
    if (msg[0] == '\0') {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "%s\n", "error - no room name.\n");
		pthread_mutex_unlock(&stdout_lock);
		return; 
	}

    packet pkt;
    pkt.header.type = 'c';
	pkt.header.error = '\0';
    strcpy(pkt.header.room_name, msg);
    int status = sendto(sock, &pkt, sizeof(pkt.header), 0, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in));
	if (status == -1) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "error in sending packet to tracker\n");
		pthread_mutex_unlock(&stdout_lock);
	}
}

void enter_room_request(int new_room_num){
    packet pkt;
	pkt.header.room = new_room_num;
	pkt.header.type = 'j';
	pkt.header.error = '\0';
	pkt.header.payload_length = 0;

    int status = sendto(sock, &pkt, sizeof(pkt.header), 0, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in));
	if (status == -1) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "error in sending packet to tracker\n");
		pthread_mutex_unlock(&stdout_lock);
	}
}

void leave_room_request(){
    packet pkt;
	pkt.header.type = 'l';
	pkt.header.error = '\0';
	pkt.header.room = room_num;
	pkt.header.payload_length = 0;

	// send packet to tracker
	int status = sendto(sock, &pkt, sizeof(pkt.header), 0, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in));
	if (status == -1) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "error in sending packet to tracker\n");
		pthread_mutex_unlock(&stdout_lock);
	}
}

void request_available_rooms(){
    packet pkt;
	pkt.header.type = 'r';
	pkt.header.error = '\0';
	pkt.header.room = room_num;
	pkt.header.payload_length = 0;

	// send packet to tracker
	int status = sendto(sock, &pkt, sizeof(pkt.header), 0, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in));
	if (status == -1) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "%s\n", "error - error sending packet to tracker");
		pthread_mutex_unlock(&stdout_lock);
	}
}

void room_info_request(){
    if (peer_num == 0) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "%s\n", "error - you are not in any room!");
		pthread_mutex_unlock(&stdout_lock);
	}
    else{
        pthread_mutex_lock(&stdout_lock);
        fprintf(stderr, "your room number %d names %s", room_num, room_name);

        printf("%s\n", "member: ");
		int i;
		char *peer_ip;
		short peer_port;
		for (i = 0; i < peer_num; i++) {
			peer_ip = inet_ntoa(peer_list[i].sin_addr);
			peer_port = htons(peer_list[i].sin_port);
			printf("%s:%d\n", peer_ip, peer_port);
		}
        pthread_mutex_unlock(&stdout_lock);
    }
}


//server reply
void create_room_reply(packet *pkt){
    char error = pkt->header.error;
	if (error != '\0') {
		pthread_mutex_lock(&stdout_lock);
		if (error == 'o') {
			fprintf(stderr, "%s\n", "error - the application is out of chatroom!");
		}
		else if (error == 'e') {
			fprintf(stderr, "%s\n", "error - you already exist in a chatroom!");
		}
		else {
			fprintf(stderr, "%s\n", "error - unspecified error.");
		}
		pthread_mutex_unlock(&stdout_lock);
		return;
	}

    pthread_mutex_lock(&peer_list_lock);
	room_num = pkt->header.room;
	peer_num = 1;
    strcpy(room_name, pkt->header.room_name);
	memcpy(peer_list, &self_addr, sizeof(struct sockaddr_in));
	pthread_mutex_unlock(&peer_list_lock);
	pthread_mutex_lock(&stdout_lock);
	printf("You've created and joined chatroom %s [%d] \n", room_name, room_num);
	pthread_mutex_unlock(&stdout_lock);
}

void enter_room_reply(packet *pkt){
    char error = pkt->header.error;
	if (error != '\0') {
		pthread_mutex_lock(&stdout_lock);
		if (error == 'f') {
			fprintf(stderr, "%s\n", "error - the chatroom is full!");
		}
		else if (error == 'e') {
			fprintf(stderr, "%s\n", "error - the chatroom does not exist!");
		}
		else if (error == 'a') {
			fprintf(stderr, "%s\n", "error - you are already in that chatroom!");
		}
		else {
			fprintf(stderr, "%s\n", "error - unspecified error.");
		}
		pthread_mutex_unlock(&stdout_lock);
		return;
	}

    pthread_mutex_lock(&peer_list_lock);
	room_num = pkt->header.room;
    peer_num = pkt->header.payload_length / sizeof(struct sockaddr_in);
	if (peer_num <= 0) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "peer list missing, can't join chatroom, leaving old chatroom if switching.");
		pthread_mutex_unlock(&stdout_lock);
		room_num = 0;
		peer_num = 0;
	}
	else {
		memcpy(peer_list, pkt->payload, peer_num * sizeof(struct sockaddr_in));
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "you have joined chatroom %d", room_num);
		pthread_mutex_unlock(&stdout_lock);
	}
	pthread_mutex_unlock(&peer_list_lock);
}

void leave_room_reply(packet *pkt){
    char error = pkt->header.error;
	if (error != '\0') {
		pthread_mutex_lock(&stdout_lock);
		if (error == 'e') {
			fprintf(stderr, "you are not in chatroom!\n");
		}
		else {
			fprintf(stderr, "unknown error occured\n");
		}
		pthread_mutex_unlock(&stdout_lock);
		return;
	}

    pthread_mutex_lock(&peer_list_lock);
	room_num = 0;
	peer_num = 0;
    memset(room_name,0,ROOM_NAME_LEN);
	pthread_mutex_unlock(&peer_list_lock);

	pthread_mutex_lock(&stdout_lock);
	printf("you have left the chatroom.\n");
	pthread_mutex_unlock(&stdout_lock);
}

void receive_available_rooms(packet *pkt){
	pthread_mutex_lock(&stdout_lock);
	printf("Room List: \n%s", pkt->payload);
	pthread_mutex_unlock(&stdout_lock);
}

//message
void send_message(char *msg){
    if (msg[0] == '\0') {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "%s\n", "error - no message content.");
		pthread_mutex_unlock(&stdout_lock);
	}

    //make packet
    packet pkt;
    pkt.header.room = room_num;
	pkt.header.type = 'm';
	pkt.header.payload_length = strlen(msg) + 1;
	memcpy(pkt.payload, msg, pkt.header.payload_length);
	pkt.header.error = '\0';
	
    //send to every peer
    int status;
    pthread_mutex_lock(&peer_list_lock);
	for (int i = 0; i < peer_num; i++) {
		status = sendto(sock, &pkt, sizeof(pkt.header) + pkt.header.payload_length, 0, (struct sockaddr *)&(peer_list[i]), sizeof(struct sockaddr_in));
		if (status == -1) {
			pthread_mutex_lock(&stdout_lock);
			fprintf(stderr, "cannot send packet to peer num %d \n", i);
			pthread_mutex_unlock(&stdout_lock);
		}
	}
	pthread_mutex_unlock(&peer_list_lock);
}
void receive_message(struct sockaddr_in *sender_addr, packet *pkt){
    char *sender_ip = inet_ntoa(sender_addr->sin_addr);
	short sender_port = htons(sender_addr->sin_port);

	// if received message is from same room
	if (pkt->header.room == room_num) {
		pthread_mutex_lock(&stdout_lock);
		printf("%s:%d : %s\n", sender_ip, sender_port, pkt->payload);
		pthread_mutex_unlock(&stdout_lock);
	}
}

//manage connection
void reply_to_ping(struct sockaddr_in *sender_addr){
    packet pkt;
	pkt.header.type = 'p';
	pkt.header.error = '\0';
	pkt.header.payload_length = 0;

    int status = sendto(sock, &pkt, sizeof(pkt.header), 0, (struct sockaddr *)sender_addr, sizeof(struct sockaddr_in));
	if (status == -1) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "replying to ping error!\n");
		pthread_mutex_unlock(&stdout_lock);
	}
}

void user_connection_updates(packet *pkt){
    pthread_mutex_lock(&peer_list_lock);
	int new_peer_num = pkt->header.payload_length / sizeof(struct sockaddr_in);
	if (new_peer_num <= 0) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "error : peer list missing.\n");
		pthread_mutex_unlock(&stdout_lock);
	}
	else {
		pthread_mutex_lock(&stdout_lock);
		printf("room is updated!\n");
		pthread_mutex_unlock(&stdout_lock);
		peer_num = new_peer_num;
		memcpy(peer_list, pkt->payload, peer_num * sizeof(struct sockaddr_in));
	}
	pthread_mutex_unlock(&peer_list_lock);
}
