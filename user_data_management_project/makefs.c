#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "userdatamgmt_fs.h"
#define NTESTI 20

char *testo[] = {
	"I have a dream  - Martin Luther King Jr\n",
	"Ask not what your country can do for you, ask what you can do for your country  - John F. Kennedy\n",
	"All men are created equal - Thomas Jefferson\n",
	"The only thing we have to fear is fear itself - Franklin D. Roosevelt\n",
	"Four score and seven years ago - Abraham Lincoln\n",
	"We shall fight on the beaches - Winston Churchill\n",
	"Injustice anywhere is a threat to justice everywhere - Martin Luther King Jr\n",
	"Veni, vidi, vici - Julius Caesar\n",
	"Give me liberty or give me death - Patrick Henry\n",
	"I came, I saw, I conquered - Julius Caesar\n",
	"Tear down this wall! - Ronald Reagan\n",
	"Et tu, Brute? - Julius Caesar\n",
	"It is a truth universally acknowledged, that a single man in possession of a good fortune, must be in want of a wife. - Jane Austen\n",
	"The fault, dear Brutus, is not in our stars, but in ourselves. - William Shakespeare\n",
	"To be or not to be, that is the question. - William Shakespeare\n",
	"Cogito, ergo sum - René Descartes\n",
	"Elementary, my dear Watson - Sherlock Holmes\n",
	"No man is an island - John Donne\n",
	"We hold these truths to be self-evident, that all men are created equal - Thomas Jefferson\n",
	"Yes we can - Barack Obama\n"};

int main(int argc, char *argv[])
{
	int fd, nbytes, i, nblocks;
	int to_read = 0;
	unsigned int invalid = INVALID;
	ssize_t ret;
	struct userdatafs_sb_info sb;
	struct userdatafs_inode root_inode;
	struct userdatafs_inode file_inode;
	struct userdatafs_dir_record record;
	char *block_padding;
	uint16_t metadata = 0x0;

	char *file_body = "Wathever content you would like.\n"; // this is the default content of the unique file

	if (argc != 3)
	{
		printf("Usage: makefs <device>\n");
		return -1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd == -1)
	{
		perror("Error opening the device");
		return -1;
	}

	// pack the superblock
	sb.version = 1; // file system version
	sb.magic = MAGIC;
	sb.block_size = BLK_SIZE;

	ret = write(fd, (char *)&sb, sizeof(sb));

	if (ret != BLK_SIZE)
	{
		printf("Bytes written [%d] are not equal to the default block size.\n", (int)ret);
		close(fd);
		return ret;
	}

	AUDIT printf("Super block written succesfully\n");

	nblocks = atoi(argv[2]);
	// write file inode
	file_inode.mode = S_IFREG;
	file_inode.inode_no = USERDATAFS_FILE_INODE_NUMBER;
	file_inode.file_size = nblocks * BLK_SIZE;
	printf("File size is %ld\n", file_inode.file_size);
	fflush(stdout);
	ret = write(fd, (char *)&file_inode, sizeof(file_inode));

	if (ret != sizeof(root_inode))
	{
		printf("The file inode was not written properly.\n");
		close(fd);
		return -1;
	}

	AUDIT printf("File inode written succesfully.\n");

	// padding for block 1
	nbytes = BLK_SIZE - sizeof(file_inode);
	block_padding = malloc(nbytes);

	ret = write(fd, block_padding, nbytes);

	if (ret != nbytes)
	{
		printf("The padding bytes are not written properly. Retry your mkfs\n");
		close(fd);
		return -1;
	}
	AUDIT printf("Padding in the inode block written sucessfully.\n");

	// write file datablocks
	for (i = 1; i <= nblocks; i++)
	{

		metadata = 0;
		if (MD_SIZE > BLK_SIZE)
		{
			printf("The block is too small");
			return -1;
		}

#ifdef ALL_VALID
#elif ALL_INVALID
#else
		if (i - 1 < NTESTI)
		{
#endif

#ifdef ALL_INVALID
#else
		to_read = (i - 1) % NTESTI;
		if (MD_SIZE + strlen(testo[to_read]) > BLK_SIZE)
		{
			printf("The block is too small");
			return -1;
		}

		nbytes = strlen(testo[to_read]);
		ret = write(fd, testo[to_read], nbytes);
		if (ret != nbytes)
		{
			printf("Writing file datablock has failed.\n");
			close(fd);
			return -1;
		}
		AUDIT printf("File block at %d written succesfully.\n", get_offset(to_read));

		nbytes = SIZE - nbytes;
		block_padding = malloc(nbytes);
		ret = write(fd, block_padding, nbytes);

		if (ret != nbytes)
		{
			printf("The padding bytes are not written properly. Retry your mkfs\n");
			close(fd);
			return -1;
		}
		AUDIT printf("Padding in the file block with offset %d block written sucessfully.\n", get_offset(to_read));

		metadata = set_length(metadata, strlen(testo[to_read]));
		metadata = set_valid(metadata);

		ret = write(fd, &metadata, MD_SIZE - POS_SIZE);
		AUDIT printf("Metadata: %x\n", metadata);
		if (ret != MD_SIZE - POS_SIZE)
		{
			printf("Writing the metadata has failed.\n");
			close(fd);
			return -1;
		}

		ret = write(fd, &i, POS_SIZE);
		AUDIT printf("Message is at position:  %d\n", i);

		if (ret != POS_SIZE)
		{
			printf("Writing the position has failed.\n");
			close(fd);
			return -1;
		}
#endif

#ifdef ALL_VALID
#elif ALL_INVALID
#else
		}
#endif

#ifdef ALL_VALID
#elif ALL_INVALID
#else
		else
		{
#endif

#ifndef ALL_VALID
		nbytes = SIZE;
		block_padding = malloc(nbytes);
		ret = write(fd, block_padding, nbytes);

		if (ret != nbytes)
		{
			printf("The padding bytes are not written properly. Retry your mkfs\n");
			close(fd);
			return -1;
		}
		AUDIT printf("Padding in the file block with offset %d block written sucessfully.\n", get_offset(i - 1));

		metadata = set_invalid(metadata);

		ret = write(fd, &metadata, MD_SIZE - POS_SIZE);
		AUDIT printf("Metadata: %x\n", metadata);
		if (ret != MD_SIZE - POS_SIZE)
		{
			printf("Writing the metadata has failed.\n");
			close(fd);
			return -1;
		}

		ret = write(fd, &invalid, POS_SIZE);
		AUDIT printf("Message is at position:  %d\n", invalid);
		if (ret != POS_SIZE)
		{
			printf("Writing the metadata has failed.\n");
			close(fd);
			return -1;
		}

		AUDIT printf("File block at %d written succesfully.\n", get_offset(i - 1));
#endif

#ifdef ALL_VALID
#elif ALL_INVALID
#else
		}
#endif

		free(block_padding);
	}
	close(fd);
	return 0;
}
