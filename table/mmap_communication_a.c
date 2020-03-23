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
	close(fd);

	while(1){
		printf("%s\n",start);
		sleep(2);
	}	

	return 0;	
}


