#include "mem/mmap.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"
#include "lib/str.h"
#include "memlayout.h"
#include "riscv.h"

// 连续虚拟空间的复制(在uvm_copy_pgtbl中使用)
/*
static void copy_range(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end)
{
    uint64 va, pa, page;
    int flags;
    pte_t* pte;

    for(va = begin; va < end; va += PGSIZE)
    {
        pte = vm_getpte(old, va, false);
        assert(pte != NULL, "uvm_copy_pgtbl: pte == NULL");
        assert((*pte) & PTE_V, "uvm_copy_pgtbl: pte not valid");
        
        pa = (uint64)PTE_TO_PA(*pte);
        flags = (int)PTE_FLAGS(*pte);

        page = (uint64)pmem_alloc(false);
        memmove((char*)page, (const char*)pa, PGSIZE);
        vm_mappages(new, va, page, PGSIZE, flags);
    }
}

// 两个 mmap_region 区域合并
// 保留一个 释放一个 不操作 next 指针
// 在uvm_munmap里使用
static void mmap_merge(mmap_region_t* mmap_1, mmap_region_t* mmap_2, bool keep_mmap_1)
{
    // 确保有效和紧临
    assert(mmap_1 != NULL && mmap_2 != NULL, "mmap_merge: NULL");
    assert(mmap_1->begin + mmap_1->npages * PGSIZE == mmap_2->begin, "mmap_merge: check fail");
    
    // merge
    if(keep_mmap_1) {
        mmap_1->npages += mmap_2->npages;
        mmap_region_free(mmap_2);
    } else {
        mmap_2->begin -= mmap_1->npages * PGSIZE;
        mmap_2->npages += mmap_1->npages;
        mmap_region_free(mmap_1);
    }
}

// 打印以 mmap 为首的 mmap 链
// for debug
void uvm_show_mmaplist(mmap_region_t* mmap)
{
    mmap_region_t* tmp = mmap;
    printf("\nmmap allocable area:\n");
    if(tmp == NULL)
        printf("NULL\n");
    while(tmp != NULL) {
        printf("allocable region: %p ~ %p\n", tmp->begin, tmp->begin + tmp->npages * PGSIZE);
        tmp = tmp->next;
    }
}

*/

// 递归释放 页表占用的物理页 和 页表管理的物理页
// ps: 顶级页表level = 0, level = 3 说明是页表管理的物理页
static void destroy_pgtbl(pgtbl_t pgtbl, uint32 level)
{
    if (pgtbl == NULL) {
        return;
    }

    // 第一个问题：我不知道如何正确的递归删除页表和页表管理的物理页
    // 先删除子页面
    if(level < 3){
        for(int i=0; i<512; i++){
            pte_t *pte = &pgtbl[i];
            pgtbl_t pa = (pgtbl_t)PTE_TO_PA(*pte);
            if(pte && pa){
                destroy_pgtbl(pa, level + 1);
            }
        }
    }
    // 然后删除自己（反正都是物理地址）
    pmem_free_auto((uint64)pgtbl);
}

// 页表销毁：trapframe 和 trampoline 单独处理
void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{
    vm_unmappages(pgtbl, TRAMPOLINE, PGSIZE, 0);
    vm_unmappages(pgtbl, TRAPFRAME, PGSIZE, 0);
    destroy_pgtbl(pgtbl, 0);
}

// 拷贝页表 (拷贝并不包括trapframe 和 trampoline)
void uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint32 ustack_pages, mmap_region_t* mmap)
{
    pte_t *pte;
    uint64 pa, va;
    uint8 flags;
    char *mem;

    /* step-1: USER_BASE ~ heap_top */
    for(va = 0; va < heap_top; va += PGSIZE){
        if((pte = vm_getpte(old, va, 0)) == 0)
            panic("uvmcopy: pte should exist");
        if((*pte & PTE_V) == 0)
            panic("uvmcopy: page not present");
        pa = PTE_TO_PA(*pte);
        flags = PTE_FLAGS(*pte);
        if((mem = pmem_alloc(false)) == 0){
            vm_unmappages(new, 0, va / PGSIZE, 1);
            printf("uvmcopy warning: mem alloc failed");
        }
        memmove(mem, (char*)pa, PGSIZE);

        vm_mappages(new, va, (uint64)mem, PGSIZE, flags);
    }

    /* step-2: ustack */
    // 由于每个进程都有对应的映射用户栈（proc_mapstacks已配备）
    // 目前暂时不用处理此部分，因为用户栈和用户代码共用一页    

    /* step-3: mmap_region */
    // 这部分未涉及mmap的东西，所以不写
}

