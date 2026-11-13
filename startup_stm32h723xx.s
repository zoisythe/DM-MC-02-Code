.syntax unified
.cpu cortex-m7
.thumb
.section .isr_vector,"a",%progbits
.balign 1024
.global vectors
vectors:
    .word _estack
    .word Reset_Handler
    .rept 254
    .word Fault_Handler
    .endr
.section .text.Reset_Handler,"ax",%progbits
.thumb_func
.global Reset_Handler
Reset_Handler:
    cpsid i
    ldr r0, =0xE000ED08
    ldr r1, =vectors
    str r1, [r0]
    dsb
    isb
    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata
1:  cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b
2:  ldr r1, =_sbss
    ldr r2, =_ebss
    movs r3, #0
3:  cmp r1, r2
    bcs 4f
    str r3, [r1], #4
    b 3b
4:  bl main
5:  b 5b
.section .note.GNU-stack,"",%progbits
