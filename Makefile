EFI_INCLUDE = /usr/include/efi
EFI_ARCH = x86_64

BOOTCFLAGS = -I$(EFI_INCLUDE) -I$(EFI_INCLUDE)/$(EFI_ARCH) \
         -ffreestanding \
         -fshort-wchar \
         -mno-red-zone \
         -fno-stack-protector \
		 -Wall

KERNCFLAGS = -I$(EFI_INCLUDE) -I$(EFI_INCLUDE)/$(EFI_ARCH) \
         -ffreestanding \
         -fno-stack-protector -fstrict-aliasing -fno-omit-frame-pointer \
         -fshort-wchar \
         -mno-red-zone \
         -c -g -O3 -D XENONOS_DEBUG=1 \
		 -D XENON_KERNEL -D KERNEL_LOG_DEBUG -D KERNEL_LOG_INFO \
 		 -D KERNEL_LOG_WARN -D KERNEL_LOG_ERROR \
		 -Werror=return-type \
		 -Werror=incompatible-pointer-types \
		 -Wall

USERCFLAGS = -ffreestanding \
         -fno-stack-protector \
         -fshort-wchar \
         -mno-red-zone -fstrict-aliasing -fno-omit-frame-pointer \
		 -I"kernel/xlibc" \
		 -I"user/" -I"kernel/" \
         -c -g -O3 -D XENONOS_DEBUG=1 \
		 -Werror=return-type \
		 -Werror=incompatible-pointer-types \
		 -Wall

ASMFLAGS = -f elf64 -g -F dwarf

# --- Source discovery ---
KERNEL_C_SRCS := $(shell find kernel -name '*.c')
KERNEL_ASM_SRCS := $(shell find kernel -name '*.asm')

KERNEL_C_OBJS := $(patsubst kernel/%.c, build/%_c.o, $(KERNEL_C_SRCS))
KERNEL_ASM_OBJS := $(patsubst kernel/%.asm, build/%_asm.o, $(KERNEL_ASM_SRCS))

LIBC_C_SRCS := $(shell find user/libc -name '*.c')
LIBC_C_OBJS := $(patsubst user/libc/%.c, build/user/libc/%_c.o, $(LIBC_C_SRCS))

LIBWINDOW_C_SRCS := $(shell find user/libwindow -name '*.c')
LIBWINDOW_C_OBJS := $(patsubst user/libwindow/%.c, build/user/libwindow/%_c.o, $(LIBWINDOW_C_SRCS))

DOOM_C_SRCS := $(shell find user/apps/doom/ -name '*.c')
DOOM_C_OBJS := $(patsubst user/apps/doom/%.c, build/user/apps/doomobjs/%.o, $(DOOM_C_SRCS))

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
	clang $(KERNCFLAGS) $< -o $@ -I"kernel/" -I"user/"

# Assemble ASM files
build/%_asm.o: kernel/%.asm
	mkdir -p $(dir $@)
	nasm $(ASMFLAGS) $< -o $@

# Link kernel
build/kernel.elf: $(KERNEL_OBJS)
	ld.lld -T kernel/linker.ld $(KERNEL_OBJS) -o $@

# user libc
build/user/libc/%_c.o: user/libc/%.c
	mkdir -p $(dir $@)
	clang $(USERCFLAGS) -c $< -o $@

build/user/libc/libc.a: $(LIBC_C_OBJS)
	mkdir -p $(dir $@)
	ar rcs $@ $^

# user libwindow
build/user/libwindow/%_c.o: user/libwindow/%.c
	mkdir -p $(dir $@)
	clang $(USERCFLAGS) -c $< -o $@

build/user/libwindow/libwindow.a: $(LIBWINDOW_C_OBJS)
	mkdir -p $(dir $@)
	ar rcs $@ $^

build/user/start_c.o: user/start.c
	mkdir -p $(dir $@)
	clang $(USERCFLAGS) $< -o $@ -I"kernel/" -I"user/"

