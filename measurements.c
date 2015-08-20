/****************************************************************************************************************
 * This program is used to take mesurements of the "Nerwork File System with CACHING and PASSIVE REPLICATION"	*
 * preformance. Makes N=1000 writes to a file, writing one byte at a time. Counts the total time and print the	*
 * apropriate messages. Try executing this on a NFS with 0, 1, 2 and 3 backup servers.				*
 ****************************************************************************************************************/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#include"nfssrvc.h"	//the NFS client interface

#define N 1000




int main(int argc, char *argv[]){
	int i, fd, ret;
	char file_name[30], text[4096];
	struct timeval t1, t2;

	LocateFileServer();

	fd = mynfs_open("measurements_file.txt");
	if(fd==-1) printf("Error opening the file\n");

	gettimeofday(&t1);

	for(i=0; i<N; i++){

		//mynfs_seek(fd, 0);
		ret = mynfs_write(fd, "a", 1);
		if(ret==-1) printf("Error reading the file\n");
		}

			
	mynfs_close(fd);

	gettimeofday(&t2);

	printf("Total time: %d usec for %d bytes writes\n", (int)(1000000*(t2.tv_sec-t1.tv_sec)+t2.tv_usec-t1.tv_usec), N);	
	printf("Average time per byte write: %d usec\n", (int)(1000000*(t2.tv_sec-t1.tv_sec)+t2.tv_usec-t1.tv_usec)/N);	

	return;
	}


		





		
