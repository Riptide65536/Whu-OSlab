#include "fs/buf.h"
#include "dev/vio.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "lib/str.h"

#define N_BLOCK_BUF 64
#define BLOCK_NUM_UNUSED 0xFFFFFFFF

// buf cache
static buf_t buf_cache[N_BLOCK_BUF];
static buf_t head_buf; // ->next 已分配 ->prev 可分配
static spinlock_t lk_buf_cache; // 这个锁负责保护 链式结构 + buf_ref + block_num

// 链表操作
static void insert_head(buf_t* buf_node, bool head_next)
{
    // 离开
    if(buf_node->next && buf_node->prev) {
        buf_node->next->prev = buf_node->prev;
        buf_node->prev->next = buf_node->next;
    }

    // 插入
    if(head_next) { // 插入 head->next
        buf_node->prev = &head_buf;
        buf_node->next = head_buf.next;
        head_buf.next->prev = buf_node;
        head_buf.next = buf_node;        
    } else { // 插入 head->prev
        buf_node->next = &head_buf;
        buf_node->prev = head_buf.prev;
        head_buf.prev->next = buf_node;
        head_buf.prev = buf_node;
    }
}

// 初始化
void buf_init()
{
    buf_t *b;
    spinlock_init(&lk_buf_cache, "bcache");

    // Create linked list of buffers
    head_buf.prev = &head_buf;
    head_buf.next = &head_buf;
    for(b = buf_cache; b < buf_cache + N_BLOCK_BUF; b++){
        sleeplock_init(&b->slk, "buffer");
        insert_head(b, true);
    }
}

/*
    首先假设这个block_num对应的block在内存中有备份, 找到它并上锁返回
    如果找不到, 尝试申请一个无人使用的buf, 去磁盘读取对应block并上锁返回
    如果没有空闲buf, panic报错
    (建议合并xv6的bget())
*/
buf_t* buf_read(uint32 dev, uint32 block_num)
{
    // 合并bread（读取块，不是面包函数）以及bget
    buf_t *b;

    spinlock_acquire(&lk_buf_cache);

    // 检查此block是否在内存中有备份（Is the block already cached?）
    for(b = head_buf.next; b != &head_buf; b = b->next){
        if(b->dev == dev && b->block_num == block_num) {
            b->buf_ref++;
            spinlock_release(&lk_buf_cache);
            sleeplock_acquire(&b->slk);

            //printf("\nRead block(cached)\n");
            //buf_print_single(b);

            return b;
        }
    }

    // 发现block中没有备份，则回收最近最不经常使用的缓冲区（LRU）
    for(b = head_buf.prev; b != &head_buf; b = b->prev){
        if(b->buf_ref == 0) {
            b->dev = dev;
            b->block_num = block_num;
            b->buf_ref = 1;
            spinlock_release(&lk_buf_cache);
            sleeplock_acquire(&b->slk);
            virtio_disk_rw(b, 0);
            
            //printf("\nRead block(not cached)\n");
            //buf_print_single(b);
            
            return b;
        }
    }
    panic("buf_read: no buffers");
    return 0;
}

// 写函数 (强制磁盘和内存保持一致)
void buf_write(buf_t* buf)
{
    if(!sleeplock_holding(&buf->slk))
        panic("buf_write");

    //printf("\nWrite block:\n");
    //buf_print_single(buf);

    virtio_disk_rw(buf, 1);
}

// buf 释放
void buf_release(buf_t* buf)
{
    if(!sleeplock_holding(&buf->slk))
        panic("buf_release");

    sleeplock_release(&buf->slk);

    spinlock_acquire(&lk_buf_cache);
    buf->buf_ref--;

    //printf("\nRelease block:\n");
    //buf_print_single(buf);

    if (buf->buf_ref == 0) {
        insert_head(buf, false);
    }
    spinlock_release(&lk_buf_cache);
}

// 输出单个buf的内容
void buf_print_single(buf_t* b)
{
    if(b == &head_buf){
        printf("The dummy head.\n");
        return;
    }

    printf("buf %x: ref = %d, block_num = %d\n",
        (int)(b - buf_cache), b->buf_ref, b->block_num);
    for(int i = 0; i < 8; i++)
        printf("%d ",b->data[i]);
    printf("\n");
}

// 输出buf_cache的情况
void buf_print()
{
    printf("\nbuf_cache:\n");
    buf_t* b = head_buf.next;
    spinlock_acquire(&lk_buf_cache);
    while(b != &head_buf)
    {
        buf_print_single(b);
        b = b->next;
    }
    spinlock_release(&lk_buf_cache);
}