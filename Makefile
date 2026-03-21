EFI_INCLUDE = /usr/include/efi
EFI_ARCH = x86_64

BOOTCFLAGS = -I$(EFI_INCLUDE) -I$(EFI_INCLUDE)/$(EFI_ARCH) \
         -ffreestanding \
         -fshort-wchar \
         -mno-red-zone \
         -fno-stack-protector

KERNCFLAGS = -I$(EFI_INCLUDE) -I$(EFI_INCLUDE)/$(EFI_ARCH) -ffreestanding -fno-stack-protector -fshort-wchar -mno-red-zone -c

build/bootloader.o: bootloader/main.c
	mkdir -p build
	clang $(BOOTCFLAGS) -target x86_64-unknown-windows -c bootloader/main.c -o build/bootloader.o

build/BOOTX64.EFI: build/bootloader.o
	lld-link \
	/subsystem:efi_application \
	/entry:efi_main \
	build/bootloader.o \
	/out:build/BOOTX64.EFI

build/kernel.o: kernel/kernel.asm
	nasm -f elf64 kernel/kernel.asm -o build/kernel.o
# 	clang $(KERNCFLAGS) kernel/kernel.c -o build/kernel.o

build/kernel.elf: build/kernel.o
	ld.lld -T kernel/linker.ld build/kernel.o -o build/kernel.elf

image: build/BOOTX64.EFI build/kernel.elf
	mkdir -p image/EFI/BOOT
	cp build/BOOTX64.EFI image/EFI/BOOT/
	cp build/kernel.elf image/kernel.elf

run: image
	qemu-system-x86_64 \
	-drive format=raw,file=fat:rw:image \
	-bios /usr/share/OVMF/OVMF_CODE.fd \
	-serial stdio

clean:
	rm -rf build image