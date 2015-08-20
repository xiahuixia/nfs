#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#include<sys/types.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<netdb.h>
#include <unistd.h>
#include <fcntl.h>

#include <errno.h>
#include <sys/time.h>
#include"dirsrvc.h"
#include"nfssrvc.h"


//#define _DEBUG


#define UDP_PACKET_SIZE 65535

#define REQUEST_OPEN		1
#define RESPONSE_OPEN		2	
#define REQUEST_READ		3
#define RESPONSE_READ		4
#define REQUEST_WRITE		5
#define RESPONSE_WRITE		6

#define INVALID_REQUEST		0
#define SUCCESS			1
#define INVALID_REFERENCE	2

#define MAX_FDs			64	//the max number of file descriptors for one client

#define OK 0
#define NOT_OK -1

#define CACHE_SIZE_IN_BLOCKS	6

#define MAX_BACKUP_SERVERS	64



struct sockaddr_in primaryServerAddress;	//the address of the Primary NFS server	
struct sockaddr_in readServerAddress;		//the addres of some random Backup server or the Primary server
int s;				//the socket number
int blocks_in_cache;		//the amound of blocks stored in cache at the moment
unsigned char msg[UDP_PACKET_SIZE];
unsigned char *match;
unsigned char NFS_server_ip[16];
int NFS_server_port;
		

#ifdef _CACHE_STATISTICS

int cache_hit;
int cache_miss;
#endif



/****************************************************************************************************************
 * SEEK definitions and managment routines 									*
 ****************************************************************************************************************/

//this struct keeps a record of a file's fd and the file's seek position
struct seek{
	int fd;	
	int seek;

	struct seek *next;
	} seekTable[MAX_FDs];	//this table holds the seek info for all the files opend from this client



//the seekTable managing functions
void initSeeks();		//initializes the seekTable
int addSeek(int fd);		//adds a new entry to the seekTable
int findSeek(int fd);		//finds an entry in the seekTable
void removeSeek(int fd);	//removes an entry from the seekTable





/****************************************************************************************************************
 * CACHE definitions and managment routines 									*
 ****************************************************************************************************************/

struct cache_block{
	int used;	
	int fd;
	int block;	//block 0 is bytes 0:511, block 1 is bytes 512:1023, block 2 is bytes 1024:1535 etc...
	int length;	//the data length is 512 or less than 512 if EOF is reached

	unsigned char data[512];

	int tm;	//the client version of the file in the cache block (maybe different from the server's version)
	struct timeval tc;  //the time of the last data-validity check

	struct timeval last_used;  //the time of the last use of this block

	} cache[CACHE_SIZE_IN_BLOCKS];


int evictLRU();		//evicts the Least Recently Used block from cache
void initCache();	//initializes the cache
void addToCache(int fd, int block, int length, int version, struct timeval *t, unsigned char *data);  //adds a data block to the cache
int findInCache(int fd, int block);  //searches the cache
int my_diff_time(struct timeval *t1, struct timeval *t2);  //finds the latest of two time values





/****************************************************************************************************************
 * BACKUPS definitions and managment routines 									*
 ****************************************************************************************************************/

struct aServer{
	int isPrimary;
	char IP[16];
	int port;
	} allServers[MAX_BACKUP_SERVERS+1];  //an array with all registered servers

int serversCount;


void pickServerToRead(){
	int i;

	LocateFileServer();

	i = rand()%serversCount;	
	readServerAddress.sin_family = AF_INET;
	inet_aton(allServers[i].IP, &readServerAddress.sin_addr);
	readServerAddress.sin_port = htons(allServers[i].port);

#ifdef _PRINT_SERVER_DIAGNOSTICS				
	if(allServers[i].isPrimary) printf("Making the read from the Primary server.\n");
	else printf("Making the read from a Backup server.\n");
#endif
	}





