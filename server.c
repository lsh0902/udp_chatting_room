#include<stdio.h>
#include<stdlib.h>
#include<stdbool.h>
#include<string.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/stat.h>
#include<time.h>
#include<fcntl.h>
#include<math.h>
#include<pthread.h>
#include<netdb.h>
#include<netinet/in.h>
#include<locale.h>
#include<limits.h>
#include<errno.h>
#include"type.h"

#define MAX_ROOM_SIZE 7
#define MAX_CAPACITY 5
#define ROOM_NAME_LEN 64

//def struct
typedef struct ROOM {
	unsigned int num;
	char name[ROOM_NAME_LEN];
}*PROOM;

typedef struct peer {
	char ip_port_address[20];
	struct ROOM room_info;
	short alive;
	struct peer *Next;
}*Ppeer;

//global variable
struct peer* head;
int sock;
int ping_sock;
pthread_mutex_t stdout_lock;
pthread_mutex_t peer_lock;

//====function prototype line====//
short parse_args(int argc, char **argv);
//linked-list function
void ADD_PEER(struct peer* p);
struct peer * FIND_PEER(char * str);
void PEER_DEL(struct peer * s);
//ping function
void send_pings();
void delete_peer();
void * user_input(void * ptr);
void * ping_input(void * ptr);
void * ping_output(void * ptr);
void check_peer(unsigned int ip, short port);
//peer and room function
void peer_create_room(struct sockaddr_in addr, packet* pkt_recv);
void peer_leave_room(struct sockaddr_in addr);
void peer_join_room(struct sockaddr_in addr, unsigned int room);
void room_list(struct sockaddr_in addr);
void peer_list(unsigned int ip, short port, unsigned int room);
void send_error(struct sockaddr_in addr, char error_type, char error);
int get_total_room_number();
char* get_room_name(int num);
//get address info function
unsigned int get_ip(char * ip_port);
short get_port(char* ip_port);
struct sockaddr_in get_sockaddr_in(unsigned int ip, short port);

int main(int argc, char **argv) {
	//peer for linked-list
	head = (struct peer*)malloc(sizeof(struct peer));
	head->Next = NULL;
	//parse argc
	short port = parse_args(argc, argv);
	fprintf(stderr, "Start server. Port number : %d, %d\n", port, port + 1);

	//udp socket
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	ping_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		fprintf(stderr, "socket create error!");
		return -1;
	}
	if(ping_sock < 0) {
		fprintf(stderr, "socket create error!");
		return -1;
	}

	struct sockaddr_in self_addr;
	self_addr.sin_family = AF_INET;
	self_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	self_addr.sin_port = htons(port);
	struct sockaddr_in ping_addr;
	ping_addr.sin_family = AF_INET;
	ping_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	ping_addr.sin_port = htons(port + 1);

	if (bind(sock, (struct sockaddr *)&self_addr, sizeof(self_addr))) {
		fprintf(stderr, "%s\n", "error - error binding sock.");
		return -1;
	}
	if (bind(ping_sock, (struct sockddr *)&ping_addr, sizeof(ping_addr))) {
		fprintf(stderr, "%s\n", "error - error binding ping_sock.");
		return -1;
	}

	//thread for manage ping
	pthread_t ping_input_thread;
	pthread_t ping_output_thread;
	pthread_t input_thread;

	pthread_create(&ping_input_thread, NULL, ping_input, NULL);
	pthread_create(&ping_output_thread, NULL, ping_output, NULL);
	pthread_create(&input_thread, NULL, user_input, NULL);

	pthread_detach(ping_input_thread);
	pthread_detach(ping_output_thread);
	pthread_detach(input_thread);

	socklen_t addrlen = 10;
	struct sockaddr_in sender_addr;
	packet recv_pkt;
	int status;
	
	while (1) {
		status = recvfrom(sock, &recv_pkt, sizeof(recv_pkt), 0, (struct sockaddr *)&sender_addr, &addrlen);

		if (status == -1) {
			pthread_mutex_lock(&stdout_lock);
			fprintf(stderr, "error in received packet. discard it.\n");
			pthread_mutex_unlock(&stdout_lock);
		}
		else {
			unsigned int ip = sender_addr.sin_addr.s_addr;
			short port = htons(sender_addr.sin_port);
			struct sockaddr_in addr;
			addr.sin_family = AF_INET;
			addr.sin_addr.s_addr = ip;
			addr.sin_port = htons(port);

			switch (recv_pkt.header.type) {
				case 'c':
					peer_create_room(addr, &recv_pkt);
					break;
				case 'j':
					peer_join_room(addr, recv_pkt.header.room);
					break;
				case 'l' :
					peer_leave_room(addr);
					break;
				case 'r':
					room_list(addr);
					break;
				default:
					pthread_mutex_lock(&stdout_lock);
					fprintf(stderr, "error in received packet type.");
					pthread_mutex_unlock(&stdout_lock);
					break;
			}
		}
	}
	return 0;
}


