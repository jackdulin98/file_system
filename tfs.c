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
	bitmap_t inode_bitmap = (bitmap_t)malloc(BLOCK_SIZE);			// allocate a block, even though you don't need a block
	bio_read(1, inode_bitmap);

	// Step 2: Traverse inode bitmap to find an available slot
	int count = 0;								// inode starts at "0" now, not "1" because of the valid attribute
	for(count = 0; count < MAX_INUM; count++){
		if(get_bitmap(inode_bitmap, count) == 0){
			// Step 3: Update inode bitmap and write to disk
			set_bitmap(inode_bitmap, count);
			bio_write(1, inode_bitmap);				// used to be bio_write(count, inode_bitmap);
			free(inode_bitmap);
			return count;						// return the inode number
		}
	}

	// this means we couldn't find a free spot for an inode
	free(inode_bitmap);
	return -1;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	// Step 1: Read data block bitmap from disk
	// idea: free the memory of the buffer once you no longer need it
	bitmap_t data_bitmap = (bitmap_t)malloc(BLOCK_SIZE);			// allocate a block, even though you don't need a block
	bio_read(2, data_bitmap);

	// Step 2: Traverse data block bitmap to find an available slot
	int count = 0;
	for(count = 0; count < MAX_DNUM; count++){
		if(get_bitmap(data_bitmap, count) == 0){
			// Step 3: Update data block bitmap and write to disk
			set_bitmap(data_bitmap, count);
			bio_write(2, data_bitmap);		// used to be bio_write(count, data_bitmap);
			free(data_bitmap);			// have to free() in both places
			return count;				// return the data block number
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
	uint16_t block_num = (ino / 16) + 3;		// starting spot for the inodes is block #3

  	// Step 2: Get offset of the inode in the inode on-disk block
	uint16_t block_offset = ino % 16;

  	// Step 3: Read the block from disk and then copy into inode structure
	void* buffer = malloc(BLOCK_SIZE);		// BUG SPOT #1 (make into an array of struct pointers?)
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
	// Don't you check to see if this is occupied first? ANSWER: I think that's done before ever doing the writei() operation.
	void* buffer = malloc(BLOCK_SIZE);		// BUG SPOT #2 (make into an array of struct pointers?)
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
	// STATUS: I think this one is done, but double check anyway.

  	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	struct inode* inode_buffer = (struct inode*)malloc(sizeof(struct inode));
	readi(ino, inode_buffer);		// read the inode disk block

  	// Step 2: Get data block of current directory from inode
	// In essence, go through all of the sixteen possible data blocks in the file. If you see a -1, that block is empty and you should stop.
	int data_block = 0;
	for(data_block = 0; data_block < 16; data_block++){
		int curr_addr = inode_buffer->direct_ptr[data_block];
		if(curr_addr == -1){
			free(inode_buffer);	// forgot to free this at first
			return -1;		// couldn't find the entry you wanted to look for
		}
		struct dirent** block_buffer = (struct dirent**)malloc(BLOCK_SIZE);
		bio_read(curr_addr, (void*)block_buffer);		// was in the first line of the for() loop before
		int dirent_no = 0;
		for(dirent_no = 0; dirent_no < 16; dirent_no++){
			struct dirent* curr_file = block_buffer[dirent_no];
			if(curr_file == NULL){
				free(inode_buffer);
				free(block_buffer);
				return -1;	// no more files can be after a NULL block
			}

			if(strcmp(curr_file->name, fname) == 0 && strlen(curr_file->name) == name_len && curr_file->valid == 1){
				memcpy(dirent, curr_file, sizeof(struct dirent));		// can't have NULL here, seg fault (this is where the actual writing takes place)
				free(inode_buffer);
				free(block_buffer);
				return 0;	// this was a successful return
			}
		}
		free(block_buffer);		// prevent memory leaks
	}

	// if you got here, this means all of the data blocks were full
	free(inode_buffer);
	return -1;				// unsuccessful return

}

// If you have time, deal with the special cases: namely, the . and .. directories in each directory that isn't the root.
// f_ino is the avaiable inode, for the child (I believe).
int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// If you find an invalid dirent struct or a NULL spot, place a dirent entry into the file system.

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	// Step 2: Check if fname (directory name) is already used in other entries
	int data_block = 0;
	for(data_block = 0; data_block < 16; data_block++){
		int curr_addr = dir_inode.direct_ptr[data_block];
		if(curr_addr == -1){
			// You need a new data block for the new directory. Try to get one.
			int get_new_block = get_avail_blkno();
			if(get_new_block == -1){
				return -1;					// couldn't allocate a new block to support another data block
			}

			// If you're able to find a new block, alter the inode that you passed in as the first argument. The size of the directory will be changing for the parent.
			int data_blk_num = get_new_block + 67;	 		// first data block is 67, so add 67 to the relative number (this is the absolute number)
			dir_inode.direct_ptr[data_block] = data_blk_num;	// assign a new data block into the data block array
			dir_inode.size += BLOCK_SIZE;				// added another block to the directory associated with the inode
			dir_inode.link++;					// for every new subdirectory added to a directory, the link count goes up by one
			(dir_inode.vstat).st_size += BLOCK_SIZE; 		// added another block to the directory associated with the inode
			(dir_inode.vstat).st_blocks++;				// another block has been added, increment the block count by one

			// Allocate a new buffer that will be placed into the new data block.
			struct dirent** block_buffer = (struct dirent**)malloc(BLOCK_SIZE);
			int count = 0;
			for(count = 0; count < 16; count++){
				block_buffer[count] = NULL;
			}

			// Add a new entry into the buffer.
			struct dirent* new_entry = (struct dirent*)malloc(sizeof(struct dirent));
			new_entry->ino = f_ino;			// inode of the child
			new_entry->valid = 1;			// it's now a valid entry
			memset(new_entry->name, 0, sizeof(new_entry->name));
			strcat(new_entry->name, fname);		// child's name
			block_buffer[0] = new_entry;		// TODO: should you do a memcpy() here instead? (I don't think so, but keep this in mind)

			bio_write(data_blk_num, (void*)block_buffer);		// write the data block back into the file
			free(block_buffer);					// can now free, as it persists on the file
			// free(new_entry);					// can now free, as it persists on the file
			return 0;						// successful return, had to allocate a new data block
		}

		// You're working with a valid data block address.
		struct dirent** block_buffer = (struct dirent**)malloc(BLOCK_SIZE);
		bio_read(curr_addr, (void*)block_buffer);			// was in the first line of the for() loop previously
		int dirent_no = 0;
		for(dirent_no = 0; dirent_no < 16; dirent_no++){
			struct dirent* curr_file = block_buffer[dirent_no];
			// The case of finding a NULL spot in the array of structs.
			if(curr_file == NULL){
				// Add a new dirent structure into the NULL spot.
				dir_inode.link++;				// adding a new subdirectory increases the link count by one
				struct dirent* new_entry = (struct dirent*)malloc(sizeof(struct dirent));
				new_entry->ino = f_ino;
				new_entry->valid = 1;
				memset(new_entry->name, 0, sizeof(new_entry->name));
				strcat(new_entry->name, fname);
				block_buffer[dirent_no] = new_entry;

				bio_write(curr_addr, (void*)block_buffer);	// now, the modified block buffer
				free(block_buffer);				// can now free, as it persists on the file
				// free(new_entry);				// can now free, as it persists on the file
				return 0;					// successful return, wrote on a pre-existing data block
			}
			// The case of the file being recently deleted and then invalidated.
			if(curr_file->valid == 0){				// doing the above step in that order prevents a seg fault
				struct dirent* new_entry = (struct dirent*)malloc(sizeof(struct dirent));
				new_entry->ino = f_ino;
				new_entry->valid = 1;
				memset(new_entry->name, 0, sizeof(new_entry->name));
				strcat(new_entry->name, fname);
				block_buffer[dirent_no] = new_entry;
				bio_write(curr_addr, (void*)block_buffer);
				free(block_buffer);
				// free(new_entry);
				return 0;		// successful return, wrote on a pre-existing data block
			}
			if(strcmp(curr_file->name, fname) == 0 && strlen(curr_file->name) == name_len && curr_file->valid == 1){
				free(block_buffer);
				return -1;		// pre-existing entry with the same exact name, can't perform the operation
			}
		}
		free(block_buffer);			// prevent memory leaks
	}

	// NOTE: This step is performed in the above for loop.
	// Step 3: Add directory entry in dir_inode's data block and write to disk (after it wasn't found)
	// Look for the first dirent struct block/data block that is either NULL or invalid (signifying deleted entries).
	// Allocate a new data block for this directory if it does not exist (not necessary in some cases, multiple dirents in one data block)
	// Update directory inode - what about the last modified business? Is that what we would do?
	// TODO: Update the last modified time, but only if you have time.
	// Write directory entry - as in, you write it to disk

	// If you got here, this means there is no more space available.
	return -1;					// not enough space to put a new entry in, all spots are filled
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {
	// STATUS: I think this one is done too, but double check anyway.
	// I don't think you decrement the size in this method.

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	// Step 2: Check if fname exist
	// Step 3: If exist, then remove it from dir_inode's data block and write to disk (don't forget to write to disk!)
	int data_block = 0;
	for(data_block = 0; data_block < 16; data_block++){
		int curr_addr = dir_inode.direct_ptr[data_block];
		if(curr_addr == -1){
			return -1;						// you couldn't find the entry you want to remove
		}
		struct dirent** block_buffer = (struct dirent**)malloc(BLOCK_SIZE);
		bio_read(curr_addr, (void*)block_buffer);			// was previously in the first line of the for() loop
		int dirent_no = 0;
		for(dirent_no = 0; dirent_no < 16; dirent_no++){
			struct dirent* curr_file = block_buffer[dirent_no];	// pointing to the same area in memory
			if(curr_file == NULL){
				free(block_buffer);
				return -1;					// you couldn't find the entry you want to remove
			}
			if(strcmp(curr_file->name, fname) == 0 && strlen(curr_file->name) == name_len && curr_file->valid == 1){
				curr_file->valid = 0;				// invalidate the file
				bio_write(curr_addr, (void*)block_buffer);	// write the change back into the disk file
				free(block_buffer);				// free buffer after resilience has been achieved
				return 0;					// you successfully "deleted" the file
			}
		}
	}

	// All data blocks were full, and you couldn't find the file you wanted to delete.
	return -1;
}

/*
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	// Remember: This only takes an absolute path, as all FUSE operations do.

	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way
	char* str = (char*)malloc(252);
	memset(str, 0, 252); 		// zero memset doesn't cause a seg fault, also ensures no foreign characters
	strcat(str, path);
	char* token;
	uint16_t curr_ino_num = ino;

	token = strtok(str, "/");

	while(token != NULL){
		// here is the token you want to extract information from
		// have a dirent struct over here, also an extra variable to store the new inode number
		struct dirent* new_dirent = (struct dirent*)malloc(sizeof(struct dirent));
		// TODO: should this be zeroed out?
		int success = dir_find(curr_ino_num, token, strlen(token), new_dirent);
		if(success == -1){
			// couldn't find the desired entry, free everything then return -1
			free(str);
			free(new_dirent);
			return -1;
		}
		// what information do we want to get out of this?
		curr_ino_num = new_dirent->ino;
		token = strtok(NULL, "/");		// forgot this line
		//free(new_dirent);			// could this be a bit shaky? this is done to prevent memory leaks
	}

	// We forgot to populate inode struct, all of the cases.
	// From here, we can read the inode information into inode variable.
	memset(inode, 0, sizeof(struct inode));
	readi(curr_ino_num, inode);

	free(str); 	// prevent memory leaks
	return 0;	// you successfully found the path
}

/* 
 * Make file system
 */
int tfs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);

	// Fill in the superblock information.
	struct superblock* first_block = (struct superblock*)malloc(BLOCK_SIZE);		// allocate a disk block for the superblock
	first_block->magic_num = MAGIC_NUM;
	first_block->max_inum = MAX_INUM;
	first_block->max_dnum = MAX_DNUM;
	first_block->i_bitmap_blk = 1;		// where the inode block bitmap is stored
	first_block->d_bitmap_blk = 2;		// where the data block bitmap is stored
	first_block->i_start_blk = 3;		// where the inode table is stored
	first_block->d_start_blk = 67;		// where the data blocks are stored (first one is stored at block #67)

	bitmap_t inode_bitmap = NULL;
	bitmap_t datablock_bitmap = NULL;

	dev_open(diskfile_path);				// open up the disk file
	bio_write(0, first_block);				// put the superblock in the first block
	free(first_block);					// we can free the in-memory DS once it's been written to disk

	inode_bitmap = (bitmap_t)malloc(BLOCK_SIZE);		// initialize the inode bitmap, allocate a whole block
	int count = 0;
	for(count = 0; count < MAX_INUM; count++){
		unset_bitmap(inode_bitmap, count);		// set all entries in the inode bitmap to zero
	}

	datablock_bitmap = (bitmap_t)malloc(BLOCK_SIZE);	// initialize the data block bitmap, allocate a whole block
	count = 0;
	for(count = 0; count < MAX_DNUM; count++){
		unset_bitmap(datablock_bitmap, count);		// set all entries in the datablock bitmap to zero
	}

	// Update bitmap information for the root directory
	set_bitmap(inode_bitmap, 0);		// root is inode number 0
	bio_write(1, inode_bitmap);		// write the inode bitmap into block #1 of the disk
	free(inode_bitmap);			// we can free() once the file has been written into
	set_bitmap(datablock_bitmap, 0);	// root's first data block will be 0 (relative to block #67, first data block)
	bio_write(2, datablock_bitmap);		// write the data block bitmap into block #2 of the disk
	free(datablock_bitmap);			// we can free() once the file has been written into

	// Initialize the first inode for the root directory.
	struct inode* first_inode = (struct inode*)malloc(sizeof(struct inode));	// we can fit multiple inodes into one inode disk block
	first_inode->ino = 0;			// root takes the first inode (inode #0)
	first_inode->valid = 1;			// don't need to worry about special inode numbers, valid attribute takes care of deleted files
	first_inode->size = BLOCK_SIZE;		// at first, directories take up one block unless added to (like in dir_add)
	first_inode->type = 1; 			// assume "0" is regular file, "1" is directory
	first_inode->link = 2;			// the link count is initialized to 2 in directories, and add one for each new subdirectory you add
	first_inode->direct_ptr[0] = 67;	// direct pointers hold the block addresses

	// Fill in the rest of the empty direct pointers with -1, to signify that they are all empty.
	int cnt = 1;
	for(cnt = 1; cnt < 16; cnt++){
		first_inode->direct_ptr[cnt] = -1;
	}

	// Fill in the stat struct with the appropriate field values, as listed on the Piazza.
	(first_inode->vstat).st_ino = 0;			// root takes the first inode (inode #0)
	(first_inode->vstat).st_mode = S_IFDIR | 0755;		// for a directory, assume that the permissions are 0755
	(first_inode->vstat).st_size = BLOCK_SIZE;		// current size of the file, in bytes
	(first_inode->vstat).st_blksize = BLOCK_SIZE;		// block size of the file system
	(first_inode->vstat).st_blocks = 1;			// this tells us how many blocks the root currently takes up

	bio_write(3, first_inode);				// write the inode into the inode data block (no offset needed here)
	free(first_inode);					// we can free() once we write the inode into the file

	// Store 16 dirent structs in the first data block, initialize all of them to NULL.
	struct dirent** dirent_buffer = (struct dirent**)malloc(BLOCK_SIZE);
	int iterate = 0;
	for(iterate = 0; iterate < 16; iterate++){
		dirent_buffer[iterate] = NULL;
	}

	bio_write(67, dirent_buffer);		// place the empty dirent struct into the first data block
	free(dirent_buffer);			// can free the data block buffer, as it was written into the file (persistence)

	return 0;
}


