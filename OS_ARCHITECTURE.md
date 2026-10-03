We already have **a kernel that can initialize memory management and transition into userspace**. Whats missing is essentially the rest of the **kernel/OS subsystem map**.

The important thing is that these aren't all independent. There is a dependency graph: interrupts enable preemption; scheduling depends on timers and context switching; processes depend on virtual memory; filesystems depend on block devices; `exec()` depends on filesystems and ELF loading; userspace depends on syscalls, etc.

Below is a fairly exhaustive checklist used as the OS master TODO list.

---

# Desktop OS From Scratch — Complete Kernel/OS Checklist

I'll divide this into:

1. **Boot & architecture**
2. **Kernel core**
3. **Memory management**
4. **Processes & threads**
5. **Scheduling**
6. **Interrupts & exceptions**
7. **System calls**
8. **IPC**
9. **Drivers & hardware**
10. **Storage & filesystems**
11. **Executable loading**
12. **Userspace**
13. **Networking**
14. **Graphics & desktop**
15. **Input**
16. **Security**
17. **Power management**
18. **SMP / multicore**
19. **Time**
20. **Debugging & observability**
21. **Kernel interfaces / ABI**
22. **Build & development infrastructure**
23. **System services**
24. **Desktop environment**
25. **Compatibility / standards**
26. **Reliability & hardening**

I'll mark particularly important dependencies with **★**.

---

# 0. First: Define Your OS Architecture

Before implementing everything, decide what kind of OS you're actually building.

### Kernel architecture

* [ ] Monolithic kernel
* [ ] Microkernel
* [ ] Hybrid kernel
* [ ] Exokernel-ish architecture

For a first desktop OS, a **monolithic kernel with reasonably modular subsystems** is probably the simplest architecture.

Decide:

* [ ] Kernel/userspace boundary
* [ ] Privileged CPU mode
* [ ] Userspace address-space model
* [ ] Kernel virtual address layout
* [ ] Physical memory layout
* [ ] Device-driver model
* [ ] VFS architecture
* [ ] Process model
* [ ] Thread model
* [ ] IPC model
* [ ] syscall ABI
* [ ] executable format
* [ ] userspace ABI
* [ ] filesystem interfaces

---

# 1. Boot System

You need to get from power-on to your kernel.

### Firmware/boot

* [ ] BIOS support, if desired
* [ ] UEFI support
* [ ] Bootloader
* [ ] Kernel image format
* [ ] Kernel entry point
* [ ] Boot protocol
* [ ] Boot information structure
* [ ] Memory map acquisition
* [ ] ACPI information
* [ ] CPU information
* [ ] framebuffer information
* [ ] boot modules/initrd
* [ ] command-line arguments
* [ ] random seed passed by bootloader
* [ ] firmware tables

### Early CPU initialization

* [ ] CPU mode initialization
* [ ] GDT
* [ ] IDT
* [ ] TSS
* [ ] segment configuration
* [ ] control registers
* [ ] paging activation
* [ ] NX support
* [ ] CPU feature detection
* [ ] SIMD/FPU initialization
* [ ] syscall CPU instructions
* [ ] MSRs
* [ ] per-CPU storage

For x86-64 this eventually includes things such as:

* [ ] `SYSCALL/SYSRET`
* [ ] `GSBASE`
* [ ] `FSBASE`
* [ ] `CR0`
* [ ] `CR3`
* [ ] `CR4`
* [ ] EFER
* [ ] PAT
* [ ] APIC configuration

---

# 2. Kernel Initialization

Your kernel needs an orderly initialization sequence.

Something conceptually like:

```text
boot
  ↓
CPU initialization
  ↓
physical memory manager
  ↓
virtual memory manager
  ↓
interrupt subsystem
  ↓
timer
  ↓
scheduler
  ↓
process/thread subsystem
  ↓
device manager
  ↓
storage
  ↓
VFS
  ↓
root filesystem
  ↓
executable loader
  ↓
init
  ↓
userspace
```

Implement:

* [ ] Kernel initialization framework
* [ ] Initialization levels/stages
* [ ] Kernel command-line parser
* [ ] Kernel configuration
* [ ] Early logging
* [ ] Panic system
* [ ] Kernel assertions
* [ ] Kernel error codes
* [ ] Kernel object management
* [ ] Kernel resource management
* [ ] Kernel shutdown/reboot

---

# 3. Physical Memory Management

You said you've already written the memory manager, but make sure you distinguish **physical** and **virtual** memory.

### Physical memory

* [ ] Physical memory map
* [ ] Physical page allocator
* [ ] Reserved memory regions
* [ ] Firmware-reserved memory
* [ ] DMA memory
* [ ] Page reference counting
* [ ] Page ownership
* [ ] Page zeroing
* [ ] Page poisoning/debugging
* [ ] Huge pages, eventually

### Kernel heap

* [ ] Early allocator
* [ ] Kernel heap
* [ ] `kmalloc`
* [ ] `kfree`
* [ ] `krealloc`
* [ ] Alignment
* [ ] Slab/slub allocator
* [ ] Per-CPU caches
* [ ] Memory leak detection

---

# 4. Virtual Memory ★

This is a huge subsystem.

* [ ] Page tables
* [ ] Address-space abstraction
* [ ] User address spaces
* [ ] Kernel address space
* [ ] Page mapping
* [ ] Page unmapping
* [ ] Page permissions
* [ ] Read/write permissions
* [ ] User/kernel permissions
* [ ] Executable/non-executable pages
* [ ] Copy-on-write
* [ ] Demand paging
* [ ] Page faults
* [ ] Memory-mapped files
* [ ] Anonymous mappings
* [ ] Shared memory
* [ ] `mmap`
* [ ] `munmap`
* [ ] `mprotect`
* [ ] `brk`
* [ ] Stack allocation
* [ ] Guard pages
* [ ] ASLR
* [ ] Kernel/user address separation
* [ ] TLB management
* [ ] TLB shootdowns on SMP
* [ ] Huge pages
* [ ] Swap, if desired
* [ ] Memory pressure handling
* [ ] Out-of-memory handling