short parse_args(int argc, char **argv) {
	if (argc < 2){
		return 8080;
	}
	else	{
		errno = 0;
		char *endptr = NULL;
		unsigned long ulPort = strtoul(argv[1], &endptr, 10);

		if (0 == errno)		{
			// if input range over
			if ('\0' != endptr[0])
				errno = EINVAL;
			else if (ulPort > USHRT_MAX)
				errno = ERANGE;
		}
		if (0 != errno)	{
			fprintf(stderr, "Failed to parse port number \"%s\": %s\n",
				argv[1], strerror(errno));
			abort();
		}
		return ulPort;
	}
}

//linked-list function
void ADD_PEER(struct peer* p) {
	struct peer* q = head;
	while (q->Next != NULL) {
		q = q->Next;
	}
	q->Next = p;
	p->Next = NULL;
}

struct peer* FIND_PEER(char * str) {
	struct 	peer* cursor = NULL;
	for (struct peer* iter = head->Next; iter != NULL; iter = iter->Next) {
		if (!strcmp(iter->ip_port_address, str)) {
			cursor = iter;
			break;
		}
	}
	return cursor;
}

void PEER_DEL(struct peer* s) {
	for (struct peer *iter = head; iter != NULL; iter = iter->Next) {
		if (s == iter->Next) {
			iter->Next = s->Next;
			break;
		}
	}
	free(s);
}

//ping function
void send_pings() {
	struct peer *p;
	for (p = head->Next; p != NULL; p = p->Next) {
		//0 = dead
		pthread_mutex_lock(&peer_lock);
		p->alive = 0;
		pthread_mutex_unlock(&peer_lock);

		unsigned int peer_ip = get_ip(p->ip_port_address);
		short peer_port = get_port(p->ip_port_address);
		packet pkt;
		pkt.header.type = 'p';
		pkt.header.error = '\0';
		pkt.header.payload_length = 0;
		struct sockaddr_in peer_addr = get_sockaddr_in(peer_ip, peer_port);
		int status = sendto(ping_sock, &pkt, sizeof(pkt.header),0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));

		if (status == -1) {
			pthread_mutex_lock(&stdout_lock);
			fprintf(stderr, "error in sending packet to peer");
			pthread_mutex_unlock(&stdout_lock);
		}
	}
}
void delete_peer() {
	struct peer *cur;
	for (cur = head->Next; cur != NULL; cur = cur->Next) {
		if (cur->alive == 0) {
			unsigned int peer_ip = get_ip(cur->ip_port_address);
			short peer_port = get_port(cur->ip_port_address);
			struct sockaddr_in peer_addr = get_sockaddr_in(peer_ip, peer_port);
			peer_leave_room(peer_addr);
		}
	}
}

void * user_input(void * ptr) {
	char input_line[100];
	char *p;
	while (1) {
		memset(input_line, 0, sizeof(input_line));
		p = fgets(input_line, sizeof(input_line), stdin);
		if (input_line[strlen(input_line) - 1] != '\n') {
			scanf("%*[^\n");
			(void)getchar();
		}
		input_line[strlen(input_line) - 1] = '\0';
		if (input_line[0] == '-' && input_line[1] == 'i') {
			if (head->Next == NULL) {
				pthread_mutex_lock(&stdout_lock);
				fprintf(stderr, "There is no room created.\n");
				pthread_mutex_unlock(&stdout_lock);
			}
			else{
				for (struct peer* cur = head->Next; cur != NULL; cur = cur->Next) {
					pthread_mutex_lock(&stdout_lock);
					fprintf(stderr, "%s in room %d - %s\n", cur->ip_port_address, cur->room_info.num, cur->room_info.name);
					pthread_mutex_unlock(&stdout_lock);
				}
			}
		}
	}
}
void * ping_input(void * ptr) {
	socklen_t addrlen = 10;
	struct sockaddr_in sender_addr;
	packet recv_pkt;
	int recv_status;

	while (1) {
		//check ping socket - mark sender alive
		recv_status = recvfrom(ping_sock, &recv_pkt, sizeof(recv_pkt), 0, (struct sockaddr *)&sender_addr, &addrlen);
		if (recv_status == -1) {
			pthread_mutex_lock(&stdout_lock);
			fprintf(stderr, "error in receiving a packet, ignoring.");
			pthread_mutex_unlock(&stdout_lock);
		}
		else {
			unsigned int ip = sender_addr.sin_addr.s_addr;
			short port = htons(sender_addr.sin_port);
			switch (recv_pkt.header.type) {
			case 'p':
				check_peer(ip, port);
				break;
			default:
				pthread_mutex_lock(&stdout_lock);
				fprintf(stderr, "error - received packet type unknown.");
				pthread_mutex_unlock(&stdout_lock);
				break;
			}
		}
	}
	return NULL;
}

