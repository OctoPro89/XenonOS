#include <elf/elf.h>
#include <memory/pmm.h>
#include <memory/vmm.h>
#include <memory/paging.h>
#include <xlibc/string.h>
#include <xlibc/stdio.h>
#include <errno.h>
#include <process.h>

static u64 align_down(u64 value) {
    return value & ~(PAGE_SIZE - 1);
}

static u64 align_up(u64 value) {
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static int elf_load_segment(FILE* file, process_t* process, const elf64_program_header_t* ph) {
    if (ph->p_memsz < ph->p_filesz) {
        return -EINVAL;
    }

    if (ph->p_memsz == 0) {
        return 0;
    }

    u64 seg_end = 0;
    if (__builtin_add_overflow(ph->p_vaddr, ph->p_memsz, &seg_end)) {
        return -EINVAL;
    }
    
    if (seg_end > VMM_USER_MAX) {
        return -EINVAL;
    }

    u64 start = align_down(ph->p_vaddr);
    u64 end = align_up(seg_end);

    for (u64 page_va = start; page_va < end; page_va += PAGE_SIZE) {
        // NOTE: this assumes PT_LOAD segments don't overlap the same page
        if (vmm_virt_to_phys(process->space, page_va) != 0) {
            return -EINVAL;
        }

        paddr_t phys = pmm_alloc_page();
        if (!phys) {
            return -ENOMEM;
        }

        void* page = (void*)phys_to_hhdm(phys);
        memset(page, 0, PAGE_SIZE);

        // determine the portion of the file-backed segment that belongs in this page
        u64 copy_start = page_va;
        if (copy_start < ph->p_vaddr) {
            copy_start = ph->p_vaddr;
        }

        u64 file_end = ph->p_vaddr + ph->p_filesz;
        u64 copy_end = page_va + PAGE_SIZE;
        if (copy_end > file_end) {
            copy_end = file_end;
        }

        if (copy_start < copy_end) {
            u64 file_offset = ph->p_offset + (copy_start - ph->p_vaddr);
            size_t copy_size = (size_t)(copy_end - copy_start);
            fseek(file, (long)file_offset, SEEK_SET);
            size_t got = fread((u8*)page + (copy_start - page_va), 1, copy_size, file);
            if (got != copy_size) {
                pmm_free_page(phys);
                return -EIO;
            }
        }

        u64 flags = PAGE_PRESENT | PAGE_USER;
        if (ph->p_align & PF_W) {
            flags |= PAGE_WRITABLE;
        }

        // TODO: NX bit
        vmm_map(process->space, page_va, phys, flags);
    }

    return 0;
}

int process_load_elf(process_t* process, const char* path) {
    if (!process || !path) {
        return -EINVAL;
    }

    FILE* file = fopen(path, "r");
    if (!file) {
        return -ENOENT;
    }

    fseek(file, 0, SEEK_SET);
    elf64_header_t header;
    if (fread(&header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        return -EIO;
    }

    if (header.e_ident[0] != ELF_MAGIC0 ||
        header.e_ident[1] != ELF_MAGIC1 ||
        header.e_ident[2] != ELF_MAGIC2 ||
        header.e_ident[3] != ELF_MAGIC3) {
        fclose(file);
        return -EINVAL;
    }

    if (header.e_ident[4] != ELFCLASS64 || header.e_ident[5] != ELFDATA2LSB) {
        fclose(file);
        return -EINVAL;
    }

    if (header.e_type != ET_EXEC || header.e_machine != EM_X86_64) {
        fclose(file);
        return -EINVAL;
    }

    if (header.e_phentsize != sizeof(elf64_program_header_t)) {
        fclose(file);
        return -EINVAL;
    }

    for (u16 i = 0; i < header.e_phnum; ++i) {
        elf64_program_header_t ph;
        u64 offset = header.e_phoff + (u64)i * header.e_phentsize;
        fseek(file, (long)offset, SEEK_SET);

        if (fread(&ph, 1, sizeof(elf64_program_header_t), file) != sizeof(elf64_program_header_t)) {
            fclose(file);
            return -EIO;
        }

        if (ph.p_type != PT_LOAD) {
            continue;
        }

        int result = elf_load_segment(file, process, &ph);
        if (result < 0) {
            fclose(file);
            return result;
        }
    }

    fclose(file);
    
    process->entry = header.e_entry;

    // stack
    for (int i = 0; i < 8; ++i) {
        paddr_t phys = pmm_alloc_page();
        if (!phys) {
            return -ENOMEM;
        }

        vmm_map(process->space, USER_STACK_TOP - ((u64)(i + 1) * PAGE_SIZE), phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
    }

    process->user_stack_top = USER_STACK_TOP;

    return 0;
}