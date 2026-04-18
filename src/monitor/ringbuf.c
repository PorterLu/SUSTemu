#include <ringbuf.h>
#include <stdio.h>
#include <string.h>

#define RINGBUFSIZE 15

typedef struct ringbuf{
	char buf[RINGBUFSIZE][128];
	uint32_t pointer;
} ringbuf;

static ringbuf rb;

void print_ringbuf(){
	for(int i = 0; i < RINGBUFSIZE; i++){
		if(i == rb.pointer)
			fprintf(stderr, "---> ");
		else
			fprintf(stderr, "     ");
		fprintf(stderr, "%s\n", rb.buf[i]);
	}
}

void add_ringbuf_inst(char* inst){
	strcpy(rb.buf[rb.pointer], inst);
	rb.pointer = (rb.pointer + 1)%RINGBUFSIZE;
}
