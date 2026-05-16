.section .interrupt_vector_table
.align 8
.global vector_table

vector_table:
    j default_handler   #

# dummy handler for non-implemented interrupts
default_handler:
    mret