/*
 * FUSE file operations
 */
static void *tfs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs
	// Try to open up the disk file. If it can't open, dev_open() should return a -1 since this implies that dev_init() wasn't called.
	if(dev_open(diskfile_path) == -1){
		tfs_mkfs();
	}

	// Step 1b: If disk file is found, just initialize in-memory data structures (in our case, there is none)
  	// and read superblock from disk
	struct superblock* superblock_buffer = (struct superblock*)malloc(BLOCK_SIZE);
	bio_read(0, superblock_buffer);		// this disk block is needed to read the magic number and verify that it's correct
	if(superblock_buffer->magic_num != MAGIC_NUM){
		tfs_mkfs();			// if the right value of the superblock is not found, reformat
	}

	free(superblock_buffer); 		// free() the superblock buffer once we're done using it
	return NULL;				// tfs_init() is supposed to return nothing
}

static void tfs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	// All buffers are allocated locally in our implementation, so we skip this step.

	// Step 2: Close diskfile
	dev_close(diskfile_path);

}

static int tfs_getattr(const char *path, struct stat *stbuf) {
	// Note: This function gets activated when you perform ls -l or stat on a given file/directory.
	// Plan of action: test this function first, on the root directory.

	// Step 1: call get_node_by_path() to get inode from path
	struct inode* inode_buffer = (struct inode*)malloc(sizeof(struct inode));
	int node_find = get_node_by_path(path, 0, inode_buffer);		// assume you always start from the root, as in recitation
	if(node_find == -1){
		free(inode_buffer);
		return -ENOENT;							// no such file or directory exists
	}

	// Step 2: fill attribute of file into stbuf from inode
	// Fill all of the relevant fields of the stat structure. (Some of these fields are redundant with the inode fields?)
	stbuf->st_ino = inode_buffer->ino;
	stbuf->st_mode = (inode_buffer->vstat).st_mode;
	stbuf->st_size = inode_buffer->size;
	stbuf->st_blksize = (inode_buffer->vstat).st_blksize;
	stbuf->st_blocks = (inode_buffer->vstat).st_blocks;

	// I think this part works, as tested by a print statement with the "/" directory.
	free(inode_buffer);
	return 0;
}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode* ino_buf = (struct inode*)malloc(sizeof(struct inode));
	int grab_node = get_node_by_path(path, 0, ino_buf);

	// Step 2: If not find, return -1
	if(grab_node == -1){
		free(ino_buf);
		return -1;
	}

	// This means you were able to find the directory successfully.
	return 0;
}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode* inode_buffer = (struct inode*)malloc(sizeof(struct inode));
	int get_node = get_node_by_path(path, 0, inode_buffer);
	if(get_node == -1){
		free(inode_buffer);
		return -1;			// this function only fails if there is no such directory (can return -ENOENT)
	}

	// Step 2: Read directory entries from its data blocks, and copy them to filler
	// Go through all of the possible data blocks until you find something null. Don't copy it over if the valid attribute it zero.
	// Note: will have to use fuse_fill_dir_t filler, one of the fuse attributes. How exactly do we use this? (online documentation)
	int data_block = 0;
	for(data_block = 0; data_block < 16; data_block++){
		int curr_addr = inode_buffer->direct_ptr[data_block];
		if(curr_addr == -1){
			break;				// break out of the loop
		}
		struct dirent** block_buffer = (struct dirent**)malloc(BLOCK_SIZE);
		bio_read(curr_addr, (void*)block_buffer);
		int dirent_no = 0;
		for(dirent_no = 0; dirent_no < 16; dirent_no++){
			struct dirent* curr_file = block_buffer[dirent_no];
			if(curr_file == NULL){
				break;			// you will end up in the 
			}
			// Only read valid file directory entries.
			if(curr_file->valid == 1){
				// Fill the buffer with all of the information.
				filler(buffer, curr_file->name, NULL, 0);
			}
		}
		free(block_buffer);			// prevent memory leaks; don't do it in the for loop (don't want to double free)
	}

	free(inode_buffer);				// wait until the end to free it
	return 0;
}