void * ping_output(void * ptr) {

	clock_t t;
	send_pings();
	t = clock();
	while (1) {
		if ((float)(clock() - t) / CLOCKS_PER_SEC >= 5) { //ping time interval over
			pthread_mutex_lock(&stdout_lock);
			fprintf(stderr, "Checking peer alive . \n");
			pthread_mutex_unlock(&stdout_lock);
			delete_peer();
			send_pings();
			t = clock();
		}
	}
}

void check_peer(unsigned int ip, short port) {

	char ip_port_add[20];
	memset(ip_port_add, 0, sizeof(ip_port_add));
	char* ip_port_address_format = (char *)"%d:%d";
	sprintf(ip_port_add, ip_port_address_format, ip, port);
	struct peer *p;
	
	p = FIND_PEER(ip_port_add);
	
	if (p != NULL) { //if there is p
		pthread_mutex_lock(&peer_lock);
		p->alive = 1;
		pthread_mutex_unlock(&peer_lock);
	}
	else { // stay p->alive = 0; 
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "%s\n", "peer not found for ping response");
		pthread_mutex_unlock(&stdout_lock);
	}
}
//peer and room function
void peer_create_room(struct sockaddr_in addr, packet* pkt_recv) {
	int number_of_rooms = get_total_room_number();
	short port = htons(addr.sin_port);
	unsigned int ip = addr.sin_addr.s_addr;

	if (number_of_rooms >= MAX_ROOM_SIZE) {
		send_error(addr, 'c', 'o');
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "Peer create room failed - max number of rooms reached.\n");
		pthread_mutex_unlock(&stdout_lock);
		return;
	}

	//get room num
	struct peer *p;
	bool flag = false;
	unsigned int room;
	for (room = 1; room < MAX_ROOM_SIZE * 2; room++) {
		for (p = head->Next; p != NULL; p = p->Next) {
			if (p->room_info.num == room) {
				flag = true;
				break;
			}
		}
		if(!flag) break;
		else flag = false;
	}

  	pthread_mutex_lock(&stdout_lock);
  	fprintf(stderr, "create room num %d\n", room);
  	pthread_mutex_unlock(&stdout_lock);

	//new peer
	struct peer *new_peer;
	new_peer = (struct peer *)malloc(sizeof(struct peer));
	char* ip_port_address_format = (char *)"%d:%d";
	sprintf(new_peer->ip_port_address, ip_port_address_format, ip, port);
	new_peer->room_info.num = room;
	strcpy(new_peer->room_info.name, pkt_recv->header.room_name);
	new_peer->alive = 1;

	p = FIND_PEER(new_peer->ip_port_address);
	if(p != NULL){
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "Peer create room failed - already in a room.\n");
		pthread_mutex_unlock(&stdout_lock);
		send_error(addr, 'c', 'e');
	}
	else{
		pthread_mutex_lock(&peer_lock);
		ADD_PEER(new_peer);
		pthread_mutex_unlock(&peer_lock);
		//print lock
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "%s created room %s[%d]\n", new_peer->ip_port_address, new_peer->room_info.name, room);
 		pthread_mutex_unlock(&stdout_lock);

		 packet pkt;
		 pkt.header.type = 'c';
		pkt.header.error = '\0';
		pkt.header.room = room;
		strcpy(pkt.header.room_name, new_peer->room_info.name);

		int status = sendto(sock, &pkt, sizeof(pkt.header), 0, (struct sockaddr *)&addr, sizeof(addr));

		if (status == -1) {
			pthread_mutex_lock(&stdout_lock);
			fprintf(stderr, "error in sending packet to peer\n");
			pthread_mutex_unlock(&stdout_lock);
    	}
	}
}

