#ifndef PROC_H
#define PROC_H

#include "types.h"
#include "riscv.h"

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

struct proc
{
    uint sz;                        // Size of process memory
    uint* pgdir;                    // Page table
    int pid;                        // Process ID
    struct proc *parent;            // Parent process
    struct trapframe *tf;           // Trap frame for current syscall
    struct context *context;        // swtch() here to run process
    void *chan;                     // If non-zero, sleeping on chan
    int killed;                     // If non-zero, have been killed
    //struct file *ofile[NOFILE];     // Open files
    //struct inode *cwd;              // Current directory
    //struct mount *cwdmount;         // Mount in which current directory lies
    char name[16];                  // Process name (debugging)
    struct nsproxy *nsproxy;        // Namespace proxy object
    struct pid_ns *child_pid_ns;    // PID namespace for child procs
    int status;                     // Process exit status
    //char cwdp[MAX_PATH_LENGTH];     // Current directory path.
    struct cgroup * cgroup;         // The process control group.
    unsigned int cpu_time;          // Process cpu time.
    unsigned int cpu_period_time;   // Cpu time in microseconds in the last accounting frame.
    unsigned int cpu_percent;       // Cpu usage percentage in the last accounting frame.
    unsigned int cpu_account_frame; // The cpu account frame.
};

#endif