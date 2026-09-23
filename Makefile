CLANG ?= clang
LLD ?= ld.lld
NASM ?= nasm
OBJCOPY ?= objcopy
PYTHON ?= python3

BUILD := build
ABI_INCLUDES := -Iabi/include
FREESTANDING := -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
	-Wall -Wextra -Werror
USER_UTILITIES := echo true false uname mkdir date pwd cat
USER_UTILITY_OBJECTS := $(USER_UTILITIES:%=$(BUILD)/userspace/bin/%.o)
USER_UTILITY_ELFS := $(USER_UTILITIES:%=$(BUILD)/userspace/bin/%.elf)

.PHONY: all bootloader kernel uefi-loader user-binaries live-iso test-user-elf test-image-qfs test-qfs2 test-exfat test-gpt test-drivers test-user-lib check test-architecture test-host abi qemu-smoke qemu-session-smoke qemu-iso-smoke qemu-uefi-smoke qemu-run qemu-window clean

all: bootloader kernel

user-binaries: $(USER_UTILITY_ELFS)

test-user-elf: user-binaries
	$(PYTHON) tests/test_user_elf.py

test-image-qfs: $(BUILD)/quantaos.img
	$(PYTHON) tests/test_image_qfs.py
test-qfs2:
	$(PYTHON) tests/test_qfs2.py
test-exfat:
	$(PYTHON) tests/test_exfat.py
test-gpt:
	$(PYTHON) tests/test_gpt.py
test-drivers: user-binaries
	$(PYTHON) tests/test_driver_capsule.py
test-user-lib: $(BUILD)/userspace/lib.o


bootloader: $(BUILD)/quantaos.img

live-iso: $(BUILD)/quantaos.iso

kernel: $(BUILD)/kernel/quanta.elf

uefi-loader: $(BUILD)/uefi/BOOTX64.EFI

$(BUILD)/userspace/%.bin: userspace/test/%.asm
	@mkdir -p $(dir $@)
	$(NASM) -f bin $< -o $@

$(BUILD)/userspace/client.o: userspace/session.c
	@mkdir -p $(dir $@)
	$(CLANG) --target=x86_64-elf -Oz $(FREESTANDING) $(ABI_INCLUDES) -Iuserspace/include -c $< -o $@

$(BUILD)/userspace/syscall.o: userspace/syscall.S
	@mkdir -p $(dir $@)
	$(CLANG) --target=x86_64-elf $(FREESTANDING) -c $< -o $@

$(BUILD)/userspace/lib.o: userspace/lib.c userspace/include/quanta/libc.h
	@mkdir -p $(dir $@)
	$(CLANG) --target=x86_64-elf -Oz $(FREESTANDING) $(ABI_INCLUDES) -Iuserspace/include -c $< -o $@

$(BUILD)/userspace/client.bin: $(BUILD)/userspace/client.o $(BUILD)/userspace/syscall.o userspace/user.ld
	$(LLD) -m elf_x86_64 -T userspace/user.ld -o $(BUILD)/userspace/client.elf $(filter %.o,$^)
	$(OBJCOPY) -O binary build/userspace/client.elf $@

$(BUILD)/userspace/bin/%.o: userspace/commands/%.c
	@mkdir -p $(dir $@)
	$(CLANG) --target=x86_64-elf -Oz $(FREESTANDING) $(ABI_INCLUDES) -Iuserspace/include -c $< -o $@

$(BUILD)/userspace/bin/%.elf: $(BUILD)/userspace/bin/%.o $(BUILD)/userspace/lib.o $(BUILD)/userspace/syscall.o userspace/user.ld
	$(LLD) -m elf_x86_64 -T userspace/user.ld -o $@ $(filter %.o,$^)

