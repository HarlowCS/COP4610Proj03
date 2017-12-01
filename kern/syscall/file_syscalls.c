/*
 * File-related system call implementations.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <copyinout.h>
#include <vfs.h>
#include <vnode.h>
#include <openfile.h>
#include <filetable.h>
#include <syscall.h>

/*
 * open() - get the path with copyinstr, then use openfile_open and
 * filetable_place to do the real work.
 */
int
sys_open(const_userptr_t upath, int flags, mode_t mode, int *retval)
{
	const int allflags = O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND | O_NOCTTY;

	char *kpath = NULL;
	struct openfile *file = NULL;
	int *fd = NULL;
	int errno = 0;
	size_t *size = NULL;

	/* 
	 * Your implementation of system call open starts here.  
	 *
	 * Check the design document design/filesyscall.txt for the steps
	 *
	 *	sys_open needs to:
  	 *	- check for invalid flags
   	 *	- copy in the supplied pathname
   	 *	- open the file (use openfile_open)
   	 *	- place the file into curproc's file table (use filetable_place) 	 *
	 */
	
	//Check for invalid flags
	//allflags contains all flags, so allflags | flags contains allflags PLUS the new flags
	if (allflags != (flags | allflags)) 
	{
		*retval = -1;
		return EINVAL; //invalid argument == 8
	} 
	
	//Copy in the supplied pathname
	kpath = (char *)kmalloc(sizeof(char) * (strlen((char*)upath + 1)));
	size = (size_t *)kmalloc(sizeof(size_t));

	errno = copyinstr(upath, kpath, strlen((char*)upath)+1, size);
	if(errno) //returns if errno != 0, meaning we have an error
	{
		*retval = -1;
		return errno;
	} 	

	//Open the file
	file = (struct openfile *)kmalloc(sizeof(struct openfile));
	errno = openfile_open(kpath, flags, mode, &file);
	if(errno) 
	{
		*retval = -1;
		return errno;
	}

	openfile_incref(file);

	//use filetable_place to place file into curproc's filetable
	fd = (int *)kmalloc(sizeof(int));
	errno = filetable_place(curproc->p_filetable, file, fd);
	if(errno)
	{
		*retval = -1;
		return errno;
	}
	
	*retval = *fd;
	kfree(kpath);
	
	return 0; //return that function executed without errors
}

/*
 * read() - read data from a file
 */
int
sys_read(int fd, userptr_t buf, size_t size, int *retval)
{
       /* 
        * Your implementation of system call read starts here.  
        * sys_read needs to:
   	*   - translate the file descriptor number to an open file object
     	*   (use filetable_get)
   	*   - lock the seek position in the open file (but only for seekable objects)
   	*   - check for files opened write-only
   	*   - construct a struct: uio
   	*   - call VOP_READ
   	*   - update the seek position afterwards
   	*   - unlock and filetable_put()
   	*   - set the return value correctly
        * Check the design document design/filesyscall.txt for the steps
        */
	int errno = 0;
	struct openfile *file;	
	

	//translate file descriptor number to an open file object
	errno = filetable_get(curproc->p_filetable, fd, &file);
	if(errno)
	{
		*retval = -1;
		return errno;
	}

	//lock the seek position in the open file (but only for seekable objects)
	//vnode.h describes vop_isseekable
	int seekable = VOP_ISSEEKABLE(file->of_vnode);
	
	if(seekable)
	{
		lock_acquire(file->of_offsetlock);
	}

	//check for files opened write-only
	if(file->of_accmode == O_WRONLY)
	{
		*retval = -1;
		return EACCES; //file permission denied
	}

	//construct a struct: uio
	struct iovec iov;
	struct uio ku;
	
	//actually initialize uio, found function in uio.c
	uio_kinit(&iov, &ku, buf, size, file->of_offset, UIO_READ);
	
	//callVOP_READ
	errno = VOP_READ(file->of_vnode, &ku);
	if(errno)
	{
		*retval = -1;
		return errno;
	}

	//set returnval since we are error free
	*retval = ku.uio_offset - file->of_offset;

	//update the seek position afterwards
	file->of_offset = ku.uio_offset;

	//if it was locked, unlock it
	if(seekable)
	{
		lock_release(file->of_offsetlock);
	}

	//filetable_put() time
	filetable_put(curproc->p_filetable, fd, file);
	
       return 0;
}

/*
 * write() - write data to a file
 * same as sys_read but it writes instead
 */
int
sys_write(int fd, userptr_t buf, size_t nbytes, int *retval)
{
	//translate file descriptor number to open fileobject
	struct openfile *file;
	int errno = 0;
	
	errno = filetable_get(curproc->p_filetable, fd, &file);
	if(errno)
	{
		*retval = -1;
		return errno;
	}

	//lockit if it's seekable
	int seekable = VOP_ISSEEKABLE(file->of_vnode);
	if(seekable)
	{
		lock_acquire(file->of_offsetlock);
	}

	//check for correct permissions (not a read only)
	if(file->of_accmode == O_RDONLY)
	{
		*retval = -1;
		return EACCES;
	}

	//create uio
	struct iovec iov;
	struct uio ku;

	uio_kinit(&iov, &ku, buf, nbytes, file->of_offset,UIO_WRITE);

	//call VOP_WRITE 
	VOP_WRITE(file->of_vnode, &ku);
	
	//error free! set return value correctly
	*retval = ku.uio_offset - file->of_offset;

	//unlock if locked
	if(seekable)
	{
		lock_release(file->of_offsetlock);
	}

	//use filetable_put
	filetable_put(curproc->p_filetable, fd, file);

	return 0;
}
/*
 * close() - remove from the file table.
 *	sys_close needs to:
 *  	- validate the fd number (ure filetable_okfd)
 *  	- use filetable_placeat to replace curproc's file table entry with NULL
 *  	- check if the previous entry in the file table was also NULL
 *    	(this means no such file was open)
 *  	- decref the open file returned by filetable_placeat 
 */