---

# 5. CPU Exceptions ★

Before you have a reliable OS, you need to know when the CPU says:

> Something went wrong.

Implement handlers for:

* [ ] Divide-by-zero
* [ ] Debug exception
* [ ] NMI
* [ ] Breakpoint
* [ ] Overflow
* [ ] Bound range
* [ ] Invalid opcode
* [ ] Device-not-available
* [ ] Double fault
* [ ] Invalid TSS
* [ ] Segment-not-present
* [ ] Stack fault
* [ ] General protection fault
* [ ] Page fault
* [ ] x87 exception
* [ ] Alignment check
* [ ] Machine check
* [ ] SIMD exception
* [ ] Virtualization exceptions, if relevant

And:

* [ ] Exception frame
* [ ] Register dump
* [ ] Fault address
* [ ] User vs kernel fault distinction
* [ ] Kill offending userspace process
* [ ] Kernel panic for unrecoverable kernel faults

---

# 6. Interrupt Subsystem ★

This is separate from exceptions.

### Interrupt controller

On x86:

* [ ] PIC support, perhaps only for early boot
* [ ] APIC
* [ ] Local APIC
* [ ] I/O APIC
* [ ] MSI
* [ ] MSI-X
* [ ] Interrupt routing

### Interrupt management

* [ ] Interrupt registration
* [ ] Interrupt dispatch
* [ ] Interrupt masking
* [ ] Interrupt affinity
* [ ] Shared interrupts
* [ ] Interrupt priorities
* [ ] Interrupt-safe locking
* [ ] Deferred interrupt work
* [ ] Bottom halves / softirqs / work queues

---

# 7. Timers ★

You need timers for scheduling and much more.

* [ ] Hardware timer
* [ ] Monotonic clock
* [ ] Real-time clock
* [ ] Timer interrupts
* [ ] Kernel timers
* [ ] High-resolution timers
* [ ] Sleep/wakeup
* [ ] `nanosleep`
* [ ] Timer queues
* [ ] Per-CPU timers
* [ ] Timekeeping
* [ ] Clock sources
* [ ] Time conversion
* [ ] Time zones, probably userspace

Potential hardware:

* [ ] PIT
* [ ] HPET
* [ ] APIC timer
* [ ] TSC
* [ ] RTC

---

# 8. Threads ★

A process and a thread should probably be separate concepts.

Implement:

```text
process
 ├── address space
 ├── file descriptor table
 ├── credentials
 └── threads
       ├── registers
       ├── stack
       └── scheduling state
```

Thread subsystem:

* [ ] Thread creation
* [ ] Thread destruction
* [ ] Kernel threads
* [ ] User threads
* [ ] Thread ID
* [ ] Kernel stack
* [ ] User stack
* [ ] Register state
* [ ] Context switching
* [ ] Thread states

  * [ ] Running
  * [ ] Runnable
  * [ ] Sleeping
  * [ ] Blocked
  * [ ] Zombie
  * [ ] Dead
* [ ] Thread-local storage
* [ ] CPU affinity

---

# 9. Context Switching ★

This is one of the fundamental pieces.

Implement:

* [ ] Save registers
* [ ] Restore registers
* [ ] Save stack pointer
* [ ] Switch kernel stacks
* [ ] Switch address spaces
* [ ] Switch FPU/SIMD state
* [ ] Switch TLS state
* [ ] Switch CPU-local state
* [ ] Return to userspace
* [ ] Return from interrupt

Eventually:

* [ ] Lazy FPU state, if desired
* [ ] Optimized context switching

---

# 10. Scheduler ★

Now you can actually schedule those threads.

### Basic scheduler

* [ ] Runnable queue
* [ ] Scheduler tick
* [ ] Context switch
* [ ] `yield`
* [ ] Sleep
* [ ] Wake
* [ ] Blocking
* [ ] Unblocking
* [ ] Idle thread

### Scheduling policy

Start with:

```text
round-robin
```

Then perhaps:

* [ ] Priorities
* [ ] Dynamic priorities
* [ ] Fair scheduling
* [ ] Real-time scheduling
* [ ] CPU affinity
* [ ] Per-CPU run queues
* [ ] Load balancing
* [ ] Scheduler statistics

---

# 11. Synchronization ★

This gets extremely important once multiple threads exist.

Implement:

* [ ] Atomic operations
* [ ] Spinlocks
* [ ] Mutexes
* [ ] Semaphores
* [ ] Read/write locks
* [ ] Condition variables
* [ ] Wait queues
* [ ] Barriers
* [ ] Futexes

Kernel-specific:

* [ ] IRQ-safe locks
* [ ] Lock ordering
* [ ] Deadlock detection/debugging
* [ ] Preemption control
* [ ] Atomic reference counting

---

# 12. Processes ★

Now implement the actual process abstraction.

* [ ] PID
* [ ] Parent PID
* [ ] Process creation
* [ ] Process destruction
* [ ] Process states
* [ ] Process address space
* [ ] Process credentials
* [ ] Process environment
* [ ] Process file descriptors
* [ ] Process working directory
* [ ] Process signal state
* [ ] Process resource limits
* [ ] Process accounting

System calls:

* [ ] `fork`
* [ ] `clone`, or equivalent
* [ ] `exit`
* [ ] `wait`
* [ ] `waitpid`
* [ ] `getpid`
* [ ] `getppid`

Or design your own process model instead.

---

# 13. Program Loading ★

This is the thing between:

```text
process exists
```

and:

```text
program is executing
```

Implement:

* [ ] Executable format parser
* [ ] ELF loader
* [ ] Program headers
* [ ] Load segments
* [ ] Permissions
* [ ] Stack creation
* [ ] Initial register state
* [ ] Entry point
* [ ] Auxiliary vector
* [ ] Environment
* [ ] Arguments
* [ ] Dynamic linker support

Then:

* [ ] `exec`
* [ ] `execve`
* [ ] Interpreter/dynamic loader