$(BUILD)/kernel/core/%.o: kernel/core/%.c $(BUILD)/userspace/client.bin $(BUILD)/userspace/server.bin $(BUILD)/userspace/fault.bin
	@mkdir -p $(dir $@)
	$(CLANG) --target=x86_64-elf $(FREESTANDING) $(ABI_INCLUDES) -Ikernel/mm/include -Ikernel/core/include -Ikernel/vfs/include -Ikernel/arch/x86_64/include -c $< -o $@

$(BUILD)/kernel/arch/x86_64/cpu.o: kernel/arch/x86_64/cpu.c
	@mkdir -p $(dir $@)
	$(CLANG) --target=x86_64-elf $(FREESTANDING) -c $< -o $@

$(BUILD)/kernel/arch/x86_64/%.o: kernel/arch/x86_64/%.c
	@mkdir -p $(dir $@)
	$(CLANG) --target=x86_64-elf $(FREESTANDING) $(ABI_INCLUDES) -Ikernel/arch/x86_64/include -c $< -o $@

$(BUILD)/kernel/arch/x86_64/%.o: kernel/arch/x86_64/%.S $(BUILD)/userspace/client.bin $(BUILD)/userspace/server.bin $(BUILD)/userspace/fault.bin
	@mkdir -p $(dir $@)
	$(CLANG) --target=x86_64-elf $(FREESTANDING) -c $< -o $@

$(BUILD)/kernel/mm/memory.o: kernel/mm/memory.c abi/include/quanta/boot_info.h
	@mkdir -p $(dir $@)
	$(CLANG) --target=x86_64-elf $(FREESTANDING) $(ABI_INCLUDES) -Ikernel/mm/include -c $< -o $@

$(BUILD)/kernel/vfs/%.o: kernel/vfs/%.c
	@mkdir -p $(dir $@)
	$(CLANG) --target=x86_64-elf $(FREESTANDING) $(ABI_INCLUDES) -Ikernel/vfs/include -Ikernel/arch/x86_64/include -c $< -o $@

KERNEL_OBJECTS := $(BUILD)/kernel/core/kernel_main.o $(BUILD)/kernel/core/task.o $(BUILD)/kernel/core/account.o $(BUILD)/kernel/core/elf.o $(BUILD)/kernel/core/qfs.o $(BUILD)/kernel/core/qfs2.o $(BUILD)/kernel/mm/memory.o $(BUILD)/kernel/vfs/ramfs.o $(BUILD)/kernel/vfs/storage.o $(BUILD)/kernel/vfs/mount.o $(BUILD)/kernel/arch/x86_64/cpu.o $(BUILD)/kernel/arch/x86_64/tables.o $(BUILD)/kernel/arch/x86_64/entry.o $(BUILD)/kernel/arch/x86_64/transitions.o $(BUILD)/kernel/arch/x86_64/user_image.o
$(BUILD)/kernel/quanta.elf: $(KERNEL_OBJECTS) kernel/linker/kernel.ld
	$(LLD) -m elf_x86_64 -z max-page-size=0x1000 -T kernel/linker/kernel.ld -o $@ $(filter %.o,$^)

$(BUILD)/live-root.img: $(USER_UTILITY_ELFS) tools/make_live_root.py tools/qfs2.py tools/account.py
	$(PYTHON) tools/make_live_root.py --utilities $(BUILD)/userspace/bin --output $@

$(BUILD)/uefi/loader.o: bootloader/uefi/loader.c abi/include/quanta/boot_info.h $(BUILD)/kernel/quanta.elf $(BUILD)/live-root.img
	@mkdir -p $(dir $@)
	$(CLANG) --target=x86_64-pc-windows-msvc -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin -Wall -Wextra -Werror $(ABI_INCLUDES) -c $< -o $@

$(BUILD)/uefi/kernel_blob.o: $(BUILD)/kernel/quanta.elf
	@mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O pe-x86-64 --binary-architecture i386:x86-64 $< $@

$(BUILD)/uefi/storage_blob.o: $(BUILD)/live-root.img
	@mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O pe-x86-64 --binary-architecture i386:x86-64 $< $@

