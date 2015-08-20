/**********************************************************************************************
 * This program is the server routine for the directory service. The directory is stored both *
 * in a linked list and in a file.                                                            *
 **********************************************************************************************/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netdb.h>
#include<arpa/inet.h>
#include <errno.h>


#define SERVER_IP	"127.0.0.1"	//the server's desired IP
#define SERVER_PORT	15000		//the server's desired port

#define UDP_PACKET_SIZE		65535	//the size in bytes of a UTP packet
#define DIR_ENTRY_SIZE		90	//the size in bytes of a directory entry

#define REQUEST_DISCOVERY	1
#define RESPONSE_DISCOVERY	2	
#define REQUEST_ADD		3
#define RESPONSE_ADD		4
#define REQUEST_REMOVE		5
#define RESPONSE_REMOVE		6
#define REQUEST_FIND		7
#define RESPONSE_FIND		8

#define USE_LOG_FILE
#define LOG_FILE_NAME "log/ds_log.txt"


//every node represents an entry to the directory
struct node{
	char name[64];
	char ip[16];
	int port;
	char properties[8];
	struct node *next;
	} *head;


int addNode(char *name, char *ip, int port, char *properties);	//adds an entry to the linked list
int removeNode(char *name, char *ip, int port);			//removes an entry from the linked list
struct node *findNextMatch(struct node *start, char *prefix);	//returns the next match by name/prefix starting from the *start node 
void openFileAndRead();						//opens and reads the log file and stores the data in the linked list
void writeFileAndClose();					//writes the data from the linked list back to the file


int main(int argc, char *argv[]){

	int s, ret, clientAddrLen, hitsCount;
	char prefix[64];
	struct node *i, *tmpNode;
	struct sockaddr_in serverAddress, clientAddress;
	struct ip_mreq mreq;
	unsigned char buff[UDP_PACKET_SIZE];

	//creates a socket 
	s = socket (PF_INET, SOCK_DGRAM, 0);
	if (s == -1){ perror(NULL); }
	
	//sets the server's address
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);	//INADDR_ANY refers to every IP on this mashine
	serverAddress.sin_port = htons (SERVER_PORT);

	//binds the socket to the server
	ret = bind(s, (struct sockaddr*)&serverAddress, sizeof(serverAddress));	
	if (ret == -1){ perror(NULL); }

	//binds the socket to the localhost's multicast address
	mreq.imr_multiaddr.s_addr=inet_addr("224.0.0.1");
	mreq.imr_interface.s_addr=htonl(INADDR_ANY);
	if(setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0){ printf("setsockopt() failed"); return 0; }

	//initializes the linked list
	head = NULL;

	//this is the server's main loop
	while(1){

		//waits to recieve a request from any (unknown) client		
		clientAddrLen = sizeof(clientAddress);
		ret = recvfrom(s, buff, UDP_PACKET_SIZE, 0, (struct sockaddr*)&clientAddress, &clientAddrLen);
		if (ret == -1){ perror(NULL); }

		#ifdef USE_LOG_FILE
		openFileAndRead();
		#endif

		//serves the request
		switch(buff[0]){

			case REQUEST_DISCOVERY:
				buff[0] = RESPONSE_DISCOVERY;
				strcpy(&buff[1],SERVER_IP);	
				buff[17] = (int)(SERVER_PORT/256);
				buff[18] = SERVER_PORT%256;	
				ret = sendto(s, buff, 19, 0, (struct sockaddr*)&clientAddress, clientAddrLen);
				if (ret == -1){ perror(NULL); }
				break;
			
			case REQUEST_ADD:
				buff[0] = RESPONSE_ADD;
				buff[1] = addNode(&buff[1], &buff[65], buff[81]*256+buff[82], &buff[83]);
				ret = sendto(s, buff, 2, 0, (struct sockaddr*)&clientAddress, clientAddrLen);									
				if (ret == -1){ perror(NULL); }
				break;
			
			case REQUEST_REMOVE:
				buff[0] = RESPONSE_REMOVE;
				buff[1] = removeNode(&buff[1], &buff[65], buff[81]*256+buff[82]);
				ret = sendto(s, buff, 2, 0, (struct sockaddr*)&clientAddress, clientAddrLen);									
				if (ret == -1){ perror(NULL); }
				break;

			case REQUEST_FIND:
				buff[0] = RESPONSE_FIND;
				strcpy(prefix, &buff[1]);
				hitsCount = 0;
				tmpNode = findNextMatch(head, prefix);
				while(tmpNode!=NULL && hitsCount < ((UDP_PACKET_SIZE-3) / DIR_ENTRY_SIZE)-1){
					strcpy(&buff[3+hitsCount*90], tmpNode->name);
					strcpy(&buff[3+hitsCount*90+64], tmpNode->ip);
					buff[3+hitsCount*90+80] = (int)(tmpNode->port/256);
					buff[3+hitsCount*90+81] = tmpNode->port%256;
					strcpy(&buff[3+hitsCount*90+82], tmpNode->properties);
					hitsCount++;
					tmpNode = findNextMatch(tmpNode->next, prefix);
					}
				buff[1] = (int)(hitsCount/256);
				buff[2] = hitsCount%256;
				ret = sendto(s, buff, 2+hitsCount*DIR_ENTRY_SIZE, 0, (struct sockaddr*)&clientAddress, clientAddrLen);									
				if (ret == -1){ perror(NULL); }
				break;
	
			default: 
				printf("invalid request code");
				break;
			}
	
			#ifdef USE_LOG_FILE
			writeFileAndClose();
			#endif
		}

	//for cosmetic reasons - never reaches this point!!
	close(s);

	return;
	}





