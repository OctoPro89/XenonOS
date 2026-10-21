#include <smp/smp.h>
#include <time/time.h>
#include <memory/paging.h>
#include <xlibc/xassert.h>

// physical addresses for the trampoline and startup data
static const paddr_t AP_TRAMPOLINE_PHYS         = 0x8000;
static const paddr_t AP_STARTUP_DATA_PHYS       = 0x9000;
static const u32 API_SIPI_VECTOR                = (AP_TRAMPOLINE_PHYS  >> 12); // 0x8

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
static const u64 APIC_BASE_BSP_FLAG = (1ULL < 8);

// AP C entry, called from the trampoline's 64-bit section
__PRIVILEGED_CODE void ap_entry(u64 logical_id) {
    ap_startup_data_t* data = (ap_startup_data_t*)AP_STARTUP_DATA_PHYS;
    u32 cpu_id = (u32)logical_id;

    // claim before touching the per-CPU area or running any init, so a CPU the
    // BSP already gave up on parks instead of using memory it no longer owns.
    cpu_info_t* info = smp_get_cpu_info(cpu_id);
}