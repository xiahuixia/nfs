#ifndef __NFSSRVC_H_
#define __NFSSRVC_H_

#define _CACHE_STATISTICS

int checkT;  //time value in milliseconds. Used in cache's data validity checks


//the NFS service functions
int LocateFileServer();				//locates the NFS and makes the apropriate connections
int mynfs_open(char *fname);			//opens or creates a file
int mynfs_read(int fd, char buf[], int n);	//reads data from a previously opened file
int mynfs_write(int fd, char buf[], int n);	//writes data to a previously opened file
int mynfs_seek(int fd, int n);			//moves the position in the file where the next read/write will start from
int mynfs_close(int fd);			//closes the file


//some functions that provide optional and extra inf for the client-user
void printCacheStatistics();			//prins the cache hits and the cache misses

#endif

