[bits 64]

global __rdtsc__
__rdtsc__:
    rdtsc ; high 32 bits into edx, low 32 bits into eax
    shl rdx, 32
    or rax, rdx
    ret 