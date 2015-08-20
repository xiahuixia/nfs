#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netdb.h>
#include<arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include"dirsrvc.h"
#include <errno.h>


#define UDP_PACKET_SIZE 65535
#define RESEND_TIME 1000000

#define OK 0
#define NOT_OK -1


struct sockaddr_in address;
int s;	//the socket number
unsigned char msg[UDP_PACKET_SIZE];




int LocateServer(){
	int ret, i;
	
	//creates a socket 	
	s = socket (PF_INET, SOCK_DGRAM, 0);
	if (s == -1){ perror(NULL); return NOT_OK; }
	fcntl(s, F_SETFL, O_NONBLOCK);

	//sets the "address" to be our chosen multicast address
	address.sin_family = AF_INET;
	inet_aton("224.0.0.1", &address.sin_addr);
	address.sin_port = htons(15000);

	//sends a message the multicast address
	msg[0] = 1;
	sendto(s, msg, 1, 0, (struct sockaddr*)&address, sizeof(address));	
	ret = recvfrom(s, msg, 19, 0, NULL, NULL);
	
	//in case the response was delayed it resends the message		
	while(ret==-1 || msg[0]!=2){
		for(i=0; i<RESEND_TIME; i++){}
		
		msg[0] = 1;
		sendto(s, msg, 1, 0, (struct sockaddr*)&address, sizeof(address));	
		ret = recvfrom(s, msg, 19, 0, NULL, NULL);
		}	

	//saves the server's address to "address"
	inet_aton(&msg[1], &address.sin_addr);
	address.sin_port = htons(msg[17]*256+msg[18]);		
	
	ret = connect(s, (struct sockaddr*)&address, sizeof(address)); //always send to specified server address
	if (ret == -1){ perror(NULL); return NOT_OK; }
	else return OK;
	}



void AddToDir(char *name, char *ip, int port, char *properties){
	int ret, i; 
	
	//forms the message need to be sent and sends it
	msg[0] = 3;	
	strcpy(&msg[1], name);
	strcpy(&msg[65], ip);
	msg[81] = (int)(port/256);
	msg[82] = port%256;
	strcpy(&msg[83], properties);
	send(s, msg, 91, 0);
	ret = recvfrom(s, msg, 2, 0, NULL, NULL);

	//in case the response was delayed it resends the message		
	while(ret==-1 || msg[0]!=4){
		for(i=0; i<RESEND_TIME; i++){}
		
		msg[0] = 3;	
		strcpy(&msg[1], name);
		strcpy(&msg[65], ip);
		msg[81] = (int)(port/256);
		msg[82] = port%256;
		strcpy(&msg[83], properties);
		send(s, msg, 91, 0);
		ret = recvfrom(s, msg, 2, 0, NULL, NULL);
		}
	}



void RemoveFromDir(char *name, char *ip, int port){
	int ret, i;
	
	//forms the message need to be sent and sends it
	msg[0] = 5;
	strcpy(&msg[1], name);
	strcpy(&msg[65], ip);
	msg[81] = (int)(port/256);
	msg[82] = port%256;
	send(s, msg, 83, 0);
	ret = recvfrom(s, msg, 2, 0, NULL, NULL);

	//in case the response was delayed it resends the message		
	while(ret==-1 || msg[0]!=6){
		for(i=0; i<RESEND_TIME; i++){}
		
		msg[0] = 5;
		strcpy(&msg[1], name);
		strcpy(&msg[65], ip);
		msg[81] = (int)(port/256);
		msg[82] = port%256;
		send(s, msg, 83, 0);
		ret = recvfrom(s, msg, 2, 0, NULL, NULL);
		}
	}



unsigned char* SearchDir(char *prefix){
	int ret, i, hitsCount;
	
	//forms the message need to be sent and sends it
	msg[0] = 7;
	strcpy(&msg[1], prefix);
	send(s, msg, 65, 0);
	ret = recvfrom(s, msg, UDP_PACKET_SIZE, 0, NULL, NULL);
	
	//in case the response was delayed it resends the message		
	while(ret==-1 || msg[0]!=8){
		for(i=0; i<RESEND_TIME; i++){}
		
		msg[0] = 7;
		strcpy(&msg[1], prefix);
		send(s, msg, 65, 0);
		ret = recvfrom(s, msg, UDP_PACKET_SIZE, 0, NULL, NULL);
		}

	return &msg[1];
	}




int EndOfTransmition(){
	int ret;

	ret = close(s);
	if (ret == -1){ perror(NULL); return NOT_OK; }
	else return OK;
	}