Eventually:

* [ ] PIE
* [ ] shared libraries
* [ ] relocations
* [ ] TLS
* [ ] symbol resolution

---

# 14. System Calls ★★★

This is the primary interface between userspace and the kernel.

You'll need categories such as:

### Process

* [ ] `fork`
* [ ] `exec`
* [ ] `exit`
* [ ] `wait`
* [ ] `getpid`
* [ ] `getppid`

### Memory

* [ ] `mmap`
* [ ] `munmap`
* [ ] `mprotect`
* [ ] `brk`

### Files

* [ ] `open`
* [ ] `close`
* [ ] `read`
* [ ] `write`
* [ ] `pread`
* [ ] `pwrite`
* [ ] `lseek`
* [ ] `stat`
* [ ] `fstat`
* [ ] `mkdir`
* [ ] `rmdir`
* [ ] `unlink`
* [ ] `rename`
* [ ] `truncate`

### Directories

* [ ] `getcwd`
* [ ] `chdir`
* [ ] `getdents`

### Scheduling

* [ ] `sched_yield`
* [ ] `nanosleep`
* [ ] `clock_gettime`

### IPC

* [ ] Pipes
* [ ] Shared memory
* [ ] Message queues
* [ ] Sockets
* [ ] Futexes
* [ ] Signals

### Networking

* [ ] `socket`
* [ ] `bind`
* [ ] `listen`
* [ ] `accept`
* [ ] `connect`
* [ ] `send`
* [ ] `recv`

### Miscellaneous

* [ ] `ioctl`
* [ ] `fcntl`
* [ ] `dup`
* [ ] `dup2`
* [ ] `poll`
* [ ] `select`
* [ ] `epoll`, eventually

Also:

* [ ] Syscall argument validation
* [ ] User-pointer validation
* [ ] Copy user → kernel
* [ ] Copy kernel → user
* [ ] Syscall numbering
* [ ] Syscall ABI
* [ ] Error convention
* [ ] Restartable syscalls
* [ ] syscall tracing

---

# 15. Signals / Asynchronous Process Events

For a Unix-like OS:

* [ ] Signal representation
* [ ] Signal delivery
* [ ] Signal masks
* [ ] Signal handlers
* [ ] Default actions
* [ ] Pending signals
* [ ] Signal interruption of syscalls
* [ ] `kill`
* [ ] `sigaction`
* [ ] `sigprocmask`
* [ ] `sigreturn`
* [ ] Ctrl+C → SIGINT
* [ ] Segfault → SIGSEGV
* [ ] Illegal instruction → SIGILL
* [ ] Broken pipe → SIGPIPE

---

# 16. IPC ★

You specifically remembered IPC, and there is quite a lot here.

### Basic

* [ ] Pipes
* [ ] Named pipes/FIFOs

### Memory

* [ ] Shared memory
* [ ] Memory-backed IPC

### Synchronization

* [ ] Futexes
* [ ] Semaphores
* [ ] Mutexes

### Messaging

* [ ] Message queues
* [ ] Kernel message passing

### Unix-style

* [ ] Unix domain sockets

### Kernel-specific

* [ ] Event objects
* [ ] Notifications
* [ ] Completion objects

---

# 17. File Descriptor System ★

This is easy to overlook.

A process needs:

```text
fd 0 → stdin
fd 1 → stdout
fd 2 → stderr
```

Implement:

* [ ] File descriptor table
* [ ] File descriptor allocation
* [ ] File descriptor reference counting
* [ ] `open`
* [ ] `close`
* [ ] `dup`
* [ ] `dup2`
* [ ] `fcntl`
* [ ] Close-on-exec
* [ ] Per-process FD limits
* [ ] Pipe FDs
* [ ] Socket FDs
* [ ] Device FDs
* [ ] Event FDs

---

# 18. VFS ★★★

This is one of the biggest missing pieces if you want multiple filesystems.

Don't make every filesystem implement `open()` directly.

Create a **Virtual Filesystem layer**.

Something like:

```text
Application
     ↓
syscall
     ↓
VFS
     ↓
filesystem driver
     ↓
block device
     ↓
storage driver
     ↓
hardware
```

VFS objects:

* [ ] Superblock
* [ ] Inode
* [ ] Dentry
* [ ] File
* [ ] Mount
* [ ] Filesystem type
* [ ] Path
* [ ] File descriptor

Operations:

* [ ] Open
* [ ] Close
* [ ] Read
* [ ] Write
* [ ] Seek
* [ ] Stat
* [ ] Create
* [ ] Delete
* [ ] Rename
* [ ] Link
* [ ] Symlink
* [ ] Mkdir
* [ ] Readdir
* [ ] Mount
* [ ] Unmount

Path resolution:

* [ ] `/`
* [ ] `.`
* [ ] `..`
* [ ] Absolute paths
* [ ] Relative paths
* [ ] Symbolic links
* [ ] Mount traversal
* [ ] Permissions
* [ ] Path caching

---

# 19. Filesystems

Start with **one simple filesystem**.

For example:

```text
yourfs
```

Then add:

* [ ] FAT32
* [ ] ext2
* [ ] ext4
* [ ] ISO9660
* [ ] tmpfs
* [ ] procfs
* [ ] sysfs
* [ ] devfs
* [ ] initramfs
* [ ] network filesystems, eventually

You don't need all of these.

A sane progression is:

```text
initramfs
    ↓
simple native filesystem
    ↓
FAT32
    ↓
ext2/ext4
```

---

# 20. Block Device Layer ★

Filesystems shouldn't know whether they're talking to SATA, NVMe, or an SD card.

Create:

```text
VFS
 ↓
filesystem
 ↓
block layer
 ↓
block device
 ↓
driver
```

Implement:

* [ ] Block device abstraction
* [ ] Block requests
* [ ] Read blocks
* [ ] Write blocks
* [ ] Request queues
* [ ] Buffer cache
* [ ] Page cache
* [ ] I/O scheduling
* [ ] Device identification
* [ ] Partition discovery