build/user/start_asm.o: user/start.asm
	mkdir -p $(dir $@)
	nasm $(ASMFLAGS) $< -o $@

# xeterm
build/user/apps/xeterm.o: user/apps/xeterm/xeterm.c
	mkdir -p $(dir $@)
	clang $(USERCFLAGS) $< -o $@ -I"kernel/" -I"user/"

build/user/apps/sh: \
	build/user/start_c.o \
	build/user/start_asm.o \
	build/user/apps/xeterm.o \
	build/user/libc/libc.a \
	build/user/libwindow/libwindow.a
	ld.lld -T user/user.ld \
		build/user/apps/xeterm.o \
		build/user/start_c.o \
		build/user/start_asm.o \
		build/user/libc/libc.a \
		build/user/libwindow/libwindow.a \
		-o $@

# ls
build/user/apps/ls.o: user/apps/ls/ls.c
	mkdir -p $(dir $@)
	clang $(USERCFLAGS) $< -o $@ -I"kernel/" -I"user/"

build/user/apps/ls: \
	build/user/start_c.o \
	build/user/start_asm.o \
	build/user/apps/ls.o \
	build/user/libc/libc.a
	ld.lld -T user/user.ld \
		build/user/apps/ls.o \
		build/user/start_c.o \
		build/user/start_asm.o \
		build/user/libc/libc.a \
		-o $@

# cat
build/user/apps/cat.o: user/apps/cat/cat.c
	mkdir -p $(dir $@)
	clang $(USERCFLAGS) $< -o $@ -I"kernel/" -I"user/"

build/user/apps/cat: \
	build/user/start_c.o \
	build/user/start_asm.o \
	build/user/apps/cat.o \
	build/user/libc/libc.a
	ld.lld -T user/user.ld \
		build/user/apps/cat.o \
		build/user/start_c.o \
		build/user/start_asm.o \
		build/user/libc/libc.a \
		-o $@

# touch
build/user/apps/touch.o: user/apps/touch/touch.c
	mkdir -p $(dir $@)
	clang $(USERCFLAGS) $< -o $@ -I"kernel/" -I"user/"

build/user/apps/touch: \
	build/user/start_c.o \
	build/user/start_asm.o \
	build/user/apps/touch.o \
	build/user/libc/libc.a
	ld.lld -T user/user.ld \
		build/user/apps/touch.o \
		build/user/start_c.o \
		build/user/start_asm.o \
		build/user/libc/libc.a \
		-o $@

# lscpu
build/user/apps/lscpu.o: user/apps/lscpu/lscpu.c
	mkdir -p $(dir $@)
	clang $(USERCFLAGS) $< -o $@ -I"kernel/" -I"user/"

build/user/apps/lscpu: \
	build/user/start_c.o \
	build/user/start_asm.o \
	build/user/apps/lscpu.o \
	build/user/libc/libc.a
	ld.lld -T user/user.ld \
		build/user/apps/lscpu.o \
		build/user/start_c.o \
		build/user/start_asm.o \
		build/user/libc/libc.a \
		-o $@

# wintest
build/user/apps/wintest.o: user/apps/wintest/wintest.c
	mkdir -p $(dir $@)
	clang $(USERCFLAGS) $< -o $@ -I"kernel/" -I"user/"

build/user/apps/wintest: \
	build/user/start_c.o \
	build/user/start_asm.o \
	build/user/apps/wintest.o \
	build/user/libc/libc.a
	ld.lld -T user/user.ld \
		build/user/apps/wintest.o \
		build/user/start_c.o \
		build/user/start_asm.o \
		build/user/libc/libc.a \
		build/user/libwindow/libwindow.a \
		-o $@

# heaptest
build/user/apps/heaptest.o: user/apps/heaptest/heaptest.c
	mkdir -p $(dir $@)
	clang $(USERCFLAGS) $< -o $@ -I"kernel/" -I"user/"

