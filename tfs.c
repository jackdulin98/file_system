/*
 *  Copyright (C) 2019 CS416 Spring 2019
 *	
 *	Tiny File System
 *
 *	File:	tfs.c
 *  Author: Yujie REN
 *	Date:	April 2019
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "tfs.h"

char diskfile_path[PATH_MAX];		// the main() function sets this for us

// Declare your in-memory data structures here
// These will be the buffers you need to read into and write from.
// Also, have to check for the magic number in the disk file. (Logic was split into two parts, from today's lecture.)

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	bitmap_t inode_bitmap = (bitmap_t)malloc(MAX_INUM / 8);			// could this become an in-memory DS?
	bio_read(1, inode_bitmap);

	// Step 2: Traverse inode bitmap to find an available slot
	int count = 1;								// starts at 1, inode #0 is reserved for empty blocks
	for(count = 1; count < MAX_INUM; count++){
		if(get_bitmap(inode_bitmap, count) == 0){
			// Step 3: Update inode bitmap and write to disk
			set_bitmap(inode_bitmap, count);
			bio_write(count, inode_bitmap);
			free(inode_bitmap);
			return 0;						// QUESTION: do you return the number of the inode instead?
		}
	}

	free(inode_bitmap);
	return -1;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	// Step 1: Read data block bitmap from disk
	// idea: free the memory of the buffer once you no longer need it
	bitmap_t data_bitmap = (bitmap_t)malloc(MAX_DNUM / 8);
	bio_read(2, data_bitmap);

	// Step 2: Traverse data block bitmap to find an available slot
	int count = 0;
	for(count = 0; count < MAX_DNUM; count++){
		if(get_bitmap(data_bitmap, count) == 0){
			// Step 3: Update data block bitmap and write to disk
			set_bitmap(data_bitmap, count);
			bio_write(count, data_bitmap);
			free(data_bitmap);			// have to free() in both places
			return 0;				// QUESTION: do you return the number of the block number instead?
		}
	}

	// If you haven't found any available blocks, return -1
	free(data_bitmap);
	return -1;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

  	// Step 1: Get the inode's on-disk block number
	uint16_t block_num = (ino / 16) + 3;

  	// Step 2: Get offset of the inode in the inode on-disk block
	uint16_t block_offset = ino % 16;

  	// Step 3: Read the block from disk and then copy into inode structure
	void* buffer = malloc(sizeof(BLOCK_SIZE));
	bio_read(block_num, buffer);
	memcpy(inode, buffer + block_offset * sizeof(struct inode), sizeof(struct inode));	// this pointer arithmetic should be right
	free(buffer);		// once you copied it into the inode, this should be able to be freed

	return 0;
}

int writei(uint16_t ino, struct inode *inode) {

	// Step 1: Get the block number where this inode resides on disk
	uint16_t block_num = (ino / 16) + 3;

	// Step 2: Get the offset in the block where this inode resides on disk
	uint16_t block_offset = ino % 16;

	// Step 3: Write inode to disk
	// don't you check to see if this is occupied first?
	void* buffer = malloc(sizeof(BLOCK_SIZE));
	bio_read(block_num, buffer);								// this is what was in the inode table before
	memcpy(buffer + block_offset * sizeof(struct inode), inode, sizeof(struct inode));	// this pointer arithmetic should be right
	bio_write(block_num, buffer);								// FORGOT THIS STEP: write back into disk
	free(buffer);										// after you write into disk, THEN YOU CAN FREE! (?)

	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

  	// Step 1: Call readi() to get the inode using ino (inode number of current directory)

  	// Step 2: Get data block of current directory from inode

	// Step 3: Read directory's data block and check each directory entry.
	// If the name matches, then copy directory entry to dirent structure

	return 0;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode

	// Step 2: Check if fname (directory name) is already used in other entries

	// Step 3: Add directory entry in dir_inode's data block and write to disk (after it wasn't found)

	// Allocate a new data block for this directory if it does not exist

	// Update directory inode

	// Write directory entry

	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode

	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	// clarifying question: this doesn't take relative addresses, just absolute?
	// idea: use strtok() and a loop for this

	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	return 0;
}

/* 
 * Make file system
 */