---

# 21. Storage Drivers

Eventually:

### ATA/SATA

* [ ] IDE/ATA
* [ ] AHCI
* [ ] SATA

### NVMe

* [ ] PCI discovery
* [ ] NVMe controller initialization
* [ ] Submission queues
* [ ] Completion queues
* [ ] Read/write commands
* [ ] Interrupts

### Removable

* [ ] USB mass storage
* [ ] SD/MMC, if relevant

---

# 22. Device Model ★

You need a unified way to represent hardware.

Implement concepts like:

```text
device
driver
bus
class
resource
```

Examples:

```text
PCI device
USB device
I2C device
SPI device
```

Driver model:

* [ ] Driver registration
* [ ] Device registration
* [ ] Device matching
* [ ] Driver probing
* [ ] Driver removal
* [ ] Device resources
* [ ] Device dependencies
* [ ] Device tree/ACPI integration

---

# 23. PCI ★

For a desktop OS this is fundamental.

* [ ] PCI enumeration
* [ ] PCI configuration space
* [ ] BARs
* [ ] MMIO
* [ ] I/O ports
* [ ] Interrupt routing
* [ ] MSI
* [ ] MSI-X
* [ ] PCI capabilities
* [ ] PCI bridges
* [ ] PCIe devices

You will discover things such as:

```text
GPU
NVMe
NIC
USB controller
audio controller
```

through PCI.

---

# 24. USB ★

USB is practically its own operating system.

You eventually need:

* [ ] USB host controller
* [ ] xHCI
* [ ] USB enumeration
* [ ] USB descriptors
* [ ] USB configuration
* [ ] USB endpoints
* [ ] USB transfers
* [ ] Interrupt transfers
* [ ] Bulk transfers
* [ ] Control transfers

Then USB classes:

* [ ] HID
* [ ] Keyboard
* [ ] Mouse
* [ ] Mass storage
* [ ] Audio
* [ ] Network
* [ ] Cameras

---

# 25. Input Devices

### Keyboard

* [ ] PS/2 keyboard, useful for early development
* [ ] USB HID keyboard
* [ ] Key events
* [ ] Modifier keys
* [ ] Keyboard layouts
* [ ] Compose/dead keys eventually

### Mouse

* [ ] PS/2 mouse
* [ ] USB HID mouse
* [ ] Relative movement
* [ ] Buttons
* [ ] Scroll wheel

### Touch

* [ ] Touchscreen
* [ ] Multi-touch

### Input subsystem

Create a unified event system:

```text
hardware
   ↓
driver
   ↓
input subsystem
   ↓
window server
   ↓
application
```

---

# 26. Display / Graphics ★★★

For a desktop OS, this becomes enormous.

Start simple.

### Framebuffer

* [ ] Boot framebuffer
* [ ] Pixel formats
* [ ] Framebuffer mapping
* [ ] Basic drawing
* [ ] Double buffering

Then:

### Graphics driver

* [ ] GPU discovery
* [ ] GPU memory
* [ ] Command submission
* [ ] GPU synchronization
* [ ] Display outputs
* [ ] Modesetting
* [ ] VSync

Depending on hardware, supporting modern GPUs can become one of the hardest parts of the OS.

---

# 27. Window System

Eventually:

```text
Applications
      ↓
GUI toolkit
      ↓
Window system
      ↓
Compositor
      ↓
GPU/framebuffer
```

Implement:

* [ ] Window creation
* [ ] Window destruction
* [ ] Window positions
* [ ] Window sizes
* [ ] Z ordering
* [ ] Input routing
* [ ] Focus
* [ ] Decorations
* [ ] Resize
* [ ] Minimize/maximize
* [ ] Clipboard
* [ ] Drag and drop
* [ ] Compositing
* [ ] Multiple monitors

---

# 28. Audio

Another major subsystem.

* [ ] Audio device model
* [ ] PCI audio discovery
* [ ] HDA
* [ ] USB audio
* [ ] PCM
* [ ] Audio buffers
* [ ] Playback
* [ ] Recording
* [ ] Mixer
* [ ] Volume
* [ ] Audio server

Userspace should probably have an audio service rather than applications talking directly to hardware.

---

# 29. Networking ★

This is an enormous subsystem.

### Network device

* [ ] NIC driver
* [ ] Ethernet
* [ ] DMA
* [ ] RX queues
* [ ] TX queues
* [ ] Interrupts
* [ ] Packet buffers

### Network stack

```text
Application
 ↓
Socket API
 ↓
TCP/UDP
 ↓
IP
 ↓
Ethernet
 ↓
NIC driver
 ↓
hardware
```

Implement:

### Ethernet

* [ ] Ethernet frames
* [ ] MAC addresses
* [ ] ARP

### IPv4

* [ ] IPv4
* [ ] Routing
* [ ] ICMP
* [ ] Fragmentation

### IPv6

* [ ] IPv6
* [ ] Neighbor Discovery
* [ ] ICMPv6
* [ ] SLAAC

### Transport

* [ ] UDP
* [ ] TCP
* [ ] TCP congestion control
* [ ] TCP retransmission
* [ ] TCP state machine

Eventually:

* [ ] DHCP
* [ ] DNS
* [ ] TLS
* [ ] HTTP client/server
* [ ] VPN interfaces

---

# 30. Socket Layer

* [ ] Socket abstraction
* [ ] Socket file descriptors
* [ ] Unix sockets
* [ ] IPv4 sockets
* [ ] IPv6 sockets
* [ ] UDP
* [ ] TCP
* [ ] `bind`
* [ ] `listen`
* [ ] `accept`
* [ ] `connect`
* [ ] `send`
* [ ] `recv`
* [ ] `sendto`
* [ ] `recvfrom`
* [ ] `shutdown`
* [ ] socket options

---

# 31. Security Model ★

Even a hobby OS needs a security architecture eventually.

### Users

* [ ] User IDs
* [ ] Group IDs
* [ ] Credentials
* [ ] Root/superuser
* [ ] Login system

