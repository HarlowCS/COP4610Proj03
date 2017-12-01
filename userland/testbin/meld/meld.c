#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
int
main(int argc, char *argv[])
{
	static char writebuf1[16] = "AAAABBBBCCCCDDDD";
	static char writebuf2[16] = "eeeeffffgggghhhh";
	static char readbuf[17];

	const char *file1, *file2, *file3;
	int fd1, fd2, fd3, errno;

	(void)argc;
	(void)argv;

	printf("\nBeginning meld test...\n");
	//name, open, and write to our files
	file1 = "meldtest1";
	file2 = "meldtest2";
	file3 = "meldtest3";
	
	fd1 = open(file1, (O_WRONLY | O_CREAT | O_TRUNC), 0664);
	if(fd1 < 0)
	{
		err(1, "%s: open for write", file1);
	}	

	errno = write(fd1, writebuf1, 16);
	if(errno < 0)
	{
		err(1, "%s: write", file1);
	}

	//close file1
	errno = close(fd1);
	if(errno < 0)
	{
		err(1, "%s: close", file1);
	}

	//begin file2
	fd2 = open(file2, (O_WRONLY | O_CREAT | O_TRUNC), 0664);
	if(fd2 < 0)
	{
		err(1, "%s: open for write", file2);
	}

	errno = write(fd2, writebuf2,16);
	if(errno < 0)
	{
		err(1, "%s: write", file2);
	}

	errno = close(fd2);
	if(errno < 0)
	{
		err(1, "%s: close", file2);
	}

	/*fd1 = open(file1, O_RDONLY, 0444);
	read(fd1, readbuf, 16);
	readbuf[16] = '\0';
	printf("readbuf was: %s", readbuf);*/
	printf("Beginning actual melding process...\n");

	//begin meld
	errno = meld(file1, file2, file3);
	
	//read from file3 and prove it is melded
	fd3 = open(file3, (O_RDONLY), 0444);
	if(fd3 < 0)
	{
		 err(1, "%s: open for write", file3);
	}

	errno = read(fd3, readbuf, 16);
	errno = close(fd3);
	readbuf[16] = '\0';

	printf("Meld finished, first 16 bytes of melded file:\n%s\n", readbuf);	
	return 0;
}