static int tfs_mkdir(const char *path, mode_t mode) {
	// We know that path only takes in absolute directories.
	// We don't make the . and .. directories, we're supposed to already have that handled. User doesn't do that manually. (Only do this if time permits.)
	// Also, handle the cases where the parent and/or child are blank. (I think that's already handled, as seen from last night.)

	// Edge case: if the path name is too long, throw an error. (Maybe do this for all operations.)
	if(strlen(path) > 252){
		return -1;					// path name was too long, will ensure you won't get a long name for other directory operations
	}

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name.
	char* str = (char*)malloc(252);				// 256 is the maximum length for a directory
	char* parent_name = (char*)malloc(252);			// parent directory
	char* child_name = (char*)malloc(252);			// child directory

	// Zero out these buffers to ensure that there are no garbage characters.
	memset(str, 0, 252);
	memset(parent_name, 0, 252);
	memset(child_name, 0, 252);

	strcat(str, path);					// full directory name

	// Split the full directory into parent and child parts.
	int split_point = strlen(path);
	while(str[split_point] != '/'){
		split_point--;
	}

	memcpy(parent_name, str, split_point);
	memcpy(child_name, str + (split_point + 1), strlen(str) - (split_point + 1));

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode* new_ino = (struct inode*)malloc(sizeof(struct inode));

	// Second parameter is 0 because the root inode number is 0 and you always assume that path passed in is absolute.
	int get_path = get_node_by_path(parent_name, 0, new_ino);
	if(get_path == -1){
		// Free the in-memory data structures.
		free(str);
		free(parent_name);
		free(child_name);
		free(new_ino);
		return -1; 				// this means the path couldn't be found
	}

	// Step 3: Call get_avail_ino() to get an available inode number (for the child piece)
	int available_inode_num = get_avail_ino();	// bitmap is set internally in this function call
	if(available_inode_num == -1){
		// Free the in-memory data structures.
		free(str);
		free(parent_name);
		free(child_name);
		free(new_ino);
		return -1;				// means you have no more inodes available
	}

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	// This step might fail because there is already a directory that matches the one you're trying to put in.
	int add_dir = dir_add(*new_ino, available_inode_num, child_name, strlen(child_name));
	// TODO: data block bitmap was modified in call to dir_add(), remember the . and .. cases (in most cases, it doesn't need to be modified)
	if(add_dir == -1){
		// Free the in-memory data structures.
		free(str);
		free(parent_name);
		free(child_name);
		free(new_ino);
		return -1;				// means adding this directory would create a duplicate, can't do that
	}

	// Step 5: Update inode for target directory
	// Get an available block number for the first data block of the directory as well.
	struct inode* child_inode = (struct inode*)malloc(sizeof(struct inode));
	child_inode->ino = available_inode_num;
	child_inode->valid = 1;
	child_inode->size = BLOCK_SIZE;
	child_inode->type = 1;				// for the type attribute, "1" signifies a directory
	child_inode->link = 2;				// TODO: each directory starts out with 2 links
	int get_block = get_avail_blkno();		// bitmap is set internally in this function call
	if(get_block == -1){
		free(child_inode);
		return -1; 				// couldn't find an available block number for the child directory, operation failed
	}
	child_inode->direct_ptr[0] = get_block + 67;
	int iter = 1;
	for(iter = 1; iter < 16; iter++){
		child_inode->direct_ptr[iter] = -1;
	}
	(child_inode->vstat).st_ino = available_inode_num;
	(child_inode->vstat).st_mode = mode | S_IFDIR;		// type specification bits may not be set, according to FUSE documentation
	(child_inode->vstat).st_size = BLOCK_SIZE;
	(child_inode->vstat).st_blksize = BLOCK_SIZE;
	(child_inode->vstat).st_blocks = 1;

	// Step 6: Call writei() to write inode to disk
	writei(available_inode_num, child_inode);
	// TODO: can free the child_inode after writing to disk (persistence)
	free(child_inode);

	printf("inode number written to = %d\n", available_inode_num);

	return 0;
}

