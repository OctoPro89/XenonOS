# Create GPT
sudo parted $1 mklabel gpt

# Create EFI partition
sudo parted $1 mkpart ESP fat32 1MiB 100%
sudo parted $1 set 1 esp on

sudo partprobe "$1"
sleep 1

# Format as FAT32
sudo mkfs.fat -F32 "${1}1"

# Mount
sudo mkdir -p /mnt/usb
sudo mount "${1}1" /mnt/usb

# Create directories
sudo mkdir -p /mnt/usb/EFI/BOOT
sudo mkdir -p /mnt/usb/bin

# Copy EFI binary
sudo cp build/BOOTX64.EFI /mnt/usb/EFI/BOOT/
sudo cp build/kernel.elf /mnt/usb/

# Copy extra files
sudo cp testlongfilename.txt /mnt/usb/
sudo cp build/user/apps/sh /mnt/usb/bin/
sudo cp build/user/apps/doom /mnt/usb/bin/
sudo cp build/user/apps/ls /mnt/usb/bin/
sudo cp build/user/apps/lscpu /mnt/usb/bin/
sudo cp build/user/apps/cat /mnt/usb/bin/
sudo cp build/user/apps/heaptest /mnt/usb/bin/
sudo cp build/user/apps/wintest /mnt/usb/bin/
sudo cp build/user/apps/fstest /mnt/usb/bin/
sudo cp build/user/apps/crashtest /mnt/usb/bin/
sudo cp build/user/apps/de_setvar /mnt/usb/bin/
sudo cp build/user/apps/hwmon /mnt/usb/bin/
sudo cp hello /mnt/usb/bin/

# Unmount
sudo umount /mnt/usb