int addNode(char *name, char *ip, int port, char *properties){
	struct node *i;
	
	//first searches if the node to be added allready exests 
	for(i=head; i!=NULL; i=i->next) 
		if(strcmp(i->name, name)==0 && strcmp(i->ip, ip)==0 && i->port==port) return 1;
	
	//allocates memory space for the new node
	i = (struct node*)malloc(sizeof(struct node));
	if (i==NULL){ perror(NULL); return 2; }
	
	//fills in the new nodes fields
	strcpy(i->name, name);
	strcpy(i->ip, ip);
	i->port = port;
	strcpy(i->properties, properties);
	
	//attashes the new node to the linked list
	i->next = head;
	head = i;

	return 1;
	}



int removeNode(char *name, char *ip, int port){
	struct node *i, *prev;

	//case where the linked list is empty
	if(head==NULL) return 2;	

	//case where the node to be removed is the first one on the linked list
	if(strcmp(head->name, name)==0 && strcmp(head->ip, ip)==0 && head->port==port){
		prev = head;
		head = head->next;
		free(prev);	
		return 1;			
		}
 
	//searches the rest of the linked list and removes the node (if found)
	for(prev=head, i=head->next; i!=NULL; prev=i, i=i->next){
		if(strcmp(i->name, name)==0 && strcmp(i->ip, ip)==0 && i->port==port){
			prev->next = i->next;
			free(i);
			return 1;			
			}
		}
	return 2;
	}



struct node *findNextMatch(struct node *start, char *prefix){
	struct node *i;

	//searches the linked list starting from the *start node untill a match is found
	for(i=start; i!=NULL; i=i->next){
		if(strncmp(i->name, prefix, strlen(prefix))==0){
			return i;
			}
		}
	return NULL;
	}


		
void openFileAndRead(){
	unsigned char buff[90];	
	int i=0;
	char c;
	FILE *f;
	f = fopen(LOG_FILE_NAME, "r");
	if(f==NULL) return; 

	//reads till the end of the file
	while(!feof(f)){
		c = fgetc(f);		
		buff[i] = c;
		i++;
		
		//a directory entry is 90 characters long
		if(i==90){
			i=0;
			addNode(buff, &buff[64], buff[80]*256+buff[81], &buff[82]);	//adds the entry to the linked list
			}
		}

	fclose(f);	
	}



void writeFileAndClose(){
	int i;
	struct node *n;
	FILE *f;

	//in case the linked list is empty it removes the log file
	if(head==NULL){
		remove(LOG_FILE_NAME);
		return;
		}

	f = fopen(LOG_FILE_NAME, "w");

	//it writes the linked list to the log file, node by node
	for(n=head; n!=NULL; n=n->next){
		for(i=0; i<64; i++) fputc(n->name[i], f);
		for(i=0; i<16; i++) fputc(n->ip[i], f);
		fputc((int)((n->port)/256), f);
		fputc(n->port%256, f);
		for(i=0; i<8; i++) fputc(n->properties[i], f);
		}

	fclose(f);
	}