### Permissions

* [ ] File permissions
* [ ] Read
* [ ] Write
* [ ] Execute
* [ ] Owner
* [ ] Group
* [ ] Others

### Process isolation

* [ ] User/kernel separation
* [ ] Address-space isolation
* [ ] Privileged syscalls
* [ ] Capability checks

Eventually:

* [ ] Capabilities
* [ ] ACLs
* [ ] Sandboxing
* [ ] Namespaces
* [ ] Secure boot
* [ ] Code signing
* [ ] Key management

---

# 32. Randomness / Cryptography

You'll need randomness for security.

* [ ] Hardware RNG
* [ ] Kernel entropy pool
* [ ] `/dev/random`
* [ ] `/dev/urandom`
* [ ] Cryptographic primitives
* [ ] Secure random API

Don't write your own cryptographic algorithms unless this is specifically a cryptography project.

---

# 33. Time

Implement:

* [ ] Monotonic clock
* [ ] Wall clock
* [ ] RTC
* [ ] Time adjustment
* [ ] Timers
* [ ] Sleep
* [ ] Timestamps
* [ ] File timestamps
* [ ] Time zones
* [ ] DST

The kernel should generally provide UTC/time primitives; timezone databases and presentation belong largely in userspace.

---

# 34. SMP / Multicore ★★

Your first version can run on one CPU.

Eventually:

* [ ] Discover CPUs
* [ ] Start secondary CPUs
* [ ] Per-CPU data
* [ ] Per-CPU scheduler
* [ ] CPU-local storage
* [ ] Inter-processor interrupts
* [ ] TLB shootdowns
* [ ] Cross-CPU calls
* [ ] Load balancing
* [ ] CPU hotplug
* [ ] Synchronization
* [ ] Cache coherence considerations

---

# 35. ACPI / Hardware Discovery

Desktop hardware needs firmware information.

* [ ] ACPI table parsing
* [ ] RSDP
* [ ] RSDT/XSDT
* [ ] MADT
* [ ] FADT
* [ ] DSDT/AML
* [ ] PCI routing
* [ ] CPU discovery
* [ ] Power management
* [ ] Battery information
* [ ] Thermal zones

AML interpretation is itself a substantial project.

---

# 36. Power Management

* [ ] Reboot
* [ ] Shutdown
* [ ] Halt
* [ ] Sleep
* [ ] Suspend
* [ ] Hibernate
* [ ] CPU frequency scaling
* [ ] CPU idle states
* [ ] Battery
* [ ] AC adapter
* [ ] Thermal management
* [ ] Fan control

---

# 37. Kernel Device Files

A Unix-like OS commonly exposes devices through something like:

```text
/dev/null
/dev/zero
/dev/random
/dev/tty
/dev/console
/dev/sda
/dev/input/...
```

Implement:

* [ ] Device filesystem
* [ ] Character devices
* [ ] Block devices
* [ ] Device major/minor numbers
* [ ] `/dev`
* [ ] `ioctl`

---

# 38. Pseudo-filesystems

Very useful for a Unix-like OS.

### `/proc`

Expose:

* [ ] Processes
* [ ] CPU information
* [ ] Memory
* [ ] Kernel information
* [ ] Mounts
* [ ] Statistics

### `/sys`

Expose:

* [ ] Devices
* [ ] Drivers
* [ ] Buses
* [ ] Hardware properties

### `/dev`

Expose:

* [ ] Device nodes
* [ ] Pseudo devices

---

# 39. Init System ★★★

This is the thing you specifically remembered.

After the kernel has initialized itself:

```text
kernel
   ↓
init
   ↓
system services
   ↓
login manager
   ↓
desktop
```

Your kernel needs to:

* [ ] Locate init
* [ ] Load executable
* [ ] Create first userspace process
* [ ] Construct argv
* [ ] Construct environment
* [ ] Set up stdin
* [ ] Set up stdout
* [ ] Set up stderr
* [ ] Set working directory
* [ ] Mount initial filesystems
* [ ] Transfer execution to init

Your `init` program then does things like:

* [ ] Mount `/proc`
* [ ] Mount `/sys`
* [ ] Mount `/dev`
* [ ] Mount root filesystem
* [ ] Start device manager
* [ ] Start networking
* [ ] Start login manager
* [ ] Start desktop

**Important:** most of that should NOT be kernel functionality.

The kernel should provide the primitives.

---

# 40. Userspace C Library ★★★

This is another thing people forget.

Your applications need a libc.

Eventually:

```text
Application
     ↓
libc
     ↓
syscalls
     ↓
kernel
```

Implement:

* [ ] `malloc`
* [ ] `free`
* [ ] `memcpy`
* [ ] `memset`
* [ ] `strlen`
* [ ] `printf`
* [ ] file APIs
* [ ] process APIs
* [ ] threading APIs
* [ ] sockets
* [ ] environment
* [ ] signals

You can initially write a tiny libc rather than implementing the whole POSIX API.

---

# 41. Userspace Runtime

You need startup code.

Something like:

```text
kernel
 ↓
ELF loader
 ↓
dynamic linker
 ↓
libc startup
 ↓
main()
```

Implement:

* [ ] `_start`
* [ ] argc
* [ ] argv
* [ ] envp
* [ ] auxiliary vector
* [ ] libc initialization
* [ ] constructors
* [ ] destructors
* [ ] `exit`

---

# 42. Dynamic Linker

Eventually:

* [ ] ELF shared objects
* [ ] `libc.so`
* [ ] Dynamic section
* [ ] Relocations
* [ ] Symbol tables
* [ ] Symbol resolution
* [ ] PLT/GOT
* [ ] TLS
* [ ] `dlopen`
* [ ] `dlsym`
* [ ] `dlclose`

You can postpone this considerably.

Initially, **statically linked executables** make your life much easier.

---

# 43. Shell

You need something to actually control the OS.

Implement:

