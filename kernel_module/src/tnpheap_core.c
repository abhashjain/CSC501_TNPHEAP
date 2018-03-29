//////////////////////////////////////////////////////////////////////
//                             North Carolina State University
//
//
//
//                             Copyright 2016
//
////////////////////////////////////////////////////////////////////////
//
// This program is free software; you can redistribute it and/or modify it
// under the terms and conditions of the GNU General Public License,
// version 2, as published by the Free Software Foundation.
//
// This program is distributed in the hope it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
//
////////////////////////////////////////////////////////////////////////
//
//   Author:  Hung-Wei Tseng
//
//   Description:
//     Skeleton of NPHeap Pseudo Device
//
////////////////////////////////////////////////////////////////////////

#include "tnpheap_ioctl.h"

#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/time.h>

#include <linux/list.h>

struct miscdevice tnpheap_dev;

struct tnpNode{
	__u64 object_id;
	__u64 version;
	struct list_head list; 
};

struct tnpNode tnpList;
__u64 tx_id = 100;
struct mutex tnpListLock;
struct mutex txIDLock;
__u64 tnpheap_get_version(struct tnpheap_cmd __user *user_cmd)
{
    struct tnpheap_cmd cmd;
	printk(KERN_INFO "TNPHEAP: get_version function\n");
    if (!copy_from_user(&cmd, user_cmd, sizeof(cmd)))
    {
		//check if list is empty;
		//if list is empty then always return 0,otherwise I will loop and get the value
		//if after looping also node is not found then also return 0
		//struct list_head *p;
		struct tnpNode *tnpPtr;
		printk(KERN_INFO "TNPHEAP: Inside get_version\n");
		if(list_empty(&tnpList.list)){
			//list is empty
			printk(KERN_INFO "TNPHEAP: list is empty,returning 0!\n");
			return 0;
		} else {
			//list is not empty
			printk(KERN_INFO "TNPHEAP: list is not empty\n");
			list_for_each_entry(tnpPtr, &tnpList.list,list){
				if(tnpPtr!=NULL && tnpPtr->object_id == cmd.offset){
					//Node is found return it's version
					printk(KERN_INFO "TNPHEAP: object ID %llu found and version is %llu\n",tnpPtr->object_id,tnpPtr->version);
					return tnpPtr->version;
				}
			}
		}	
    }    
    return 0;
}

__u64 tnpheap_start_tx(struct tnpheap_cmd __user *user_cmd)
{
    struct tnpheap_cmd cmd;
    __u64 ret=0;
	printk(KERN_INFO "TNPHEAP: Enter Fn tnpheap_start_tx\n");
	//return 0 if some problem in copy_from_user otherwise 
    if (!copy_from_user(&cmd, user_cmd, sizeof(cmd)))
    {
		mutex_lock_interruptible(&txIDLock);
		tx_id +=1;
		mutex_unlock(&txIDLock);	
		printk(KERN_INFO "TNPHEAP: starting the transaction with id %llu\n",tx_id);
        return tx_id;
    }    
    return ret;
}

