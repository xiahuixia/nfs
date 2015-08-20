/************************************************************************************************************************
 * This program is the server routine for the Network File System Service (NFS). It uses the Directory Service (DS) we 	*
 * built before in order to share it's IP and Port number with the potential NFS clients. This server is stateless so 	*
 * it does not keep any info about the clients. The only info stored in this server is the matching of every file name	*
 * to a specific and unique file desrcriptor integer identification and the current version of the file. This info is	*
 * stored temporarily on a linked list and on a log file in case the server crashes. All the files this server manages 	*
 * are stored under ./mynfs/ in a flat namespace. Does not print any messages exept from error messages using perror().	*
 * Define _PRINT_DIAGNOSTICS to print diagnostic messages. This is the code for the Primary NFS server. It handles	*
 * read, write and open file requests from clients as well as synchronization requests from other Backup NFS servers.	*
 * For every open and write request, it forwards the exact same request to all registered Baclup NFS servers.		*
 ************************************************************************************************************************/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netdb.h>
#include<arpa/inet.h>
#include<errno.h>
#include <unistd.h>
#include <fcntl.h>
#include"dirsrvc.h"

#define SERVER_IP	"127.0.0.1"	//the Primary server's desired IP
#define SERVER_PORT	15001		//the Primary server's desired port

#define UDP_PACKET_SIZE		65535	//the size in bytes of a UTP packet

#define REQUEST_OPEN		1
#define RESPONSE_OPEN		2	
#define REQUEST_READ		3
#define RESPONSE_READ		4
#define REQUEST_WRITE		5
#define RESPONSE_WRITE		6
#define REQUEST_SYNC_ALL	7
#define RESPONSE_SYNC_ALL	8
#define RESPONSE_SYNC_ALL_END	9
#define SYNC_SINGLE_FILE_OPEN	10
#define SYNC_SINGLE_FILE_WRITE	11

#define INVALID_REQUEST		0
#define SUCCESS			1
#define INVALID_REFERENCE	2

#define LOG_FILE_NAME "log/nfs_log.txt"	

#define NFS_FILES_DIR	"mynfs/"


int next_fd;	//a counter responsible for assigning the next fd
char tmp[128];	//for temporary opperations



//describes a file by it's name and it's file descriptor. All filename and fd data are stored on a list and on a logfile
//so the server will assign the exact same fd to the same filename even if the file is opened by different clients.
struct aFile{
	char name[128];
	int fd;
	int version;
		
	struct aFile *next;
	} *fileList;




//this function searches the fileList by name and if the file with the specified fname exists it returns a pointer to
//this struct aFile. If the file is not found it adds a new entry to the list and returns a pointer to the new entry.
struct aFile *addFile(char *fname){
	struct aFile *i, *newFile;

	//if file allready exists then just returns the file descriptor
	for(i=fileList; i!=NULL; i=i->next){
		if(!strcmp(fname,i->name)) return i;
		}
	
	//creates and adds a new entry to the fileList
	newFile = (struct aFile *)malloc(sizeof(struct aFile));	
	if(newFile==NULL){ perror(NULL); return; }
	strcpy(newFile->name, fname);	
	newFile->fd = next_fd++;
	newFile->version = 1;
	newFile->next = fileList;
	fileList = newFile;		
	return newFile;
	}




//searches the fileList by fd and returns a pointer to the particular aFile. returns NULL if there is no entry by that fd.
struct aFile *findFile(int fd){
	struct aFile *i;
	
	//checks if file has been opened before
	for(i=fileList; i!=NULL; i=i->next){
		if(fd==i->fd) return i;
		}
	return NULL;
	}




//opens the log file, reads all the data and stores them back to the fileList.
void openLogFileAndRead(){
	struct aFile *newFile;
	int i=0;
	FILE *f;
	
	f = fopen(LOG_FILE_NAME, "r");
	if(f==NULL) return; 

	while(!feof(f)){
		i++;
		fread(tmp, sizeof(char), 128, f);
		newFile = addFile(tmp);
		fread(&newFile->fd, sizeof(int), 1, f);
		fread(&newFile->version, sizeof(int), 1, f);
		}
	next_fd = i+1;
	fclose(f);
	}




//writes the data from the fileList to the log file.
void writeLogFileAndClose(){
	struct aFile *i;
	FILE *f;

	f = fopen(LOG_FILE_NAME, "w");
	if(f==NULL){ perror(NULL); return; }
	
	for(i=fileList; i!=NULL; i=i->next){
		fwrite(i->name, sizeof(char), 128, f);
		fwrite(&i->fd, sizeof(int), 1, f);
		fwrite(&i->version, sizeof(int), 1, f);
		}
	fclose(f);
	}




