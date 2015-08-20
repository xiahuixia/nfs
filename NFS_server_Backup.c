/************************************************************************************************************************
 * This program is the server routine for the Network File System Service (NFS) with Passive Replication. This code 	*
 * handles only read requests from clients as well as open and write requests from the Primary NFS server. Uses the 	*
 * Directory Service to register it's self, an can be unregistered by typing 'remove' at the command line wile running.	*
 *  Does not print any messages exept from perror messages or diagnoric messages if _PRINT_DIAGNOSTICS is defined.	*
 ***********************************************************************************************************************/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/time.h>
#include<netdb.h>
#include<arpa/inet.h>
#include<errno.h>
#include<unistd.h>
#include<fcntl.h>

#include "dirsrvc.h"

#define SERVER_IP	"127.0.0.1"	//the Backup server's desired IP
#define SERVER_PORT	15001		//the Backup server's desired port (plus a little something to seperate one Backup server from another)

#define UDP_PACKET_SIZE		65535	//the size in bytes of a UTP packet

#define REQUEST_READ		3
#define RESPONSE_READ		4
#define REQUEST_SYNC_ALL	7
#define RESPONSE_SYNC_ALL	8
#define RESPONSE_SYNC_ALL_END	9
#define SYNC_SINGLE_FILE_OPEN	10
#define SYNC_SINGLE_FILE_WRITE	11

#define INVALID_REQUEST		0
#define SUCCESS			1
#define INVALID_REFERENCE	2

#define LOG_FILE_NAME "log/nfs_log"	

#define NFS_FILES_DIR	"mynfs"

int filesCount;
int myID;	//an identifier of which backup server this is. Should be given via command line

char tmp[128];
char tmp2[4];


//describes a file by it's name and it's file descriptor.
struct aFile{
	char name[128];
	int fd;
	int version;
		
	struct aFile *next;
	} *fileList;




//this function searches the fileList by name and if the file with the specified fname exists it returns a pointer to
//this struct aFile. If the file is not found it adds a new entry to the list and returns a pointer to the new entry.
struct aFile *addFile(char *fname, int fd, int version){
	struct aFile *i, *newFile;

	//if file allready exists then just returns the file descriptor
	for(i=fileList; i!=NULL; i=i->next){
		if(!strcmp(fname,i->name)) return i;
		}
	
	//creates and adds a new entry to the fileList
	newFile = (struct aFile *)malloc(sizeof(struct aFile));	
	if(newFile==NULL){ perror(NULL); return; }
	strcpy(newFile->name, fname);	
	newFile->fd = fd;
	newFile->version = version;
	newFile->next = fileList;
	fileList = newFile;		
	return newFile;
	}




//searches the fileList by fd and returns a pointer to the particular aFile. returns NULL if there is no entry by that fd.
struct aFile *findFile(int fd){
	struct aFile *i;
	
	//checks if file exist in the linked list
	for(i=fileList; i!=NULL; i=i->next){
		if(fd==i->fd) return i;
		}
	return NULL;
	}




//opens the log file, reads all the data and stores them back to the fileList.
void openLogFileAndRead(){
	struct aFile *newFile;
	FILE *f;
	
	tmp2[0] = myID + '0';
	tmp2[1] = '\0';
	strcpy(tmp, LOG_FILE_NAME);
	strcat(tmp, tmp2);
	strcat(tmp, ".txt");
	f = fopen(tmp, "r");
	if(f==NULL) return; 

	filesCount=0;
	while(!feof(f)){
		filesCount++;
		fread(tmp, sizeof(char), 128, f);
		newFile = addFile(tmp, 0, 0);
		fread(&newFile->fd, sizeof(int), 1, f);
		fread(&newFile->version, sizeof(int), 1, f);
		}
	fclose(f);
	}




