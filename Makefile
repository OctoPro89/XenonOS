EFI_INCLUDE = /usr/include/efi
EFI_ARCH = x86_64

BOOTCFLAGS = -I$(EFI_INCLUDE) -I$(EFI_INCLUDE)/$(EFI_ARCH) \
         -ffreestanding \
         -fshort-wchar \
         -mno-red-zone \
         -fno-stack-protector

KERNCFLAGS = -I$(EFI_INCLUDE) -I$(EFI_INCLUDE)/$(EFI_ARCH) \
         -ffreestanding \
         -fno-stack-protector \
         -fshort-wchar \
         -mno-red-zone \
         -c -g -O0

ASMFLAGS = -f elf64 -g -F dwarf

# --- Source discovery ---
KERNEL_C_SRCS := $(shell find kernel -name '*.c')
KERNEL_ASM_SRCS := $(shell find kernel -name '*.asm')

KERNEL_C_OBJS := $(patsubst kernel/%.c, build/%_c.o, $(KERNEL_C_SRCS))
KERNEL_ASM_OBJS := $(patsubst kernel/%.asm, build/%_asm.o, $(KERNEL_ASM_SRCS))

KERNEL_OBJS := $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)

# --- Bootloader ---
build/bootloader.o: bootloader/main.c
	mkdir -p build
	clang $(BOOTCFLAGS) -target x86_64-unknown-windows -c $< -o $@

build/BOOTX64.EFI: build/bootloader.o
	lld-link \
	/subsystem:efi_application \
	/entry:efi_main \
	$< \
	/out:$@

# --- Kernel build rules ---

# Compile C files
build/%_c.o: kernel/%.c
	mkdir -p $(dir $@)
	clang $(KERNCFLAGS) $< -o $@ -I"kernel/"

# Assemble ASM files
build/%_asm.o: kernel/%.asm
	mkdir -p $(dir $@)
	nasm $(ASMFLAGS) $< -o $@

# Link kernel
build/kernel.elf: $(KERNEL_OBJS)
	ld.lld -T kernel/linker.ld $(KERNEL_OBJS) -o $@

# --- Image ---
image: build/BOOTX64.EFI build/kernel.elf
	mkdir -p image/EFI/BOOT
	cp build/BOOTX64.EFI image/EFI/BOOT/
	cp build/kernel.elf image/kernel.elf

# --- Run ---
run: image
	qemu-system-x86_64 \
	-drive format=raw,file=fat:rw:image \
	-bios /usr/share/ovmf/OVMF_CODE.fd \
	-serial stdio

#  	-d int \
#  	-S -gdb tcp::1234 \
#  	-no-reboot
# 	-no-shutdown

# --- Clean ---
clean:
	rm -rf build image