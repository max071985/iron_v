#include "types.h"

struct trapframe {
    // General Purpose Registers (x1 - x31)
    uint ra;       // x1: Return address
    uint sp;       // x2: Stack pointer
    uint gp;       // x3: Global pointer
    uint tp;       // x4: Thread pointer
    uint t0;       // x5: Temporary
    uint t1;       // x6: Temporary
    uint t2;       // x7: Temporary
    uint s0;       // x8: Saved register / Frame pointer
    uint s1;       // x9: Saved register
    uint a0;       // x10: Function arg / Return value
    uint a1;       // x11: Function arg / Return value
    uint a2;       // x12: Function arg
    uint a3;       // x13: Function arg
    uint a4;       // x14: Function arg
    uint a5;       // x15: Function arg
    uint a6;       // x16: Function arg
    uint a7;       // x17: Function arg
    uint s2;       // x18: Saved register
    uint s3;       // x19: Saved register
    uint s4;       // x20: Saved register
    uint s5;       // x21: Saved register
    uint s6;       // x22: Saved register
    uint s7;       // x23: Saved register
    uint s8;       // x24: Saved register
    uint s9;       // x25: Saved register
    uint s10;      // x26: Saved register
    uint s11;      // x27: Saved register
    uint t3;       // x28: Temporary
    uint t4;       // x29: Temporary
    uint t5;       // x30: Temporary
    uint t6;       // x31: Temporary

    // Control and Status Registers (CSRs)
    uint epc;      // Exception Program Counter
    uint status;   // Privilege Status
    uint cause;    // Trap Cause
    uint tval;     // Trap Value (e.g., bad memory address)
};