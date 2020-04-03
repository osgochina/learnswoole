#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>



//简化了swoole的警告输出，具体的可以直接看swoole.h是怎么实现的
#define swSysWarn(str,...) \
    do{ \
		printf("%s\n",str);\
    } while(0)

//定义对其长度 32位机器上4字节  64位系统上8字节
#define SW_DEFAULT_ALIGNMENT   sizeof(unsigned long)

#define SW_MEM_ALIGNED_SIZE(size) \
		SW_MEM_ALIGNED_SIZE_EX(size,SW_DEFAULT_ALIGNMENT)
//表示按8取整对齐  size等于11，计算后变成16  size等于17 计算后变成24
#define SW_MEM_ALIGNED_SIZE_EX(size, alignment) \
        (((size) + ((alignment) - 1LL)) & ~((alignment) - 1LL))

// 定义mmap映射文件名的最大长度
#define SW_SHM_MMAP_FILE_LEN 64

//共享内存的数据结构
typedef struct _swShareMemory_mmap
{
	size_t size;						//共享内存的大小(包含swShareMemory结构体的大小)
	char mapfile[SW_SHM_MMAP_FILE_LEN]; //共享内存使用的内存映射文件的文件名，最长64个字节。
	int tmpfd;							//内存映射文件的描述符。
	int key;							//System V的shm系列函数创建共享内存的key的值。
	int shmid;                          //System V的shm系列函数创建的共享内存的id(类似于fd)。
	void *mem;							/** void 类型的指针，这个变量里面存的是申请到的共享内存首地址，
											可以理解为相当于面向对象中的this指针。
											可以很方便的访问到swShareMemory结构体的各个变量**/
} swShareMemory;

//定义
void *swShareMemory_mmap_create(swShareMemory *object, size_t size, char *mapfile);
void *swShareMemory_sysv_create(swShareMemory *object, size_t size, int key);
int swShareMemory_sysv_free(swShareMemory *object, int rm);
int swShareMemory_mmap_free(swShareMemory *object);



//swoole申请共享内存
void* sw_shm_malloc(size_t size)
{
	size = SW_MEM_ALIGNED_SIZE(size);
	swShareMemory object;
	void *mem;
	size += sizeof(swShareMemory);
	mem = swShareMemory_mmap_create(&object,size,NULL);
	if(mem == NULL){
		return NULL;
	}else{
		memcpy(mem,&object,sizeof(swShareMemory));
		return (char *) mem + sizeof(swShareMemory);
	}
	
}

//swoole申请共享内存
void* sw_shm_calloc(size_t num, size_t _size)
{
    swShareMemory object;
    void *mem;
    void *ret_mem;
    size_t size = sizeof(swShareMemory) + (num * _size);
    size = SW_MEM_ALIGNED_SIZE(size);

    mem = swShareMemory_mmap_create(&object, size, NULL);
    if (mem == NULL)
    {
        return NULL;
    }
    else
    {
        memcpy(mem, &object, sizeof(swShareMemory));
        ret_mem = (char *) mem + sizeof(swShareMemory);
        bzero(ret_mem, size - sizeof(swShareMemory));
        return ret_mem;
    }
}

//修改共享内存权限
//共享内存映射完成以后，有port标记的PROT_READ,PROT_WRITE,PROT_EXEC,PROT_NONE权限仍然可以被mprotect系统调用修改。具体用法参见 mmap详解
int sw_shm_protect(void *addr, int flags)
{
    swShareMemory *object = (swShareMemory *) ((char *) addr - sizeof(swShareMemory));
    return mprotect(object, object->size, flags);
}

//释放共享内存
void sw_shm_free(void *ptr)
{
    swShareMemory *object = (swShareMemory *) ((char *) ptr - sizeof(swShareMemory));
    swShareMemory_mmap_free(object);
}

/**
如果我们申请了一段共享内存，使用的时候发现不够用了怎么办？
这个时候就要使用到sw_shm_realloc 这个函数， 这个函数很简单，就是新申请一段共享内存，
把旧的数据复制到新的内存中，然后再把旧的内存释放。
这段代码swShareMemory *object = (swShareMemory *) ((char *) ptr - sizeof(swShareMemory));
大家注意到了没，怎么样使用swShareMemory这个对象？
只需要把传入的内存对象指针往前移动sizeof(swShareMemory)长度，就能得到真正的共享内存首地址。
也就能很方便的使用这个对象。接下来的代码大家都能看到这个用法。
**/
void* sw_shm_realloc(void *ptr, size_t new_size)
{
    swShareMemory *object = (swShareMemory *) ((char *) ptr - sizeof(swShareMemory));
    void *new_ptr;
    new_ptr = sw_shm_malloc(new_size);
    if (new_ptr == NULL)
    {
        return NULL;
    }
    else
    {
        memcpy(new_ptr, ptr, object->size);
        sw_shm_free(ptr);
        return new_ptr;
    }
}





