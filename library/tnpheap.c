#include <npheap/tnpheap_ioctl.h>
#include <npheap/npheap.h>
#include <npheap.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <malloc.h>
#include <string.h>

struct node{
//	__u64 transaction_id; //Key is Object ID given by process
	struct node* next;
	void * kdata;
	struct tnpheap_cmd cmd;
};

//Global variable for storing data in Linkedlist
struct node* head = NULL;
int debug = 0;

__u64 tnpheap_get_version(int npheap_dev, int tnpheap_dev, __u64 offset)
{
    offset = offset * getpagesize();
    if(debug)
    printf("\nEnter TNPHeap::getversion");
     struct tnpheap_cmd cmd;
     cmd.offset = offset;
    if(debug)
    printf("\nExit TNPHeap::getversion");
     return ioctl( tnpheap_dev, TNPHEAP_IOCTL_GET_VERSION , &cmd);
}



int tnpheap_handler(int sig, siginfo_t *si)
{
    printf("SIGFAULT. Shutting down");
    exit(0);
}


struct node* searchNode(__u64 offset)
{
    if(debug)
    printf("\nEnter TNPHeap::searchNode");
    struct node * searchIter = head;
    while(searchIter != NULL)
    {
	if(searchIter->cmd.offset == offset){
		if(debug)
			printf("\nExit TNPHeap::searchNode, Node Found");
		return searchIter;
	}
	searchIter = searchIter->next;
    }
    if(debug)
    printf("\nExit TNPHeap::searchNode, Node not found");
    return NULL;
}

void *tnpheap_alloc(int npheap_dev, int tnpheap_dev, __u64 offset, __u64 size)
{
    int pid;
    pid = getpid();
    offset = offset * getpagesize();
    if(debug)
    printf("\n%d : Enter TNPHeap::alloc",pid);
    struct node * search = NULL;
	search = (struct node*)malloc(sizeof(struct node));
    	struct tnpheap_cmd cmd;
	cmd.offset = offset;
	cmd.size = size;
	cmd.version = ioctl( tnpheap_dev, TNPHEAP_IOCTL_GET_VERSION , &cmd);
	//__u64 aligned_size= ((size + getpagesize() - 1) / getpagesize())*getpagesize();
    	//search->kdata = mmap(0,aligned_size,PROT_READ|PROT_WRITE,MAP_SHARED,npheap_dev,offset);
	search->cmd = cmd;
	search->kdata =NULL;
	/*npheap_lock(npheap_dev,search->cmd.offset/getpagesize());
	search->kdata = npheap_alloc(npheap_dev, search->cmd.offset/getpagesize(), search->cmd.size);
	npheap_unlock(npheap_dev,search->cmd.offset/getpagesize());
	*/
	if(search->cmd.version > 0)
	{
		npheap_lock(npheap_dev,search->cmd.offset/getpagesize());
		__u64 npsize;
			search->kdata = npheap_alloc(npheap_dev, search->cmd.offset/getpagesize(), search->cmd.size);
     		npsize = npheap_getsize(npheap_dev, search->cmd.offset/getpagesize());
		search->cmd.data = malloc(search->cmd.size);
    		if(debug)
    		printf("\n%d : TNPHeap::alloc node found in tnpheap kernel. Size in input was : %llu and size from NPheap : %llu", pid, size, search->cmd.size);
		memset(search->cmd.data, 0 , search->cmd.size);
		memcpy(search->cmd.data,search->kdata,search->cmd.size > npsize ? npsize : search->cmd.size);
		npheap_unlock(npheap_dev,search->cmd.offset/getpagesize());
	}
	if(head != NULL)
	{
		if(search->cmd.version == 0)
		search->cmd.data = malloc(size);
    		if(debug)
    		printf("\n%d : TNPHeap::alloc library list not empty, inserting element at head",pid);
		search->next = head;
		head = search;
	}
	else
	{
		if(search->cmd.version == 0)
		search->cmd.data = malloc(size);
    		if(debug)
    		printf("\n%d : TNPHeap::alloc library list empty, adding first node", pid);
		head = search;
		search->next = NULL;
	}
	if(debug)
                printf("\n%d : Exit TNPHeap::alloc, New node created and added to list", pid );
	return search->cmd.data;
}

__u64 tnpheap_start_tx(int npheap_dev, int tnpheap_dev)
{
    int pid;
    pid = getpid();
    char *arr;
    arr = getenv("DEBUG");
    if(arr && arr[0] == 'Y')
	debug=1;
    if(debug)
    printf("\n%d : Enter TNPHeap::start trx",pid);
    struct tnpheap_cmd cmd; 
    __u64 trxid;
    trxid = ioctl( tnpheap_dev, TNPHEAP_IOCTL_START_TX , &cmd);
    if(debug)
    printf("\n%d : Exit TNPHeap::start trx, with trx id: %llu", pid , trxid);
    return trxid;
}

int tnpheap_commit(int npheap_dev, int tnpheap_dev)
{   
    int pid;
    pid = getpid();
    if(debug)
    printf("\n%d : Enter TNPHeap::commit",pid);
    struct node* iter = head;
    while(iter != NULL) {
	struct tnpheap_cmd cmd;
    	cmd = iter->cmd;
	__u64 lastlocked = -1;
	if(iter == head || lastlocked != iter->cmd.offset/getpagesize()){
		lastlocked = iter->cmd.offset/getpagesize();
		npheap_unlock(npheap_dev,lastlocked);
		npheap_lock(npheap_dev,lastlocked);
		if(debug)
		printf("\n%d : TNPHeap::commit, locking offset : %llu",pid, lastlocked);
	}
	if(ioctl( tnpheap_dev, TNPHEAP_IOCTL_COMMIT , &cmd)){
		struct node* rollbackIter = head;
		while(rollbackIter != NULL) {
			npheap_unlock(npheap_dev,rollbackIter->cmd.offset/getpagesize());	
			if(rollbackIter == iter)
				break;
			rollbackIter = rollbackIter->next;
		}
		while(head != NULL) {
		        struct node* temp = head;
			head = head->next;
		        free(temp->cmd.data);
        		temp->kdata = NULL;
		        free(temp);
		}
		if(debug)
    			printf("\n%d : Exit TNPHeap::commit, commit failed, rollback. return 1",pid);
		return 1;
	}
	iter = iter->next;
    }
    while(head != NULL) {
		struct node* temp = head;
		if(temp->kdata == NULL){
			temp->kdata = npheap_alloc(npheap_dev, temp->cmd.offset/getpagesize(), temp->cmd.size);
		}
		__u64 npsize = npheap_getsize(npheap_dev, temp->cmd.offset/getpagesize());
        if(npsize < temp->cmd.size){
            npheap_delete(npheap_dev,temp->cmd.offset/getpagesize());
            temp->kdata = npheap_alloc(npheap_dev, temp->cmd.offset/getpagesize(), temp->cmd.size);
            memset(temp->kdata, 0 , temp->cmd.size);
        }
		else {
			memset(temp->kdata, 0 , npsize);
		}
		memcpy(temp->kdata,temp->cmd.data,temp->cmd.size);
		npheap_unlock(npheap_dev,temp->cmd.offset/getpagesize());
		if(debug)
			printf("\n%d : TNPHeap::commit unlocking offest : %llu" , pid, temp->cmd.offset/getpagesize());
		head = head->next;
		free(temp->cmd.data);
		temp->kdata = NULL;
		free(temp);
    } 
    if(debug)
    	printf("\n%d : Exit TNPHeap::commit successfull, returning zero",pid);
    return 0;
}

