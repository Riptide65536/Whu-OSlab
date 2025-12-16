#include "fs/buf.h"
#include "fs/inode.h"
#include "fs/fs.h"
#include "fs/bitmap.h"
#include "lib/print.h"
#include "lib/str.h"

extern super_block_t sb;

// 辅助函数，使得某个块归零
// TODO：如果使用将会导致系统卡住（sleeplock?），请解决
void bzero(uint32 dev, uint32 bno)
{
    printf("Bzero no: %d\n", bno);
    struct buf *bp;

    bp = buf_read(dev, bno);
    memset(bp->data, 0, BLOCK_SIZE);
    //log_write(bp);
    buf_write(bp);
}

// 功能：在指定的位图块(bitmap_block)中，查找第一个为0的位，将其设置为1。
// 返回值：如果找到空闲位，则返回该位在块内的偏移量(0 ~ BPB-1)；如果该位图块已满，则返回0xFFFFFFFF。
static uint32 bitmap_search_and_set(uint32 dev, uint32 bitmap_block)
{
    buf_t *bp;
    uint32 bi; // 位索引 (Bit Index)
    int m;     // 位掩码 (Bit Mask)

    // 1. 将位图数据从磁盘读取到缓冲区
    bp = buf_read(dev, bitmap_block);
    if (bp == NULL) {
        // 处理错误，例如磁盘读取失败
        printf("bitmap_search_and_set: read bitmap block %d failed\n", bitmap_block);
        return 0xFFFFFFFF;
    }

    // 2. 遍历该位图块中的每一个位（最多BPB个）
    for (bi = 0; bi < BPB; bi++) {
        // 计算该位在字节数组中的索引和对应的掩码
        // bi/8 找到该位所在的字节
        // 1 << (bi % 8) 生成一个掩码，其中只有目标位是1，其余位是0
        m = 1 << (bi % 8);

        // 3. 检查该位是否空闲（即是否为0）
        if ((bp->data[bi / 8] & m) == 0) {
            // 4. 找到空闲位，将其标记为已使用（设置为1）
            bp->data[bi / 8] |= m;

            // 5. 将修改写回磁盘（这里被注释了，实际使用时可能需要开启）
            // log_write(bp);
            buf_write(bp);

            // 6. 释放缓冲区
            buf_release(bp);

            // 7. 返回找到的位的偏移量
            return bi;
        }
    }

    // 8. 循环结束，未找到空闲位，释放缓冲区并返回一个错误值
    buf_release(bp);
    return 0xFFFFFFFF; // 表示该位图块已满
}

// 功能：在指定的位图块上实现取消设置位
// 功能：在指定的位图块(bitmap_block)中，将指定偏移量(num)的位清零。
static void bitmap_unset(uint32 dev, uint32 bitmap_block, uint32 num)
{
    buf_t *bp;
    int m;

    // 1. 参数检查，确保偏移量num在有效范围内
    if (num >= BPB) {
        printf("bitmap_unset: bit offset %d out of range (BPB=%d)\n", num, BPB);
        return;
    }

    // 2. 将位图数据从磁盘读取到缓冲区
    bp = buf_read(dev, bitmap_block);
    if (bp == NULL) {
        printf("bitmap_unset: read bitmap block %d failed\n", bitmap_block);
        return;
    }

    // 3. 计算要清除的位的掩码
    m = 1 << (num % 8);

    // 4. 安全检查：确保要清除的位当前确实是1（即已分配状态）
    if ((bp->data[num / 8] & m) == 0) {
        // 如果该位已经是0，清除操作可能有问题，可以根据需要记录警告或报错
        printf("bitmap_unset: warning: bit %d in block %d was not set\n", num, bitmap_block);
        // panic("freeing free block");
    }

    // 5. 将指定位清零。使用按位与~m（掩码取反）实现。
    // 例如，m是 00000100，则 ~m 是 11111011。
    // 与原字节进行与操作后，目标位被清零，其他位保持不变。
    bp->data[num / 8] &= ~m;

    // 6. 将修改写回磁盘
    // log_write(bp);
    buf_write(bp);

    // 7. 释放缓冲区
    buf_release(bp);
}

uint32 bitmap_alloc_block(uint32 dev)
{
    unsigned int b;
    for(b = sb.data_bitmap_start; b < sb.data_start; b++){
        uint32 bi = (uint32)bitmap_search_and_set(dev, b);
        if(bi != 0xFFFFFFFF){
            // 清零该块，防止数据混杂
            // bzero(dev, sb.data_start + bi);
            return sb.data_start + bi;
        }
    }
    printf("balloc: out of data blocks\n");
    return 0;
}

void bitmap_free_block(uint32 dev, uint32 block_num)
{
    uint32 bitmap_num = (block_num - sb.data_start) / BPB;
    uint32 bitmap_bisas = (block_num - sb.data_start) % BPB;
    bitmap_unset(dev, sb.data_bitmap_start + bitmap_num, bitmap_bisas);
}

// 注：由于一个block有多个inode，所以该其只返回其相对于inode_start的偏移（inum）
// 对于编号为inum的inode，其详细的物理地块为 sb.inode_start + inum / INODE_PER_BLOCK
// 偏移为inum % INODE_PER_BLOCK

uint16 bitmap_alloc_inode(uint32 dev)
{
    unsigned int b;
    for(b = sb.inode_bitmap_start; b < sb.inode_start; b++){
        uint16 bi = (uint16)bitmap_search_and_set(dev, b);
        if(bi != 0xFFFFFFFF){
            // 清零该块，防止数据混杂
            // bzero(dev, sb.inode_start + bi);
            return bi;
        }
    }
    printf("balloc: out of inode blocks\n");
    return 0xFFFF;
}

void bitmap_free_inode(uint32 dev, uint16 inode_num)
{
    uint32 bitmap_num = (inode_num - sb.inode_start) / BPB;
    uint32 bitmap_bisas = (inode_num - sb.inode_start) % BPB;
    bitmap_unset(dev, sb.inode_bitmap_start + bitmap_num, bitmap_bisas);
}

// 打印所有已经分配出去的bit序号(序号从0开始)
// for debug
void bitmap_print(uint32 dev, uint32 bitmap_block_num)
{
    uint8 bit_cmp;
    uint32 byte, shift;

    printf("\nbitmap:\n");

    buf_t* buf = buf_read(dev, bitmap_block_num);
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