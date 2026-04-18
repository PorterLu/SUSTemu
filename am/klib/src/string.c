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
	size_t i;
	if((int64_t)n < 0) return s;
	for(i=0; i<n; i++)
		*((char*)s + i) = c;
	return s;
}

void *memmove(void *dst, const void *src, size_t n) {
  	char* tmp = (char *)malloc(n);
  	size_t i;

  	//firstly, we copy the source to tmp array
  	for(i = 0; i < n; i++)
		tmp[i] = *((char*) src + i);

  	//then we move data to destination
  	for(i = 0; i < n; i++)
		*((char*) dst + i) = tmp[i];

	free(tmp);
  	return dst;
}

void *memcpy(void *out, const void *in, size_t n) {
	return memmove(out, in, n);
	/*bool overtop = false;

	if(n == 0)
		return 0;

	size_t i;
	if(in + n > out || out + n > in)
		overtop = true;
	
	if(overtop)
	{
		if(out > in)
		{
			for(i = n-1; i>=0; i--)
				*((char*)out + i) = *((char*)in + i);
		}
		else
		{
			for(i = 0; i < n; i++)
				*((char*)out + i) = *((char*)in + i);
		}
	}
	else
	{
		for(i = 0; i < n; i++)
		*((char*)out + i) = *((char*)in + i);

	}*/
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