* [ ] Command parser
* [ ] Environment variables
* [ ] Builtins
* [ ] Program execution
* [ ] PATH lookup
* [ ] Pipes
* [ ] Redirection
* [ ] Background processes
* [ ] Job control
* [ ] Signals
* [ ] Terminal handling

Eventually:

```bash
$ ls
$ cd
$ cat
$ ps
$ kill
$ mount
$ mkdir
$ cp
$ mv
$ rm
```

---

# 44. Terminal / TTY

This is easy to underestimate.

* [ ] TTY abstraction
* [ ] Console
* [ ] Terminal input
* [ ] Terminal output
* [ ] Canonical mode
* [ ] Raw mode
* [ ] Echo
* [ ] Line discipline
* [ ] Control characters
* [ ] Ctrl+C
* [ ] Ctrl+Z
* [ ] Job control
* [ ] Virtual terminals
* [ ] PTYs

---

# 45. Login System

For a desktop OS:

* [ ] User database
* [ ] Password authentication
* [ ] Login process
* [ ] Session creation
* [ ] Environment initialization
* [ ] User home directory
* [ ] Session permissions

Then:

```text
login
 ↓
user session
 ↓
window server
 ↓
desktop
```

---

# 46. Desktop Services

Eventually:

* [ ] Display server
* [ ] Window manager/compositor
* [ ] Input server
* [ ] Clipboard manager
* [ ] Notification daemon
* [ ] Audio server
* [ ] Network manager
* [ ] Settings service
* [ ] Power manager
* [ ] Device manager
* [ ] File manager
* [ ] Application launcher

---

# 47. GUI Toolkit

Applications need something above the window server.

* [ ] Windows
* [ ] Buttons
* [ ] Labels
* [ ] Text boxes
* [ ] Menus
* [ ] Dialogs
* [ ] Layout
* [ ] Fonts
* [ ] Images
* [ ] Rendering
* [ ] Event system
* [ ] Accessibility
* [ ] Themes

---

# 48. Internationalization

Eventually:

* [ ] Unicode
* [ ] UTF-8
* [ ] Locale
* [ ] Character classification
* [ ] Case conversion
* [ ] Number formatting
* [ ] Date formatting
* [ ] Time formatting
* [ ] Input methods
* [ ] Keyboard layouts
* [ ] RTL languages

---

# 49. Font System

Desktop OS:

* [ ] Font loading
* [ ] Font rasterization
* [ ] Glyph caching
* [ ] Text shaping
* [ ] Unicode support
* [ ] OpenType/TrueType

This can mostly live in userspace.

---

# 50. Debugging Infrastructure ★★★

Do this **much earlier** than you think.

Implement:

* [ ] Kernel logging
* [ ] Serial console
* [ ] Debug console
* [ ] Panic handler
* [ ] Stack traces
* [ ] Symbol lookup
* [ ] Register dumps
* [ ] Page fault diagnostics
* [ ] Heap debugging
* [ ] Lock debugging
* [ ] Syscall tracing
* [ ] Process tracing
* [ ] Driver tracing
* [ ] Kernel assertions
* [ ] Crash dumps

And extremely useful:

```text
printk()
panic()
assert()
backtrace()
hexdump()
```

---

# 51. Kernel Debugging Tools

Eventually create:

* [ ] `ps`
* [ ] `top`
* [ ] `free`
* [ ] `dmesg`
* [ ] `mount`
* [ ] `ls`
* [ ] `cat`
* [ ] `strace`-like syscall tracer
* [ ] memory statistics
* [ ] `/proc` diagnostics
* [ ] device listing
* [ ] PCI listing
* [ ] interrupt statistics

---

# 52. Resource Management

Every process consumes resources.

Implement limits for:

* [ ] Memory
* [ ] File descriptors
* [ ] Processes
* [ ] Threads
* [ ] CPU time
* [ ] Disk space
* [ ] Network buffers
* [ ] IPC objects

Eventually:

* [ ] Resource accounting
* [ ] Quotas

---

# 53. Caching

Performance eventually requires:

* [ ] Page cache
* [ ] Buffer cache
* [ ] Dentry cache
* [ ] Inode cache
* [ ] Filesystem cache
* [ ] Executable cache
* [ ] Network cache

---

# 54. I/O Subsystem

You want a generalized I/O architecture.

* [ ] Blocking I/O
* [ ] Nonblocking I/O
* [ ] Asynchronous I/O
* [ ] Polling
* [ ] `select`
* [ ] `poll`
* [ ] `epoll`/equivalent
* [ ] Completion queues
* [ ] DMA
* [ ] Scatter/gather I/O
* [ ] Direct I/O

---

# 55. DMA

Drivers need to interact with devices efficiently.

* [ ] DMA abstraction
* [ ] DMA buffers
* [ ] DMA mapping
* [ ] Physical/virtual address translation
* [ ] Scatter/gather
* [ ] IOMMU, eventually
* [ ] Cache coherency

---

# 56. IOMMU

Eventually:

* [ ] IOMMU discovery
* [ ] Device address spaces
* [ ] DMA isolation
* [ ] Interrupt remapping
* [ ] Device assignment

---

# 57. Filesystem Reliability

For a serious OS:

* [ ] Journaling
* [ ] Write ordering
* [ ] Crash recovery
* [ ] Filesystem checking
* [ ] fsck
* [ ] Mount recovery
* [ ] Read-only fallback
* [ ] Disk error handling

---

# 58. Kernel Error Handling

Every subsystem needs to handle:

* [ ] Hardware failure
* [ ] Invalid user input
* [ ] Out-of-memory
* [ ] Device timeout
* [ ] Device removal
* [ ] Corrupt filesystem
* [ ] Invalid syscall
* [ ] Invalid pointer
* [ ] Permission failure
* [ ] Interrupted operation
* [ ] Partial I/O

Don't let:

```text
driver returned error
```

become:

```text
kernel randomly crashed
```

---

# 59. Hotplug

Modern desktops constantly change hardware.