//reads n bytes from the specified by it's fd file, and puts them in buf. Returns the number of bytes that were actually read or
//returns -1 for invalid file name or file reference.
int mynfs_read(int fd, char buf[], int n){
	int ret, index, real_n=0, total_real_n=0, i, j, k, start_read, end_read, real_start_read, real_end_read, seek;
	struct timeval t_timeout, t_now;
	fd_set readfds;

	start_read = seek = seekTable[findSeek(fd)].seek; 	//the point where the client wants the read to start from
	end_read = start_read + n; 	//the point where the client wants the read to end to

	real_start_read = ((int)(start_read / 512)) * 512;	//the point where the read is really going to start from (last multiple of 512 befor start_read)
	real_end_read = (((int)((end_read-1)/512))+1) * 512;	//the point where the read is really going to end to (first multiple of 512 after end_read)


	//this loop breaks the data to be read in blocks of 512 bytes and makes the read from either the cache or the NFS server
	for(i=real_start_read; i<real_end_read; i+=512){
		
		//checks in CACHE if the data exist
		index = findInCache(fd, i/512);

		//saves the current time to t_now. The whole code of one iteration of this for-loop uses this exact same t_now
		gettimeofday(&t_now, NULL);

		//case where the block is found in the cache and the data are valid
		if(index>=0 && my_diff_time(&t_now, &cache[index].tc)){

#ifdef _DEBUG
	printf("Block %d found in cache and the data are valid\n", i/512);
#endif

#ifdef _CACHE_STATISTICS
	cache_hit++;
#endif

			//reads the block from the cache
			if(i<start_read){
				for(j=0; j<512-start_read+real_start_read && j+start_read<end_read; j++)
					buf[j] = cache[index].data[j+start_read-real_start_read];
				}
			else{
				for(j=0; j<512 && j+i<end_read && j<cache[index].length; j++) 
					buf[j+i-start_read+real_start_read] = cache[index].data[j];
				}

			//sets the last_used time value of the cache block to t_now			
			cache[index].last_used.tv_sec = t_now.tv_sec;
			cache[index].last_used.tv_usec = t_now.tv_usec;
			}

		//case where 1) the block is not found in the cache or 2) the block is found in the cache but 
		//the timestamp of the last data-validity check is too old
		else{

			do{	
				//pick the Primary or any Backup server to read randomly
				pickServerToRead();
			
				//forms the request message
				msg[0] = REQUEST_READ;
				msg[1] = (int)(fd/256);
				msg[2] = fd%256;
				msg[3] = (int)(seek/256);
				msg[4] = seek%256;
				msg[5] = 2;	//allways request to read 512 bytes
				msg[6] = 0;
				if(index<0) msg[7] = -1;  //if the block is not in the cache sends an invalid timestamp
				else msg[7] = cache[index].tm;  					

				//sends the request
				sendto(s, msg, 8, 0, (struct sockaddr*)&readServerAddress, sizeof(readServerAddress));

				//sets some time values and some fd sets and calls select()
				//if it does not get a responce it choores another server next time		
				t_timeout.tv_sec = 1;
				t_timeout.tv_usec = 500000;
				FD_ZERO(&readfds);
				FD_SET(s, &readfds);
				if (select(s+1, &readfds, NULL, NULL, &t_timeout)==0) continue;
		
				//recieves the NFS server's responce	
				ret = recvfrom(s, msg, 517, 0, NULL, NULL);

				}while(ret<=0 || msg[0]!=RESPONSE_READ);
		
			
			//makes some validity checks
			if(msg[1]==INVALID_REQUEST){
				printf("nfssrvc: invalid file name\n");
				return NOT_OK;
				}
			else if(msg[1]==INVALID_REFERENCE){
				printf("nfssrvc: invalid reference\n");
				return NOT_OK;
				}	
			

			//if the version of the cached block is the same as the server's block, reads the block from the cache
			if(index>=0 && msg[4] == cache[index].tm){
			
#ifdef _DEBUG
	printf("Block %d found in cache and the NFS server responses that the data are valid (ver: %d)\n", i/512, msg[4]);
#endif

#ifdef _CACHE_STATISTICS
	cache_hit++;
#endif
				if(i<start_read){
					for(j=0; j<512-start_read+real_start_read && j+start_read<end_read; j++)
						buf[j] = cache[index].data[j+start_read-real_start_read];
					}
				else{
					for(j=0; j<512 && j+i<end_read && j<cache[index].length; j++) 
						buf[j+i-start_read+real_start_read] = cache[index].data[j];
					}

				//updates the tc and the last_used timestamp
				cache[index].tc.tv_sec = t_now.tv_sec + (int)((t_now.tv_usec + checkT*1000)/1000000);  //checkT is in milliseconds
				cache[index].tc.tv_usec = (t_now.tv_usec + checkT*1000)%1000000;  //converts mseconds to useconds
				cache[index].last_used.tv_sec = t_now.tv_sec;
				cache[index].last_used.tv_usec = t_now.tv_usec;
				}

			//otherwise, reads the block from the NFS response message
			else{

#ifdef _DEBUG
	printf("Block %d not found in cache\n", i/512);
#endif

				//extracts info from the responce message 
				real_n = msg[2]*256+msg[3];
			
				if(i<start_read){
					for(j=0; j<real_n-start_read+real_start_read && j+start_read<end_read; j++) 
						buf[j] = msg[5+j+start_read-real_start_read];
					}
				else{
					for(j=0; j<real_n && j+i<end_read; j++) 
						buf[j+i+start_read-real_start_read] = msg[5+j];
					}
				
				//makes all the blocks of this file that are found in the cache invalid		
				for(k=0; k<CACHE_SIZE_IN_BLOCKS; k++){
					if(cache[k].used==1 && cache[k].fd==fd && cache[k].tm<msg[4]){
						cache[k].used = 0;
						blocks_in_cache--;
						}
					}

				//adds the new block into the cache
				addToCache(fd, i/512, real_n, msg[4], &t_now, &msg[5]);
				}	
			}

		seek += j; 
		total_real_n += j;
		
		//an optimization break point in case the EOF is reached
		if((i<start_read && j<512-start_read+real_start_read) || (i>=start_read && j<512)) break;	
		}
	seekTable[findSeek(fd)].seek = seek;

	return total_real_n;
	}