static int tfs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	char* str = (char*)malloc(252);
	char* parent_name = (char*)malloc(252);
	char* child_name = (char*)malloc(252);

	// Zero out the buffers, as was done in tfs_mkdir().
	memset(str, 0, 252);
	memset(parent_name, 0, 252);
	memset(child_name, 0, 252);

	strcat(str, path);

	// Split the directory into its parent and child counterparts, as done in tfs_mkdir().
	int split_point = strlen(path);
	while(str[split_point] != '/'){
		split_point--;
	}

	memcpy(parent_name, str, split_point);
	memcpy(child_name, str + (split_point + 1), strlen(str) - (split_point + 1));

	// Step 2: Call get_node_by_path() to get inode of target directory
	struct inode* new_ino = (struct inode*)malloc(sizeof(struct inode));

	// You want to put the full path into this one, or it won't be able to traverse properly.
	int get_target = get_node_by_path(str, 0, new_ino);
	if(get_target == -1){
		// Free everything, return an error.
		free(str);
		free(parent_name);
		free(child_name);
		free(new_ino);
		return -1;
	}

	// Step 3: Clear data block bitmap of target directory
	// Read the data block with the data block bitmap, block #2.
	// Remember, you might have to loop through up to 16 blocks. (This is a simpler loop.)
	bitmap_t data_block_buffer = (bitmap_t)malloc(BLOCK_SIZE);		// this has to be written into disk
	bio_read(2, data_block_buffer);
	int data_block_num = 0;
	for(data_block_num = 0; data_block_num < 16; data_block_num++){
		int actual_db = new_ino->direct_ptr[data_block_num];		// data block number in disk
		if(actual_db == -1){
			break;
		}
		// I don't think you need to set the direct pointer blocks to -1 (but leave a note here)
		unset_bitmap(data_block_buffer, actual_db);
	}
	bio_write(2, data_block_buffer);
	free(data_block_buffer);

	// Step 4: Clear inode bitmap and its data block (s)
	// There are 16 data blocks for each inode, clear all of them.
	bitmap_t inode_bit_buffer = (bitmap_t)malloc(BLOCK_SIZE);
	bio_read(1, inode_bit_buffer);
	unset_bitmap(inode_bit_buffer, new_ino->ino);
	int iterate = 0;
	for(iterate = 0; iterate < 16; iterate++){
		new_ino->direct_ptr[iterate] = -1;
	}
	bio_write(1, inode_bit_buffer);
	free(inode_bit_buffer);

	// Step 5: Call get_node_by_path() to get inode of parent directory
	struct inode* parent_inode = (struct inode*)malloc(sizeof(struct inode));
	get_node_by_path(parent_name, 0, parent_inode);
	// No errors should happen here, so no error checking will be done. It was implicitly done in the check above.

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
	int can_remove = dir_remove(*parent_inode, child_name, strlen(child_name));
	if(can_remove == -1){
		free(parent_inode);
		return -1;			// weren't able to remove it from the parent directory
	}

	return 0;
}

