#include "proc/cpu.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "mem/mmap.h"
#include "lib/str.h"
#include "lib/print.h"
#include "syscall/sysfunc.h"
#include "syscall/syscall.h"

// 系统调用测试（无功能）
// 输出一行调试信息，然后返回0
uint64 sys_test()
{
    printf("System call test here.\n");
    return 0;
}

// 堆伸缩
// uint64 new_heap_top 新的堆顶 (如果是0代表查询, 返回旧的堆顶)
// 成功返回新的堆顶 失败返回-1
uint64 sys_brk()
{
    uint64 new_heap_top;
    uint64 old_heap_top;
    uint64 res;
    pgtbl_t pgtbl = myproc()->pgtbl;

    arg_uint64(0, &new_heap_top);
    old_heap_top = myproc()->heap_top;

    if(new_heap_top == 0){
        printf("堆未改变，堆查询结果：%x\n", old_heap_top);
        return old_heap_top;
    }
    else if(new_heap_top > old_heap_top){
        // 需要增大堆空间
        res = uvm_heap_grow(pgtbl, old_heap_top, new_heap_top - old_heap_top);
        printf("增大的新堆：%x\n", res);
        return res;
    }
    else{
        // 需要缩小堆空间
        res = uvm_heap_ungrow(pgtbl, old_heap_top, old_heap_top - new_heap_top);
        printf("缩小的新堆：%x\n", res);
        return res;
    }
}

// 内存映射
// uint64 start 起始地址 (如果为0则由内核自主选择一个合适的起点, 通常是顺序扫描找到一个够大的空闲空间)
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回映射空间的起始地址, 失败返回-1
uint64 sys_mmap()
{
    return -1;
}

// 取消内存映射
// uint64 start 起始地址
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回0 失败返回-1
uint64 sys_munmap()
{
    return -1;
}

// copyin 测试 (int 数组)
// uint64 addr
// uint32 len
// 返回 0
uint64 sys_copyin()
{
    proc_t* p = myproc();
    uint64 addr;
    uint32 len;

    arg_uint64(0, &addr);
    arg_uint32(1, &len);

    int tmp;
    for(int i = 0; i < len; i++) {
        uvm_copyin(p->pgtbl, (uint64)&tmp, addr + i * sizeof(int), sizeof(int));
        printf("get a number from user: %d\n", tmp);
    }

    return 0;
}

// copyout 测试 (int 数组)
// uint64 addr
// 返回数组元素数量
uint64 sys_copyout()
{
    int L[5] = {1, 2, 3, 4, 5};
    proc_t* p = myproc();
    uint64 addr;

    arg_uint64(0, &addr);
    uvm_copyout(p->pgtbl, addr, (uint64)L, sizeof(int) * 5);

    return 5;
}

// copyinstr测试
// uint64 addr
// 成功返回0
uint64 sys_copyinstr()
{
    char s[64];

    arg_str(0, s, 64);
    printf("get str from user: %s\n", s);

    return 0;
}
