#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "userdatamgmt_fs.h"
/*
	This makefs will write the following information onto the disk
	- BLOCK 0, superblock;
	- BLOCK 1, inode of the unique file (the inode for root is volatile);
	- BLOCK 2, ..., datablocks of the unique file
*/

char* testo[] = {
"Ei fu. Siccome immobile, Dato il mortal sospiro, Stette la spoglia immemore Orba di tanto spiro, Così percossa, attonita La terra al nunzio sta,",
"Muta pensando all’ultima Ora dell’uom fatale; Nè sa quando una simile Orma di piè mortale La sua cruenta polvere A calpestar verrà.", 
"Lui folgorante in solio Vide il mio genio e tacque; Quando, con vece assidua, Cadde, risorse e giacque, Di mille voci al sonito Mista la sua non ha:",
"Vergin di servo encomio E di codardo oltraggio, Sorge or commosso al subito Sparir di tanto raggio: E scioglie all’urna un cantico Che forse non morrà.",
"Dall’Alpi alle Piramidi, Dal Manzanarre al Reno, Di quel securo il fulmine Tenea dietro al baleno; Scoppiò da Scilla al Tanai, Dall’uno all’altro mar.",
"ciao sono lucia e sono una sirena", "può sembrare strano ma è una storia vera", "la leggenda su di noi è già la verità ...", 
"dragon ball gt, siamo tutti qui", "non c'è un drago più super di così", "dragon ball perchè, ogni sfera è ...", "l'energia che risplende in te!" };

int main(int argc, char *argv[])
{
	int fd, nbytes;
	ssize_t ret;
	struct userdatafs_sb_info sb;
	struct userdatafs_inode root_inode;
	struct userdatafs_inode file_inode;
	struct userdatafs_dir_record record;
	char *block_padding;
	unsigned int metadata = 1;

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
	{
		if (MD_SIZE + strlen(testo[i]) > BLK_SIZE)
		{
			printf("The block is too small");
			return -1;
		}

		ret = write(fd, &metadata, MD_SIZE);
		if (ret != MD_SIZE)
		{
			printf("Writing the metadata has failed.\n");
			close(fd);
			return -1;
		}

		nbytes = strlen(testo[i]);
		ret = write(fd, testo[i], nbytes);
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