$(BUILD)/uefi/BOOTX64.EFI: $(BUILD)/uefi/loader.o $(BUILD)/uefi/kernel_blob.o $(BUILD)/uefi/storage_blob.o
	@mkdir -p $(dir $@)
	$(LLD) -flavor link /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:x64 /out:$@ $(filter %.o,$^)

$(BUILD)/quantaos.img: $(BUILD)/kernel/quanta.elf $(USER_UTILITY_ELFS) bootloader/stage1/boot.asm bootloader/stage2/stage2.asm bootloader/stage2/include/constants.inc bootloader/include/boot_info.inc tools/make_image.py tools/account.py tools/qfs2.py
	$(PYTHON) tools/make_image.py --nasm $(NASM) --stage1 bootloader/stage1/boot.asm --stage2 bootloader/stage2/stage2.asm --boot-include bootloader/include --kernel $(BUILD)/kernel/quanta.elf --build $(BUILD)/bootloader --output $@

$(BUILD)/quantaos.iso: $(BUILD)/live-root.img $(BUILD)/uefi/BOOTX64.EFI bootloader/stage1/boot.asm bootloader/stage2/stage2.asm bootloader/stage2/include/constants.inc bootloader/include/boot_info.inc tools/make_iso.py
	$(PYTHON) tools/make_iso.py --nasm $(NASM) --xorriso xorriso --efi $(BUILD)/uefi/BOOTX64.EFI --stage1 bootloader/stage1/boot.asm --stage2 bootloader/stage2/stage2.asm --boot-include bootloader/include --kernel $(BUILD)/kernel/quanta.elf --storage $(BUILD)/live-root.img --build $(BUILD)/bootloader --output $@

check: $(BUILD)/kernel/quanta.elf
	$(PYTHON) scripts/check_architecture.py
	$(PYTHON) scripts/check_exports.py $(BUILD)/kernel/quanta.elf kernel/exports.txt

test-architecture:
	$(PYTHON) tests/test_architecture_checker.py

test-host:
	$(PYTHON) tests/test_elf64.py
	$(PYTHON) tests/test_shell_utilities.py
	$(PYTHON) tests/test_qfs2.py

abi:
	@mkdir -p $(BUILD)/tests
	$(CLANG) --target=x86_64-elf -std=c11 -Wall -Wextra -Werror $(ABI_INCLUDES) -c tests/abi_layout.c -o $(BUILD)/tests/abi_layout.o

qemu-smoke: $(BUILD)/quantaos.img
	$(PYTHON) scripts/qemu_smoke.py $< --marker QUANTA_BOOT_STAGE1_READY --marker QUANTA_BOOT_STAGE2_READY --marker QUANTA_LONG_MODE_READY --marker QUANTA_KERNEL_READY --marker QUANTA_LOGIN_READY

qemu-session-smoke: $(BUILD)/quantaos.img
	$(PYTHON) scripts/qemu_session_smoke.py $<

qemu-iso-smoke: $(BUILD)/quantaos.iso
	$(PYTHON) scripts/qemu_smoke.py $< --cdrom --marker QUANTA_BOOT_STAGE1_READY --marker QUANTA_BOOT_STAGE2_READY --marker QUANTA_LONG_MODE_READY --marker QUANTA_KERNEL_READY --marker QUANTA_LOGIN_READY

qemu-uefi-smoke: $(BUILD)/quantaos.iso
	$(PYTHON) scripts/qemu_uefi_smoke.py $<

qemu-run: $(BUILD)/quantaos.img
	qemu-system-x86_64 -drive format=raw,file=$<,if=ide,index=0,media=disk -serial stdio -display none -monitor none -no-reboot

qemu-window: $(BUILD)/quantaos.img
	qemu-system-x86_64 -drive format=raw,file=$<,if=ide,index=0,media=disk -serial stdio -display gtk -monitor none -no-reboot

clean:
	rm -rf $(BUILD)
