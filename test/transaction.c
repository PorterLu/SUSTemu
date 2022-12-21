#include "transaction.h"
#include "sbi_call.h"
#include "sha256.h"
#include <stdio.h>
#include "block.h"
#include <time.h>

transaction *head;
transaction *pointer;
uint8_t public_key[32] = {0x03,0x61,0x08,0x2a,0x8a,0x35,0x4c,0xe2,0xad,0xe2,0x98,0x01,0x1d,0x8c,0xcc,0x26,0x93,0xfd,0xa1,0x68,0x97,0x77,0x66,0x37,0x94,0x69,0xc0,0x04,0x80,0x23,0x8a,0x40};
uint8_t receiver[32] = {0};
transaction item[9999];
uint8_t sign[9999][64];
uint8_t hash[9999][32];
uint32_t global_index = 0;
char message[9999][128];

struct Block block[128];
uint32_t global_block_index = 0;

transaction* pack(){
	transaction* tmp = head;
	head = &item[global_index++];
	pointer = head;
	return head;
}

//struct context ctx;
void add_a_transaction(){
	pointer->sender = public_key;
	pointer->receiver = public_key;
	pointer->ammount = 1;
	sprintf(message[global_index], "create a transaction %d", global_index);
	if(global_index < 9998)
			pointer->next = &item[global_index];
	else
			pointer->next = NULL;
	sha256(pointer, sizeof(pointer), hash[global_index]);
	//asm volatile("csrw mscratch, %0"::"r"(&ctx));
	SBI_CALL(1, sign[global_index], hash[global_index], NULL)
	pointer->signature = sign[global_index];
	global_index++;
	pointer = pointer->next;
}


int main(){
	head = pointer = &item[0];
	for(int i = 0; i < 9999; i++){	
		add_a_transaction();
		printf("Transaction %d, amount 1 from I to I\n", global_index);
		if(i % 100 == 0){
			block[global_block_index].index = global_block_index;
			block[global_block_index].time = get_time();
			block[global_block_index].data = (void*)pack();
			if(global_block_index == 0)
					for(int j = 0; j < 32; j++)
						block[global_block_index].pre_hash[j] = 0;
			else {
					for(int j = 0; j < 32; j++)
						block[global_block_index].pre_hash[j] = block[global_block_index-1].hash[j];
			}
			sha256(&block[global_block_index], sizeof(block), block[global_block_index].hash);
			int j = get_time();
			while(j % block[global_block_index].hash[0] > 3) { 
				j = get_time();
				printf("mining\n");
			}
			printf("block be created\n");
			printf("===================\n");
			printf("Id: %d; Time:\nHash:", global_block_index, block[global_block_index].time);
			for(int i = 0; i < 32; i++){
				printf("%d ", block[global_block_index].hash[i]);
			}
			printf("\n==================\n");
		}
	}
}