static int tfs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int tfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Edge case: make sure the path name isn't too long.
	if(strlen(path) > 252){
		return -1;			// the pathname is too long, return an error
	}

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char* str = (char*)malloc(252);
	char* parent_name = (char*)malloc(252);
	char* child_name = (char*)malloc(252);

	memset(str, 0, 252);
	memset(parent_name, 0, 252);
	memset(child_name, 0, 252);

	strcat(str, path);

	int split_point = strlen(path);
	while(str[split_point] != '/'){
		split_point--;
	}

	memcpy(parent_name, str, split_point);
	memcpy(child_name, str + (split_point + 1), strlen(str) - (split_point + 1));

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode* new_ino = (struct inode*)malloc(sizeof(struct inode));

	int get_path = get_node_by_path(parent_name, 0, new_ino);
	if(get_path == -1){
		// Free the in-memory data structures.
		free(str);
		free(parent_name);
		free(child_name);
		free(new_ino);
		return -1;			// the path name couldn't be found
	}

	// Step 3: Call get_avail_ino() to get an available inode number (for the child piece)
	int available_ino_num = get_avail_ino();
	if(available_ino_num == -1){
		// Free the in-memory data structures.
		free(str);
		free(parent_name);
		free(child_name);
		free(new_ino);
		return -1;			// means you have no more inodes available
	}

	// Step 4: Call dir_add() to add directory entry of target file to parent directory
	int add_dir = dir_add(*new_ino, available_ino_num, child_name, strlen(child_name));
	if(add_dir == -1){
		// Free the in-memory data structures.
		free(str);
		free(parent_name);
		free(child_name);
		free(new_ino);
		return -1;			// means adding this directory would create a duplicate
	}

	// Step 5: Update inode for target file
	struct inode* child_inode = (struct inode*)malloc(sizeof(struct inode));
	child_inode->ino = available_ino_num;
	child_inode->valid = 1;
	child_inode->size = 0;			// at first, files have a size of zero
	child_inode->type = 0; 			// files are type "0" (include in the documentation)
	child_inode->link = 1;			// it will stay at 1, since we're not worrying about soft/hard links
	int get_block = get_avail_blkno();
	if(get_block == -1){
		free(child_inode);
		return -1;			// couldn't find an available block number
	}
	child_inode->direct_ptr[0] = get_block + 67;
	int iter = 1;
	for(iter = 1; iter < 16; iter++){
		child_inode->direct_ptr[iter] = -1;
	}
	(child_inode->vstat).st_ino = available_ino_num;
	(child_inode->vstat).st_mode = mode | S_IFREG;
	(child_inode->vstat).st_size = 0;			// initialize the size of files to 0
	(child_inode->vstat).st_blksize = BLOCK_SIZE;
	(child_inode->vstat).st_blocks = 1; 			// each file starts with only one block

	// Step 6: Call writei() to write inode to disk
	writei(available_ino_num, child_inode);
	free(child_inode);

	printf("inode number written to (file) = %d\n", available_ino_num);

	return 0;
}