build/user/apps/heaptest: \
	build/user/start_c.o \
	build/user/start_asm.o \
	build/user/apps/heaptest.o \
	build/user/libc/libc.a
	ld.lld -T user/user.ld \
		build/user/apps/heaptest.o \
		build/user/start_c.o \
		build/user/start_asm.o \
		build/user/libc/libc.a \
		-o $@

# fstest
build/user/apps/fstest.o: user/apps/fstest/fstest.c
	mkdir -p $(dir $@)
	clang $(USERCFLAGS) $< -o $@ -I"kernel/" -I"user/"

build/user/apps/fstest: \
	build/user/start_c.o \
	build/user/start_asm.o \
	build/user/apps/fstest.o \
	build/user/libc/libc.a
	ld.lld -T user/user.ld \
		build/user/apps/fstest.o \
		build/user/start_c.o \
		build/user/start_asm.o \
		build/user/libc/libc.a \
		-o $@
	
# crashtest
build/user/apps/crashtest.o: user/apps/crashtest/crashtest.c
	mkdir -p $(dir $@)
	clang $(USERCFLAGS) $< -o $@ -I"kernel/" -I"user/"

build/user/apps/crashtest: \
	build/user/start_c.o \
	build/user/start_asm.o \
	build/user/apps/crashtest.o \
	build/user/libc/libc.a
	ld.lld -T user/user.ld \
		build/user/apps/crashtest.o \
		build/user/start_c.o \
		build/user/start_asm.o \
		build/user/libc/libc.a \
		-o $@

# de_setvar
build/user/apps/de_setvar.o: user/apps/de_setvar/de_setvar.c
	mkdir -p $(dir $@)
	clang $(USERCFLAGS) $< -o $@ -I"kernel/" -I"user/"

build/user/apps/de_setvar: \
	build/user/start_c.o \
	build/user/start_asm.o \
	build/user/apps/de_setvar.o \
	build/user/libc/libc.a
	ld.lld -T user/user.ld \
		build/user/apps/de_setvar.o \
		build/user/start_c.o \
		build/user/start_asm.o \
		build/user/libc/libc.a \
		-o $@

# hwmon
build/user/apps/hwmon.o: user/apps/hwmon/hwmon.c
	mkdir -p $(dir $@)
	clang $(USERCFLAGS) $< -o $@ -I"kernel/" -I"user/"

build/user/apps/hwmon: \
	build/user/start_c.o \
	build/user/start_asm.o \
	build/user/apps/hwmon.o \
	build/user/libc/libc.a
	ld.lld -T user/user.ld \
		build/user/apps/hwmon.o \
		build/user/start_c.o \
		build/user/start_asm.o \
		build/user/libc/libc.a \
		build/user/libwindow/libwindow.a \
		-o $@

# doom
build/user/apps/doomobjs/%.o: user/apps/doom/%.c
	mkdir -p $(dir $@)
	clang $(USERCFLAGS) -I"kernel/" -I"user/" -I"kernel/xlibc/" $< -o $@

build/user/apps/doom: \
    build/user/start_c.o \
    build/user/start_asm.o \
    $(DOOM_C_OBJS) \
    build/user/libc/libc.a \
	build/user/libwindow/libwindow.a
	ld.lld -T user/user.ld \
	    build/user/start_c.o \
	    build/user/start_asm.o \
	    $(DOOM_C_OBJS) \
	    build/user/libc/libc.a \
		build/user/libwindow/libwindow.a \
	    -o $@

userapps: build/user/apps/sh build/user/apps/ls build/user/apps/lscpu \
		  build/user/apps/doom build/user/apps/wintest build/user/apps/heaptest \
		  build/user/apps/fstest build/user/apps/crashtest build/user/apps/de_setvar \
		  build/user/apps/hwmon build/user/apps/cat build/user/apps/touch

# --- Image ---
image: build/BOOTX64.EFI build/kernel.elf
	mkdir -p image/EFI/BOOT
	cp build/BOOTX64.EFI image/EFI/BOOT/
	cp build/kernel.elf image/kernel.elf

