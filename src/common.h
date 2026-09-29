/* common.h -- shared types and constants */
#ifndef GB_COMMON_H
#define GB_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint32_t u32;
typedef uint64_t u64;

#define GB_W 160
#define GB_H 144

#define CPU_HZ        4194304       /* T-cycles per second */
#define FRAME_CYCLES  70224         /* 154 scanlines * 456 dots  */

#endif /* GB_COMMON_H */