//makes some validity checks on the file name according to "Unix File Naming rules" source: http://www.december.com/unix/tutor/filenames.html
//returns -1 for invalid file name or 0 otherwise.
int checkFileName(char *fname){
	int i;
	
	//a file name can only contain alpharethmetics or dots or underscores or commas
	for(i=0; i<strlen(fname); i++)
		if(!((fname[i]>='a' && fname[i]<='z') || (fname[i]>='A' && fname[i]<='Z') || (fname[i]>='0' && fname[i]<='9') || fname[i]=='.' || fname[i]=='_' || fname[i]==','))
			return -1;

	//a file name cannot have zero length
	if(strlen(fname)==0) return -1;
	
	//you cannot name a file period (.) or period period (..)
	if(strcmp(fname,".")==0 || strcmp(fname,"..")==0) return -1;

	return 0;
	}




int main(int argc, char *argv[]){

	int s, i, fd, pos, n, ver, ret, clientAddrLen, hitsCount;
	struct aFile *fptr;
	FILE *f;
	struct sockaddr_in myAddress, clientAddress, someBackupServerAddress;
	unsigned char buff[UDP_PACKET_SIZE];
	int matchesCount, matchLength;
	unsigned char *match;
	
	//locates the DS server and adds the Primary NFS server info to the directory
	LocateServer();			
	AddToDir("Network File System Service", SERVER_IP, SERVER_PORT, "primary");			
	EndOfTransmition();

#ifdef _PRINT_SERVER_DIAGNOSTICS
	printf("Primary NFS server registerd to Directory Service.\n");
#endif

	//creates a socket 
	s = socket (PF_INET, SOCK_DGRAM, 0);
	if (s == -1){ perror(NULL); return; }
	
	//sets the NFS server's address
	myAddress.sin_family = AF_INET;
	myAddress.sin_addr.s_addr = htonl(INADDR_ANY);	//INADDR_ANY refers to every IP on this mashine
	myAddress.sin_port = htons(SERVER_PORT);

	//binds the socket to the server
	ret = bind(s, (struct sockaddr*)&myAddress, sizeof(myAddress));	
	if (ret == -1){ perror(NULL); return; }

	fileList = NULL;	//initializes the fileList
	next_fd = 0;		//initializes the next_fd
	openLogFileAndRead();	//restores data from the log file


	//this is the server's main loop
	while(1){
		
		//waits to recieve a request from any (unknown) client or any (unknown) Backup NFS server	
		clientAddrLen = sizeof(clientAddress);
		ret = recvfrom(s, buff, UDP_PACKET_SIZE, 0, (struct sockaddr*)&clientAddress, &clientAddrLen);
		if (ret == -1){ perror(NULL); return; }


		switch(buff[0]){


			case REQUEST_OPEN:
				
				//makes some validity checks					
				if(checkFileName(&buff[1])==-1) buff[1] = INVALID_REQUEST;
				else{			
					//adds entry to the fileList				
					fptr = addFile(&buff[1]);

					//forwards the open-request to every Backup NFS server. Finds those servers using the Directory Service
					LocateServer();			
					match = SearchDir("Network File System Service");
					matchLength = 64+16+2+8;
					matchesCount = match[0]*265+match[1];
					buff[0] = SYNC_SINGLE_FILE_OPEN;
					buff[1+128] = fptr->fd;
					buff[1+128+1] = fptr->version;
 
					for(i=0; i<matchesCount; i++){
						if(strcmp(&match[2+i*matchLength+64+16+2],"backup")==0){
							someBackupServerAddress.sin_family = AF_INET;
							inet_aton(&match[2+i*matchLength+64], &someBackupServerAddress.sin_addr);
							someBackupServerAddress.sin_port = htons(match[2+i*matchLength+64+16]*256+match[2+i*matchLength+64+16+1]);
							sendto(s, buff, 129, 0, (struct sockaddr*)&someBackupServerAddress, sizeof(someBackupServerAddress));
							}
						}
					EndOfTransmition();
	
					//makes the open or create
					strcpy(tmp, NFS_FILES_DIR);
					strcat(tmp, fptr->name);
					f = fopen(tmp, "a");	
					if(f==NULL) perror(NULL);
					else fclose(f); 		
	
					//forms the response message	
					buff[1] = SUCCESS;
					buff[2] = (int)(fptr->fd/256);
					buff[3] = fptr->fd%256;
		
					//makes a backup to the log file
					writeLogFileAndClose();	
					}
				buff[0] = RESPONSE_OPEN;

				//responces to the client
				sendto(s, buff, 4, 0, (struct sockaddr*)&clientAddress, clientAddrLen);
				break;
			

			case REQUEST_READ:
				
				//extracts info from the recieved message 				
				fd = buff[1]*256+buff[2];
				pos = buff[3]*256+buff[4];
				n = buff[5]*256+buff[6];
				ver = buff[7];

				//finds the filename that the recieved fd references to
				fptr = findFile(fd);				

				//makes some validity checks					
				if(n>512) buff[1] = INVALID_REQUEST;
				else if(fptr==NULL) buff[1] = INVALID_REFERENCE;
				else if(ver == fptr->version){
					buff[1] = SUCCESS;
					buff[4] = fptr->version;
					}
				else if(ver != fptr->version){
	
					//opens file for reading
					strcpy(tmp, NFS_FILES_DIR);
					strcat(tmp, fptr->name);
					f = fopen(tmp, "r");
					if(f==NULL) perror(NULL);
					fseek(f, pos, SEEK_SET);		
					
					//reads the file
					i = fread(&buff[5], sizeof(char), n, f);
					fclose(f);		

					//forms the response message	
					buff[1] = SUCCESS;
		 			buff[2] = (int)(i/256);		//this is the number of bytes actually read
					buff[3] = i%256;
					buff[4] = fptr->version;
					}
				buff[0] = RESPONSE_READ;	

				//responces to the client
				sendto(s, buff, 517, 0, (struct sockaddr*)&clientAddress, clientAddrLen);
				break;


			case REQUEST_WRITE: 

				//extracts info from the recieved message 				
				fd = buff[1]*256+buff[2];
				pos = buff[3]*256+buff[4];
				n = buff[5]*256+buff[6];
				
				//finds the filename that the recieved fd references to
				fptr = findFile(fd);				
				
				//makes some validity checks					
				if(n>512) buff[1] = INVALID_REQUEST;
				else if(fptr==NULL) buff[1] = INVALID_REFERENCE;
				else{	

					//forwards the write-request to every Backup NFS server. Finds those servers using the Directory Service
					LocateServer();			
					match = SearchDir("Network File System Service");
					matchLength = 64+16+2+8;
					matchesCount = match[0]*265+match[1];
					buff[0] = SYNC_SINGLE_FILE_WRITE;
				
					for(i=0; i<matchesCount; i++){
						if(strcmp(&match[2+i*matchLength+64+16+2],"backup")==0){
							someBackupServerAddress.sin_family = AF_INET;
							inet_aton(&match[2+i*matchLength+64], &someBackupServerAddress.sin_addr);
							someBackupServerAddress.sin_port = htons(match[2+i*matchLength+64+16]*256+match[2+i*matchLength+64+16+1]);
							sendto(s, buff, 519, 0, (struct sockaddr*)&someBackupServerAddress, sizeof(someBackupServerAddress));
							}
						}
					EndOfTransmition();	
						
					//opens file for writing (appending)
					strcpy(tmp, "mynfs/");
					strcat(tmp, fptr->name);
					f = fopen(tmp, "r+");	
					if(f==NULL) perror(NULL);
					fseek(f, pos, SEEK_SET);		
					
					//writes to the file
					fwrite(&buff[7], sizeof(char), n, f);
					fclose(f);

					//increase the version of the file
					fptr->version++;
				
					//makes a backup to the log file
					writeLogFileAndClose();
					
					//forms the response message	
					buff[1] = SUCCESS;
		 			}
				buff[0] = RESPONSE_WRITE;	
				
				//responces to the client
				sendto(s, buff, 2, 0, (struct sockaddr*)&clientAddress, clientAddrLen);
				break;


			case REQUEST_SYNC_ALL: 

#ifdef _PRINT_SERVER_DIAGNOSTICS
	printf("A Backup NFS server requested synchronization for ALL files.\n");
#endif

				//sends all files to the Backup NFS server that made the sync request, one by one
				for(fptr=fileList; fptr!=NULL; fptr=fptr->next){

					buff[0] = RESPONSE_SYNC_ALL;
					strcpy(&buff[1], fptr->name);
					buff[1+128] = fptr->fd;
					buff[1+128+1] = fptr->version; 

					//opens file for reading
					strcpy(tmp, NFS_FILES_DIR);
					strcat(tmp, fptr->name);
					f = fopen(tmp, "r");
					if(f==NULL) perror(NULL);
					fseek(f, 0, SEEK_SET);		
					
					//reads the file
					i = fread(&buff[1+128+1+1+2], sizeof(char), UDP_PACKET_SIZE, f);
					fclose(f);

					buff[1+128+1+1] = (int)(i/256);
					buff[1+128+1+1+1] = i%256;	
	
					//responces to the Backup NFS server
					sendto(s, buff, 1+128+1+1+2+i, 0, (struct sockaddr*)&clientAddress, clientAddrLen);
					}	
				buff[0] = RESPONSE_SYNC_ALL_END;					
				sendto(s, buff, 1, 0, (struct sockaddr*)&clientAddress, clientAddrLen);
				break;
			

			default: 

				break;
			}
		}

	//for cosmetic reasons - never reaches this point!!
	close(s);

	return;
	}







