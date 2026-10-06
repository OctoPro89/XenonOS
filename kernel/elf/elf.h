#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <process.h>
#include <kernel.h>

#define ELF_MAGIC0 0x7F
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'

#define ELFCLASS64 2
#define ELFDATA2LSB 1

#define ET_EXEC 2

#define EM_X86_64 62

#define PT_LOAD 1

#define PF_X 1
#define PF_W 2
#define PF_R 4

typedef struct __packed__ {
    u8  e_ident[16];

    u16 e_type;
    u16 e_machine;
    u32 e_version;

    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;

    u32 e_flags;

    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;

    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} elf64_header_t;

typedef struct __packed__ {
    u32 p_type;
    u32 p_flags;

    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;

    u64 p_filesz;
    u64 p_memsz;

    u64 p_align;
} elf64_program_header_t;

int process_load_elf(process_t* process, const char* path);