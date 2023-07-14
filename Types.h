#ifndef _MYTYPES_H_
#define _MYTYPES_H_

#include <stdint.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

#define COUNTOF(x) (sizeof(x) / sizeof(x[0]))

#endif // _MYTYPES_H_