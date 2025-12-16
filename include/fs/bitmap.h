#ifndef __BITMAP_H__
#define __BITMAP_H__

#include "common.h"

// Bitmap bits per block
#define BPB           (BLOCK_SIZE*8)

uint32 bitmap_alloc_block(uint32 dev);
void   bitmap_free_block(uint32 dev, uint32 block_num);
uint16 bitmap_alloc_inode(uint32 dev);
void   bitmap_free_inode(uint32 dev, uint16 inode_num);
void   bitmap_print(uint32 dev, uint32 bitmap_block_num);

#endif