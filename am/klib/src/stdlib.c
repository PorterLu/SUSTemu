#include <am.h>
#include <klib.h>
#include <klib-macros.h>

extern Area heap;

//#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)
static unsigned long int next = 1;

int rand(void) {
  // RAND_MAX assumed to be 32767
  next = next * 1103515245 + 12345;
  return (unsigned int)(next/65536) % 32768;
}

void srand(unsigned int seed) {
  next = seed;
}

int abs(int x) {
  return (x < 0 ? -x : x);
}

int atoi(const char* nptr) {
  int x = 0;
  while (*nptr == ' ') { nptr ++; }
  while (*nptr >= '0' && *nptr <= '9') {
    x = x * 10 + *nptr - '0';
    nptr ++;
  }
  return x;
}

#if !(defined(__ISA_NATIVE__) && defined(__NATIVE_USE_KLIB__))

static struct {
  void *ptr;
  uintptr_t size;
} last = { .ptr = NULL, .size = 0 };



void *malloc(size_t size) {
  // On native, malloc() will be called during initializaion of C runtime.
  // Therefore do not call panic() here, else it will yield a dead recursion:
  //   panic() -> putchar() -> (glibc) -> malloc() -> panic()
  /*
	
	static void* heap_addr;
	static bool is_first = true;

	if(is_first){
		is_first = false;
		heap_addr =  heap.start;
	}


	//char buf[1024];
	//int len = sprintf(buf, "%d", heap_addr);
	//for(int i = 0; i<len ;i++)
		//putch(buf[i]);
	//printf(";size%d\n",size);
	//putch('\n');

#define PG_SIZE (1 << 12)
	void* old_addr = heap_addr;
	//if(((uint64_t) old_addr) % 8 != 0)
	//	old_addr += (((uint64_t) old_addr) % 8);
	//heap_addr = (void*)(((uint64_t)(old_addr + PG_SIZE - 1)/PG_SIZE)*PG_SIZE + size);
	heap_addr += size;
	return old_addr;    
#endif*/ 
  //return NULL;
  if (last.ptr == NULL) {
    last.ptr = heap.start;
    printf("heap start = %d", last.ptr);
  }
  
  // aligning
  size = ROUNDUP(size, sizeof(uintptr_t));

  // skip the region allocated by the last call
  last.ptr += last.size;
  if (last.ptr + size >= heap.end) return NULL;
  void *ret = last.ptr;
  last.size = size;
  return ret;

}

/*
void free(void *ptr) {
}*/
void free(void *ptr) {
  if (ptr == last.ptr) last.size = 0;
}


#endif
