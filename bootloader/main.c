#include <efi.h>
#include <efilib.h>
#include "../shared/boot_info.h"

#include <stdint.h>

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

    // Load program headers
    for (Elf64_Half i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr phdr;
        UINTN size = sizeof(phdr);
        KernelFile->SetPosition(KernelFile, ehdr.e_phoff + i * sizeof(phdr));
        KernelFile->Read(KernelFile, &size, &phdr);

        if (phdr.p_type != PT_LOAD) continue;

        // Allocate pages for segment
        UINTN num_pages = (phdr.p_memsz + 0xFFF) / 0x1000;
        void *segment;
        EFI_PHYSICAL_ADDRESS segment_addr = phdr.p_vaddr;
        SystemTable->BootServices->AllocatePages(
            AllocateAddress,
            EfiLoaderData,
            num_pages,
            &segment_addr
        );
        segment = (void*)segment_addr;

        // Read segment data from file
        UINTN seg_size = phdr.p_filesz;
        KernelFile->SetPosition(KernelFile, phdr.p_offset);
        status = KernelFile->Read(KernelFile, &seg_size, segment);
        if (EFI_ERROR(status) || seg_size != phdr.p_filesz) {
            SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to read segment!\r\n");
            while(1);
        }

        // Zero memory for bss
        if (phdr.p_memsz > phdr.p_filesz) {
            uint8_t *bss = (uint8_t*)segment + phdr.p_filesz;
            for (UINTN j = 0; j < phdr.p_memsz - phdr.p_filesz; j++)
                bss[j] = 0;
        }
    }

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Kernel loaded!\r\n");

    // GOP
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    status = SystemTable->BootServices->LocateProtocol(&gopGuid, NULL, (void**)&gop);
    if (status != EFI_SUCCESS || !gop) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"GOP not found!\r\n");
        while(1);
    }

    print_gop_info(SystemTable, gop);

    Framebuffer fb;
    fb.BaseAddress = (void*)gop->Mode->FrameBufferBase;
    fb.BufferSize = gop->Mode->FrameBufferSize;
    fb.Width = gop->Mode->Info->HorizontalResolution;
    fb.Height = gop->Mode->Info->VerticalResolution;
    fb.PixelsPerScanLine = gop->Mode->Info->PixelsPerScanLine;

    // Setup paging
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;

    EFI_PHYSICAL_ADDRESS addr;

    // PML4
    addr = 0;
    SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &addr);
    pml4 = (uint64_t*)addr;

    // PDPT
    addr = 0;
    SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &addr);
    pdpt = (uint64_t*)addr;

    // PD
    addr = 0;
    SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &addr);
    pd = (uint64_t*)addr;

    for (int i = 0; i < 512; i++) {
        pml4[i] = 0;
        pdpt[i] = 0;
        pd[i] = 0;
    }

    pml4[0] = ((uint64_t)pdpt) | PAGE_PRESENT | PAGE_WRITABLE;
    pdpt[0] = ((uint64_t)pd)   | PAGE_PRESENT | PAGE_WRITABLE;

    for (int i = 0; i < 512; i++) {
        uint64_t phys = (uint64_t)i * 0x200000; // 2MB
        pd[i] = phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_PS;
    }

    uint64_t pml4_phys = (uint64_t)pml4;

    UINTN mapSize = 0;
    UINTN mapKey;
    UINTN descSize;
    UINT32 descVersion;

    EFI_MEMORY_DESCRIPTOR *memMap = NULL;

    // First call to get size
    status = SystemTable->BootServices->GetMemoryMap(
        &mapSize, NULL, &mapKey, &descSize, &descVersion
    );

    if (status != EFI_BUFFER_TOO_SMALL) {
        // error
    }

    // Add slack BEFORE allocation
    mapSize += descSize * 2;

    // Allocate ONCE
    SystemTable->BootServices->AllocatePool(
        EfiLoaderData, mapSize, (void**)&memMap
    );

    // Now retry UNTIL success (no realloc!)
    while (1) {
        status = SystemTable->BootServices->GetMemoryMap(
            &mapSize, memMap, &mapKey, &descSize, &descVersion
        );

        if (status == EFI_SUCCESS) break;

        if (status != EFI_BUFFER_TOO_SMALL) {
            SystemTable->ConOut->OutputString(SystemTable->ConOut, L"GetMemoryMap failed!\r\n");
            while(1);
        }

        // If still too small, increase size BUT reuse buffer carefully
        mapSize += descSize * 2;
    }

    // uint32_t *pixel = (uint32_t*)fb.BaseAddress;
    // for (uint32_t y = 0; y < fb.Height; y++) {
    //     for (uint32_t x = 0; x < fb.Width; x++) {
    //         pixel[y * fb.PixelsPerScanLine + x] = 0x00FFFF00; // yellow
    //     }
    // }

    BootInfo bootInfo;
    bootInfo.fb = fb;
    bootInfo.MemoryMap = (uint64_t)memMap;
    bootInfo.MemoryMapSize = mapSize;
    bootInfo.MemoryDescriptorSize = descSize;

    // Exit boot services
    status = SystemTable->BootServices->ExitBootServices(ImageHandle, mapKey);
    if (EFI_ERROR(status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Failed to exit boot services!\r\n");
        while(1);
    }
    
    // pixel = (uint32_t*)fb.BaseAddress;
    // for (uint32_t y = 0; y < fb.Height; y++) {
    //     for (uint32_t x = 0; x < fb.Width; x++) {
    //         pixel[y * fb.PixelsPerScanLine + x] = 0x0000FF00; // green
    //     }
    // }

    // Jump to kernel, passing framebuffer pointer
    typedef void (*kernel_entry_t)(BootInfo*);
    kernel_entry_t kernel = (kernel_entry_t)ehdr.e_entry;

    asm volatile (
        "mov %0, %%cr3"
        :
        : "r"(pml4_phys)
        : "memory"
    );

    asm volatile (
        "mov %0, %%rdi\n"
        "jmp *%1\n"
        :
        : "r"(&bootInfo), "r"(kernel)
        : "rdi"
    );

    while(1);

    return EFI_SUCCESS;
}