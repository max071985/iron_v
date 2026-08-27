#ifndef TEST_H
#define TEST_H

#include <stdint.h>

/* Linker defined boundary symbols */
extern char _stext[];
extern char _etext[];
extern char _srodata[];
extern char _erodata[];
extern char _sdata[];
extern char _edata[];
extern char _sbss[];
extern char _ebss[];
extern char _stack_top[];

typedef enum {
    MEM_ACCESS_INVALID = 0,
    MEM_ACCESS_READONLY,
    MEM_ACCESS_READWRITE,
    MEM_ACCESS_MMIO
} mem_access_t;

/* Validates address accessibility and permission */
mem_access_t check_mem_access(uint32_t addr);

/* Executes full automated validation test suite */
void run_validation_suite(void);

#endif // TEST_H