//locates the NFS server using the DS server we created during Assignment 1 of this course. Returns -1 if NFS server is not
//found or 0 otherwise.
int LocateFileServer(){
	int ret, i, matchesCount, matchLength;
	
	//locates the DS server and searches for all the NFS servers' info (Primary and Backups)
	LocateServer();			
	match = SearchDir("Network File System Service");
	matchLength = 64+16+2+8;
	matchesCount = match[0]*265+match[1];

	for(i=0; i<matchesCount; i++){
		strcpy(allServers[i].IP, &match[2+i*matchLength+64]);
		allServers[i].port = match[2+i*matchLength+64+16]*256+match[2+i*matchLength+64+16+1] ;
		
		if(strcmp(&match[2+i*matchLength+64+16+2],"primary")==0){
			strcpy(NFS_server_ip, allServers[i].IP);
			NFS_server_port = allServers[i].port;
			allServers[i].isPrimary = 1;
			}
		else allServers[i].isPrimary = 0;
		}

	serversCount = matchesCount;

	EndOfTransmition();	

	initSeeks();	//initializes the seekTable
	initCache();	//initializes the cache

	//creates a socket 	
	s = socket (PF_INET, SOCK_DGRAM, 0);
	if (s == -1){ perror(NULL); return NOT_OK; }
	
	//saves the Primary NFS server's address to "primaryServerAddress"
	primaryServerAddress.sin_family = AF_INET;
	inet_aton(NFS_server_ip, &primaryServerAddress.sin_addr);
	primaryServerAddress.sin_port = htons(NFS_server_port);	
	}








