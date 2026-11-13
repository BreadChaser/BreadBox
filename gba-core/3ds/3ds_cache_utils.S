/* Luma documented cache SVCs. No privileged callbacks. */
.text
.arm
.balign 4
.global ctr_clear_cache
.type ctr_clear_cache, %function
ctr_clear_cache:
    push {r4, lr}
    svc 0x92
    svc 0x94
    pop {r4, pc}
.section .note.GNU-stack,"",%progbits
