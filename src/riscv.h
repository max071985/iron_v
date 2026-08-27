#include "types.h"

struct trapframe {
  /*   0 */ uint kernel_satp;   // kernel page table
  /*   8 */ uint kernel_sp;     // top of process's kernel stack
  /*  16 */ uint kernel_trap;   // usertrap()
  /*  24 */ uint epc;           // saved user program counter
  /*  32 */ uint kernel_hartid; // saved kernel tp
  /*  40 */ uint ra;
  /*  48 */ uint sp;
  /*  56 */ uint gp;
  /*  64 */ uint tp;
  /*  72 */ uint t0;
  /*  80 */ uint t1;
  /*  88 */ uint t2;
  /*  96 */ uint s0;
  /* 104 */ uint s1;
  /* 112 */ uint a0;
  /* 120 */ uint a1;
  /* 128 */ uint a2;
  /* 136 */ uint a3;
  /* 144 */ uint a4;
  /* 152 */ uint a5;
  /* 160 */ uint a6;
  /* 168 */ uint a7;
  /* 176 */ uint s2;
  /* 184 */ uint s3;
  /* 192 */ uint s4;
  /* 200 */ uint s5;
  /* 208 */ uint s6;
  /* 216 */ uint s7;
  /* 224 */ uint s8;
  /* 232 */ uint s9;
  /* 240 */ uint s10;
  /* 248 */ uint s11;
  /* 256 */ uint t3;
  /* 264 */ uint t4;
  /* 272 */ uint t5;
  /* 280 */ uint t6;
};