* [ ] USB insertion
* [ ] USB removal
* [ ] Device discovery
* [ ] Device removal
* [ ] Driver binding
* [ ] Driver unbinding
* [ ] Filesystem mount events
* [ ] Network device changes
* [ ] Monitor insertion/removal

---

# 60. Security Hardening

Eventually:

* [ ] Stack canaries
* [ ] W^X
* [ ] NX
* [ ] ASLR
* [ ] KASLR
* [ ] SMEP
* [ ] SMAP
* [ ] User/kernel isolation
* [ ] Control-flow protection
* [ ] Heap hardening
* [ ] Reference-count safety
* [ ] Integer overflow auditing
* [ ] Bounds checking
* [ ] Secure boot

---

# 61. Testing Infrastructure ★★★

Don't wait until the end.

Create:

* [ ] Unit tests
* [ ] Kernel tests
* [ ] Userspace tests
* [ ] Filesystem tests
* [ ] Memory tests
* [ ] Syscall tests
* [ ] Scheduler tests
* [ ] IPC tests
* [ ] Driver tests
* [ ] Network tests
* [ ] Stress tests
* [ ] Fuzzing
* [ ] Regression tests

And run the OS automatically under:

* [ ] QEMU
* [ ] Bochs, optionally
* [ ] real hardware

---

# 62. Build System

* [ ] Cross compiler
* [ ] Cross libc
* [ ] Kernel linker script
* [ ] Kernel build
* [ ] Userspace build
* [ ] Initramfs generation
* [ ] Disk image generation
* [ ] Filesystem image generation
* [ ] Boot image generation
* [ ] Debug build
* [ ] Release build
* [ ] Symbol files
* [ ] Reproducible builds

---

# 63. Installation / Bootable Image

Eventually:

* [ ] Partition table
* [ ] GPT
* [ ] EFI System Partition
* [ ] Bootloader installation
* [ ] Kernel installation
* [ ] Initramfs
* [ ] Root filesystem
* [ ] OS installer
* [ ] Upgrade mechanism

---

# 64. Package Management

If this is intended to be a real desktop OS:

* [ ] Package format
* [ ] Package metadata
* [ ] Dependency resolution
* [ ] Package installation
* [ ] Package removal
* [ ] Package upgrades
* [ ] Repository
* [ ] Package signatures

---

# 65. System Update Mechanism

* [ ] OS updates
* [ ] Kernel updates
* [ ] Userspace updates
* [ ] Rollback
* [ ] Atomic updates
* [ ] Recovery environment

---

# 66. Crash Recovery

* [ ] Kernel panic
* [ ] Crash dump
* [ ] Userspace crash reporting
* [ ] Core dumps
* [ ] Process restart
* [ ] Service restart
* [ ] Filesystem recovery
* [ ] Boot recovery mode

---

# 67. Service Manager

Your `init` will eventually become more sophisticated.

Something conceptually like:

```text
init
 ├── device manager
 ├── filesystem manager
 ├── network manager
 ├── logging service
 ├── audio service
 ├── display server
 ├── login manager
 └── desktop
```

Implement eventually:

* [ ] Service definitions
* [ ] Dependency ordering
* [ ] Service startup
* [ ] Service shutdown
* [ ] Restart-on-failure
* [ ] Service supervision
* [ ] Logging
* [ ] IPC between services

---

# 68. System Logging

* [ ] Kernel log
* [ ] Userspace log
* [ ] Log levels
* [ ] Persistent logs
* [ ] Log rotation
* [ ] Structured logging
* [ ] Crash logs

---

# 69. Configuration System

* [ ] System configuration
* [ ] User configuration
* [ ] Environment variables
* [ ] Configuration files
* [ ] Runtime configuration
* [ ] Hardware configuration

---

# 70. Documentation

Seriously: include this in the project.

Document:

* [ ] Kernel architecture
* [ ] Memory layout
* [ ] Syscall ABI
* [ ] Driver API
* [ ] VFS API
* [ ] Process API
* [ ] Scheduler
* [ ] IPC
* [ ] Userspace ABI
* [ ] Boot protocol
* [ ] Kernel data structures
* [ ] Coding conventions

---

# The Big Picture

If we compress everything above into the architecture of the OS, it looks like this:

```text
                         USERSPACE
┌──────────────────────────────────────────────────────────────┐
│                                                              │
│  Applications                                                │
│      │                                                       │
│  GUI applications                                            │
│      │                                                       │
│  GUI toolkit                                                 │
│      │                                                       │
│  Shell / utilities / services                                │
│      │                                                       │
│  libc / runtime / dynamic linker                             │
│      │                                                       │
├──────┴──────────────────── SYSCALL ABI ──────────────────────┤
│                                                              │
│                         KERNEL                               │
│                                                              │
│  Process / Thread Management                                 │
│          │                                                   │
│  Scheduler                                                   │
│          │                                                   │
│  IPC ─────── Signals ───── Synchronization                   │
│          │                                                   │
│  Virtual Memory ───── Physical Memory                        │
│          │                                                   │
│  VFS ───── Page Cache ───── Block Layer                      │
│          │                                                   │
│  Network Stack ───── Socket Layer                            │
│          │                                                   │
│  Device Model                                                │
│      │                                                       │
│  ┌───┼───────────┬────────────┬────────────┐                │
│  │   │           │            │            │                │
│ PCI USB        Storage       Network      Audio             │
│  │   │           │            │            │                │
├──┴───┴───────────┴────────────┴────────────┴────────────────┤
│                                                              │
│                      HARDWARE                                │
│                                                              │
│ CPU │ RAM │ GPU │ NVMe │ USB │ NIC │ Audio │ Keyboard │ etc │
└──────────────────────────────────────────────────────────────┘
```

---

# But Don't Build It In This Order

This is the important part.

If you look at the giant list and think:

> "Jesus, I have to implement 70 subsystems."

**No.**

You need to build a series of increasingly capable operating systems.

I'd use roughly these milestones.

---

## Milestone 1 — Kernel that can execute one program

You already appear to be around here.

