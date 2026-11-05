// Minimal sys/types.h for nxdk (pdclib does not ship one).
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef long off_t;
typedef int ssize_t;
typedef unsigned short mode_t;
