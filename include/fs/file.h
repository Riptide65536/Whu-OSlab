#ifndef __FILE_H__
#define __FILE_H__

#include "common.h"

// file->type 选项

#define FD_UNUSED   0
#define FD_DIR      1
#define FD_FILE     2
#define FD_DEVICE   3
#define FD_PIPE     4
#define FD_INODE    5

// 文件打开方式 (readable writable)

#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400

typedef struct inode inode_t;

typedef struct file {
    uint16 type;      // 文件类型
    bool readable;    // 可读?
    bool writable;    // 可写?
    uint32 ref;       // 引用数
    uint16 major;     // 主设备号 (for device)
    uint32 offset;    // 偏移量   (for file)
    inode_t* ip;      // 对应的inode (for dir file device)
} file_t;

typedef struct file_state {
    uint16 type;
    uint16 inode_num;
    uint16 nlink;
    uint32 size;
} file_state_t;


#define N_DEV 10    // 设备类型数

#define DEV_CONSOLE 1    // 系统根目录号

#define MAXOPBLOCKS  10  // max # of blocks any FS op writes

// 设备文件需要提供读写接口
typedef struct dev {
    uint32 (*read)(uint32 len, uint64 dst, bool user_dst);
    uint32 (*write)(uint32 len, uint64 src, bool user_src);
} dev_t;


void    file_init();
file_t* file_alloc();
file_t* file_create_dev(char* path, uint16 major, uint16 minor);
file_t* file_open(char* path, uint32 open_mode);
void    file_close(file_t* file);
uint32  file_read(file_t* file, uint32 len, uint64 dst, bool user);
uint32  file_write(file_t* file, uint32 len, uint64 src, bool user);
uint32  file_lseek(file_t* file, uint32 offset, int flags);
file_t* file_dup(file_t* file);
int     file_stat(file_t* file, uint64 addr);

#endif