//opens or creates the specified file to the NFS server and returns it's referencing fd (this fd is NOT a FILE* type of pointer)
//or returns -1 for invalid file name.
int mynfs_open(char *fname){
	int ret, fd;
	struct timeval t;
	fd_set readfds;

	do{
		//forms the request message
		msg[0] = REQUEST_OPEN;
		strcpy(&msg[1],fname);
	
		//sends the request
		sendto(s, msg, 129, 0, (struct sockaddr*)&primaryServerAddress, sizeof(primaryServerAddress));
		
		//sets some time values and some fd sets and calls select()		
		t.tv_sec = 1;
		t.tv_usec = 500000;
		FD_ZERO(&readfds);
		FD_SET(s, &readfds);
		if (select(s+1, &readfds, NULL, NULL, &t)==0) continue;

		//recieves the NFS server's responce	
		ret = recvfrom(s, msg, 4, 0, NULL, NULL);
		
		}while(ret<=0 || msg[0]!=RESPONSE_OPEN);


	//makes some validity checks
	if(msg[1]==INVALID_REQUEST){
		printf("nfssrvc: invalid file name\n");
		return NOT_OK;
		}

	//extracts info from the responce message 
	fd = msg[2]*256+msg[3];
	addSeek(fd);
	return fd;
	}








//writes n bytes from buf to the specified by it's fd file. Returns 0 for successful write or -1 for invalid file name or file reference.
int mynfs_write(int fd, char buf[], int n){
	int ret, i, j;
	struct timeval t;
	fd_set readfds;

	for(i=0; i<n; i+=512){
		do{
			//forms the request message
			msg[0] = REQUEST_WRITE;
			msg[1] = (int)(fd/256);
			msg[2] = fd%256;
			msg[3] = (int)(seekTable[findSeek(fd)].seek/256);
			msg[4] = seekTable[findSeek(fd)].seek%256;
			if(n-i<512){
				msg[5] = (int)((n-i)/256);
				msg[6] = (n-i)%256;
				for(j=0; j<n-i; j++) msg[7+j] = buf[i+j];
				}
			else{
				msg[5] = 2;
				msg[6] = 0;
				for(j=0; j<512; j++) msg[7+j] = buf[i+j];
				}
			
			//sends the request allways to the Primary server
			sendto(s, msg, 519, 0, (struct sockaddr*)&primaryServerAddress, sizeof(primaryServerAddress));	
		
			//sets some time values and some fd sets and calls select()		
			t.tv_sec = 1;
			t.tv_usec = 500000;
			FD_ZERO(&readfds);
			FD_SET(s, &readfds);
			if (select(s+1, &readfds, NULL, NULL, &t)==0) continue;
	
			//recieves the NFS server's responce	
			ret = recvfrom(s, msg, 2, 0, NULL, NULL);
	
			}while(ret<=0 || msg[0]!=RESPONSE_WRITE);
			

		//moves seek position
		if(n-i<512) seekTable[findSeek(fd)].seek += n-i;		
		else seekTable[findSeek(fd)].seek += 512;	
			

		//makes some validity checks
		if(msg[1]==INVALID_REQUEST){
			printf("nfssrvc: invalid file name\n");
			return NOT_OK;
			}
		else if(msg[1]==INVALID_REFERENCE){
			printf("nfssrvc: invalid reference\n");
			return NOT_OK;
			}
		}
	return OK;
	}
	
	




	

//sets the seek position of a file to a specified value. Returns -1 for invalid fd or 0 otherwise. Does not communicate with
//the NFS server.
int mynfs_seek(int fd, int n){
	if(fd>MAX_FDs) return NOT_OK;
	seekTable[findSeek(fd)].seek = n;
	return OK;	
	}




//destroyies this file's fd. Returns -1 for invalid fd or 0 otherwise. Does not communicate with the NFS server.
int mynfs_close(int fd){
	if(fd>MAX_FDs) return NOT_OK;
	removeSeek(fd);
	return OK;	
	}




//initializes all the fd's to -1 (-1 means not in use) and their seek numbers to 0
void initSeeks(){
	int i;
	srand(time(NULL));  
	for(i=0; i<MAX_FDs; i++){
		seekTable[i].fd = -1;
		seekTable[i].seek = 0;
		}
	}




//adds a new fd to the table and initiaizes it's seek to 0
int addSeek(int fd){
	int i;
	for(i=0; i<MAX_FDs; i++)
		if(seekTable[i].fd==-1){
			seekTable[i].fd = fd;
			seekTable[i].seek = 0;
			return OK;
			}
	return NOT_OK;
	}




//returns the index of the table where this fd is stored 
int findSeek(int fd){
	int i;
	for(i=0; i<MAX_FDs; i++)
		if(seekTable[i].fd==fd){
			return i;
			}
	return NOT_OK;
	}




