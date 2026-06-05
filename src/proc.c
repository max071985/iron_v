#include "proc.h"

struct {
    //struct spinlock lock; TODO
    struct proc proc[NPROC];
} ptable;