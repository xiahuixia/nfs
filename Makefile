CC = gcc
CFLAGS = -D_PRINT_SERVER_DIAGNOSTICS

.PHONY: all dirs clean


ROOT	  = $(CURDIR)
SRCDIR    = $(ROOT)/src
OBJDIR 	  = $(ROOT)/obj
LOGDIR	  = $(ROOT)/log
MYNFSDIR  = $(ROOT)/mynfs



all: dirs DS NFSP NFSB p1 measurements


dirs: 
	mkdir -p $(OBJDIR)
	mkdir -p $(LOGDIR)
	mkdir -p $(MYNFSDIR)

dirs1:
	mkdir -p $(MYNFSDIR)1

dirs2:
	mkdir -p $(MYNFSDIR)1
	mkdir -p $(MYNFSDIR)2

dirs3:
	mkdir -p $(MYNFSDIR)1
	mkdir -p $(MYNFSDIR)2
	mkdir -p $(MYNFSDIR)3

dirs4:
	mkdir -p $(MYNFSDIR)1
	mkdir -p $(MYNFSDIR)2
	mkdir -p $(MYNFSDIR)3
	mkdir -p $(MYNFSDIR)4


DS: $(OBJDIR)/DS_server.o
	$(CC) $(OBJDIR)/DS_server.o -o DS

NFSP: $(OBJDIR)/dirsrvc.o $(OBJDIR)/NFS_server_Primary.o 
	$(CC) $(OBJDIR)/dirsrvc.o $(OBJDIR)/NFS_server_Primary.o -o NFSP

NFSB: $(OBJDIR)/dirsrvc.o $(OBJDIR)/NFS_server_Backup.o
	$(CC) $(OBJDIR)/dirsrvc.o $(OBJDIR)/NFS_server_Backup.o -o NFSB

p1: $(OBJDIR)/dirsrvc.o $(OBJDIR)/nfssrvc.o $(OBJDIR)/p1.o
	$(CC) $(OBJDIR)/dirsrvc.o $(OBJDIR)/nfssrvc.o $(OBJDIR)/p1.o -o p1
	
measurements: $(OBJDIR)/dirsrvc.o $(OBJDIR)/nfssrvc.o $(OBJDIR)/measurements.o 
	$(CC) $(OBJDIR)/dirsrvc.o $(OBJDIR)/nfssrvc.o $(OBJDIR)/measurements.o -o measurements


$(OBJDIR)/DS_server.o: $(SRCDIR)/DS_server.c
	$(CC) -c $(SRCDIR)/DS_server.c $(CFLAGS) -o $(OBJDIR)/DS_server.o

$(OBJDIR)/NFS_server_Primary.o: $(SRCDIR)/NFS_server_Primary.c
	$(CC) -c $(SRCDIR)/NFS_server_Primary.c $(CFLAGS) -o $(OBJDIR)/NFS_server_Primary.o

$(OBJDIR)/NFS_server_Backup.o: $(SRCDIR)/NFS_server_Backup.c
	$(CC) -c $(SRCDIR)/NFS_server_Backup.c $(CFLAGS) -o $(OBJDIR)/NFS_server_Backup.o

$(OBJDIR)/dirsrvc.o: $(SRCDIR)/dirsrvc.c $(SRCDIR)/dirsrvc.h
	$(CC) -c $(SRCDIR)/dirsrvc.c $(CFLAGS) -o $(OBJDIR)/dirsrvc.o

$(OBJDIR)/nfssrvc.o: $(SRCDIR)/nfssrvc.c $(SRCDIR)/nfssrvc.h
	$(CC) -c $(SRCDIR)/nfssrvc.c $(CFLAGS) -o $(OBJDIR)/nfssrvc.o

$(OBJDIR)/p1.o: $(SRCDIR)/p1.c
	$(CC) -c $(SRCDIR)/p1.c $(CFLAGS) -o $(OBJDIR)/p1.o

$(OBJDIR)/measurements.o: $(SRCDIR)/measurements.c
	$(CC) -c $(SRCDIR)/measurements.c $(CFLAGS) -o $(OBJDIR)/measurements.o	

clean:
	\rm -rf *o *~ DS NFSP NFSB p1 measurements $(LOGDIR) $(OBJDIR) $(MYNFSDIR)*
	