//removes the entry of the specified fd from the table
void removeSeek(int fd){
	int i = findSeek(fd);
	seekTable[fd].fd = -1;
	seekTable[fd].seek = 0;
	}




//evicts the Least Recently Used block from cache. Assumes the cache is full so must be called with caution
//returns the index of the evicted block in the cache array.
int evictLRU(){
	int i, lru;
	unsigned long min_sec, min_usec;

	min_sec = cache[0].last_used.tv_sec;
	min_usec = cache[0].last_used.tv_usec;
	lru = 0;
	
	for(i=1; i<CACHE_SIZE_IN_BLOCKS; i++){
		if(cache[i].last_used.tv_sec <= min_sec  &&  cache[i].last_used.tv_usec < min_usec){
			min_sec = cache[i].last_used.tv_sec;
			min_usec = cache[i].last_used.tv_usec;
			lru = i;
			}
		}

	cache[lru].used = 0;
	
	blocks_in_cache--;


#ifdef _DEBUG
	printf("Evicting cache block index %d where file bytes %d:%d were stored\n", lru, cache[lru].block*512, cache[lru].block*512+511);
#endif

	return lru;
	}




//makes some initializations for the cache
void initCache(){
	int i;

	//checkT = 20000;  //default value is 20 sec 
	checkT = 100;
	blocks_in_cache = 0;
	
#ifdef _CACHE_STATISTICS
	cache_hit = 0;
	cache_miss = 0;
#endif

	//sets all the cache's blocks to not-in-use
	for(i=0; i<CACHE_SIZE_IN_BLOCKS; i++) cache[i].used=0;
	}




//adds a data block to the cache
void addToCache(int fd, int block, int length, int version, struct timeval *t, unsigned char *data){
	int index, j;	
	
	//case where cache is full, evicts the least recently used block from cache
	if(blocks_in_cache==CACHE_SIZE_IN_BLOCKS){
		index = evictLRU();
		}
	
	//case where the cache is not full, finds the first block in cache that is not in use
	else{
		for(index=0; index<CACHE_SIZE_IN_BLOCKS; index++)
			if(cache[index].used==0) break;
		}
	
	//puts the data in the cache
	cache[index].used = 1;
	cache[index].fd = fd;
	cache[index].block = block;
	cache[index].length = length;
	cache[index].tm = version;
	

	cache[index].tc.tv_sec = t->tv_sec + (int)((t->tv_usec + checkT*1000)/1000000);  //checkT is in milliseconds
	cache[index].tc.tv_usec = (t->tv_usec + checkT*1000)%1000000 ;  //converts mseconds to useconds

	cache[index].last_used.tv_sec = t->tv_sec; 
	cache[index].last_used.tv_usec = t->tv_usec;
	for(j=0; j<length; j++) cache[index].data[j] = data[j];
	blocks_in_cache++;
	
	
#ifdef _DEBUG
	printf("File bytes %d:%d added to index %d of cache\n\n", block*512, block*512+511, index);
#endif

#ifdef _CACHE_STATISTICS
	cache_miss++;
#endif
	}




//locates a specified data block in cache. Returns the index of this block in the cache array or -1 if the block
//does is not found in the cache
int findInCache(int fd, int block){
	int i;

	for(i=0; i<CACHE_SIZE_IN_BLOCKS; i++){
		if(cache[i].used==1 && cache[i].fd==fd && cache[i].block==block) return i;
		}
	return -1;
	}




//returns 1 if t1<t2 or 0 otherwise
int my_diff_time(struct timeval *t1, struct timeval *t2){
	if(t1->tv_sec < t2->tv_sec) return 1;
	else if(t1->tv_sec == t2->tv_sec && t1->tv_usec < t2->tv_usec) return 1;
	else return 0;
	}



#ifdef _CACHE_STATISTICS
void printCacheStatistics(){
	printf("\n--- CACHE STATISTICS ---\nCache hits:\t %d\nCache miss:\t %d\n------------------------\n\n", cache_hit, cache_miss);
	}
#endif







