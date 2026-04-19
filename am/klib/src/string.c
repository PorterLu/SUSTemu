#include <klib.h>
#include <klib-macros.h>
#include <stdint.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

size_t strlen(const char *s) {
	size_t n = 0;
	while(*(s + n) != 0)
		n++;
	return n;
}

char *strcpy(char *dst, const char *src) {
	size_t i = 0;
	do{
		dst[i] = src[i];
	}while(src[i++] != 0);
	return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
	size_t i;

	for (i = 0; i < n && src[i] != '\0'; i++)
		dst[i] = src[i];
	for ( ; i < n; i++)
		dst[i] = '\0';

	if(n > 0)
		dst[i-1] = '\0'; 

	return dst;
}

char *strcat(char *dst, const char *src) {
	size_t i=0,n = 0;
	while(*(dst + n) != '\0')
		n++;
	while(src[i] != '\0')
	{
		dst[n+i] = src[i];
		i++;
	}
	dst[n+i] = '\0';
	return dst;
}

int strcmp(const char *s1, const char *s2) {
	size_t i = 0;
	while(s1[i] != '\0' && s2[i] != '\0')
	{
		if(s1[i] == s2[i])
			i++;
		else
			return s1[i] - s2[i];
	}

	return s1[i] - s2[i];
}

int strncmp(const char *s1, const char *s2, size_t n) {
	size_t i = 0;
	while(i<n && s1[i] != '\0' && s2[i] != '\0')
	{
		if(s1[i] == s2[i])
			i++;
		else
			return s1[i] - s2[i];
	}

	return s1[i] - s2[i];

}

void *memset(void *s, int c, size_t n) {
	uint8_t *d = (uint8_t *)s;
	uint8_t  b = (uint8_t)c;

	/* Fill leading unaligned bytes */
	while (n > 0 && ((uintptr_t)d & 7)) {
		*d++ = b;
		n--;
	}
	/* Fill 8 bytes at a time */
	if (n >= 8) {
		uint64_t w = (uint64_t)b * 0x0101010101010101ULL;
		uint64_t *p = (uint64_t *)d;
		size_t words = n >> 3;
		for (size_t i = 0; i < words; i++) p[i] = w;
		d += words << 3;
		n &= 7;
	}
	/* Fill trailing bytes */
	while (n--) *d++ = b;
	return s;
}

void *memmove(void *dst, const void *src, size_t n) {
	uint8_t *d = (uint8_t *)dst;
	const uint8_t *s = (const uint8_t *)src;
	if (d == s || n == 0) return dst;
	if (d < s || d >= s + n) {
		/* Forward copy — no overlap or dst before src */
		/* Align dst to 8-byte boundary */
		while (n > 0 && ((uintptr_t)d & 7)) { *d++ = *s++; n--; }
		/* Copy 8 bytes at a time if src is also aligned */
		if (!((uintptr_t)s & 7)) {
			uint64_t *pd = (uint64_t *)d;
			const uint64_t *ps = (const uint64_t *)s;
			while (n >= 8) { *pd++ = *ps++; n -= 8; }
			d = (uint8_t *)pd;
			s = (const uint8_t *)ps;
		}
		while (n--) *d++ = *s++;
	} else {
		/* Backward copy — dst overlaps src from behind */
		d += n; s += n;
		while (n--) *--d = *--s;
	}
	return dst;
}

void *memcpy(void *out, const void *in, size_t n) {
	/* Non-overlapping: forward copy with 8-byte words */
	uint8_t *d = (uint8_t *)out;
	const uint8_t *s = (const uint8_t *)in;
	/* Align dst */
	while (n > 0 && ((uintptr_t)d & 7)) { *d++ = *s++; n--; }
	/* 8-byte copy if src aligned */
	if (!((uintptr_t)s & 7)) {
		uint64_t *pd = (uint64_t *)d;
		const uint64_t *ps = (const uint64_t *)s;
		while (n >= 8) { *pd++ = *ps++; n -= 8; }
		d = (uint8_t *)pd; s = (const uint8_t *)ps;
	}
	while (n--) *d++ = *s++;
	return out;
}

int memcmp(const void *s1, const void *s2, size_t n) {
	
	size_t i = 0;
	for(i = 0; i < n; i++)
	{
		if(*((char*)s1 + i) != *((char*)s2 + i))
			return *((char*)s1 + i) - *((char*)s2 + i);
	}

	return 0;
}

#endif
