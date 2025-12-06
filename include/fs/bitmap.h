#ifndef __BITMAP_H__
#define __BITMAP_H__

#include "common.h"

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

uint32 bitmap_alloc_block(uint32 dev);
uint16 bitmap_alloc_inode();
void   bitmap_free_block(uint32 dev, uint32 block_num);
void   bitmap_free_inode(uint16 inode_num);
void   bitmap_print(uint32 bitmap_block_num);

#endif