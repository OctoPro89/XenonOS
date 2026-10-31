#include <smp/smp.h>
#include <smp/ipi.h>
#include <smp/percpu.h>
#include <hw/mmio.h>
#include <hw/delay.h>
#include <acpi/madt.h>
#include <time/time.h>
#include <memory/vmm.h>
#include <memory/paging.h>
#include <xlibc/xassert.h>
#include <xlibc/string.h>
#include <xlibc/errno.h>
#include <arch/x86_64/msr.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/syscall.h>
#include <arch/arch_smp.h>
#include <arch/x86_64/apic/lapic.h>
#include <arch/x86_64/sync/critical_section.h>

#define X86_VEC_IPI 0xF0

extern vmm_space_t kernel_space;

extern char asm_ap_trampoline[];
extern char asm_ap_trampoline_end[];

// physical addresses for the trampoline and startup data
static const paddr_t AP_TRAMPOLINE_PHYS         = 0x8000;
static const paddr_t AP_STARTUP_DATA_PHYS       = 0x9000;
static const u32 AP_SIPI_VECTOR                 = (AP_TRAMPOLINE_PHYS  >> 12); // 0x8

static const u32 AP_STACK_PAGES = 4;
static const u16 AP_GUARD_PAGES = 1;

// LAPIC ICR command constants
static const u32 ICR_DM_FIXED       = (0 << 8);
static const u32 ICR_DM_INIT        = (5 << 8);
static const u32 ICR_DM_STARTUP     = (6 << 8);
static const u32 ICR_LEVEL_ASSERT   = (1 << 14);
static const u32 ICR_TRIGGER_LEVEL  = (1 << 15);
static const u32 ICR_DELIVERY_BUSY  = (1 << 12);
static const u32 ICR_DEST_SHIFT     = 24;

// IPI timing
static const u32 IPI_INIT_DELAY_MS = 10;
static const u32 IPI_SIPI_DELAY_MS = 1;
static const u32 IPI_TIMEOUT_MS    = 400;

static const u32 IRQ_LAPIC_ID = 0x020;

static const u32 PERCPU_PAGES = 2;

// startup data shared between BSP and AP (matches trampoline offsets at 0x9000)
typedef struct {
    u64 page_table_phys;
    u64 stack_top;
    u64 logical_id;
    u64 c_entry;
    u64 percpu_base; //(per-CPU area VA, read by ap_entry)
} ap_startup_data_t;

STATIC_ASSERT(sizeof(ap_startup_data_t) == 40);

static const u32 MSR_IA32_APIC_BASE = 0x1B;
static const u64 APIC_BASE_BSP_FLAG = (1ULL << 8);