// 在用户页表和进程mmap链里 新增mmap区域 [begin, begin + npages * PGSIZE)
// 页面权限为perm
void uvm_mmap(uint64 begin, uint32 npages, int perm)
{
    if(npages == 0) return;
    assert(begin % PGSIZE == 0, "uvm_mmap: begin not aligned");

    // 修改 mmap 链 (分情况的链式操作)

    // 修改页表 (物理页申请 + 页表映射)

}

// 在用户页表和进程mmap链里释放mmap区域 [begin, begin + npages * PGSIZE)
void uvm_munmap(uint64 begin, uint32 npages)
{
    if(npages == 0) return;
    assert(begin % PGSIZE == 0, "uvm_munmap: begin not aligned");

    // new mmap_region 的产生

    // 尝试合并 mmap_region

    // 页表释放

}

// 用户堆空间增加, 返回新的堆顶地址 (注意栈顶最大值限制)
// 在这里无需修正 p->heap_top
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    uint64 a;
    char* mem;

    heap_top = PG_ROUND_UP(heap_top);
    uint64 new_heap_top = heap_top + len;
    for(a = heap_top; a < new_heap_top; a += PGSIZE){
        mem = pmem_alloc(false);
        if(mem == 0){
            uvm_heap_ungrow(pgtbl, a, a - heap_top);
            return -1;
        }
        memset(mem, 0, PGSIZE);
        vm_mappages(pgtbl, a, (uint64)mem, PGSIZE, PTE_W|PTE_R|PTE_U);
    }

    myproc()->heap_top = new_heap_top;

    return new_heap_top;
}

// 用户堆空间减少, 返回新的堆顶地址
// 在这里无需修正 p->heap_top
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    uint64 new_heap_top = heap_top - len;

    vm_unmappages(pgtbl, new_heap_top, len, 1);

    myproc()->heap_top = new_heap_top;

    return new_heap_top;
}

// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint64 n, va0, pa0;

    while(len > 0){
        va0 = PG_ROUND_DOWN(src);
        pa0 = vm_getpa(pgtbl, va0);
        if(pa0 == 0) return;
        n = PGSIZE - (src - va0);
        if(n > len)
            n = len;
        memmove((char *)dst, (void *)(pa0 + (src - va0)), n);

        len -= n;
        dst += n;
        src = va0 + PGSIZE;
    }
}

// 内核态地址空间[src, src+len） 拷贝至 用户态地址空间[dst, dst+len)
void uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint64 n, va0, pa0;

    while(len > 0){
        va0 = PG_ROUND_DOWN(dst);
        pa0 = vm_getpa(pgtbl, va0);
        if(pa0 == 0) return;
        n = PGSIZE - (dst - va0);
        if(n > len)
            n = len;
        memmove((void *)(pa0 + (dst - va0)), (char *)src, n);

        len -= n;
        src += n;
        dst = va0 + PGSIZE;
    }
}

// 用户态字符串拷贝到内核态
// 最多拷贝maxlen字节, 中途遇到'\0'则终止
// 注意: src dst 不一定是 page-aligned
void uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen)
{
    uint64 n, va0, pa0;
    int got_null = 0;

    while(got_null == 0 && maxlen > 0){
        va0 = PG_ROUND_DOWN(src);
        pa0 = vm_getpa(pgtbl, va0);
        if(pa0 == 0) return;
        n = PGSIZE - (src - va0);
        if(n > maxlen) n = maxlen;

        char *p = (char *) (pa0 + (src - va0));
        while(n > 0){
            if(*p == '\0'){
                *((char *)dst) = '\0';
                got_null = 1;
                break;
            } else {
                *((char *)dst) = *p;
            }
            --n;
            --maxlen;
            p++;
            dst++;
        }

        src = va0 + PGSIZE;
    }
}