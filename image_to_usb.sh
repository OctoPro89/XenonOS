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

# Copy EFI binary
sudo cp build/BOOTX64.EFI /mnt/usb/EFI/BOOT/
sudo cp build/kernel.elf /mnt/usb/

# Unmount
sudo umount /mnt/usb