//writes the data from the fileList to the log file.
void writeLogFileAndClose(){
	struct aFile *i;
	FILE *f;

	tmp2[0] = myID + '0';
	tmp2[1] = '\0';
	strcpy(tmp, LOG_FILE_NAME);
	strcat(tmp, tmp2);
	strcat(tmp, ".txt");
	f = fopen(tmp, "w");
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

	int s, i, fd, pos, n, ver, ret, clientAddrLen, hitsCount, filesCount;
	struct aFile *fptr;
	FILE *f;
	struct sockaddr_in myAddress, clientAddress, primaryServerAddress;
	unsigned char buff[UDP_PACKET_SIZE];
	struct timeval t;

	//the number of the Backup server must be given from command line. This way you can shut down and restart the exact servers you want.
	if(argc<2){
		printf("Give the number of the backup server like: ./NFSB <number>\n");
		return;
		}
	else myID = atoi(argv[1]);

	//locates the DS server and adds the Backup NFS server info to the directory
	LocateServer();			
	AddToDir("Network File System Service", SERVER_IP, SERVER_PORT+myID, "backup");			
	EndOfTransmition();

	fileList = NULL;	//initializes the fileList
	filesCount = 0;		//initializes the next_fd
	openLogFileAndRead();	//restores data from the log file


#ifdef _PRINT_SERVER_DIAGNOSTICS
	printf("Backup NFS server #%d registerd to Directory Service. Type 'remove' to unregister and exit.\n",myID);
#endif

	//creates a socket 
	s = socket (PF_INET, SOCK_DGRAM, 0);
	if (s == -1){ perror(NULL); return; }

	//sets the Backup NFS server's address
	myAddress.sin_family = AF_INET;
	myAddress.sin_addr.s_addr = htonl(INADDR_ANY);	//INADDR_ANY refers to every IP on this mashine
	myAddress.sin_port = htons(SERVER_PORT+myID);

	//binds the socket to the server
	ret = bind(s, (struct sockaddr*)&myAddress, sizeof(myAddress));	
	if (ret == -1){ perror(NULL); return; }

	//sets the Primary NFS server's address
	primaryServerAddress.sin_family = AF_INET;
	primaryServerAddress.sin_addr.s_addr = htonl(INADDR_ANY);	//INADDR_ANY refers to every IP on this mashine
	primaryServerAddress.sin_port = htons(SERVER_PORT);

	//sends the SYNC request message
	buff[0] = REQUEST_SYNC_ALL;
	sendto(s, buff, 1, 0, (struct sockaddr*)&primaryServerAddress, sizeof(primaryServerAddress));

	//recieves the files from the Primary NFS server by synchronizing
	do{
		ret = recvfrom(s, buff, UDP_PACKET_SIZE, 0, NULL, NULL);
		if(ret>0 && buff[0]==RESPONSE_SYNC_ALL_END) break;
		if(ret<=0 || buff[0]!=RESPONSE_SYNC_ALL) continue;
			
		fptr = addFile(&buff[1], buff[1+128], buff[1+128+1]);
		
		//opens file for writing
		tmp2[0] = myID + '0';
		tmp2[1] = '/';
		tmp2[2] = '\0';
		strcpy(tmp, NFS_FILES_DIR);
		strcat(tmp, tmp2);
		strcat(tmp, fptr->name);
		f = fopen(tmp, "w");
		if(f==NULL) perror(NULL);

		//writes to the file
		fwrite(&buff[1+128+1+1+2], sizeof(char), buff[1+128+1+1]*256+buff[1+128+1+1+1], f);
		fclose(f);
	}while(1);

	
#ifdef _PRINT_SERVER_DIAGNOSTICS
	printf("Backup NFS server #%d is now synchronized with Primary NFS server.\n",myID);
#endif


	//this is the server's main loop
	while(1){
		
		//waits to recieve a READ request from any (unknown) client or an WRITE request from the Primary server or
		//waits for the user to type 'remove' to unregister from Directory Service and terminate
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(s, &readfds);
		FD_SET(0, &readfds);
		select(s+1, &readfds, NULL, NULL, NULL);
		if (FD_ISSET(0, &readfds))
			scanf("%s",tmp);
			if(strcmp(tmp,"remove")==0){
				LocateServer();			
				RemoveFromDir("Network File System Service", SERVER_IP, SERVER_PORT+myID);
				EndOfTransmition();
#ifdef _PRINT_SERVER_DIAGNOSTICS				
	printf("Backup server #%d is no longer registerd to Directory Service.\n", myID);
#endif
				return;
				}

		
		clientAddrLen = sizeof(clientAddress);
		ret = recvfrom(s, buff, UDP_PACKET_SIZE, 0, (struct sockaddr*)&clientAddress, &clientAddrLen);
		if (ret == -1){ perror(NULL); return; }
				
		switch(buff[0]){


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
					tmp2[0] = myID + '0';
					tmp2[1] = '/';
					tmp2[2] = '\0';
					strcpy(tmp, NFS_FILES_DIR);
					strcat(tmp, tmp2);
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


			case SYNC_SINGLE_FILE_OPEN:

				//adds entry to the fileList				
				fptr = addFile(&buff[1], buff[1+128], buff[1+128+1]);

				//makes the open or create
				tmp2[0] = myID + '0';
				tmp2[1] = '/';
				tmp2[2] = '\0';
				strcpy(tmp, NFS_FILES_DIR);
				strcat(tmp, tmp2);
				strcat(tmp, fptr->name);
				f = fopen(tmp, "a");	
				if(f==NULL) perror(NULL);
				else fclose(f); 		

				//makes a backup to the log file
				writeLogFileAndClose();				
				break;

			
			case SYNC_SINGLE_FILE_WRITE:

				//extracts info from the recieved message 				
				fd = buff[1]*256+buff[2];
				pos = buff[3]*256+buff[4];
				n = buff[5]*256+buff[6];
				
				//finds the filename that the recieved fd references to
				fptr = findFile(fd);				
				if(fptr==NULL){ printf("EEEERRRROOOORRRRR NULL fptr\n"); fflush(stdout);}

				//opens file for writing (appending)
				tmp2[0] = myID + '0';
				tmp2[1] = '/';
				tmp2[2] = '\0';
				strcpy(tmp, NFS_FILES_DIR);
				strcat(tmp, tmp2);
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
				break;

			default: 

				break;
			}
		}

	//for cosmetic reasons - never reaches this point!!
	close(s);

	return;
	}







