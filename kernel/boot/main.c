#include "riscv.h"
#include "lib/print.h"
#include "lib/lock.h"
#include "proc/proc.h"

volatile static int started = 0;

void main()
{
    int cpuid = mycpuid();
    if(cpuid == 0){
        print_init();
        printf("cpu %d is booting!\n", cpuid);
        __sync_synchronize();
        started = 1;
        
        printf("cpu %d report: The first sentence\n", cpuid);
        
    }
    else{
        while(started == 0) {};
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        
        printf("cpu %d report: The another sentence\n", cpuid);
        
    }

    while(1){};
}