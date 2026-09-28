#include <efi.h>
#include <efilib.h>
#include "../shared/boot_info.h"

#include <stdint.h>

#define KERNEL_VMA 0xFFFFFFFF80000000ULL
#define KERNEL_LMA 0x00200000ULL

#define HHDM_OFFSET 0xFFFF800000000000ULL

#define PAGE_SIZE  0x1000ULL
#define PAGE_MASK  ~(PAGE_SIZE - 1)

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;

#define PT_LOAD 1

typedef struct {
    unsigned char e_ident[16];
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off  e_phoff;
    Elf64_Off  e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;
    Elf64_Half e_phentsize;
    Elf64_Half e_phnum;
    Elf64_Half e_shentsize;
    Elf64_Half e_shnum;
    Elf64_Half e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    Elf64_Word p_type;
    Elf64_Word p_flags;
    Elf64_Off  p_offset;
    Elf64_Addr p_vaddr;
    Elf64_Addr p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
} Elf64_Phdr;

EFI_GUID gEfiLoadedImageProtocolGuid = 
    {0x5B1B31A1,0x9562,0x11d2,{0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B}};

EFI_GUID gEfiSimpleFileSystemProtocolGuid =
    {0x0964e5b22,0x6459,0x11d2,{0x8E,0x39,0x00,0xA0,0xC9,0x69,0x72,0x3B}};

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_PS       (1ULL << 7)

static __attribute__((noreturn))
void reboot(void)
{
    // Disable interrupts.
    asm volatile ("cli");

    // Wait until the 8042 input buffer is empty.
    for (uint32_t timeout = 0; timeout < 1000000; ++timeout) {
        uint8_t status;

        asm volatile (
            "inb $0x64, %0"
            : "=a"(status)
        );

        if ((status & 0x02) == 0) {
            // 0xFE = pulse CPU reset line.
            asm volatile (
                "outb %0, $0x64"
                :
                : "a"((uint8_t)0xFE)
            );
            break;
        }
    }

    // If the reset command didn't work, don't continue executing.
    for (;;) {
        asm volatile ("hlt");
    }
}

static int guid_equal(EFI_GUID* a, EFI_GUID* b) {
    if (a->Data1 != b->Data1) return 0;
    if (a->Data2 != b->Data2) return 0;
    if (a->Data3 != b->Data3) return 0;

    for (int i = 0; i < 8; ++i) {
        if (a->Data4[i] != b->Data4[i]) {
            return 0;
        }
    }

    return 1;
}

void print_hex(EFI_SYSTEM_TABLE *st, uint64_t value) {
    CHAR16 hex[18]; // "0x" + 16 hex digits
    hex[0] = L'0';
    hex[1] = L'x';
    for (int i = 2; i < 18; i++) hex[i] = L'0'; // manually initialize
    hex[17] = L'\0'; // null terminator

    for (int i = 0; i < 16; i++) {
        uint8_t nibble = (value >> (60 - i*4)) & 0xF;
        if (nibble < 10)
            hex[2+i] = L'0' + nibble;
        else
            hex[2+i] = L'A' + (nibble - 10);
    }
    st->ConOut->OutputString(st->ConOut, hex);
}

void print_dec(EFI_SYSTEM_TABLE *st, uint64_t value) {
    CHAR16 buf[21];
    int i = 20;
    buf[i--] = L'\0';
    if (value == 0) buf[i--] = L'0';
    while (value > 0 && i >= 0) {
        buf[i--] = L'0' + (value % 10);
        value /= 10;
    }
    st->ConOut->OutputString(st->ConOut, &buf[i+1]);
}

static uint64_t align_down_page(uint64_t value)
{
    return value & PAGE_MASK;
}

static uint64_t align_up_page(uint64_t value)
{
    return (value + PAGE_SIZE - 1) & PAGE_MASK;
}

static void print_efi_error(EFI_SYSTEM_TABLE *st, const CHAR16 *message, EFI_STATUS status)
{
    st->ConOut->OutputString(st->ConOut, message);
    st->ConOut->OutputString(st->ConOut, L" Status: ");
    print_hex(st, status);
    st->ConOut->OutputString(st->ConOut, L"\r\n");
}

