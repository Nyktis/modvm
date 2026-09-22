/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_UTIL_BYTEORDER_H
#define MODVM_UTIL_BYTEORDER_H

#include <stdint.h>
#include <modvm/util/stddef.h>
#include <modvm/util/types.h>

/* Use compiler built-ins for optimal byte swapping instructions */
#define swab16(x) __builtin_bswap16(x)
#define swab32(x) __builtin_bswap32(x)
#define swab64(x) __builtin_bswap64(x)

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

#define cpu_to_le16(x) ((le16_t){ (uint16_t)(x) })
#define le16_to_cpu(x) ((uint16_t)((x).__val))
#define cpu_to_le32(x) ((le32_t){ (uint32_t)(x) })
#define le32_to_cpu(x) ((uint32_t)((x).__val))
#define cpu_to_le64(x) ((le64_t){ (uint64_t)(x) })
#define le64_to_cpu(x) ((uint64_t)((x).__val))

#define cpu_to_be16(x) ((be16_t){ swab16((uint16_t)(x)) })
#define be16_to_cpu(x) swab16((uint16_t)((x).__val))
#define cpu_to_be32(x) ((be32_t){ swab32((uint32_t)(x)) })
#define be32_to_cpu(x) swab32((uint32_t)((x).__val))
#define cpu_to_be64(x) ((be64_t){ swab64((uint64_t)(x)) })
#define be64_to_cpu(x) swab64((uint64_t)((x).__val))

#elif defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

#define cpu_to_le16(x) ((le16_t){ swab16((uint16_t)(x)) })
#define le16_to_cpu(x) swab16((uint16_t)((x).__val))
#define cpu_to_le32(x) ((le32_t){ swab32((uint32_t)(x)) })
#define le32_to_cpu(x) swab32((uint32_t)((x).__val))
#define cpu_to_le64(x) ((le64_t){ swab64((uint64_t)(x)) })
#define le64_to_cpu(x) swab64((uint64_t)((x).__val))

#define cpu_to_be16(x) ((be16_t){ (uint16_t)(x) })
#define be16_to_cpu(x) ((uint16_t)((x).__val))
#define cpu_to_be32(x) ((be32_t){ (uint32_t)(x) })
#define be32_to_cpu(x) ((uint32_t)((x).__val))
#define cpu_to_be64(x) ((be64_t){ (uint64_t)(x) })
#define be64_to_cpu(x) ((uint64_t)((x).__val))

#else
#error "Unknown host byte order. Compiler does not define __BYTE_ORDER__."
#endif

static inline void le16_add_cpu(le16_t *var, uint16_t val)
{
	*var = cpu_to_le16(le16_to_cpu(*var) + val);
}

static inline void le32_add_cpu(le32_t *var, uint32_t val)
{
	*var = cpu_to_le32(le32_to_cpu(*var) + val);
}

static inline void le64_add_cpu(le64_t *var, uint64_t val)
{
	*var = cpu_to_le64(le64_to_cpu(*var) + val);
}

static inline void be16_add_cpu(be16_t *var, uint16_t val)
{
	*var = cpu_to_be16(be16_to_cpu(*var) + val);
}

static inline void be32_add_cpu(be32_t *var, uint32_t val)
{
	*var = cpu_to_be32(be32_to_cpu(*var) + val);
}

static inline void be64_add_cpu(be64_t *var, uint64_t val)
{
	*var = cpu_to_be64(be64_to_cpu(*var) + val);
}

static inline void le32_to_cpu_array(uint32_t *dst, const le32_t *src, size_t words)
{
	size_t i;
	for (i = 0; i < words; i++)
		dst[i] = le32_to_cpu(src[i]);
}

static inline void cpu_to_le32_array(le32_t *dst, const uint32_t *src, size_t words)
{
	size_t i;
	for (i = 0; i < words; i++)
		dst[i] = cpu_to_le32(src[i]);
}

static inline void be32_to_cpu_array(uint32_t *dst, const be32_t *src, size_t words)
{
	size_t i;
	for (i = 0; i < words; i++)
		dst[i] = be32_to_cpu(src[i]);
}

static inline void cpu_to_be32_array(be32_t *dst, const uint32_t *src, size_t words)
{
	size_t i;
	for (i = 0; i < words; i++)
		dst[i] = cpu_to_be32(src[i]);
}

#endif /* MODVM_UTIL_BYTEORDER_H */
