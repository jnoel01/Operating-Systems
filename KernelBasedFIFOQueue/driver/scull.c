/*
 * main.c -- the bare scull char module
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>			/* printk() */
#include <linux/slab.h>				/* kmalloc() */
#include <linux/fs.h>				/* everything... */
#include <linux/errno.h>			/* error codes */
#include <linux/types.h>			/* size_t */
#include <linux/cdev.h>

#include <linux/uaccess.h>			/* copy_*_user */
#include <linux/mutex.h>			/* mutex */
#include <linux/semaphore.h> 		/* semaphore */

#include "scull.h"					/* local definitions */
#include "access_ok_version.h"

/*
 * Our parameters which can be set at load time.
 */

static int scull_major =   SCULL_MAJOR;
static int scull_minor =   0;
static int scull_fifo_elemsz = SCULL_FIFO_ELEMSZ_DEFAULT; /* SIZE */
static int scull_fifo_size   = SCULL_FIFO_SIZE_DEFAULT; /* N */

static DEFINE_MUTEX(fifo_lock);
static struct semaphore consumer_lock;
static struct semaphore producer_lock;


module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_fifo_size, int, S_IRUGO);
module_param(scull_fifo_elemsz, int, S_IRUGO);

MODULE_AUTHOR("Jessica Noel");
MODULE_LICENSE("Dual BSD/GPL");


static struct cdev scull_cdev;		/* Char device structure

/*
 * Open and close
 */

static int scull_open(struct inode *inode, struct file *filp) {
	printk(KERN_INFO "scull open\n");
	return 0;          /* success */
}
static int scull_release(struct inode *inode, struct file *filp) {
	printk(KERN_INFO "scull close\n");
	return 0;          /* success */
}



static ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
	printk(KERN_INFO "scull read\n");

	int copy_error_check = 0, num_bytes_read = 0;

	// Lock and unlock
	if(down_interruptible(&consumer_lock)) {
		return -EFAULT;
	}
	if(mutex_lock_interruptible(&fifo_lock)) {
		up(&consumer_lock);
		return -EFAULT;
	};

	if(count <= *(ssize_t*) fifo_start) {
		copy_error_check = __copy_to_user(buf, fifo_start + sizeof(ssize_t), count);
		if(copy_error_check != 0) {
			mutex_unlock(&fifo_lock);
			up(&consumer_lock);
			return -EFAULT;
		}
		num_bytes_read = count;
	} 
	else {
		copy_error_check = __copy_to_user(buf, fifo_start + sizeof(ssize_t), *(ssize_t*) fifo_start);
		if(copy_error_check != 0) {
			mutex_unlock(&fifo_lock);
			up(&consumer_lock);
			return -EFAULT;
		}
		num_bytes_read = *(ssize_t*) fifo_start;
	}
	
	fifo_start = fifo_start + scull_fifo_elemsz + sizeof(ssize_t);
	if (fifo_start >= fifo_queue[scull_fifo_size]){
		fifo_start = fifo_queue;
	}

	mutex_unlock(&fifo_lock);
	up(&producer_lock);

	return num_bytes_read;
}

static ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
	printk(KERN_INFO "scull write\n");

	int copy_error_check = 0;
	int num_bytes_read = 0;

	if(down_interruptible(&producer_lock)) {
		return -EFAULT;
	}
	if(mutex_lock_interruptible(&fifo_lock)) {
		up(&producer_lock);
		return -EFAULT;
	};

	if(count <= scull_fifo_elemsz) {
		copy_error_check = __copy_from_user(fifo_end + sizeof(ssize_t), buf, count);
		if(copy_error_check != 0) {
			mutex_unlock(&fifo_lock);
			up(&producer_lock);
			return -EFAULT;
		}
		num_bytes_read = count;
	} 
	else {
		copy_error_check = __copy_from_user(fifo_end + sizeof(ssize_t), buf, scull_fifo_elemsz);
		if(copy_error_check != 0) {\
			mutex_unlock(&fifo_lock);
			up(&producer_lock);
			return -EFAULT;
		}
		num_bytes_read = scull_fifo_elemsz;
	}

	memcpy(fifo_end, &num_bytes_read, sizeof(num_bytes_read));

	
	fifo_end = fifo_end + scull_fifo_elemsz + sizeof(ssize_t);
	if(fifo_end >= fifo_queue[scull_fifo_size]){
		fifo_end = fifo_queue;
	}

	mutex_unlock(&fifo_lock);
	up(&consumer_lock);

	return num_bytes_read;
}

/*
 * The ioctl() implementation
 */

static long scull_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{

	int err = 0;
	int retval = 0;
    
	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > SCULL_IOC_MAXNR) return -ENOTTY;

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok_wrapper(VERIFY_WRITE, (void __user *)arg,
				_IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok_wrapper(VERIFY_READ, (void __user *)arg,
				_IOC_SIZE(cmd));
	if (err) return -EFAULT;

	switch(cmd) {
	case SCULL_IOCGETELEMSZ:
		return scull_fifo_elemsz;

	default:  /* redundant, as cmd was checked against MAXNR */
		return -ENOTTY;
	}
	return retval;

}


struct file_operations scull_fops = {
	.owner 		= THIS_MODULE,
	.unlocked_ioctl = scull_ioctl,
	.open 		= scull_open,
	.release	= scull_release,
	.read 		= scull_read,
	.write 		= scull_write,
};

/* Finally, the module stuff */
void scull_cleanup_module(void) {
	dev_t devno = MKDEV(scull_major, scull_minor);

	/* Free FIFO safely */
	kfree(fifo_queue);

	/* Get rid of the char dev entry */
	cdev_del(&scull_cdev);

	/* cleanup_module is never called if registering failed */
	unregister_chrdev_region(devno, 1);
}
int scull_init_module(void) {
	int result;
	dev_t dev = 0;

	/*
	 * Get a range of minor numbers to work with, asking for a dynamic
	 * major unless directed otherwise at load time.
	 */
	if (scull_major) {
		dev = MKDEV(scull_major, scull_minor);
		result = register_chrdev_region(dev, 1, "scull");
	} else {
		result = alloc_chrdev_region(&dev, scull_minor, 1, "scull");
		scull_major = MAJOR(dev);
	}
	if (result < 0) {
		printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
		return result;
	}

	cdev_init(&scull_cdev, &scull_fops);
	scull_cdev.owner = THIS_MODULE;
	result = cdev_add (&scull_cdev, dev, 1);
	/* Fail gracefully if need be */
	if (result) {
		printk(KERN_NOTICE "Error %d adding scull character device", result);
		goto fail;
	}

	// Initalize fifo queue
    printk(KERN_INFO "scull: FIFO SIZE=%u, ELEMSZ=%u\n", scull_fifo_size, 
            scull_fifo_elemsz);

    fifo_queue = kmalloc(scull_fifo_size * (scull_fifo_elemsz+sizeof(ssize_t)), GFP_KERNEL); 
    if (fifo_queue == NULL) {
        printk(KERN_NOTICE "Error: Failed to allocated memory for FIFO array.");
        goto fail;
    }

	// Initialize start and end
	fifo_start = fifo_queue;
	fifo_end = fifo_queue;

	// Initialize semaphores
	sema_init(&consumer_lock, 0);
	sema_init(&producer_lock, scull_fifo_size);

	return 0; /* succeed */

	fail:
		scull_cleanup_module();
		return result;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);