#ifndef __DIRSRVC_H_
#define __DIRSRVC_H_

int LocateServer();							//locates the server via multicast
int EndOfTransmition();							//just closes the used socket
void AddToDir(char *name, char *ip, int port, char *properties);	//adds an entry to the directory
void RemoveFromDir(char *name, char *ip, int port);			//removes an entry from the directory
unsigned char* SearchDir(char *prefix);					//searches the directory for matches by name

#endif