int tfs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);

	// write superblock information
	struct superblock* first_block = (struct superblock*)malloc(sizeof(struct superblock));
	first_block->magic_num = MAGIC_NUM;
	first_block->max_inum = MAX_INUM;
	first_block->max_dnum = MAX_DNUM;
	first_block->i_bitmap_blk = 1;		// where the inode block bitmap is stored
	first_block->d_bitmap_blk = 2;		// where the data block bitmap is stored
	first_block->i_start_blk = 3;		// where the inode table is stored
	first_block->d_start_blk = 67;		// where the data blocks are stored

	bitmap_t inode_bitmap = NULL;
	bitmap_t datablock_bitmap = NULL;

	// Basically, this is assumed to always work.
	if(dev_open(diskfile_path) == 0){
		// put the superblock in the first block
		bio_write(0, first_block);
		free(first_block);					// we can free() once we write into the file
		// initialize inode bitmap
		inode_bitmap = (bitmap_t)malloc(MAX_INUM / 8);
		int count = 0;
		for(count = 0; count < MAX_INUM; count++){
			unset_bitmap(inode_bitmap, count);		// setting all entries in inode bitmap to zero
		}
		// initialize data block bitmap
		datablock_bitmap = (bitmap_t)malloc(MAX_DNUM / 8);
		count = 0;
		for(count = 0; count < MAX_DNUM; count++){
			unset_bitmap(datablock_bitmap, count);		// setting all entries in datablock bitmap to zero
		}
	}

	// update bitmap information for root directory (inode number 0 is allocated for deleted files)
	set_bitmap(inode_bitmap, 1);		// settled: root is inode number 1
	bio_write(1, inode_bitmap);
	free(inode_bitmap);			// we can free() once we write into the file
	set_bitmap(datablock_bitmap, 0);	// root is the very first data block - data block 0
	bio_write(2, datablock_bitmap);
	free(datablock_bitmap);			// we can free() once we write into the file

	// Initialize the first inode for the root directory.
	struct inode* first_inode = (struct inode*)malloc(sizeof(struct inode));
	first_inode->ino = 1;			// settled: this should be "1"
	first_inode->valid = 1;			// QUESTION: what does the valid attribute mean? (asked on Piazza)
	first_inode->size = BLOCK_SIZE;
	first_inode->type = 1; 			// assume "0" is regular file, "1" is directory (don't have to worry about symbolic links?)
	first_inode->link = 2;			// POINT: just to clarify, the link count is initialized to 2 in directories.
	first_inode->direct_ptr[0] = 67;	// insight on bus: just put in the block number

	// Fill in the rest of the empty direct pointers with -1.
	int cnt = 1;
	for(cnt = 1; cnt < 16; cnt++){
		first_inode->direct_ptr[cnt] = -1;
	}

	// QUESTION: what fields would we have to populate the stat struct with? do we fill those in here?
	// worry about this more for get_attr()

	bio_write(3, first_inode);
	free(first_inode);			// we can free() once we write the inode into the file

	void* dirent_buffer = malloc(BLOCK_SIZE);
	memset(dirent_buffer, 0, 16*sizeof(struct dirent));		// initialize the data block with NULL

	bio_write(67, dirent_buffer);		// placing the dirent struct into the first data block
	free(dirent_buffer);			// can free the (first) data block buffer, written into the file

	return 0;
}


/*
 * FUSE file operations
 */
static void *tfs_init(struct fuse_conn_info *conn) {
	// QUESTION: what is the conn argument used for, if at all?

	// Step 1a: If disk file is not found, call mkfs
	// Try to open up the disk file. If it can't open, dev_open() should return a -1 since dev_init() wasn't called.
	if(dev_open(diskfile_path) == -1){
		tfs_mkfs();
	}

	// Step 1b: If disk file is found, just initialize in-memory data structures (in our case, there is none)
  	// and read superblock from disk
	struct superblock* superblock_buffer = (struct superblock*)malloc(BLOCK_SIZE);
	bio_read(0, superblock_buffer);		// needed to read the magic number, and verify that it's correct
	if(superblock_buffer->magic_num != MAGIC_NUM){
		// QUESTION: Does reformatting mean calling tfs_mkfs()?
		tfs_mkfs();			// if the right value of the superblock is not found, reformat
	}

	free(superblock_buffer); 		// future refactoring: use this as an in-memory DS (possibly)
	return NULL;				// confirms: this returns nothing
}

static void tfs_destroy(void *userdata) {
	// QUESTION: what do we do with the userdata?

	// Step 1: De-allocate in-memory data structures
	// QUESTION: Do we even need to have in-memory data structures? Can we just skip this step if we perform the operations within a function scope?

	// Step 2: Close diskfile
	dev_close(diskfile_path);

}

static int tfs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path

	// Step 2: fill attribute of file into stbuf from inode
	// idea: the inode tells you whether it's a directory or not.

	// is it a directory or a file?
		stbuf->st_mode   = S_IFDIR | 0755;
		stbuf->st_nlink  = 2;
		time(&stbuf->st_mtime);

	return 0;
}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

    return 0;
}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: Read directory entries from its data blocks, and copy them to filler
	// Note: will have to use fuse_fill_dir_t filler, one of the fuse attributes.

	return 0;
}


static int tfs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	// this step might be unsuccessful

	// Step 5: Update inode for target directory

	// Step 6: Call writei() to write inode to disk

	return 0;
}

static int tfs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of target directory

	// Step 3: Clear data block bitmap of target directory

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

	return 0;
}

static int tfs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target file to parent directory

	// Step 5: Update inode for target file

	// Step 6: Call writei() to write inode to disk

	return 0;
}

static int tfs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

	return 0;
}

static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	return 0;
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk

	// Note: this function should return the amount of bytes you write to disk
	return size;
}

static int tfs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of target file

	// Step 3: Clear data block bitmap of target file

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

	return 0;
}

static int tfs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int tfs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations tfs_ope = {
	.init		= tfs_init,
	.destroy	= tfs_destroy,

	.getattr	= tfs_getattr,
	.readdir	= tfs_readdir,
	.opendir	= tfs_opendir,
	.releasedir	= tfs_releasedir,
	.mkdir		= tfs_mkdir,
	.rmdir		= tfs_rmdir,

	.create		= tfs_create,
	.open		= tfs_open,
	.read 		= tfs_read,
	.write		= tfs_write,
	.unlink		= tfs_unlink,

	.truncate   = tfs_truncate,
	.flush      = tfs_flush,
	.utimens    = tfs_utimens,
	.release	= tfs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &tfs_ope, NULL);

	return fuse_stat;
}

