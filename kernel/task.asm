[bits 64]

extern task_bootstrap_entry

global task_start_trampoline

; extern ASMCALL void task_start_trampoline();
task_start_trampoline:
    ; x86_64_context_switch restores:
    ; R12 = task pointer
    ;
    ; convert to first SysV argument
    mov rdi, r12

    call task_bootstrap_entry

    ; task_bootstrap_entry should never return,
    ; but don't fall through if something goes wrong
.hang:
    cli
    hlt
    jmp .hang