# QuantaOS VM-first release roadmap

`OS_ARCHITECTURE.md` is the long-term subsystem inventory. This file is the
release contract and execution checklist for the first installable desktop.

## Supported release matrix

| Target | Firmware | Storage | Required |
| --- | --- | --- | --- |
| QEMU | BIOS | IDE disk | yes |
| QEMU | BIOS | El Torito live ISO + IDE target | yes |
| QEMU | UEFI | ISO | yes |
| VirtualBox | BIOS | IDE disk | manual acceptance |

AHCI/NVMe, USB, networking, audio, SMP, third-party application compatibility,
and a UEFI installer are deliberately out of scope.

## Release invariants

- Shared ABI lives only under `abi/include/quanta`; syscall numbers are append-only.
- `userspace/session.c` is the only interactive shell/desktop authority.
- Public paths are `prime:path>to>item` and `live:path>to>item`; QFS2 paths stay
  internal to the kernel.
- The installed BIOS disk layout is MBR at LBA 0, stage 2 at LBA 1, accounts at
  LBA 256, and QFS2 at LBA 258.

## Delivery checklist

- [x] Desktop baseline: bounded draw protocol, PS/2 timeouts, window
  focus/drag/resize, Files/Settings/Terminal, and QEMU screenshot smoke.
- [x] BIOS baseline: bounded serial output, A20 fallback, chunked HDD EDD reads,
  active MBR partition, and small El Torito loader with aligned payload reads.
- [ ] BIOS portability: validate the isohybrid USB form and VirtualBox BIOS path.
- [ ] Storage/installer: four IDE probes, separate read-only live device, shared
  layout constants, safe `install diskN`, post-write validation, reboot smoke.
- [x] Shell baseline: strict drive grammar, named-drive reporting, and internal
  QFS2 path isolation.
- [ ] Shell polish: exact three-line prompt, named-drive reporting,
  protected system directories, persistent QFS2 mutation checks.
- [ ] Gates: hosted checks, BIOS disk/ISO/desktop smoke, UEFI smoke, Bochs smoke,
  and the documented VirtualBox IDE walkthrough pass twice from clean builds.
