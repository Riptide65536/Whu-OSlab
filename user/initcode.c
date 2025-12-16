#include "sys.h"

// 与内核保持一致
#define VA_MAX       (1ul << 38)
#define PGSIZE       4096
#define MMAP_END     (VA_MAX - 34 * PGSIZE)
#define MMAP_BEGIN   (MMAP_END - 8096 * PGSIZE) 

char *str1, *str2;

int main()
{
    // 用户进程开始（测试在fsinit中做）
    syscall(SYS_print, "Hello world! ");
    syscall(SYS_print, "Now just counting dots:\n");

    for(int i=0; i<100; i++){
        syscall(SYS_sleep, 10);
        syscall(SYS_print, ".");
    }

    while(1);
    
    return 0;
}