__u64 tnpheap_commit(struct tnpheap_cmd __user *user_cmd)
{
    struct tnpheap_cmd cmd;
	struct tnpNode *tnpPtr;
	struct tnpNode *newNode;
	//struct list_head *p;
    __u64 ret=1;
	printk(KERN_INFO "TNPHEAP: Enter tnpheap_commit fn\n");
    if (!copy_from_user(&cmd, user_cmd, sizeof(cmd)))
    {
		printk(KERN_INFO "TNPHEAP: GEttting the lock before commit object %llu\n",cmd.offset);
		mutex_lock_interruptible(&tnpListLock);
		if(list_empty(&tnpList.list)){
			//list is empty
			//create a new entry of tnpNode and assign the values to it and then add
			struct tnpNode *newNode = (struct tnpNode*)kmalloc(sizeof(struct tnpNode),GFP_KERNEL);
			if(newNode ==NULL){
				printk(KERN_ERR "TNPHEAP: Memory allocation failed for tnpNode\n");
				mutex_unlock(&tnpListLock);
				return 1;
			}
			//TODO: Commit this node then only I will update the version and add the node to list
			printk(KERN_INFO "TNPHEAP: will update the version and add the node to list for object %llu\n",cmd.offset);
			newNode->version = 1;
			newNode->object_id = cmd.offset;
			INIT_LIST_HEAD(&newNode->list);
			list_add(&(newNode->list),&(tnpList.list));
			mutex_unlock(&tnpListLock);
			return 0;	//return 0 when success
		} else {
			//looking for the node with object_id
			//list is also not empty
			printk(KERN_INFO "TNPHEAP: looking for NOde with offset %llu\n",cmd.offset);
			list_for_each_entry(tnpPtr, &tnpList.list,list){
            	if(tnpPtr!=NULL && tnpPtr->object_id == cmd.offset){
            		//Node is found check the version with the version number in our list
					printk(KERN_INFO "TNPHEAP: Found node offset is %llu\n",cmd.offset);
					printk(KERN_INFO "TNPHEAP: Node is found,checking the version number tnpPtr.version %llu with cmd version %llu\n",tnpPtr->version,cmd.version);
					if(tnpPtr->version == cmd.version){
						printk(KERN_INFO "TNPHEAP: Both version are equal,proceeding with commit for object %llu\n",cmd.offset);
						//Two Version is equal
						//TODO: Commit this node and update the version number in local db
						tnpPtr->version +=1;
						mutex_unlock(&tnpListLock);
						return 0;		//return 0 on success
					} else {
						//Two version doesn't match return 1 to discard the commit
						printk(KERN_ERR "TNPHEAP: two version are not equal discard the change for offset %llu\n",cmd.offset);
						mutex_unlock(&tnpListLock);
						return 1;		//return 1 on failure	
					}
            	}
		}
		//if it is still here then not return means node doesn't exists in list
		//add the node at head of list and commit the change with version 1 and return 0 on success
		newNode = (struct tnpNode*)kmalloc(sizeof(struct tnpNode),GFP_KERNEL);
            if(newNode ==NULL){
                printk(KERN_ERR "TNPHEAP: Memory allocation failed for tnpNode in list not empty case\n");
                mutex_unlock(&tnpListLock);
                return 1;
            }
            //TODO: Commit this node then only I will update the version and add the node to list
            printk(KERN_INFO "TNPHEAP: you are in commit last for offset %llu\n",cmd.offset);
			newNode->version = 1;
            newNode->object_id = cmd.offset;
            INIT_LIST_HEAD(&newNode->list);
            list_add(&(newNode->list),&(tnpList.list));
			printk(KERN_INFO "TNPHEAP: new node is added with object_id %llu when it is not in list\n",newNode->object_id);
            mutex_unlock(&tnpListLock);
            return 0;   //return 0 when success
		}
        //return -1 ;
    	//if it is here then check if lock is still locked then unlock it and then return
		if(mutex_is_locked(&tnpListLock)){
			printk(KERN_INFO "TNPHEAP: mutex was locked still, unlocking then return\n");
			mutex_unlock(&tnpListLock);
		}
	}
	printk(KERN_INFO "TNPHEAP: In the last of commit \n");
    return ret;
}

void printList(void){
	struct tnpNode *tnp,*temp;
    printk(KERN_INFO "TNPHEAP: printing the list");
    list_for_each_entry_safe(tnp,temp,&tnpList.list,list){
        printk(KERN_INFO "TNPHEAP: node with object_id %llu with version number %llu\n",tnp->object_id,tnp->version);
    }
}

long tnpheap_ioctl(struct file *filp, unsigned int cmd,
                                unsigned long arg)
{
    switch (cmd) {
    case TNPHEAP_IOCTL_START_TX:
        return tnpheap_start_tx((void __user *) arg);
    case TNPHEAP_IOCTL_GET_VERSION:
        return tnpheap_get_version((void __user *) arg);
    case TNPHEAP_IOCTL_COMMIT:
        return tnpheap_commit((void __user *) arg);
    default:
        return -ENOTTY;
    }
}

static const struct file_operations tnpheap_fops = {
    .owner                = THIS_MODULE,
    .unlocked_ioctl       = tnpheap_ioctl,
};

struct miscdevice tnpheap_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "tnpheap",
    .fops = &tnpheap_fops,
};

static int __init tnpheap_module_init(void)
{
    int ret = 0;
    if ((ret = misc_register(&tnpheap_dev)))
        printk(KERN_ERR "Unable to register \"tnpheap\" misc device\n");
    else
        printk(KERN_ERR "\"tnpheap\" misc device installed\n");
	INIT_LIST_HEAD(&tnpList.list);
	printk(KERN_INFO "TNPHEAP: before the mutex\n");
 	mutex_init(&(tnpListLock));
	mutex_init(&(txIDLock));
	printk(KERN_INFO "TNPHEAP:tnpheap install\n");
	return ret; 
}

static void __exit tnpheap_module_exit(void)
{
	struct tnpNode *tnp,*temp;
	printk(KERN_INFO "TNPHEAP: doing cleanup in exit");
	//printig the list before doing exit
	printList();
	list_for_each_entry_safe(tnp,temp,&tnpList.list,list){
		printk(KERN_INFO "TNPHEAP: freeing node with object_id %llu\n",tnp->object_id);
		list_del(&tnp->list);
		kfree(tnp);
	}
	printk(KERN_INFO "TNPHEAP: List cleanup DONE now exiting!\n");
    misc_deregister(&tnpheap_dev);
    return;
}

MODULE_AUTHOR("Hung-Wei Tseng <htseng3@ncsu.edu>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
module_init(tnpheap_module_init);
module_exit(tnpheap_module_exit);