void print_gop_info(EFI_SYSTEM_TABLE *st, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop) {
    st->ConOut->OutputString(st->ConOut, L"GOP info:\r\nWidth: ");
    print_dec(st, gop->Mode->Info->HorizontalResolution);
    st->ConOut->OutputString(st->ConOut, L"\r\nHeight: ");
    print_dec(st, gop->Mode->Info->VerticalResolution);
    st->ConOut->OutputString(st->ConOut, L"\r\nStride: ");
    print_dec(st, gop->Mode->Info->PixelsPerScanLine);
    st->ConOut->OutputString(st->ConOut, L"\r\nFB base: ");
    print_hex(st, gop->Mode->FrameBufferBase);
    st->ConOut->OutputString(st->ConOut, L"\r\nFB size: ");
    print_hex(st, gop->Mode->FrameBufferSize);
    st->ConOut->OutputString(st->ConOut, L"\r\n");
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_LOADED_IMAGE *LoadedImage;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    EFI_FILE_PROTOCOL *Root;
    EFI_FILE_PROTOCOL *KernelFile;

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"XenonOS Bootloader\r\n");

    SystemTable->BootServices->HandleProtocol(
        ImageHandle,
        &gEfiLoadedImageProtocolGuid,
        (void**)&LoadedImage
    );

    SystemTable->BootServices->HandleProtocol(
        LoadedImage->DeviceHandle,
        &gEfiSimpleFileSystemProtocolGuid,
        (void**)&FileSystem
    );

    FileSystem->OpenVolume(FileSystem, &Root);

    EFI_STATUS status = Root->Open(
        Root,
        &KernelFile,
        L"kernel.elf",
        EFI_FILE_MODE_READ,
        0
    );

    if (status != EFI_SUCCESS) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Kernel missing!\r\n");
        while(1);
    }

    // Read ELF header
    Elf64_Ehdr ehdr;
    UINTN read_size = sizeof(ehdr);
    KernelFile->Read(KernelFile, &read_size, &ehdr);

    // Verify ELF magic
    if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Invalid ELF!\r\n");
        while(1);
    }

        /*
     * -------------------------------------------------------------------------
     * Load ELF PT_LOAD segments
     *
     * ELF PT_LOAD segments are allowed to overlap at page granularity.
     *
     * Therefore we cannot AllocatePages() independently for every segment.
     * Instead:
     *
     *   1. Find the complete physical range occupied by the kernel.
     *   2. Allocate that range once.
     *   3. Zero it.
     *   4. Load each PT_LOAD into its specified p_paddr.
     * -------------------------------------------------------------------------
     */

    uint64_t kernel_phys_start = UINT64_MAX;
    uint64_t kernel_phys_end = 0;
    UINTN load_segment_count = 0;

    for (Elf64_Half i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr phdr;

        UINTN size = sizeof(phdr);

        status = KernelFile->SetPosition(
            KernelFile,
            ehdr.e_phoff + ((UINT64)i * ehdr.e_phentsize)
        );

        if (EFI_ERROR(status)) {
            print_efi_error(
                SystemTable,
                L"Failed to seek to ELF program header.",
                status
            );
            while (1);
        }

        status = KernelFile->Read(
            KernelFile,
            &size,
            &phdr
        );

        if (EFI_ERROR(status) || size != sizeof(phdr)) {
            SystemTable->ConOut->OutputString(
                SystemTable->ConOut,
                L"Failed to read ELF program header!\r\n"
            );
            while (1);
        }

        if (phdr.p_type != PT_LOAD)
            continue;

        /*
         * Basic ELF sanity checks.
         */
        if (phdr.p_memsz < phdr.p_filesz) {
            SystemTable->ConOut->OutputString(
                SystemTable->ConOut,
                L"Invalid ELF: p_memsz < p_filesz!\r\n"
            );
            while (1);
        }

        if (phdr.p_offset > UINT64_MAX - phdr.p_filesz) {
            SystemTable->ConOut->OutputString(
                SystemTable->ConOut,
                L"Invalid ELF: segment file range overflows!\r\n"
            );
            while (1);
        }

        if (phdr.p_paddr > UINT64_MAX - phdr.p_memsz) {
            SystemTable->ConOut->OutputString(
                SystemTable->ConOut,
                L"Invalid ELF: segment physical range overflows!\r\n"
            );
            while (1);
        }

        /*
         * ELF requires p_vaddr and p_paddr to have the same page offset
         * when p_align is greater than one page.
         */
        if (phdr.p_align != 0 &&
            (phdr.p_vaddr % phdr.p_align) !=
            (phdr.p_paddr % phdr.p_align)) {

            SystemTable->ConOut->OutputString(
                SystemTable->ConOut,
                L"Invalid ELF: vaddr/paddr alignment mismatch!\r\n"
            );
            while (1);
        }

        uint64_t segment_start =
            align_down_page(phdr.p_paddr);

        uint64_t segment_end =
            align_up_page(phdr.p_paddr + phdr.p_memsz);

        if (segment_end < segment_start) {
            SystemTable->ConOut->OutputString(
                SystemTable->ConOut,
                L"Invalid ELF: segment address overflow!\r\n"
            );
            while (1);
        }

        if (segment_start < kernel_phys_start)
            kernel_phys_start = segment_start;

        if (segment_end > kernel_phys_end)
            kernel_phys_end = segment_end;

        load_segment_count++;
    }

    if (load_segment_count == 0) {
        SystemTable->ConOut->OutputString(
            SystemTable->ConOut,
            L"ELF contains no PT_LOAD segments!\r\n"
        );
        while (1);
    }

    uint64_t kernel_size =
        kernel_phys_end - kernel_phys_start;

    UINTN kernel_pages =
        (UINTN)(kernel_size / PAGE_SIZE);

    /*
     * For your current kernel layout, this should be:
     *
     *     kernel_phys_start = 0x00200000
     *
     * Do not silently move the kernel. The linker script and page tables
     * currently expect this physical address.
     */
    SystemTable->ConOut->OutputString(
        SystemTable->ConOut,
        L"Kernel physical start: "
    );
    print_hex(SystemTable, kernel_phys_start);

    SystemTable->ConOut->OutputString(
        SystemTable->ConOut,
        L"\r\nKernel physical end:   "
    );
    print_hex(SystemTable, kernel_phys_end);

    SystemTable->ConOut->OutputString(
        SystemTable->ConOut,
        L"\r\nKernel pages: "
    );
    print_dec(SystemTable, kernel_pages);

    SystemTable->ConOut->OutputString(
        SystemTable->ConOut,
        L"\r\n"
    );

    /*
     * Allocate the complete kernel image in one allocation.
     *
     * This is important because PT_LOAD segments can overlap pages.
     */
    EFI_PHYSICAL_ADDRESS kernel_phys = kernel_phys_start;

    status = SystemTable->BootServices->AllocatePages(
        AllocateAddress,
        EfiLoaderData,
        kernel_pages,
        &kernel_phys
    );

    if (EFI_ERROR(status)) {
        print_efi_error(
            SystemTable,
            L"Failed to allocate physical memory for kernel.",
            status
        );
        while (1);
    }

    /*
     * AllocateAddress should return exactly the address requested.
     */
    if (kernel_phys != kernel_phys_start) {
        SystemTable->ConOut->OutputString(
            SystemTable->ConOut,
            L"UEFI returned unexpected kernel address!\r\n"
        );
        while (1);
    }

    /*
     * Zero the entire kernel image.
     *
     * This handles .bss and also makes the handling of overlapping
     * PT_LOAD segments straightforward.
     */
    uint8_t *kernel_memory =
        (uint8_t *)(UINTN)kernel_phys_start;

    for (uint64_t i = 0; i < kernel_size; i++)
        kernel_memory[i] = 0;

    /*
     * Second pass: actually load every PT_LOAD segment.
     */
    for (Elf64_Half i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr phdr;

        UINTN size = sizeof(phdr);

        status = KernelFile->SetPosition(
            KernelFile,
            ehdr.e_phoff + ((UINT64)i * ehdr.e_phentsize)
        );

        if (EFI_ERROR(status)) {
            print_efi_error(
                SystemTable,
                L"Failed to seek to ELF program header.",
                status
            );
            while (1);
        }

        status = KernelFile->Read(
            KernelFile,
            &size,
            &phdr
        );

        if (EFI_ERROR(status) || size != sizeof(phdr)) {
            SystemTable->ConOut->OutputString(
                SystemTable->ConOut,
                L"Failed to read ELF program header!\r\n"
            );
            while (1);
        }

        if (phdr.p_type != PT_LOAD)
            continue;

        /*
         * Verify that this segment lies completely inside the
         * allocation we just made.
         */
        uint64_t segment_end =
            phdr.p_paddr + phdr.p_memsz;

        if (phdr.p_paddr < kernel_phys_start ||
            segment_end > kernel_phys_end) {

            SystemTable->ConOut->OutputString(
                SystemTable->ConOut,
                L"ELF segment lies outside kernel allocation!\r\n"
            );
            while (1);
        }

        /*
         * Physical destination specified by the ELF.
         */
        uint8_t *load_addr =
            (uint8_t *)(UINTN)phdr.p_paddr;

        SystemTable->ConOut->OutputString(
            SystemTable->ConOut,
            L"Loading segment: paddr="
        );
        print_hex(SystemTable, phdr.p_paddr);

        SystemTable->ConOut->OutputString(
            SystemTable->ConOut,
            L" filesz="
        );
        print_hex(SystemTable, phdr.p_filesz);

        SystemTable->ConOut->OutputString(
            SystemTable->ConOut,
            L" memsz="
        );
        print_hex(SystemTable, phdr.p_memsz);

        SystemTable->ConOut->OutputString(
            SystemTable->ConOut,
            L"\r\n"
        );

        /*
         * Copy the file-backed portion.
         *
         * The remainder of p_memsz was already zeroed above, which
         * gives us the correct BSS contents.
         */
        if (phdr.p_filesz != 0) {
            UINTN seg_size = (UINTN)phdr.p_filesz;

            status = KernelFile->SetPosition(
                KernelFile,
                phdr.p_offset
            );

            if (EFI_ERROR(status)) {
                print_efi_error(
                    SystemTable,
                    L"Failed to seek to ELF segment.",
                    status
                );
                while (1);
            }

            status = KernelFile->Read(
                KernelFile,
                &seg_size,
                load_addr
            );

            if (EFI_ERROR(status) ||
                seg_size != (UINTN)phdr.p_filesz) {

                print_efi_error(
                    SystemTable,
                    L"Failed to read ELF segment.",
                    status
                );
                while (1);
            }
        }
    }

    SystemTable->ConOut->OutputString(
        SystemTable->ConOut,
        L"Kernel loaded!\r\n"
    );


    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Kernel loaded!\r\n");

    // GOP
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    status = SystemTable->BootServices->LocateProtocol(&gopGuid, NULL, (void**)&gop);
    if (EFI_ERROR(status) || !gop) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"GOP not found!\r\n");
        while(1);
    }

    // Find the highest resolution 32-bit mode
    UINT32 best_mode = gop->Mode->Mode;
    UINT32 best_pixels = 0;
    for (UINT32 i = 0; i < gop->Mode->MaxMode; i++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info;
        UINTN size;
        status = gop->QueryMode(gop, i, &size, &info);
        if (EFI_ERROR(status)) continue;
        if (info->PixelFormat != PixelRedGreenBlueReserved8BitPerColor && info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor)
            continue;
        UINT32 pixels = info->HorizontalResolution * info->VerticalResolution;
        if (pixels == (1280 * 720)) {
            best_pixels = pixels;
            best_mode = i;
        }
    }

    // Set the mode
    status = gop->SetMode(gop, best_mode);
    if (EFI_ERROR(status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to set GOP mode!\r\n");
        while(1);
    }

    print_gop_info(SystemTable, gop);

    Framebuffer fb;
    fb.BaseAddress = (void*)gop->Mode->FrameBufferBase;
    fb.BufferSize = gop->Mode->FrameBufferSize;
    fb.Width = gop->Mode->Info->HorizontalResolution;
    fb.Height = gop->Mode->Info->VerticalResolution;
    fb.PixelsPerScanLine = gop->Mode->Info->PixelsPerScanLine;
    fb.PixelFormat = gop->Mode->Info->PixelFormat;

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Setting up paging\r\n");

    // Setup paging
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t **pd;

    EFI_PHYSICAL_ADDRESS addr;

    // PML4
    addr = 0;
    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &addr);
    if (EFI_ERROR(status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to allocate pages for PML4!\r\n");
        while(1);
    }
    pml4 = (uint64_t*)addr;

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Allocated pml4\r\n");

    // PDPT
    addr = 0;
    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &addr);
    if (EFI_ERROR(status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to allocate pages for PDPT!\r\n");
        while(1);
    }
    pdpt = (uint64_t*)addr;

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Allocated pdpt\r\n");

    // PD
    addr = 0;
    status = SystemTable->BootServices->AllocatePool(EfiLoaderData,sizeof(uint64_t*) * 8, (void**)&pd);
    if (EFI_ERROR(status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to allocate pages for PD!\r\n");
        while(1);
    }
    pd = (uint64_t**)addr;

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Allocated pd\r\n");

    for (int i = 0; i < 512; i++) {
        pml4[i] = 0;
        pdpt[i] = 0;
        pd[i] = 0;
    }

    pml4[0] = ((uint64_t)pdpt) | PAGE_PRESENT | PAGE_WRITABLE;
    pdpt[0] = ((uint64_t)pd)   | PAGE_PRESENT | PAGE_WRITABLE;

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Setting pd\r\n");

    // TODO: this is a bad way of doing stuff. Realistically should just map what we need.
    // The problem is if LoadedImage is outside of this range the kernel won't load :(
    for (int i = 0; i < 8; i++) {
        addr = 0;
        status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &addr);
        if (EFI_ERROR(status)) {
            SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to allocate pages for paging!\r\n");
            while(1);
        }
        pd[i] = (uint64_t*)addr;

        for (int j = 0; j < 512; j++) {
            uint64_t phys = (uint64_t)i * 0x40000000 + (uint64_t)j * 0x200000;
            pd[i][j] = phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_PS;
        }

        pdpt[i] = ((uint64_t)pd[i]) | PAGE_PRESENT | PAGE_WRITABLE;
    }

    uint64_t pml4_index = (KERNEL_VMA >> 39) & 0x1FF;
    uint64_t pdpt_index = (KERNEL_VMA >> 30) & 0x1FF;

    pml4[pml4_index] = ((uint64_t)pdpt) | PAGE_PRESENT | PAGE_WRITABLE;

    // allocate PD for kernel
    addr = 0;
    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &addr);
    if (EFI_ERROR(status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to allocate pages for kernel PD!\r\n");
        while(1);
    }
    uint64_t* kernel_pd = (uint64_t*)addr;

    // map first ~1GB of phys at high half
    for (int j = 0; j < 512; j++) {
        uint64_t phys = KERNEL_LMA + j * 0x200000;
        kernel_pd[j] = phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_PS;
    }

    pdpt[pdpt_index] = ((uint64_t)kernel_pd) | PAGE_PRESENT | PAGE_WRITABLE;

    uint64_t hhdm_index = (HHDM_OFFSET >> 39) & 0x1FF;

    // allocate new PDPT for HHDM
    addr = 0;
    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &addr);
    if (EFI_ERROR(status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to allocate pages for HHDM!\r\n");
        while(1);
    }
    uint64_t* hhdm_pdpt = (uint64_t*)addr;

    // zero it (IMPORTANT)
    for (int i = 0; i < 512; i++) hhdm_pdpt[i] = 0;

    // hook into PML4
    pml4[hhdm_index] = ((uint64_t)hhdm_pdpt) | PAGE_PRESENT | PAGE_WRITABLE;

    // map physical memory
    for (int i = 0; i < 512; i++) {
        addr = 0;
        status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &addr);
        if (EFI_ERROR(status)) {
            SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to allocate pages for physical memory pages!\r\n");
            while(1);
        }
        uint64_t* pd = (uint64_t*)addr;

        // zero PD
        for (int k = 0; k < 512; k++) pd[k] = 0;

        hhdm_pdpt[i] = ((uint64_t)pd) | PAGE_PRESENT | PAGE_WRITABLE;

        for (int j = 0; j < 512; j++) {
            uint64_t phys = (uint64_t)i * 0x40000000ULL + (uint64_t)j * 0x200000ULL;
            pd[j] = phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_PS;
        }
    }

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"pd set\r\n");

    uint64_t pml4_phys = (uint64_t)pml4;

    UINTN mapSize = 0;
    EFI_MEMORY_DESCRIPTOR *memMap = NULL;

    UINTN mapKey;
    UINTN descSize;
    UINT32 descVersion;

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Entry: ");
    print_hex(SystemTable, ehdr.e_entry);
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"\r\n");

    EFI_GUID acpi20 = ACPI_20_TABLE_GUID;
    EFI_GUID acpi10 = ACPI_TABLE_GUID;

    void* rsdp = NULL;

    for (UINTN i = 0; i < SystemTable->NumberOfTableEntries; ++i) {
        EFI_CONFIGURATION_TABLE* table =
            &SystemTable->ConfigurationTable[i];

        if (guid_equal(&table->VendorGuid, &acpi20)) {
            rsdp = table->VendorTable;
            break;
        }
    }

    // do this BEFORE getting memory map:
    EFI_PHYSICAL_ADDRESS boot_info_phys = 0xFFFFFFFF;
    status = SystemTable->BootServices->AllocatePages(
        AllocateMaxAddress,
        EfiLoaderData,
        1,
        &boot_info_phys
    );

    if (EFI_ERROR(status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to allocate pages for BootParams!\r\n");
        while (1);
    }

    if (boot_info_phys >= 0x100000000ULL) {
        SystemTable->ConOut->OutputString(
            SystemTable->ConOut,
            L"BootParams is outside identity map!\r\n"
        );
        while (1);
    }

    BootInfo* bootInfo = (BootInfo*)boot_info_phys;
    typedef void (*kernel_entry_t)(BootInfo*);
    kernel_entry_t kernel = ((void*)0);

    void *dummy;

    SystemTable->ConOut->OutputString(
        SystemTable->ConOut,
        L"LoadedImage Base: "
    );
    print_hex(SystemTable, (uint64_t)LoadedImage->ImageBase);

    SystemTable->ConOut->OutputString(
        SystemTable->ConOut,
        L"\r\nLoadedImage Size: "
    );
    print_hex(SystemTable, (uint64_t)LoadedImage->ImageSize);

    SystemTable->ConOut->OutputString(
        SystemTable->ConOut,
        L"\r\npml4: "
    );
    print_hex(SystemTable, (uint64_t)pml4);

    SystemTable->ConOut->OutputString(
        SystemTable->ConOut,
        L"\r\n"
    );

    uint64_t rsp;

    asm volatile (
        "mov %%rsp, %0"
        : "=r"(rsp)
    );

    SystemTable->ConOut->OutputString(
        SystemTable->ConOut,
        L"Current stack: "
    );
    print_hex(SystemTable, rsp);
    SystemTable->ConOut->OutputString(
        SystemTable->ConOut,
        L"\r\n"
    );

    // First call
    status = SystemTable->BootServices->GetMemoryMap(&mapSize, NULL, &mapKey, &descSize, &descVersion);
    if (status != EFI_BUFFER_TOO_SMALL) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to get memory map size!\r\n");
        while (1);
    }

    // Add slack
    mapSize += descSize * 8;

    while (1) {
        // Allocate fresh buffer every attempt
        if (memMap != NULL) {
            SystemTable->BootServices->FreePool(memMap);
        }

        status = SystemTable->BootServices->AllocatePool(EfiLoaderData, mapSize, (void**)&memMap);
        if (EFI_ERROR(status)) {
            SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to allocate memory map!\r\n");
            while (1);
        }

        UINTN newSize = mapSize;

        status = SystemTable->BootServices->GetMemoryMap(&newSize, memMap, &mapKey, &descSize, &descVersion);

        if (status == EFI_SUCCESS) {
            mapSize = newSize;
            break;
        }

        if (status != EFI_BUFFER_TOO_SMALL) {
            SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to get memory map!\r\n");
            while (1);
        }

        // Update size and retry
        mapSize = newSize + descSize * 8;
    }

    bootInfo->fb = fb;
    bootInfo->MemoryMap = (uint64_t)memMap;
    bootInfo->MemoryMapSize = mapSize;
    bootInfo->MemoryDescriptorSize = descSize;
    bootInfo->PML4 = pml4_phys;
    bootInfo->AcpiRsdp = (uint64_t)rsdp;

    // Exit boot services
    status = SystemTable->BootServices->ExitBootServices(ImageHandle, mapKey);
    if (EFI_ERROR(status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to exit boot services!\r\n");
        while(1);
    }
    
    // Jump to kernel, passing framebuffer pointer
    kernel = (kernel_entry_t)ehdr.e_entry;

    asm volatile (
        "mov %0, %%cr3"
        :
        : "r"(pml4_phys)
        : "memory"
    );

    asm volatile (
        "mov %0, %%cr3\n"
        :
        : "r"(pml4_phys)
        : "memory"
    );

    asm volatile (
        "mov %0, %%rdi\n"
        "jmp *%1\n"
        :
        : "r"(bootInfo), "r"(kernel)
        : "rdi"
    );

    while(1);

    return EFI_SUCCESS;
}