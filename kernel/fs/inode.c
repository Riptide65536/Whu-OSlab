#include "fs/buf.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/fs.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"
#include "lib/str.h"

extern super_block_t sb;

// 内存中的inode资源 + 保护它的锁
#define N_INODE 32
static inode_t icache[N_INODE];
static spinlock_t lk_icache;

#define min(a, b) ((a) < (b) ? (a) : (b))

// TODO:
// 1) 部分函数中涉及到了balloc, bfree等与bitmap等相关的函数（可能为了避嫌已经格式化）
//    请思考这两个函数以及当前bitmap.c以及后方函数的关系，采用合理的方式进行改写
// 2) 部分函数的调用可能有dev的问题，请检查相关函数的调用并适当改写
//    如果有必要，可以改写函数定义使得其增加dev参数
// 3) 我希望这天结束之后inode可以全部搞完，且测试用例通过

// icache初始化
void inode_init()
{
    int i = 0;

    spinlock_init(&lk_icache, "lk_icache");
    for(i = 0; i < N_INODE; i++) {
        sleeplock_init(&icache[i].lock, "inode");
    }
}

/*---------------------- 与inode本身相关 -------------------*/

// 使用磁盘里的inode更新内存里的inode (write = false, ip->xx = dip->xx)
// 或使用内存里的inode更新磁盘里的inode (write = true, dip->xx = ip->xx)
// PS: iupdate(ip) = inode_rw(ip, true)
// 调用者需要设置inode_num并持有睡眠锁
void inode_rw(inode_t* ip, bool write)
{
  buf_t *bp;
  struct dinode *dip;

  bp = buf_read(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%INODE_PER_BLOCK;

  if(write){
    dip->type = ip->type;
    dip->major = ip->major;
    dip->minor = ip->minor;
    dip->nlink = ip->nlink;
    dip->size = ip->size;
    memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  }
  else{
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(dip->addrs));
  }

  // log_write(bp);
  buf_write(bp);
  buf_release(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
inode_t* inode_get(uint32 dev, uint16 inum)
{
    inode_t *ip, *empty;

    spinlock_acquire(&lk_icache);

    // 在表中是不是已经有了？
    empty = 0;
    for(ip = &icache[0]; ip < &icache[N_INODE]; ip++){
        if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
            ip->ref++;
            spinlock_release(&lk_icache);
            return ip;
        }
        if(empty == 0 && ip->ref == 0)    // 顺便记忆空位
            empty = ip;
    }

    // 回收为空的iget表项
    if(empty == 0)
        panic("iget: no inodes");

    ip = empty;
    ip->dev = dev;
    ip->inum = inum;
    ip->ref = 1;
    ip->valid = 0;
    spinlock_release(&lk_icache);

    return ip;
}

// 在设备号为dev的设备上分配一个inode块，将其标记为已使用
// 返回未锁定且有引用的inode。如果没有空闲inode返回NULL
inode_t* inode_alloc(uint32 dev, uint16 type)
{
    int inum;
    buf_t *bp;
    struct dinode *dip;

    inum = bitmap_alloc_inode(dev);

    if(inum == 0xFFFF){
        printf("ialloc: no inodes\n");
        return 0;
    }

    bp = buf_read(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%INODE_PER_BLOCK;
    memset(dip, 0, sizeof(*dip));
    dip->type = type;
    // log_write(bp);
    buf_write(bp);
    buf_release(bp);
    return inode_get(dev, inum);
}

// 供inode_free调用
// 在磁盘上删除一个inode及其管理的文件 (修改inode bitmap + block bitmap)
// 调用者需要持有lk_icache, 但不应该持有slk
static void inode_destroy(inode_t* ip)
{
    sleeplock_acquire(&ip->lock);
    spinlock_release(&lk_icache);

    inode_free_data(ip);
    ip->type = 0;
    inode_rw(ip, true);
    ip->valid = 0;

    sleeplock_release(&ip->lock);
    spinlock_acquire(&lk_icache);
}

// 向icache里归还inode
// inode->ref--
// 调用者不应该持有slk
// 类似于xv6的iput
void inode_free(inode_t* ip)
{
    spinlock_acquire(&lk_icache);
    if(ip->ref == 1 && ip->valid && ip->nlink == 0){
        inode_destroy(ip);
    }
    ip->ref--;
    spinlock_release(&lk_icache);
}

// ip->ref++ with lock
inode_t* inode_dup(inode_t* ip)
{
    spinlock_acquire(&lk_icache);
    ip->ref++;
    spinlock_release(&lk_icache);
    return ip;
}

// 给inode上锁
// 如果valid失效则从磁盘中读入
void inode_lock(inode_t* ip)
{
    if(ip == 0 || ip->ref < 1)
        panic("ilock");

    sleeplock_acquire(&ip->lock);

    if(ip->valid == 0){
        inode_rw(ip, false);
        ip->valid = 1;
        if(ip->type == 0)
            panic("ilock: no type");
    }
}

// 给inode解锁
void inode_unlock(inode_t* ip)
{
    if(ip == 0 || !sleeplock_holding(&ip->lock) || ip->ref < 1)
        panic("iunlock");

    sleeplock_release(&ip->lock);
}

// 连招: 解锁 + 释放
void inode_unlock_free(inode_t* ip)
{
    inode_unlock(ip);
    inode_free(ip);
}

/*---------------------------- 与inode管理的data相关 --------------------------*/

// TODO：我觉得剩余需要实现的函数就是如下这些
// 当然，还需要与bitmap相连接

// 辅助 inode_locate_block
// 递归查询或创建block
static uint32 locate_block(uint32* entry, uint32 dev, uint32 bn, uint32 size)
{
    if(*entry == 0)
        *entry = bitmap_alloc_block(dev);

    if(size == 1)
        return *entry;    

    uint32* next_entry;
    uint32 next_size = size / ENTRY_PER_BLOCK;
    uint32 next_bn = bn % next_size;
    uint32 ret = 0;

    buf_t* buf = buf_read(dev, *entry);
    next_entry = (uint32*)(buf->data) + bn / next_size;
    ret = locate_block(next_entry, dev, next_bn, next_size);
    buf_release(buf);

    return ret;
}

// 确定inode里第bn块data block的block_num
// 如果不存在第bn块data block则申请一个并返回它的block_num
// 由于inode->addrs的结构, 这个过程比较复杂, 需要单独处理
// 相当于bmap
static uint32 inode_locate_block(inode_t* ip, uint32 bn)
{
    uint32 addr;
    
    // 情况1: 直接块 (0-9)
    if (bn < N_ADDRS_1) {
        // 如果该直接块还未分配，则分配新块
        if ((addr = ip->addrs[bn]) == 0) {
            addr = bitmap_alloc_block(ip->dev);  // 分配新的数据块
            ip->addrs[bn] = addr;
        }
        return addr;
    }
    bn -= N_ADDRS_1;
    
    // 情况2: 二级索引 (10-11)
    // 每个二级索引块可以管理 ENTRY_PER_BLOCK 个数据块
    if (bn < N_ADDRS_2 * ENTRY_PER_BLOCK) {
        uint32 idx_level2 = bn / ENTRY_PER_BLOCK;  // 确定是第几个二级索引 (0或1)
        uint32 bn_in_level2 = bn % ENTRY_PER_BLOCK; // 在该二级索引中的相对块号
        
        // 调用 locate_block，传入对应的 addrs 项
        return locate_block(&ip->addrs[N_ADDRS_1 + idx_level2], 
                           ip->dev, 
                           bn_in_level2, 
                           ENTRY_PER_BLOCK);
    }
    bn -= N_ADDRS_2 * ENTRY_PER_BLOCK;
    
    // 情况3: 三级索引 (entry-13)
    // 三级索引可以管理 ENTRY_PER_BLOCK * ENTRY_PER_BLOCK 个数据块
    if (bn < ENTRY_PER_BLOCK * ENTRY_PER_BLOCK) {
        return locate_block(&ip->addrs[N_ADDRS_1 + N_ADDRS_2], 
                           ip->dev, 
                           bn, 
                           ENTRY_PER_BLOCK * ENTRY_PER_BLOCK);
    }
    
    // 超出文件大小限制
    panic("inode_locate_block: block number out of range");
    return 0;
}


// 读取 inode 管理的 data block
// 调用者需要持有 inode 锁
// 成功返回读出的字节数, 失败返回0
uint32 inode_read_data(inode_t* ip, uint32 off, uint32 len, void* dst, bool user)
{
    uint32 tot, m;
    struct buf *bp;

    if(off > ip->size || off + len < off)
        return 0;
    if(off + len > ip->size)
        len = ip->size - off;

    for(tot=0; tot<len; tot+=m, off+=m, dst+=m){
        uint32 addr = inode_locate_block(ip, off/BLOCK_SIZE);
        if(addr == 0)
            break;
        bp = buf_read(ip->dev, addr);
        m = min(len - tot, BLOCK_SIZE - off%BLOCK_SIZE);
        if(either_copyout(user, (uint64)dst, bp->data + (off % BLOCK_SIZE), m) == -1) {
            buf_release(bp);
            tot = -1;
            break;
        }
        buf_release(bp);
    }
    return tot;
}

// 写入 inode 管理的 data block (可能导致管理的 block 增加)
// 调用者需要持有 inode 锁
// 成功返回写入的字节数, 失败返回0
uint32 inode_write_data(inode_t* ip, uint32 off, uint32 len, void* src, bool user)
{
    uint32 tot, m;
    buf_t *bp;

    if(off > ip->size || off + len < off)
        return -1;
    if(off + len > INODE_MAXSIZE*BLOCK_SIZE)
        return -1;

    for(tot=0; tot<len; tot+=m, off+=m, src+=m){
        uint32 addr = inode_locate_block(ip, off/BLOCK_SIZE);
        if(addr == 0)
            break;
        bp = buf_read(ip->dev, addr);
        m = min(len - tot, BLOCK_SIZE - off%BLOCK_SIZE);
        if(either_copyin(bp->data + (off % BLOCK_SIZE), user, (uint64)src, m) == -1) {
            buf_release(bp);
            break;
        }
        // log_write(bp);
        buf_write(bp);
        buf_release(bp);
    }

    if(off > ip->size)
        ip->size = off;

    // write the i-node back to disk even if the size didn't change
    // because the loop above might have called bmap() and added a new
    // block to ip->addrs[].
    inode_rw(ip, true);

    return tot;
}

// 递归释放索引块及其管理的所有块
// entry: 当前索引项的块号
// dev: 设备号
// level: 当前层级 (1=数据块, 2=二级索引, 3=三级索引)
static void free_block_recursive(uint32 entry, uint32 dev, uint32 level)
{
    if (entry == 0) return;
    
    // 如果是数据块层级，直接释放
    if (level == 1) {
        bitmap_free_block(dev, entry);
        return;
    }
    
    // 读取索引块
    buf_t* buf = buf_read(dev, entry);
    uint32* entries = (uint32*)(buf->data);
    
    // 递归释放该索引块指向的所有子块
    for (int i = 0; i < ENTRY_PER_BLOCK; i++) {
        if (entries[i] != 0) {
            free_block_recursive(entries[i], dev, level - 1);
        }
    }
    
    buf_release(buf);
    
    // 释放当前索引块本身
    bitmap_free_block(dev, entry);
}

// 释放inode管理的所有 data block
// ip->addrs被清空 ip->size置0
// 调用者需要持有ip->lock
// 相当于xv6中的itrunc
void inode_free_data(inode_t* ip)
{
    // 情况1: 释放直接块 (addrs[0-9])
    for (int i = 0; i < N_ADDRS_1; i++) {
        if (ip->addrs[i] != 0) {
            bitmap_free_block(ip->dev, ip->addrs[i]);
            ip->addrs[i] = 0;
        }
    }
    
    // 情况2: 释放二级索引块 (addrs[10-11])
    for (int i = 0; i < N_ADDRS_2; i++) {
        uint32 idx = N_ADDRS_1 + i;
        if (ip->addrs[idx] != 0) {
            free_block_recursive(ip->addrs[idx], ip->dev, 2);
            ip->addrs[idx] = 0;
        }
    }
    
    // 情况3: 释放三级索引块 (addrs[12])
    uint32 idx_level3 = N_ADDRS_1 + N_ADDRS_2;
    if (ip->addrs[idx_level3] != 0) {
        free_block_recursive(ip->addrs[idx_level3], ip->dev, 3);
        ip->addrs[idx_level3] = 0;
    }
    
    // 清空文件大小并写回磁盘
    ip->size = 0;
    inode_rw(ip, true);
}


static char* inode_types[] = {
    "INODE_UNUSED",
    "INODE_DIR",
    "INODE_FILE",
    "INODE_DEVICE",
};

// 输出inode信息
// for dubug
void inode_print(inode_t* ip)
{
    assert(sleeplock_holding(&ip->lock), "inode_print: lk");

    printf("\ninode information:\n");
    printf("num = %d, ref = %d, valid = %d\n", ip->inum, ip->ref, ip->valid);
    printf("type = %s, major = %d, minor = %d, nlink = %d\n", inode_types[ip->type], ip->major, ip->minor, ip->nlink);
    printf("size = %d, addrs =", ip->size);
    for(int i = 0; i < N_ADDRS; i++)
        printf(" %d", ip->addrs[i]);
    printf("\n");
}

// 输出dinode信息
// for dubug
void dinode_print(struct dinode* ip)
{
    printf("\ndinode information:\n");
    printf("type = %s, major = %d, minor = %d, nlink = %d\n", inode_types[ip->type], ip->major, ip->minor, ip->nlink);
    printf("size = %d, addrs =", ip->size);
    for(int i = 0; i < N_ADDRS; i++)
        printf(" %d", ip->addrs[i]);
    printf("\n");
}