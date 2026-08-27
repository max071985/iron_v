#include "proc.h"

struct {
    //struct spinlock lock; TODO
    struct proc proc[NPROC];
} ptable;

int nextpid = 1;

void procinit(void) {
    struct proc *p;

    // TODO: Lock critical section
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        // TODO: add per process lock? (idk if needed here)
        p->state = UNUSED;
        // TODO: p->kstack = KSTACK((int)(p - proc));
    }
}

int allocpid() {
    int pid;

    // TODO: Lock first(?)
    pid = nextpid;
    nextpid = nextpid + 1;
}

static struct proc * allocproc(void) {
    struct proc *p;

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->state == UNUSED) {
            goto found;
        }
        // TODO: Else release lock
    }
    return 0;

    found:
        p->pid = allocpid();
        p->state = USED;

        // TODO: allocate trapframe page

        // TODO: allocate an empty page table

        // TODO: allocate and setup context
}

static void freeproc(struct proc *p) {
    if (p->tf) {
        // TODO: free the trapframe
    }
    p->tf = 0;
    // TODO: free pagetable
    p->pgdir = 0;
    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = '\0';
    p->chan = 0;
    p->killed = 0;
    p->status = 0;
    p->state = UNUSED;
}