#include "fs/buf.h"
#include "fs/fs.h"
#include "fs/bitmap.h"
#include "lib/print.h"

extern super_block_t sb;


// TODO：没想到这两个函数怎么写
// 感觉跟老师给予的答案还是有一定区别
// ...
// search and set bit
static uint32 bitmap_search_and_set(uint32 bitmap_block)
{
    return 0;
}

// unset bit
static void bitmap_unset(uint32 bitmap_block, uint32 num)
{

}


// 将编号为dev的块置零
static void block_zero(int dev, int bno)
{
  struct buf *bp;

  bp = buf_read(dev, bno);
  memset(bp->data, 0, BLOCK_SIZE);
  // log_write(bp);
  brelse(bp);
}

uint32 bitmap_alloc_block(uint32 dev)
{
    int b, bi, m;
    struct buf *bp;

    bp = 0;
    for(b = 0; b < sb.size; b += BPB){
        bp = buf_read(dev, BBLOCK(b, sb));
        for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
            m = 1 << (bi % 8);
            if((bp->data[bi/8] & m) == 0){  // Is block free?
                bp->data[bi/8] |= m;  // Mark block in use.
                // log_write(bp);
                buf_release(bp);
                block_zero(dev, b + bi);
                return b + bi;
            }
        }
        buf_release(bp);
    }
    printf("bitmap_alloc_block: out of blocks\n");
    return 0;
}

void bitmap_free_block(uint32 dev, uint32 block_num)
{
    struct buf *bp;
    int bi, m;

    bp = buf_read(dev, BBLOCK(block_num, sb));
    bi = block_num % BPB;
    m = 1 << (bi % 8);
    if((bp->data[bi/8] & m) == 0)
        panic("freeing free block");
    bp->data[bi/8] &= ~m;
    // log_write(bp);
    buf_release(bp);
}

uint16 bitmap_alloc_inode()
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  printf("ialloc: no inodes\n");
  return 0;
}

void bitmap_free_inode(uint16 inode_num)
{

}

// 打印所有已经分配出去的bit序号(序号从0开始)
// for debug
void bitmap_print(uint32 bitmap_block_num)
{
    uint8 bit_cmp;
    uint32 byte, shift;

    printf("\nbitmap:\n");

    buf_t* buf = buf_read(bitmap_block_num);
    for(byte = 0; byte < BLOCK_SIZE; byte++) {
        bit_cmp = 1;
        for(shift = 0; shift <= 7; shift++) {
            if(bit_cmp & buf->data[byte])
               printf("bit %d is alloced\n", byte * 8 + shift);
            bit_cmp = bit_cmp << 1;
        }
    }
    printf("over\n");
    buf_release(buf);
}