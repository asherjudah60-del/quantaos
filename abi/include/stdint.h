#ifndef QUANTA_FREESTANDING_STDINT_H
#define QUANTA_FREESTANDING_STDINT_H

/* The kernel targets x86_64-elf and cannot consume the host libc headers. */
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef long long intmax_t;
typedef unsigned long long uintmax_t;
/* Windows x64 keeps `long` at 32 bits; the ABI is shared with the EFI build. */
typedef long long intptr_t;
typedef unsigned long long uintptr_t;
#endif
