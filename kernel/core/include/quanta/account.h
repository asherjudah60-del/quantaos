#ifndef QUANTA_KERNEL_ACCOUNT_H
#define QUANTA_KERNEL_ACCOUNT_H

#include <stdint.h>

int quanta_account_verify(const char *password, uint64_t length, uint32_t *temporary);
int quanta_account_change(const char *password, uint64_t length);

#endif