void peer_leave_room(struct sockaddr_in addr){

	short port = htons(addr.sin_port);
	unsigned int ip = addr.sin_addr.s_addr;
	char ip_port_addr[20];
	memset(ip_port_addr, 0, sizeof(ip_port_addr));

	 char* ip_port_address_format = (char *)"%d:%d";
  sprintf(ip_port_addr, ip_port_address_format, ip, port);
  struct peer *p;
  
   p = FIND_PEER(ip_port_addr);
   if(p != NULL){
	unsigned int left_room = p->room_info.num;
    pthread_mutex_lock(&peer_lock);
    PEER_DEL(p);
    
    pthread_mutex_unlock(&peer_lock);
    pthread_mutex_lock(&stdout_lock);
    fprintf(stderr, "%s left %d\n", ip_port_addr, left_room);
    pthread_mutex_unlock(&stdout_lock);

		packet pkt;
		pkt.header.type = 'l';
		pkt.header.error = '\0';
		pkt.header.payload_length = 0;

		int status = sendto(sock,&pkt, sizeof(pkt.header), 0, (struct sockaddr *)&addr, sizeof(addr));
		if(status == -1){
			 pthread_mutex_lock(&stdout_lock);
				fprintf(stderr, "error in sending packet to peer\n");
				pthread_mutex_unlock(&stdout_lock);
		}
		peer_list(0, -1, left_room);
   }
   else{
	   send_error(addr, 'l', 'e');
   }
}
void peer_join_room(struct sockaddr_in addr, unsigned int room){
	struct peer *p;
	int count = 0;
	bool room_exist = false;
	short port = htons(addr.sin_port);
	unsigned int ip = addr.sin_addr.s_addr;
	int room_update_num = -1;
	//check if the room is exist and available
	for(p = head->Next; p!= NULL; p=p->Next){
		if(p->room_info.num == room){
			count++;
			if(count>=MAX_CAPACITY){
				pthread_mutex_lock(&stdout_lock);
				fprintf(stderr, "Peer join failed - room is full.\n");
				pthread_mutex_unlock(&stdout_lock);
				send_error(addr, 'j', 'f');
				return;
			}
			room_exist = true;
		}
	}
	if(!room_exist){
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "Peer join failed - room does not exist.\n");
		pthread_mutex_unlock(&stdout_lock);
		send_error(addr, 'j', 'e');
		return;
	}

	//set new peer to join
	struct peer *new_peer;
	new_peer = (struct peer *)malloc(sizeof(struct peer));
	char* ip_port_address_format = (char *)"%d:%d";
	sprintf(new_peer->ip_port_address, ip_port_address_format, ip, port);
	new_peer->room_info.num = room;
	strcpy(new_peer->room_info.name, get_room_name(room));
	new_peer->alive = 1;


	p = FIND_PEER(new_peer->ip_port_address);
	if(p!= NULL && p->room_info.num == room){
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "Peer join failed - peer is already in the room.\n");
		pthread_mutex_unlock(&stdout_lock);
		send_error(addr, 'j', 'a');
		return;
	}
	if(p == NULL){
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "new joining now to exist room.\n");
		pthread_mutex_unlock(&stdout_lock);

		pthread_mutex_lock(&peer_lock);
		ADD_PEER(new_peer);
		pthread_mutex_unlock(&peer_lock);		
	}
	else{
		//if peer is in another room. switch it.
		room_update_num = p->room_info.num;
		pthread_mutex_lock(&peer_lock);
		PEER_DEL(p);
		ADD_PEER(new_peer);
		pthread_mutex_unlock(&peer_lock);	

		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "move peer to requested room.\n");
		pthread_mutex_unlock(&stdout_lock);
	}
	if(room_update_num != -1){
		//if update needed
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "%s peer switched from %d to %d.\n", new_peer->ip_port_address, room_update_num, room);
		pthread_mutex_unlock(&stdout_lock);
		peer_list(ip, port, room);
		peer_list(0, -1, room_update_num);
	}
	else{
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "%s joined %d\n", new_peer->ip_port_address, room);
		pthread_mutex_unlock(&stdout_lock);
		peer_list(ip, port, room);
	}
}

