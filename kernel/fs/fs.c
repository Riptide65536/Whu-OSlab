#include "fs/fs.h"
#include "fs/buf.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/dir.h"
#include "fs/file.h"
#include "lib/str.h"
#include "lib/print.h"

// 超级块在内存的副本
super_block_t sb;

#define FS_MAGIC 0x12345678
#define SB_BLOCK_NUM 0

// 输出super_block的信息
static void sb_print()
{
    printf("\nsuper block information:\n");
    printf("magic = %x\n", sb.magic);
    printf("block size = %d\n", sb.block_size);
    printf("inode blocks = %d\n", sb.inode_blocks);
    printf("data blocks = %d\n", sb.data_blocks);
    printf("total blocks = %d\n", sb.total_blocks);
    printf("inode bitmap start = %d\n", sb.inode_bitmap_start);
    printf("inode start = %d\n", sb.inode_start);
    printf("data bitmap start = %d\n", sb.data_bitmap_start);
    printf("data start = %d\n", sb.data_start);
}

// 有关于文件系统的测试
uint8 str[2 * BLOCK_SIZE];
uint8 tmp[2 * BLOCK_SIZE];

void inode_test(){
    printf("\n=== Inode test begin. ===\n");
    uint32 ret = 0;

    for(int i = 0; i < BLOCK_SIZE * 2; i++)
        str[i] = i % 114;

    // 创建新的inode
    printf("\n === Inode alloc === \n");
    inode_t* nip = inode_alloc(DEV_CONSOLE, FT_FILE);
    // 为什么会no type呢？不是传了个FT_FILE进去吗（已解决）
    inode_lock(nip);
    
    // 第一次查看
    inode_print(nip);

    // 第一次写入
    //printf("\n === The 1st write === \n");
    ret = inode_write_data(nip, 0, BLOCK_SIZE / 2, str, false);
    assert(ret == BLOCK_SIZE / 2, "inode_write_data: fail");

    // 第二次写入
    //printf("\n === The 2nd write === \n");
    ret = inode_write_data(nip, BLOCK_SIZE / 2, BLOCK_SIZE + BLOCK_SIZE / 2, str + BLOCK_SIZE / 2, false);
    assert(ret == BLOCK_SIZE +  BLOCK_SIZE / 2, "inode_write_data: fail");

    // 一次读取
    //printf("\n === The Reading === \n");
    ret = inode_read_data(nip, 0, BLOCK_SIZE * 2, tmp, false);
    assert(ret == BLOCK_SIZE * 2, "inode_read_data: fail");

    // 第二次查看
    inode_print(nip);
    
    //printf("\n === Inode unlock free === \n");
    inode_unlock_free(nip);

    // 测试
    for(int i = 0; i < BLOCK_SIZE * 2; i++){
        assert(str[i] == tmp[i], "Inode test failed.\n");
    }
    printf("Congrats! Test success!\n");

    printf("\n=== ALL test done. ===\n");
}

// 文件系统初始化
void fs_init()
{
    buf_t* buf;
    buf = buf_read(DEV_CONSOLE, SB_BLOCK_NUM);
    memmove(&sb, buf->data, sizeof(sb));
    assert(sb.magic == FS_MAGIC, "fs_init: magic");
    assert(sb.block_size == BLOCK_SIZE, "fs_init: block size");
    buf_release(buf);
    sb_print();

    inode_test();
}