#include "security_monitor.h"
#include "ecc.h"
#include <stdio.h>

#define SBI_CALL(which, arg0, arg1, arg2) {          \
    register unsigned long a0 asm ("a0") = (unsigned long)(arg0);   \
    register unsigned long a1 asm ("a1") = (unsigned long)(arg1);   \
    register unsigned long a2 asm ("a2") = (unsigned long)(arg2);   \
    register unsigned long a7 asm ("a7") = (unsigned long)(which);  \
    asm volatile ("ecall"                   \
              : "+r" (a0)               \
              : "r" (a1), "r" (a2), "r" (a7)        \
              : "memory");              \
    a0;                         \
}

uint8_t p_hash[ECC_BYTES];
uint8_t p_signature[ECC_BYTES*2];

#define SIGN_A_TRANSACTION 1
#define VERIFY_A_TRANSACTION 2
extern uint8_t publick_key[32];
extern uint8_t private_key[32];
uint64_t trap_handler(struct context *ctx){
	printf("entering enclave\n");
	switch(ctx->a7){
		case SIGN_A_TRANSACTION:{
			for(int i = 0; i < ECC_BYTES; i++)
				p_hash[i] = *((uint8_t*)(ctx->a1));
			ecdsa_sign(private_key, p_hash, p_signature);
			for(int i = 0; i < ECC_BYTES*2; i++)
				*((uint8_t*)(ctx->a0)) = p_signature[i];
			break;
		}
	}
	printf("leaving enclave\n");
}