int 
sys_close(int fd, int *retval)
{
	//validate the fd number (use filetable_okfd)
	struct openfile *file;
	if(!(filetable_okfd(curproc->p_filetable, fd)))
	{
		*retval = -1;
		return EBADF; //in errno.h, indicates bad file number
	}	

	//use filetable_placeat to replace curproc's file tbale entry with NULL
	filetable_placeat(curproc->p_filetable, NULL, fd, &file);

	//check if the previous entry in the file table was also null
	if(file == NULL)
	{
		*retval = -1;
		return EFAULT; //bad memory reference (closing a file that was never open)
	}

	//decref the file returned by filetable_placeat
	openfile_decref(file);
	*retval = 0;
	
	return 0;
}

/* 
* meld () - combine the content of two files word by word into a new file
- copy in the supplied pathnames (pn1, pn2, and pn3)
   - open the first two files (use openfile_open) for reading
   - open the third file (use openfile_open) for writing
   - return if any file is not open'ed correctly
   - place them into curproc's file table (use filetable_place)
   - refer to sys_read() for reading the first two files
   - refer to sys_write() for writing the third file
   - refer to sys_close() to complete the use of three files
   - set the return value correctly for successful completion
*/
int
sys_meld(const_userptr_t pn1, const_userptr_t pn2, const_userptr_t pn3, int *retval)
{
	int errno = 0;
	struct openfile *ofile1, *ofile2, *ofile3;
	char *file1, *file2, *file3;

	ofile1 = (struct openfile *)kmalloc(sizeof(struct openfile));
	ofile2 = (struct openfile *)kmalloc(sizeof(struct openfile));
	ofile3 = (struct openfile *)kmalloc(sizeof(struct openfile));
	
	kprintf("SYS_meld begins...");
	//make sure both pn1 and pn2 exist
	if(pn1 == NULL || pn2 == NULL)
	{
		*retval = -1;
		return EINVAL; //invalid argument
	}

	file1 = (char *) kmalloc(sizeof(char) * PATH_MAX);
	file2 = (char *) kmalloc(sizeof(char) * PATH_MAX);
	
	//flags cause it to create either file if they don't exist
	//copying in pathnames and opening files
	errno = copyinstr(pn1, file1, strlen(((char *)pn1)) + 1, NULL);
	if(errno)
	{
		*retval = -1;
		return errno;
	}

	errno = openfile_open(file1, (O_RDONLY), 055, &ofile1);
	if(errno)
	{
		*retval = -1;
		return errno;
	}

	//open file2
	errno = copyinstr(pn2, file2, strlen(((char *)pn2)) + 1, NULL);
	if(errno)
	{
		*retval = -1;
		return errno;
	}

	errno = openfile_open(file2, (O_RDONLY), 055, &ofile2);
	if(errno)
	{
		*retval = -1;
		return errno;
	}

	//open 3rd file for writing
	file3 = (char *) kmalloc(sizeof(char) * PATH_MAX);

	errno = copyinstr(pn3, file3, strlen(((char *)pn3)) + 1, NULL);
	if(errno)
	{
		
		*retval = -1;
		return errno;
	}
	
	errno = openfile_open(file3,  O_APPEND | O_RDWR | O_EXCL | O_CREAT, 666, &ofile3);
	if(errno)
	{
		*retval = -1;
		return errno;
	}
	
	//increase references
	openfile_incref(ofile1);
	openfile_incref(ofile2);
	openfile_incref(ofile3);
	
	//use filetable_place on all 3 files to get the file descriptors
	int fd1, fd2, fd3;

	errno = filetable_place(curproc->p_filetable, ofile1, &fd1);
	if(errno)
	{
		*retval = -1;
		return errno;
	}

	errno = filetable_place(curproc->p_filetable, ofile2, &fd2);
	if(errno)
	{
		*retval = -1;
		return errno;
	}

	errno = filetable_place(curproc->p_filetable, ofile3, &fd3);
	if(errno)
	{
		*retval = -1;
		return errno;
	}

	kprintf("All files opened and placed successfully...\n");
	//actually begin reading files 	
	int done = 1;
	int write1size = -1, write2size = -1, write3size;

	kprintf("All files ready to meld!\n");

	while(done)
	{
		if(write1size == 0 && write2size == 0)
                {
                        done = 0;
                        break;
                }

		kprintf("loop boi\n");
		char readbuf[4];
		//read 4 bytes from both files
		errno = sys_read(fd1, (userptr_t)readbuf, 4, &write1size);
		if(errno)
		{
			*retval = -1;
			return errno;
		} 

		kprintf("readbuf was: %s\nwrite1size was %d\n", readbuf, write1size);
		
		 //write to merge file
		if(write1size)
		{
                	errno = sys_write(fd3, (userptr_t)readbuf, 4, &write3size);
                	if(errno)
                	{
                        	*retval = -1;
                        	return errno;
                	}
		}

		errno = sys_read(fd2, (userptr_t)readbuf, 4, &write2size);
		if(errno)
		{
			*retval = -1;
			return errno;
		}

		 kprintf("readbuf was: %s\n", readbuf);	
		//write to merge file
		if(write2size)
		{
			errno = sys_write(fd3, (userptr_t)readbuf, 4, &write3size);
			if(errno)
			{
				*retval = -1;
				return errno;
			}
		}
	}
	//decref
	
	sys_close(fd1, errno);
	sys_close(fd2, errno);
	sys_close(fd3, errno);

	kprintf("SYS_meld finished...\n");	
	return 0;
}
