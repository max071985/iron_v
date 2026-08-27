#ifndef PROC_H
#define PROC_H

#include "types.h"
#include "riscv.h"

#define NPROC 8     // Max process count

struct context {
    uint ra;       // Return address
    uint sp;       // Stack pointer

    // Callee-saved registers
    uint s0;
    uint s1;
    uint s2;
    uint s3;
    uint s4;
    uint s5;
    uint s6;
    uint s7;
    uint s8;
    uint s9;
    uint s10;
    uint s11;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct proc
{
    uint sz;                        // Size of process memory
    uint kstack;                    // Virtual address of kernel stack
    uint* pgdir;                    // Page table
    int pid;                        // Process ID
    enum procstate state;           // Process state
    struct proc *parent;            // Parent process
    struct trapframe *tf;           // Trap frame for current syscall
    struct context context;        // swtch() here to run process
    void *chan;                     // If non-zero, sleeping on chan
    int killed;                     // If non-zero, have been killed
    char name[16];                  // Process name (debugging)
    int status;                     // Process exit status
    struct cgroup * cgroup;         // The process control group.
    uint cpu_time;                  // Process cpu time.
    uint cpu_period_time;           // Cpu time in microseconds in the last accounting frame.
    uint cpu_percent;               // Cpu usage percentage in the last accounting frame.
    uint cpu_account_frame;         // The cpu account frame.
};

#endif