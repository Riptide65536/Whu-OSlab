#include "fs/fs.h"
#include "fs/buf.h"
#include "fs/dir.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/file.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"

// 设备列表(读写接口)
dev_t devlist[N_DEV];

// ftable + 保护它的锁
#define N_FILE 32
file_t ftable[N_FILE];
spinlock_t lk_ftable;

// ftable初始化 + devlist初始化
void file_init()
{
    spinlock_init(&lk_ftable, "ftable");
}

// alloc file_t in ftable
// 失败则panic
file_t* file_alloc()
{
    struct file *f;

    spinlock_acquire(&lk_ftable);
    for(f = ftable; f < ftable + N_FILE; f++){
        if(f->ref == 0){
            f->ref = 1;
            spinlock_release(&lk_ftable);
            return f;
        }
    }
    spinlock_release(&lk_ftable);
    return 0;
}

// 创建设备文件(供proczero创建console)
// TODO：在xv6中没有
file_t* file_create_dev(char* path, uint16 major, uint16 minor)
{
    return 0;
}

// 打开一个文件
file_t* file_open(char* path, uint32 open_mode)
{
    int fd;
    struct file *f;
    struct inode *ip;
    int n;

    //begin_op();

    if(open_mode & O_CREATE){
        ip = file_create(path, FT_FILE, 0, 0);
        if(ip == 0){
        //end_op();
        return -1;
        }
    } else {
        if((ip = namei(path)) == 0){
        //end_op();
        return -1;
        }
        ilock(ip);
        if(ip->type == FT_DIR && open_mode != O_RDONLY){
        iunlockput(ip);
        //end_op();
        return -1;
        }
    }

    if(ip->type == FT_DEVICE && (ip->major < 0 || ip->major >= N_DEV)){
        iunlockput(ip);
        //end_op();
        return -1;
    }

    if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
        if(f)
        fileclose(f);
        iunlockput(ip);
        //end_op();
        return -1;
    }

    if(ip->type == FT_DEVICE){
        f->type = FD_DEVICE;
        f->major = ip->major;
    } else {
        f->type = FD_INODE;
        f->off = 0;
    }
    f->ip = ip;
    f->readable = !(open_mode & O_WRONLY);
    f->writable = (open_mode & O_WRONLY) || (open_mode & O_RDWR);

    if((open_mode & O_TRUNC) && ip->type == FT_FILE){
        itrunc(ip);
    }

    iunlock(ip);
    end_op();

    return fd;
}

// 释放一个file
void file_close(file_t* f)
{
    struct file ff;

    spinlock_acquire(&lk_ftable);;
    if(f->ref < 1)
        panic("fileclose");
    if(--f->ref > 0){
        spinlock_release(&lk_ftable);
        return;
    }
    ff = *f;
    f->ref = 0;
    f->type = FD_UNUSED;
    spinlock_release(&lk_ftable);

    if(ff.type == FD_PIPE){
        // pipeclose(ff.pipe, ff.writable);
    } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
        // begin_op();
        inode_free(ff.ip);
        // end_op();
    }
}

// 文件内容读取
// 返回读取到的字节数
uint32 file_read(file_t* f, uint32 len, uint64 dst, bool user)
{
    // TODO：很怪，基本上跟xv6的不一样。
    int r = 0;

    if(f->readable == 0)
        return -1;

    if(f->type == FD_PIPE){
        // r = piperead(f->pipe, addr, n);
    } else if(f->type == FD_DEVICE){
        if(f->major < 0 || f->major >= N_DEV || !devlist[f->major].read)
            return -1;
        r = devlist[f->major].read(1, dst, user);
    } else if(f->type == FD_INODE){
        inode_lock(f->ip);
        if((r = inode_read_data(f->ip, f->offset, len, (void*)dst, user)) > 0)
            f->offset += r;
        inode_unlock(f->ip);
    } else {
        panic("fileread");
    }

    r = 0;

    return r;
}

// 文件内容写入
// 返回写入的字节数
uint32 file_write(file_t* f, uint32 len, uint64 src, bool user)
{
    // TODO：还是太怪了！！

    int r, ret = 0;

    if(f->writable == 0)
        return -1;
    if(f->type == FD_PIPE){
        // ret = pipewrite(f->pipe, addr, n);
    } else if(f->type == FD_DEVICE){
        if(f->major < 0 || f->major >= N_DEV || !devlist[f->major].write)
        return -1;
        ret = devlist[f->major].write(1, src, user);
    } else if(f->type == FD_INODE){
        // write a few blocks at a time to avoid exceeding
        // the maximum log transaction size, including
        // i-node, indirect block, allocation blocks,
        // and 2 blocks of slop for non-aligned writes.
        // this really belongs lower down, since writei()
        // might be writing a device like the console.
        int max = ((MAXOPBLOCKS-1-1-2) / 2) * BLOCK_SIZE;
        int i = 0;
        while(i < len){
            int n1 = len - i;
            if(n1 > max) n1 = max;

            //begin_op();
            inode_lock(f->ip);
            if ((r = inode_write_data(f->ip, 1,f->offset, (void*)(src + i), n1)) > 0)
                f->offset += r;
            inode_unlock(f->ip);
            //end_op();

            if(r != n1){
                // error from writei
                break;
            }
            i += r;
        }
        ret = (i == len ? len : -1);
    } else {
        panic("filewrite");
    }

    return ret;
}

// flags 可能取值
#define LSEEK_SET 0  // file->offset = offset
#define LSEEK_ADD 1  // file->offset += offset
#define LSEEK_SUB 2  // file->offset -= offset

// 修改file->offset (只针对FD_FILE类型的文件)
uint32 file_lseek(file_t* file, uint32 offset, int flags)
{
    if(file->type == FD_FILE){
        switch (flags)
        {
        case LSEEK_SET:
            file->offset = offset;
            break;
        case LSEEK_ADD:
            file->offset += offset;
            break;
        case LSEEK_SUB:
            file->offset -= offset;
            break;
        default:
            break;
        }
        return file->offset;
    }
    printf("Warning: file type not right!");
    return -1;
}

// file->ref++ with lock
file_t* file_dup(file_t* file)
{
    spinlock_acquire(&lk_ftable);
    assert(file->ref > 0, "file_dup: ref");
    file->ref++;
    spinlock_release(&lk_ftable);
    return file;
}

// 获取文件状态
int file_stat(file_t* file, uint64 addr)
{
    file_state_t state;
    if(file->type == FD_FILE || file->type == FD_DIR)
    {
        inode_lock(file->ip);
        state.type = file->ip->type;
        state.inode_num = file->ip->inum;
        state.nlink = file->ip->nlink;
        state.size = file->ip->size;
        inode_unlock(file->ip);

        uvm_copyout(myproc()->pgtbl, addr, (uint64)&state, sizeof(file_state_t));
    }
    return -1;
}