static int tfs_open(const char *path, struct fuse_file_info *fi) {

	// Note: this follows the same process as tfs_opendir().

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode* ino_buf = (struct inode*)malloc(sizeof(struct inode));
	int grab_node = get_node_by_path(path, 0, ino_buf);

	// Step 2: If not find, return -1
	if(grab_node == -1){
		free(ino_buf);
		return -1;
	}

	free(ino_buf);
	return 0;
}

static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path
	// struct inode* inode_buffer = (struct inode*)malloc(sizeof(struct inode));
	// int grab_node = get_node_by_path(path, 0, inode_buffer);

	// Step 2: Based on size and offset, read its data blocks from disk
	// The offset and size will tell you which data blocks to read.

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	return 0;
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode* inode_buffer = (struct inode*)malloc(sizeof(struct inode));
	int grab_node = get_node_by_path(path, 0, inode_buffer);
	// If you can't find the file, you can't write to it.
	if(grab_node == -1){
		free(inode_buffer);
		return -1; 						// put in the right error code later
	}

	// Step 2: Based on size and offset, read its data blocks from disk
	// This will tell you which data blocks to read.
	int bytes_written = 0;						// keep a running tally of how much you wrote
	int data_block = offset / BLOCK_SIZE; 				// which block you want to start from
	int data_block_offset = offset % BLOCK_SIZE;			// offset within the first data block

	int end_point = offset + size;					// where you're projected to end in the file
	int blocks_added = 0;						// the number of blocks allocated, needed for struct vstat

	// If the file is larger than the 16 blocks, then return an error.
	if(end_point > BLOCK_SIZE * 16){
		return -EFBIG;						// file too big for file system
	}

	// If you got to this point in the code, then the write operation will be successful.

	// Step 3: Write the correct amount of data from offset to disk
	// The data block pointer will tell you which block to read from.
	void* first_buffer = malloc(BLOCK_SIZE);
	if(inode_buffer->direct_ptr[data_block] == -1){
		inode_buffer->direct_ptr[data_block] = get_avail_blkno() + 67;
	}
	bio_read(inode_buffer->direct_ptr[data_block], first_buffer);		// the first block is not guaranteed a valid pointer (faulty assumption)

	// If the amount of data you have to write is small, you will only have to write in one block.
	if(size <= BLOCK_SIZE - data_block_offset){
		memcpy(first_buffer, buffer, size);
		bio_write(inode_buffer->direct_ptr[data_block], first_buffer);		// write into the disk before freeing
		bytes_written += size;							// forgot this step, was a bug
		// printf("first_buffer = %s\n", first_buffer);
		free(first_buffer);
		// don't want to return just yet
	}

	// If you get to this point of execution, this means the file spans more than one block.
	else{
		// Substep 1: Fill in the first block.
		memcpy(first_buffer, buffer, BLOCK_SIZE - data_block_offset);
		bio_write(inode_buffer->direct_ptr[data_block], first_buffer);		// the first block is guaranteed to have a valid pointer
		bytes_written += (BLOCK_SIZE - data_block_offset);			// increment this number, keep track of where you are in the buffer
		free(first_buffer);

		// Substep 2: Fill in all of the full blocks in the middle.
		int full_blocks = (size - bytes_written) / BLOCK_SIZE;			// this is how many full blocks you have to write
		int written = 0;
		for(written = 0; written < full_blocks; written++){
			void* middle_man = malloc(BLOCK_SIZE);
			// Validate the data block if it hasn't been allocated yet. Bitmap is internally set within that function.
			if(inode_buffer->direct_ptr[data_block + (1 + written)] == -1){
				blocks_added++;
				inode_buffer->direct_ptr[data_block + (1 + written)] = get_avail_blkno() + 67;
			}
			bio_read(inode_buffer->direct_ptr[data_block + (1 + written)], middle_man);
			memcpy(middle_man, buffer + bytes_written, BLOCK_SIZE);
			bio_write(inode_buffer->direct_ptr[data_block + (1 + written)], middle_man);
			bytes_written += BLOCK_SIZE;					// you wrote in one more block
			free(middle_man);
		}

		// Substep 3: Check to see if there is anything else to write.
		int remaining = (size - bytes_written);
		if(remaining != 0){
			void* final_block = malloc(BLOCK_SIZE);
			// Validate the data block if it hasn't been allocated yet.
			if(inode_buffer->direct_ptr[data_block + (1 + written)] == -1){
				blocks_added++;
				inode_buffer->direct_ptr[data_block + (1 + written)] = get_avail_blkno() + 67;
			}
			bio_read(inode_buffer->direct_ptr[data_block + (1 + written)], final_block);
			memcpy(final_block, buffer + bytes_written, remaining);
			bio_write(inode_buffer->direct_ptr[data_block + (1 + written)], final_block);
			bytes_written += remaining;
			free(final_block);
		}
	}

	// Step 4: Update the inode info and write it to disk
	// Substep 1: Update the relevant information.
	inode_buffer->size += bytes_written;
	(inode_buffer->vstat).st_size += bytes_written;
	(inode_buffer->vstat).st_blocks++;			// keep track of the number of blocks you add

	// Substep 2: Write the updated inode to disk.
	writei(inode_buffer->ino, inode_buffer);

	// Note: this function should return the amount of bytes you write to disk
	return bytes_written;
}

