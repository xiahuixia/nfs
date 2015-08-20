/************************************************************************************************************************
 * This is a program used to test the function of the NFS client interface. It prints a menu and lets the user perform 	*
 * operations on files. Also collects from the user all the neccessary attributes to perform the operation. Prints 	*
 * diagnostic messages as well. All diagnostic messages with "nfssrvc" as a prefix are generated from the nfssrvc.h	*
 * interface and the rest are created by this program. The NFS server does not print any diagnostic messages.		*
 ************************************************************************************************************************/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#include"nfssrvc.h"	//the NFS client interface

#define MAX_LEN 1024*64



int main(int argc, char *argv[]){
	int fd, ret, len, n, i;
	char *text, *name, choice;
	FILE *f; 	//just needed for whole file I/O (menu choice 6 and 7)

	text = (char*)malloc(MAX_LEN*sizeof(char));
	name = (char*)malloc(64*sizeof(char));
	if(text==NULL || name==NULL) printf("malloc failed\n");

	LocateFileServer();

	while(1){
	
		printf("Select your action:\n\t1. Open file\n\t2. Read bytes from file\n\t3. Write bytes to file\n\t4. Move seek position\n\t5. Close file\n\n\t6. Read a whole file\n\t7. Write a whole file\n\n\t8. Change checkT\n\t9. Print cache statistics\n\n\t0. Exit\n");
		choice = getchar();

		switch(choice){
		
			case '1':
	
				printf("Type a file name: ");
				scanf("%s", text);

				fd = mynfs_open(text);
				if(fd!=-1) printf("File \"%s\" opened. Use fd=%d to reference.\n", text, fd);
				break;
	
	
			case '2':
	
				printf("Type a file descriptor: ");
				scanf("%d", &fd);
				
				ret = mynfs_read(fd, text, MAX_LEN);
				if(ret!=-1){
					printf("Read from file %d bytes: ", ret);
					for(i=0; i<ret; i++) printf("%c", text[i]);
					printf("\n");	
					}
				break;
	
		
			case '3':
	
				printf("Type a file descriptor: ");
				scanf("%d", &fd);
				printf("Type some text to write: ");
				scanf("%s", text);
				
				ret = mynfs_write(fd, text, strlen(text));
				break;
	
		
			case '4':
				
				printf("Type a file descriptor: ");
				scanf("%d", &fd);
				printf("Type the new seek: ");
				scanf("%d", &ret);
				mynfs_seek(fd, ret);
				break;
	
	
			case '5':
		
				printf("Type a file descriptor: ");
				scanf("%d", &fd);
				mynfs_close(fd);
				break;


			case '6':
	
				printf("Type a file descriptor: ");
				scanf("%d", &fd);
				ret = mynfs_read(fd, text, MAX_LEN);
				
				printf("Type a file name to save: ");
				scanf("%s", name);
				
				f = fopen(name, "w");
				fwrite(text, sizeof(char), ret, f);
				fclose(f);				

				printf("Which viewer to open with: ");
				scanf("%s", text);
				int tmp = strlen(text);
				strcpy(&text[tmp+1], name);
				text[tmp] = ' ';						
		
				system(text);
				break;


			case '7':
	
				printf("Type a file descriptor: ");
				scanf("%d", &fd);
				printf("Type source file: ");
				scanf("%s", text);
				f = fopen(text, "r");
				fseek(f, 0, SEEK_END);
				len = ftell(f);
				fseek(f, 0, SEEK_SET);
				fread(text, sizeof(char), len, f);
				fclose(f);
				printf("Writing file with size: %d Bytes\n", len);
				ret = mynfs_write(fd, text, len);
				break;
	
				
			case '8':
	
				printf("Type the new checkT (milliseconds): ");
				scanf("%d", &checkT);
				break;


			case '9':
	
				printCacheStatistics();
				break;


			case '0':
			
				return;
		

			default:
		
				printf("Please type a valid choice\n");

			}
			getchar();
		}	
	return;
	}






