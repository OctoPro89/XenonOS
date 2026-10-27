#include <elf/elf.h>
#include <memory/pmm.h>
#include <memory/vmm.h>
#include <memory/paging.h>
#include <xlibc/string.h>
#include <xlibc/stdio.h>
#include <errno.h>
#include <process.h>

// TODO: this loader is NOT perfect by any means.
// this needs to be redone later on.

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

    u64 flags = PAGE_PRESENT | PAGE_USER;
    if (ph->p_flags & PF_W) {
        flags |= PAGE_WRITABLE;
    }

    for (u64 page_va = start; page_va < end; page_va += PAGE_SIZE) {
        paddr_t mapped = vmm_virt_to_phys(process->space, page_va);

        if (mapped == 0) {
            paddr_t phys = pmm_alloc_page();
            if (!phys) {
                return -ENOMEM;
            }

            memset((void*)phys_to_hhdm(phys), 0, PAGE_SIZE);

            int rc = vmm_map(process->space, page_va, phys, flags);

            if (rc < 0) {
                pmm_free_page(phys);
                return rc;
            }
        } else if (flags & PAGE_WRITABLE) {
            int rc = vmm_update_page_flags(process->space, page_va, PAGE_WRITABLE, 0);

            if (rc < 0) {
                return rc;
            }
        }
    }

    u64 remaining = ph->p_filesz;
    u64 file_offset = ph->p_offset;
    u64 dest_va = ph->p_vaddr;

    while (remaining != 0) {
        u64 page_va = align_down(dest_va);
        u64 page_offset = dest_va - page_va;

        u64 chunk = PAGE_SIZE - page_offset;
        if (chunk > remaining) {
            chunk = remaining;
        }

        paddr_t phys = vmm_virt_to_phys(process->space, page_va);

        if (phys == 0) {
            return -EFAULT;
        }

        void* dest = (u8*)phys_to_hhdm(phys) + page_offset;

        if (fseek(file, (long)file_offset, SEEK_SET) != 0) {
            return -EIO;
        }

        size_t got = fread(
            dest, 1, (size_t)chunk, file
        );

        if (got != chunk) {
            return -EIO;
        }

        dest_va += chunk;
        file_offset += chunk;
        remaining -= chunk;
    }

    return 0;
}

int process_load_elf(process_t* process, const char* path) {
    if (!process || !path) {
        #if XENONOS_DEBUG
            printf("process_load_elf(): invalid process or path (process: 0x%x | path: 0x%x!\n", process, path);
        #endif
        return -EINVAL;
    }

    FILE* file = fopen(path, "r");
    if (!file) {
        #if XENONOS_DEBUG
            printf("process_load_elf(): failed to open '%s'!\n", path);
        #endif
        return -ENOENT;
    }

    fseek(file, 0, SEEK_SET);
    elf64_header_t header;
    if (fread(&header, 1, sizeof(header), file) != sizeof(header)) {
        #if XENONOS_DEBUG
            printf("process_load_elf(): failed to read '%s'!\n", path);
        #endif
        fclose(file);
        return -EIO;
    }

    if (header.e_ident[0] != ELF_MAGIC0 ||
        header.e_ident[1] != ELF_MAGIC1 ||
        header.e_ident[2] != ELF_MAGIC2 ||
        header.e_ident[3] != ELF_MAGIC3) {
        #if XENONOS_DEBUG
            printf("process_load_elf(): invalid file magic!\n", path);
        #endif
        fclose(file);
        return -EINVAL;
    }

    if (header.e_ident[4] != ELFCLASS64 || header.e_ident[5] != ELFDATA2LSB) {
        #if XENONOS_DEBUG
            printf("process_load_elf(): invalid ELF!\n", path);
        #endif
        fclose(file);
        return -EINVAL;
    }

    if (header.e_type != ET_EXEC || header.e_machine != EM_X86_64) {
        #if XENONOS_DEBUG
            printf("process_load_elf(): invalid ELF type!\n", path);
        #endif
        fclose(file);
        return -EINVAL;
    }

    if (header.e_phentsize != sizeof(elf64_program_header_t)) {
        #if XENONOS_DEBUG
            printf("process_load_elf(): invalid ELF header size!\n", path);
        #endif
        fclose(file);
        return -EINVAL;
    }

    u64 highest_segment_end = 0;

    for (u16 i = 0; i < header.e_phnum; ++i) {
        elf64_program_header_t ph;
        u64 offset = header.e_phoff + (u64)i * header.e_phentsize;
        fseek(file, (long)offset, SEEK_SET);

        if (fread(&ph, 1, sizeof(elf64_program_header_t), file) != sizeof(elf64_program_header_t)) {
            #if XENONOS_DEBUG
                printf("process_load_elf(): failed to read program header!\n", path);
            #endif
            fclose(file);
            return -EIO;
        }

        if (ph.p_type != PT_LOAD) {
            continue;
        }

        if (ph.p_memsz != 0) {
            u64 segment_end;

            if (__builtin_add_overflow(ph.p_vaddr, ph.p_memsz, &segment_end)) {
                #if XENONOS_DEBUG
                    printf("process_load_elf(): address overflow!\n", path);
                #endif
                fclose(file);
                return -EINVAL;
            }

            if (segment_end > highest_segment_end) {
                highest_segment_end = segment_end;
            }
        }

        int result = elf_load_segment(file, process, &ph);
        if (result < 0) {
            #if XENONOS_DEBUG
                printf("process_load_elf(): failed to load segment!\n", path);
            #endif
            fclose(file);
            return result;
        }
    }

    fclose(file);
    
    process->entry = header.e_entry;

    // stack
    for (int i = 0; i < PROCESS_STACK_PAGES; ++i) {
        paddr_t phys = pmm_alloc_page();
        if (!phys) {
            return -ENOMEM;
        }

        memset((void*)phys_to_hhdm(phys), 0, PAGE_SIZE);

        int rc = vmm_map(process->space, USER_STACK_TOP - ((u64)(i + 1) * PAGE_SIZE), phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
        if (rc < 0) {
            pmm_free_page(phys);
            return rc;
        }
    }

    process->user_stack_top = USER_STACK_TOP;

    vaddr_t stack_bottom = USER_STACK_TOP - (PROCESS_STACK_PAGES * PAGE_SIZE);

    // leave at least one unmapped page below the stack
    vaddr_t heap_limit = stack_bottom - PAGE_SIZE;

    if (highest_segment_end > heap_limit) {
        return -ENOMEM;
    }

    process->heap_start = (highest_segment_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    process->heap_break = process->heap_start;
    process->heap_limit = heap_limit;

    return 0;
}