void *swShareMemory_mmap_create(swShareMemory *object, size_t size, char *mapfile)
{
    void *mem;
    int tmpfd = -1;
	//MAP_SHARED 与其它所有映射这个对象的进程共享映射空间。
    int flag = MAP_SHARED;
    bzero(object, sizeof(swShareMemory));

//MAP_ANONYMOUS 匿名映射，映射区不与任何文件关联。
#ifdef MAP_ANONYMOUS
    flag |= MAP_ANONYMOUS;
#else
//使用了宏判断，是否支持匿名映射，注意MAP_ANONYMOUS这个宏定义在-std=c99模式下是不支持的，但是-std=gnu99是支持的。
//如果使用了MAP_ANONYMOUS,那么会忽视fd参数，在不支持MAP_ANONYMOUS这个宏的系统中，
//使用操作系统提供的/dev/zero文件提供匿名映射支持。
    if (mapfile == NULL)
    {
        mapfile = "/dev/zero";
    }
    if ((tmpfd = open(mapfile, O_RDWR)) < 0)
    {
        return NULL;
    }
    strncpy(object->mapfile, mapfile, SW_SHM_MMAP_FILE_LEN);
    object->tmpfd = tmpfd;
#endif
/**
MAP_HUGE_PAGE 是否支持大页模式分配内存
这里要注意的是MAP_HUGE_PAGE这个宏定义，这个是linux内核在2.6.32 引入的一个flags,确定是否使用大页分配共享内存。
内核传统使用的是4k小页,常见的体系架构都会提供两种大页模式，一般是2M和1G这两个size。
使用大页可以减少页表的数量，从而减少TLB MISS的概率，提高系统访问性能，但是也会降低内存管理的粒度和灵活性，在使用小内存的时候造成内存浪费。
 swoole在这里是判断要申请的内存size大于2M，就使用大页模式。 MAP_ALIGNED_SUPER 这个参数是FreeBSD中的特有参数，使用大页模式。
**/
#if defined(SW_USE_HUGEPAGE) && defined(MAP_HUGE_PAGE)
    if (size > 2 * 1024 * 1024)
    {
#if defined(MAP_HUGETLD)
        flag |= MAP_HUGETLB;
#elif defined(MAP_ALIGNED_SUPER)
        flag &= ~MAP_ANONYMOUS;
        flag |= MAP_ALIGNED_SUPER;
#endif
    }
#endif
/**
mmap函数的调用，PROT_READ|PROT_WRITE 表示申请的映射区可读可写，最后把申请的的内存映射区地址赋值给传入的object->mem。以后就可以通过swShareMemory来使用了。
**/
    mem = mmap(NULL, size, PROT_READ | PROT_WRITE, flag, tmpfd, 0);
#ifdef MAP_FAILED
    if (mem == MAP_FAILED)
#else
    if (!mem)
#endif
    {
        swSysWarn("mmap(%ld) failed", size);
        return NULL;
    }
    else
    {
        object->size = size;
        object->mem = mem;
        return mem;
    }
}

int swShareMemory_mmap_free(swShareMemory *object)
{
    return munmap(object->mem, object->size);
}
/**
System V是Unix操作系统的一个分支，是一些Unix共同特性的源头，
linux实现了System V一套应用于系统的接口协议， POSIX相对于System V可以说是比较新的标准，语法也相对简单。
所以swoole使用的基于POSIX协议实现的接口。 以下代码目前应该没有被使用，以后就说不准了。
**/
void *swShareMemory_sysv_create(swShareMemory *object, size_t size, int key)
{
    int shmid;
    void *mem;
    bzero(object, sizeof(swShareMemory));

    if (key == 0)
    {
        key = IPC_PRIVATE;
    }
    //SHM_R | SHM_W
    if ((shmid = shmget(key, size, IPC_CREAT)) < 0)
    {
        swSysWarn("shmget(%d, %ld) failed", key, size);
        return NULL;
    }
    if ((mem = shmat(shmid, NULL, 0)) == (void *) -1)
    {
        swSysWarn("shmat() failed");
        return NULL;
    }
    else
    {
        object->key = key;
        object->shmid = shmid;
        object->size = size;
        object->mem = mem;
        return mem;
    }
}

int swShareMemory_sysv_free(swShareMemory *object, int rm)
{
    int shmid = object->shmid;
    int ret = shmdt(object->mem);
    if (rm == 1)
    {
        shmctl(shmid, IPC_RMID, NULL);
    }
    return ret;
}


#define TEXT_SZ 2048  
  
struct shared_use_st  
{  
    int written;//作为一个标志，非0：表示可读，0表示可写  
    char text[TEXT_SZ];//记录写入和读取的文本  
};  


int main(int argc,char **argv)
{
	//printf("size=%d,aligned=%lld\n",25,SW_MEM_ALIGNED_SIZE(25));
	//printf("size=%d,aligned=%lld\n",11,SW_MEM_ALIGNED_SIZE(11));

	void *ptr = sw_shm_malloc(sizeof(struct shared_use_st));
    if(ptr == NULL){
         printf("create shared memory faild\n");
         exit(1);
    }
	swShareMemory *object = (swShareMemory *) ((char *) ptr - sizeof(swShareMemory));
	if(fork() > 0){
        sleep(1);
        printf("你现在是在子进程中\n");
        printf("创建的共享内存长度是%ld\n",object->size);
        printf("获取到父进程写入的数据: %s\n",((struct shared_use_st *)ptr)->text);
	}else{
        printf("你现在是在父进程中\n");
        printf("创建的共享内存长度是%ld\n",object->size);
        char *data = "共享的数据";
        printf("父进程写入的数据: %s\n",data);
        strncpy(((struct shared_use_st *)ptr)->text, data, strlen(data));
    }
    return 0;	
}





