```text
Bootloader
    ↓
Kernel
    ↓
Memory management
    ↓
Userspace
    ↓
hello world
```

You need:

* [x] Boot
* [x] Kernel
* [x] Memory manager
* [x] Userspace transition
* [ ] Exceptions
* [ ] Interrupts
* [ ] Timer
* [ ] Context switching
* [ ] Basic scheduler
* [ ] Syscalls

---

# Milestone 2 — Actual multitasking OS

Get to:

```text
$ program1 &
$ program2 &
$ program3
```

Implement:

* [ ] Interrupts
* [ ] Timer
* [ ] Threads
* [ ] Context switching
* [ ] Scheduler
* [ ] Processes
* [ ] Address spaces
* [ ] `fork`/equivalent
* [ ] `exec`
* [ ] `exit`
* [ ] `wait`
* [ ] Basic syscalls
* [ ] IPC
* [ ] Signals

At this point you have a **real operating system** rather than merely a kernel that can run a program.

---

# Milestone 3 — Filesystem OS

Get to:

```text
$ ls
$ mkdir test
$ echo hello > test/file
$ cat test/file
```

Implement:

* [ ] VFS
* [ ] File descriptors
* [ ] Inodes
* [ ] Paths
* [ ] Directories
* [ ] Block layer
* [ ] One filesystem
* [ ] Storage driver
* [ ] `open`
* [ ] `read`
* [ ] `write`
* [ ] `close`
* [ ] `stat`
* [ ] `mkdir`
* [ ] `unlink`

---

# Milestone 4 — Self-hosting userspace

Now:

```text
kernel
 ↓
init
 ↓
shell
 ↓
compiler
 ↓
your programs
```

You need:

* [ ] Init
* [ ] libc
* [ ] shell
* [ ] core utilities
* [ ] compiler/toolchain
* [ ] filesystem
* [ ] dynamic linking, eventually

This is a **massive psychological milestone**.

Your OS is now capable of running programs that were built *for the OS*.

---

# Milestone 5 — Real hardware

Now attack:

```text
PCI
 ↓
USB
 ↓
keyboard
 ↓
mouse
 ↓
storage
 ↓
network
```

I'd prioritize:

1. **PCI**
2. **interrupt controller**
3. **timer**
4. **USB/xHCI**
5. **USB HID**
6. **NVMe**
7. **NIC**

---

# Milestone 6 — Networking

Get:

```text
$ ping 8.8.8.8
```

Then:

```text
$ curl example.com
```

Implement:

```text
NIC driver
 ↓
Ethernet
 ↓
ARP
 ↓
IPv4
 ↓
UDP
 ↓
TCP
 ↓
DNS
 ↓
sockets
```

---

# Milestone 7 — Desktop

Only now should you attack the enormous graphical stack:

```text
GPU/framebuffer
       ↓
display server
       ↓
compositor
       ↓
window system
       ↓
GUI toolkit
       ↓
applications
```

Then:

```text
boot
 ↓
init
 ↓
services
 ↓
login
 ↓
desktop
```

---

# Your Immediate TODO

Given what you told me:

> "I wrote the memory manager, and I can get the kernel to load userspace."

I'd **not** start implementing filesystems, USB, networking, GUI, etc. yet.

Your immediate dependency chain should be approximately:

```text
                    YOU ARE HERE
                         │
                         ▼
                 ┌───────────────┐
                 │ Memory manager│
                 └───────┬───────┘
                         │
                         ▼
              ┌─────────────────────┐
              │ CPU exceptions      │
              │ Interrupt subsystem │
              └──────────┬──────────┘
                         │
                         ▼
                    ┌─────────┐
                    │ Timers  │
                    └────┬────┘
                         │
                         ▼
                ┌─────────────────┐
                │ Context switching│
                └────────┬────────┘
                         │
                         ▼
                    ┌─────────┐
                    │Threads  │
                    └────┬────┘
                         │
                         ▼
                   ┌───────────┐
                   │ Scheduler │
                   └─────┬─────┘
                         │
                         ▼
                    ┌─────────┐
                    │Processes│
                    └────┬────┘
                         │
             ┌───────────┴───────────┐
             ▼                       ▼
        ┌─────────┐             ┌─────────┐
        │ Syscalls│             │   IPC   │
        └────┬────┘             └─────────┘
             │
             ▼
         ┌────────┐
         │ exec() │
         └────┬───┘
              │
              ▼
            init
              │
              ▼
            shell
```

**Then**:

```text
init
 │
 ├── VFS
 │    └── filesystem
 │         └── block layer
 │              └── storage driver
 │
 ├── device manager
 │    └── PCI
 │         ├── USB
 │         ├── NVMe
 │         ├── NIC
 │         ├── GPU
 │         └── audio
 │
 ├── networking
 │
 └── graphical system
```

---

# The Most Important Concept

The thing that tends to make OS development feel overwhelming is treating this as one giant list.

It isn't.

Think of your OS as **layers of contracts**:

```text
                Applications
                     │
                  libc/API
                     │
                  syscalls
                     │
              kernel subsystems
                     │
              hardware abstraction
                     │
                 drivers
                     │
                  hardware
```

For example, `cat foo.txt` eventually becomes:

```text
cat
 │
 └─ libc.read()
       │
       └─ syscall(SYS_read)
              │
              └─ kernel sys_read()
                     │
                     └─ VFS
                          │
                          └─ filesystem
                               │
                               └─ block cache
                                    │
                                    └─ block layer
                                         │
                                         └─ NVMe driver
                                              │
                                              └─ PCI
                                                   │
                                                   └─ NVMe hardware
```

**You don't need to implement the whole tree before `cat` works.** You implement exactly one path through it, then progressively replace/simple components with more capable ones.

If you're writing this from scratch, I'd strongly recommend keeping a document in the repository called something like **`OS_ARCHITECTURE.md`** containing the subsystem tree above, and turning each `[ ]` into an actual issue/task. That prevents exactly the "I know there were twenty other things I needed to implement but I can't remember what they were" problem you're having now.
