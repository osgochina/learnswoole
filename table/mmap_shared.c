#include <sys/mman.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <error.h>
#include <unistd.h>
#include <fcntl.h>


int main(int argc,char *argv[])
{

	int fd,nread;
	struct stat st;
	char *start;

	if((fd = open(argv[1],O_RDWR)) < 0){
		perror("please usage: mmapread <filepath>");
		return -1;
	}
	if(fstat(fd,&st) == -1){
		perror("fstat");
		return -1;
	}
	
	if((start = mmap(NULL,st.st_size,PROT_READ | PROT_WRITE,MAP_SHARED,fd,0)) == (void *)-1  ){
		perror("mmap");
		return -1;
	}
	start[0] = '1';
	start[11] = '2';
	if((msync((void *)start,st.st_size,MS_SYNC)) == -1){
		perror("msync");
		return -1;
	}
	
	if((munmap((void *)start,st.st_size)) == -1){
		perror("munmap");
		return -1;
	}
	close(fd);
	return 0;	
}