static int tfs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char* str = (char*)malloc(252);
	char* parent_name = (char*)malloc(252);
	char* child_name = (char*)malloc(252);

	memset(str, 0, 252);
	memset(parent_name, 0, 252);
	memset(child_name, 0, 252);

	strcat(str, path);

	int split_point = strlen(path);
	while(str[split_point] != '/'){
		split_point--;
	}

	memcpy(parent_name, str, split_point);
	memcpy(child_name, str + (split_point + 1), strlen(str) - (split_point + 1));

	// Step 2: Call get_node_by_path() to get inode of target file
	struct inode* new_ino = (struct inode*)malloc(sizeof(struct inode));

	int get_target = get_node_by_path(str, 0, new_ino);
	if(get_target == -1){
		// Free everything, return an error.
		free(str);
		free(parent_name);
		free(child_name);
		free(new_ino);
		return -1;
	}

	// Step 3: Clear data block bitmap of target file
	bitmap_t data_block_buffer = (bitmap_t)malloc(BLOCK_SIZE);
	bio_read(2, data_block_buffer);
	int data_block_num = 0;
	for(data_block_num = 0; data_block_num < 16; data_block_num++){
		int actual_db = new_ino->direct_ptr[data_block_num];
		if(actual_db == -1){
			break;
		}
		unset_bitmap(data_block_buffer, actual_db);
	}
	bio_write(2, data_block_buffer);
	free(data_block_buffer);

	// Step 4: Clear inode bitmap and its data block
	bitmap_t inode_bit_buffer = (bitmap_t)malloc(BLOCK_SIZE);
	bio_read(1, inode_bit_buffer);
	unset_bitmap(inode_bit_buffer, new_ino->ino);
	int iterate = 0;
	for(iterate = 0; iterate < 16; iterate++){
		new_ino->direct_ptr[iterate] = -1;
	}
	bio_write(1, inode_bit_buffer);
	free(inode_bit_buffer);

	// Step 5: Call get_node_by_path() to get inode of parent directory
	struct inode* parent_inode = (struct inode*)malloc(sizeof(struct inode));
	get_node_by_path(parent_name, 0, parent_inode);

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory
	int can_remove = dir_remove(*parent_inode, child_name, strlen(child_name));
	if(can_remove == -1){
		free(parent_inode);
		return -1;
	}

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

