#pragma once

#include <io/fd_table.h>
#include <xlibc/xstdint.h>
#include <memory/vmm.h>

typedef struct process process_t;

struct process {
    u64 pid;
    vmm_space_t* space;
    fd_table_t fd_table;
};

process_t* process_create();
void process_destroy(process_t* process);

process_t* process_current();