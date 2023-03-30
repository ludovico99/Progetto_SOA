#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "userdatamgmt_fs.h"

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
	int fd, nbytes;
	ssize_t ret;
	int index;
	struct userdatafs_sb_info sb;
	struct userdatafs_inode root_inode;
	struct userdatafs_inode file_inode;
	struct userdatafs_dir_record record;
	char *block_padding;
	uint16_t metadata = 0x0;

	char *file_body = "Wathever content you would like.\n"; // this is the default content of the unique file

	if (argc != 2)
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

	printf("Super block written succesfully\n");

	// write file inode
	file_inode.mode = S_IFREG;
	file_inode.inode_no = USERDATAFS_FILE_INODE_NUMBER;
	file_inode.file_size = NBLOCKS * BLK_SIZE;
	printf("File size is %ld\n", file_inode.file_size);
	fflush(stdout);
	ret = write(fd, (char *)&file_inode, sizeof(file_inode));

	if (ret != sizeof(root_inode))
	{
		printf("The file inode was not written properly.\n");
		close(fd);
		return -1;
	}

	printf("File inode written succesfully.\n");

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
	printf("Padding in the inode block written sucessfully.\n");

	// write file datablocks
	for (int i = 0; i < NBLOCKS; i++)
	{	index = i % 20; //20 is the total number of texts hardcoded
		if (MD_SIZE + strlen(testo[index]) > BLK_SIZE)
		{
			printf("The block is too small");
			return -1;
		}

		metadata = set_valid(metadata);
		metadata = set_not_free(metadata);
		metadata = set_length(metadata, strlen(testo[index]));
		
		ret = write(fd, &metadata, MD_SIZE);
		printf("Metadata: %x\n", metadata);
		if (ret != MD_SIZE)
		{
			printf("Writing the metadata has failed.\n");
			close(fd);
			return -1;
		}

		nbytes = strlen(testo[index]);
		ret = write(fd, testo[index], nbytes);
		if (ret != nbytes)
		{
			printf("Writing file datablock has failed.\n");
			close(fd);
			return -1;
		}
		printf("File block at %d written succesfully.\n", get_offset(i));

		nbytes = BLK_SIZE - nbytes - MD_SIZE;
		block_padding = malloc(nbytes);
		ret = write(fd, block_padding, nbytes);

		if (ret != nbytes)
		{
			printf("The padding bytes are not written properly. Retry your mkfs\n");
			close(fd);
			return -1;
		}
		printf("Padding in the file block with offset %d block written sucessfully.\n", get_offset(i));
	}

	close(fd);

	return 0;
}