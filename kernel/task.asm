[bits 64]

extern task_bootstrap_entry

global task_start_trampoline

task_start_trampoline:
    ; R12 contains the task_t pointer from the manufactured context.
    mov rdi, r12

    ; SysV x86-64:
    ; At function entry: RSP % 16 == 8
    ; Before CALL:       RSP % 16 == 0
    sub rsp, 8

    call task_bootstrap_entry

    ; task_bootstrap_entry should never return.
.hang:
    cli
    hlt
    jmp .hang