void room_list(struct sockaddr_in addr){
	int number_of_rooms = get_total_room_number();
	int max_room_num_len;
	unsigned int room_indexed[number_of_rooms];
	memset(room_indexed, 0, number_of_rooms * sizeof(room_indexed[0]));
	unsigned int room_nums[number_of_rooms];
	memset(room_nums, 0, number_of_rooms * sizeof(room_nums[0]));
	int room_stats[number_of_rooms];
	memset(room_stats, 0, number_of_rooms * sizeof(room_stats[0]));
	char * room_name[number_of_rooms];

	//each room num, name, peer num
	for(int i=0;i<number_of_rooms;i++)
		room_name[i] = (char*)malloc(sizeof(char)* 64);
	for(int i=0;i<number_of_rooms;i++)
		room_nums[i] = i + 1;

	struct peer *p;
	int max_occupants=0;
	unsigned int max_room_number =0;
	int num_rooms_indexed = 0;
	for(p = head->Next; p!=NULL; p = p->Next){
		int room_index = -1;
		unsigned int save_index;
		for(save_index=0; save_index<number_of_rooms; save_index++){
			if(room_nums[save_index]==p->room_info.num){
				room_index=save_index;
				break;
      		}
    	}
		if(room_indexed[room_index]==0){
			room_index = num_rooms_indexed;
			room_nums[room_index] = p->room_info.num;
			room_stats[room_index] = 1;
			room_indexed[room_index]= 1;
			strcpy(room_name[room_index] , p->room_info.name);
			num_rooms_indexed = num_rooms_indexed + 1;
		}
		else{
			room_stats[room_index] = room_stats[room_index] + 1;
		}
		if(room_stats[room_index]>max_occupants){
			max_occupants=room_stats[room_index];
		}
		if(room_nums[room_index]>max_room_number){
			max_room_number=room_nums[room_index];
		}
	}

	if(max_room_number==0){
		max_room_num_len = 1;
	}
	else{
		max_room_num_len = (int)floor(log10((float)max_room_number)) + 1;
	}
	//set packet
	int MAX_CAPACITY_len = (int)floor(log10((float)MAX_CAPACITY)) + 1;
	int max_num_room_len = (int)floor(log10((float)max_occupants)) + 1;
	char *list_entry_format = (char *)"room %d : %s- %d/%d\n";
	int list_entry_size = max_room_num_len+max_num_room_len+MAX_CAPACITY_len+strlen(list_entry_format) + 64 * number_of_rooms; //length setting. 64?
	int list_size = max_num_room_len*list_entry_size;
	char *list_entry = (char *)malloc(list_entry_size);
	char *list = (char *)malloc(list_size);
	unsigned int i;
	char *list_i = list;
	for(i=0; i<sizeof(room_stats)/sizeof(room_stats[0]); i++){
		sprintf(list_entry, list_entry_format, room_nums[i], room_name[i], room_stats[i], MAX_CAPACITY);
		strcpy(list_i, list_entry);
		list_i += strlen(list_entry);
	}
	if(number_of_rooms==0){
		list=(char*)"There are no chatrooms\n";
	}
	pthread_mutex_lock(&stdout_lock);
	fprintf(stderr, "room list\n%s\n", list);
	pthread_mutex_unlock(&stdout_lock);

	packet pkt;
	pkt.header.type = 'r';
	pkt.header.error = '\0';
	pkt.header.payload_length = list_size;
	strcpy(pkt.payload, list);
	
	int status = sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&addr, sizeof(addr));
	if(status == -1){
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "error occured in sending packet to peer");
		pthread_mutex_unlock(&stdout_lock);
	}
}
void peer_list(unsigned int ip, short port, unsigned int room){
	struct peer * p;
	char name_of_room[ROOM_NAME_LEN];
	int num_in_room = 0;

	for(p = head->Next; p != NULL; p = p->Next){
		if(p->room_info.num==room){
			num_in_room = num_in_room+1;
		}
	}
	for(p = head->Next; p != NULL; p = p->Next){
		if(p->room_info.num==room){
			strcpy(name_of_room, p->room_info.name);
			break;
		}
	}

	struct sockaddr_in list[num_in_room];
  	int count = 0;
	for(p = head->Next; p != NULL; p = p->Next){
		if(p->room_info.num == room){
			unsigned int peer_ip = get_ip(p->ip_port_address);
			short peer_port = get_port(p->ip_port_address);
			struct sockaddr_in peer_info = get_sockaddr_in(peer_ip, peer_port);
			struct sockaddr_in* peer_info_ptr = &peer_info;
			memcpy((struct sockaddr_in*)&list[count], peer_info_ptr, sizeof(peer_info));
			count = count + 1;
		}
	}

	packet update_pkt;
	update_pkt.header.type = 'u';
	update_pkt.header.error = '\0';
	update_pkt.header.payload_length = num_in_room * sizeof(struct sockaddr_in);
	memcpy(update_pkt.payload, list, num_in_room * sizeof(struct sockaddr_in));
	for(p = head->Next ; p != NULL; p = p->Next){
		if(p->room_info.num == room){
			unsigned int peer_ip = get_ip(p->ip_port_address);
			short peer_port = get_port(p->ip_port_address);
			if( (port!= -1 && ip != 0) && (peer_ip == ip && peer_port == port)){
				//set join packet
				packet join_pkt;
				join_pkt.header.type = 'j';
				join_pkt.header.error = '\0';
				join_pkt.header.room = room;
				strcpy(join_pkt.header.room_name, name_of_room);

				join_pkt.header.payload_length = num_in_room * sizeof(struct sockaddr_in);
				memcpy(join_pkt.payload, list, num_in_room * sizeof(struct sockaddr_in));
				struct sockaddr_in peer_addr = get_sockaddr_in(peer_ip, peer_port);
				int status = sendto(sock, &join_pkt, sizeof(join_pkt), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
				if (status == -1) {
					pthread_mutex_lock(&stdout_lock);
					fprintf(stderr, "%s\n", "error - error sending packet to peer");
					pthread_mutex_unlock(&stdout_lock);
				}
			}
			else{
				struct sockaddr_in peer_addr = get_sockaddr_in(peer_ip, peer_port);
				int status = sendto(sock, &update_pkt, sizeof(update_pkt), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
				if (status == -1) {
					pthread_mutex_lock(&stdout_lock);
					fprintf(stderr, "%s\n", "error - error sending packet to peer");
					pthread_mutex_unlock(&stdout_lock);
				}	
			}
		}
	}
}

void send_error(struct sockaddr_in addr, char error_type, char error){
	packet err_pkt;
	err_pkt.header.type = error_type;
	err_pkt.header.error = error;
	err_pkt.header.payload_length = 0;

	int status = sendto(sock, &err_pkt, sizeof(err_pkt.header), 0, (struct sockaddr *)&addr, sizeof(addr));
	if(status == -1){
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "error occured in sending packet to peer");
		pthread_mutex_unlock(&stdout_lock);

	}
}

int get_total_room_number(){
	int count = 0;
	unsigned int room_found[MAX_ROOM_SIZE];
	memset(room_found, 0, MAX_ROOM_SIZE * sizeof(room_found[0]));
	unsigned int rooms[MAX_ROOM_SIZE];
	memset(rooms, 0, MAX_ROOM_SIZE * sizeof(rooms[0]));

	struct peer *p;
	int i;
	for(p = head->Next ; p!= NULL; p = p->Next){
		bool found = false;
		for(i=0;i<MAX_ROOM_SIZE;i++){
			if(rooms[i] == p->room_info.num && room_found[i] == 1){
				found = true;
				break;
			}
		}
		if(!found){
			room_found[count]=1;
			rooms[count]=p->room_info.num;
			count++;
		}
	}

	return count;
}

char* get_room_name(int num){
	for(struct peer* cur = head->Next; cur != NULL; cur = cur->Next){
    if(num == cur->room_info.num) return cur->room_info.name;
  }
}

//get address info function
unsigned int get_ip(char * ip_port){
	int i;
	for(i=0; i<20; i++){
		if(ip_port[i]==':'){
		break;
		}
	}

	char c_ip[i+1];
	strncpy(c_ip, ip_port, i);
	c_ip[i] = '\0';

	return (unsigned int)strtoul(c_ip, NULL, 0);
}

short get_port(char* ip_port){
	int i;
	int start=-1;
	int end=-1;
	//get num after :
	for(i=0; i<20; i++){
		if(start==-1 && ip_port[i]==':'){
		start=i+1;
		}
		if(ip_port[i]=='\0'){
		end=i;
		break;
		}
	}

	int index = end - start;
	char c_port[index + 1];
	strncpy(c_port, ip_port + start, index);
	c_port[index] = '\0';
	
	return (short)strtoul(c_port, NULL, 0);
}

struct sockaddr_in get_sockaddr_in(unsigned int ip, short port){
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = ip;
	addr.sin_port = htons(port);
	return addr;
}