# --- Run ---
run: image userapps
	# Create empty 64 MB raw image
	dd if=/dev/zero of=image/disk.img bs=1M count=64

	sudo parted image/disk.img --script \
    mklabel gpt \
    mkpart ESP fat32 1MiB 100% \
    set 1 esp on

	sudo losetup -Pf image/disk.img

	# Format it as FAT32
	sudo mkfs.fat -F 32 /dev/loop0p1

	# Mount the image to copy files
	sudo mount /dev/loop0p1 /mnt
	sudo mkdir -p /mnt/EFI/BOOT
	sudo mkdir -p /mnt/bin
	sudo cp build/BOOTX64.EFI /mnt/EFI/BOOT/
	sudo cp build/kernel.elf /mnt/
	sudo cp testlongfilename.txt /mnt/
	sudo cp build/user/apps/sh /mnt/bin/
	sudo cp build/user/apps/ls /mnt/bin/
	sudo cp build/user/apps/lscpu /mnt/bin/
	sudo cp build/user/apps/wintest /mnt/bin/
	sudo cp build/user/apps/heaptest /mnt/bin/
	sudo cp build/user/apps/fstest /mnt/bin/
	sudo cp build/user/apps/crashtest /mnt/bin/
	sudo cp build/user/apps/de_setvar /mnt/bin/
	sudo cp build/user/apps/doom /mnt/bin/
	sudo cp hello /mnt/bin/
	sudo umount /mnt

	sudo losetup -d /dev/loop0

	# USB drive for testing
	dd if=/dev/zero of=image/usb.img bs=1M count=64

	sudo parted image/usb.img --script \
    mklabel gpt \
    mkpart ESP fat32 1MiB 100% \
    set 1 esp on

	sudo losetup -Pf image/usb.img

	# Format it as FAT32
	sudo mkfs.fat -F 32 /dev/loop0p1

	# Mount the image to copy files
	sudo mount /dev/loop0p1 /mnt
	sudo mkdir -p /mnt/EFI/BOOT
	sudo mkdir -p /mnt/bin
	sudo cp build/BOOTX64.EFI /mnt/EFI/BOOT/
	sudo cp build/kernel.elf /mnt/
	sudo cp testlongfilename.txt /mnt/
	sudo cp build/user/apps/sh /mnt/bin/
	sudo cp build/user/apps/ls /mnt/bin/
	sudo cp build/user/apps/lscpu /mnt/bin/
	sudo cp build/user/apps/heaptest /mnt/bin/
	sudo cp build/user/apps/wintest /mnt/bin/
	sudo cp build/user/apps/fstest /mnt/bin/
	sudo cp build/user/apps/crashtest /mnt/bin/
	sudo cp build/user/apps/de_setvar /mnt/bin/
	sudo cp build/user/apps/doom /mnt/bin/
	sudo cp hello /mnt/bin/
	sudo umount /mnt

	sudo losetup -d /dev/loop0

	qemu-system-x86_64 \
    -drive if=none,id=disk0,format=raw,file=image/disk.img \
	-drive if=none,id=stick,format=raw,file=image/usb.img \
    -device ahci,id=ahci \
    -device ide-hd,bus=ahci.0,drive=disk0 \
	-device qemu-xhci,p3=2,id=xhci \
	-device usb-kbd,bus=xhci.0 \
	-device usb-mouse,bus=xhci.0 \
	-device usb-storage,bus=xhci.0,drive=stick \
    -bios /usr/share/ovmf/OVMF.fd \
    -boot order=c \
    -serial stdio \
 	-S -gdb tcp::1234 \
	-no-reboot \
 	-no-shutdown \
	-m 1024M
# 	-trace usb_xhci_* -D xhci.log
#   	-d int

# 	qemu-system-x86_64 \
# 	-drive format=raw,file=fat:rw:image \
# 	-bios /usr/share/ovmf/OVMF_CODE.fd \
# 	-serial stdio

# 	-S -gdb tcp::1234 \
#	-no-reboot \
# 	-no-shutdown \
#  	-d int

# --- Clean ---
clean:
	rm -rf build image