// AP C entry, called from the trampoline's 64-bit section
__PRIVILEGED_CODE void ap_entry(u64 logical_id) {
    ap_startup_data_t* data = (ap_startup_data_t*)AP_STARTUP_DATA_PHYS;
    u32 cpu_id = (u32)logical_id;

    // claim before touching the per-CPU area or running any init, so a CPU the
    // BSP already gave up on parks instead of using memory it no longer owns.
    smp_cpu_info_t* info = smp_get_cpu_info(cpu_id);
    u32 claim = SMP_CPU_BOOTING;
    // TODO: check
    if (!info || !__atomic_compare_exchange_n(&info->state, &claim, SMP_CPU_CLAIMED, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        while (1) { asm volatile("cli; hlt"); }
    }

    // per-cpu area first, enables this_cpu() for everything else
    percpu_init_ap(cpu_id, data->percpu_base);

    // allocate IST stacks (VMM safe after GS set, spinlocks compile down to pushfq/cli)
    uintptr_t ist1_base = 0, ist1_top = 0;
    uintptr_t ist2_base = 0, ist2_top = 0;
    uintptr_t ist3_base = 0, ist3_top = 0;
    if (vmm_alloc_stack(&kernel_space, 2, 0, &ist1_base, &ist1_top) != 0 ||
        vmm_alloc_stack(&kernel_space, 2, 0, &ist2_base, &ist2_top) != 0 ||
        vmm_alloc_stack(&kernel_space, 2, 0, &ist3_base, &ist3_top) != 0) {
        while (true) { asm volatile("cli; hlt"); }
    }

    // GDT / TSS, uses this_cpu() for per-cpu GDT / TSS structs
    gdt_init_percpu(data->stack_top, ist1_top, ist2_top, ist3_top);
    gdt_load();

    // load shared idt (lidt only, no rebuild)
    idt_load_local();

    // cpu features (PAT MSR, p5er-cpu registers)

    syscall_init();
    arch_smp_ipi_init_ap();

    serial_write_str("AP running!\n");

    // TODO:

    __atomic_store_n(&info->state, SMP_CPU_ONLINE, __ATOMIC_RELEASE);

    while (1) { asm volatile("pause"); }
    // scheduler_run_idle();
}

__PRIVILEGED_CODE static void wait_icr_idle() {
    uintptr_t lapic_va = lapic_get_va();
    while (mmio_read32(lapic_va + APIC_ICR_LO) & ICR_DELIVERY_BUSY) {
        asm volatile("pause");
    }
}

__PRIVILEGED_CODE static void send_init_ipi(u8 apic_id) {
    uintptr_t lapic_va = lapic_get_va();

    // assert INIT
    mmio_write32(lapic_va + APIC_ICR_HI, (u32)(apic_id) << ICR_DEST_SHIFT);
    mmio_write32(lapic_va + APIC_ICR_LO, ICR_DM_INIT | ICR_LEVEL_ASSERT | ICR_TRIGGER_LEVEL);
    wait_icr_idle();

    // deassert INIT
    mmio_write32(lapic_va + APIC_ICR_HI, (i32)(apic_id) << ICR_DEST_SHIFT);
    mmio_write32(lapic_va + APIC_ICR_LO, ICR_DM_INIT | ICR_TRIGGER_LEVEL);
    wait_icr_idle();
}

__PRIVILEGED_CODE static void send_startup_ipi(u8 apic_id, u32 vector) {
    uintptr_t lapic_va = lapic_get_va();

    mmio_write32(lapic_va + APIC_ICR_HI, (u32)(apic_id) << ICR_DEST_SHIFT);
    mmio_write32(lapic_va + APIC_ICR_LO, (vector & 0xFF) | ICR_DM_STARTUP);
    wait_icr_idle();
}

__PRIVILEGED_CODE u32 arch_smp_enumerate(smp_cpu_info_t* cpus, u32 max) {
    const madt_info_t* madt = madt_get_info();

    u8 bsp_apic_id = 0;
    u64 apic_base_msr = msr_read(MSR_IA32_APIC_BASE);
    if (apic_base_msr & APIC_BASE_BSP_FLAG) {
        u64 lapic_va = lapic_get_va();
        bsp_apic_id = (u8)(mmio_read32(lapic_va + IRQ_LAPIC_ID) >> 24);
    }

    u32 count = 0;
    for (u32 i = 0; i < madt->lapic_count && count < max; ++i) {
        if (!madt->lapics[i].enabled) {
            continue;
        }

        cpus[count].logical_id = count;
        cpus[count].hw_id = madt->lapics[i].apic_id;
        // TODO: check
        __atomic_store_n(&cpus[count].state, SMP_CPU_OFFLINE, __ATOMIC_RELAXED);
        cpus[count].is_bsp = (madt->lapics[i].apic_id == bsp_apic_id);
        ++count;
    }

    return count;
}

struct phys_range arch_smp_fixed_boot_frames() {
    return (struct phys_range){ AP_TRAMPOLINE_PHYS, AP_STARTUP_DATA_PHYS + PAGE_SIZE };
}

__PRIVILEGED_CODE i32 arch_smp_prepare(void) {
    paddr_t pt_root = paging_get_kernel_pt_root();

    if (pt_root >= 0x100000000ULL) {
        serial_write_str("SMP: PML4 is above 4GB\n");
        return SMP_CPU_INIT_RESULT_ERR_PREPARE;
    }

    int rc;

    rc = vmm_map(&kernel_space,
                 AP_TRAMPOLINE_PHYS,
                 AP_TRAMPOLINE_PHYS,
                 PAGE_PRESENT | PAGE_WRITABLE);
    if (rc != 0 && rc != -EEXIST) {
        serial_write_str("SMP: Failed to map trampoline identity page\n");
        return SMP_CPU_INIT_RESULT_ERR_PREPARE;
    }

    rc = vmm_map(&kernel_space,
                 AP_STARTUP_DATA_PHYS,
                 AP_STARTUP_DATA_PHYS,
                 PAGE_PRESENT | PAGE_WRITABLE);
    if (rc != 0 && rc != -EEXIST) {
        serial_write_str("SMP: Failed to map startup data identity page\n");
        return SMP_CPU_INIT_RESULT_ERR_PREPARE;
    }

    invlpg(AP_TRAMPOLINE_PHYS);
    invlpg(AP_STARTUP_DATA_PHYS);

    size_t trampoline_size =
        (size_t)(asm_ap_trampoline_end - asm_ap_trampoline);

    if (trampoline_size > PAGE_SIZE) {
        serial_write_str("SMP: Trampoline is larger than one page\n");
        return SMP_CPU_INIT_RESULT_ERR_PREPARE;
    }

    memcpy((void*)AP_TRAMPOLINE_PHYS,
           asm_ap_trampoline,
           trampoline_size);

    ap_startup_data_t* data =
        (ap_startup_data_t*)AP_STARTUP_DATA_PHYS;

    data->page_table_phys = pt_root;
    data->c_entry = (u64)&ap_entry;

    return SMP_CPU_INIT_RESULT_OK;
}

// TODO: check
__PRIVILEGED_CODE i32 arch_smp_boot_cpu(smp_cpu_info_t* cpu) {
    // allocate per-cpu area (2 pages zeroed)
    vaddr_t percpu_va = vmm_alloc_virtual_pages(2);
    if (!percpu_va) {
        return SMP_CPU_INIT_RESULT_ERR_BOOT_TIMEOUT;
    }

    for (size_t i = 0; i < 2; ++i) {
        paddr_t phys = pmm_alloc_page();
        if (!phys) {
            vmm_unmap_pages(&kernel_space, percpu_va, i); 
            return SMP_CPU_INIT_RESULT_ERR_BOOT_TIMEOUT;
        }

        memset((void*)phys_to_hhdm(phys), 0, PAGE_SIZE);
        if (vmm_map(&kernel_space, percpu_va + (i * PAGE_SIZE), phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
            return SMP_CPU_INIT_RESULT_ERR_BOOT_TIMEOUT;
        }
    }

    // allocate the initial execution task stack
    vaddr_t stack_base = 0;
    vaddr_t stack_top = 0;
    // assuming a 4-page stack with 1 guard page
    if (vmm_alloc_stack(&kernel_space, 4, 1, &stack_base, &stack_top) != 0) {
        vmm_unmap_pages(&kernel_space, percpu_va, 2);
        return SMP_CPU_INIT_RESULT_ERR_BOOT_TIMEOUT;
    }

    // Fill per-ap fields in startup data
    ap_startup_data_t* data = (ap_startup_data_t*)AP_STARTUP_DATA_PHYS;
    data->stack_top = stack_top;
    data->logical_id = cpu->logical_id;
    data->percpu_base = percpu_va;

    // TODO: this is wrong for x2APIC iirc
    if (cpu->hw_id > 0xFF) {
        serial_write_str("SMP: APIC ID requires x2APIC support\n");
        return SMP_CPU_INIT_RESULT_ERR_PREPARE;
    }

    u8 apic_id = (u8)cpu->hw_id;

    // INIT-SIPI-SIPI sequence
    send_init_ipi(apic_id);
    delay_pit_ms(IPI_INIT_DELAY_MS);

    send_startup_ipi(apic_id, AP_SIPI_VECTOR);
    delay_pit_ms(IPI_SIPI_DELAY_MS);

    // check if AP came online after the first SIPI
    if (__atomic_load_n(&cpu->state, __ATOMIC_ACQUIRE) == SMP_CPU_ONLINE) {
        return SMP_CPU_INIT_RESULT_OK;
    }

    // send the second SIPI (as noted in the reference code)
    send_startup_ipi(apic_id, AP_SIPI_VECTOR);

    // Poll with a timeout waiting for the AP to come online
    for (u32 waited = 0; waited < IPI_TIMEOUT_MS; waited++) {
        delay_pit_ms(1);
        if (__atomic_load_n(&cpu->state, __ATOMIC_ACQUIRE) == SMP_CPU_ONLINE) {
            return SMP_CPU_INIT_RESULT_OK;
        }
    }

    // a SIPI cannot be recalled, so the deadline expiring does not prove the AP is dead.
    // abandon atomically: winning means the AP parks before using anything, losing means it is
    // alive and owns its memory.
    i32 abandon = SMP_CPU_BOOTING;
    if (__atomic_compare_exchange_n(&cpu->state, (u32*)&abandon, SMP_CPU_ABANDONED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        // the trampoline already ran on this stack, so both allocations are leaked rather
        // than handed out again under a CPU that may still run.
        return SMP_CPU_INIT_RESULT_ERR_BOOT_TIMEOUT;
    }

    // the AP claimed after the deadline, so let it finish coming up.
    // Note: The loop condition checks for SMP_CPU_CLAIMED, matching the reference code logic
    for (u32 waited = 0; waited < IPI_TIMEOUT_MS && __atomic_load_n(&cpu->state, __ATOMIC_ACQUIRE) == SMP_CPU_CLAIMED; waited++) {
        delay_pit_ms(1);
    }

    return __atomic_load_n(&cpu->state, __ATOMIC_ACQUIRE) == SMP_CPU_ONLINE ? SMP_CPU_INIT_RESULT_OK : SMP_CPU_INIT_RESULT_ERR_BOOT_TIMEOUT;
}

__PRIVILEGED_CODE smp_ipi_result_t arch_smp_ipi_init() {
    return SMP_IPI_RESULT_OK;
}

__PRIVILEGED_CODE smp_ipi_result_t arch_smp_ipi_init_ap() {
    return SMP_IPI_RESULT_OK;
}

__PRIVILEGED_CODE smp_ipi_result_t arch_smp_raise_ipi(const smp_cpu_info_t* target) {
    // physical destinations in the ICR hold an 8-bit APIC id
    if (target->hw_id > 0xFF) {
        return SMP_IPI_RESULT_ERR_UNREACHABLE;
    }

    uintptr_t lapic_va = lapic_get_va();
    u32 apic_id = (u32)target->hw_id;
    
    // the destination and command halves must land as a pair,
    // an interrupt handler sending its own IPI between them would redirect this one
    u64 flags = irq_save();
    wait_icr_idle();
    mmio_write32(lapic_va + APIC_ICR_HI, apic_id << ICR_DEST_SHIFT);
    mmio_write32(lapic_va + APIC_ICR_LO, X86_VEC_IPI | ICR_DM_FIXED | ICR_LEVEL_ASSERT);
    irq_restore(flags);

    return SMP_IPI_RESULT_OK;
}