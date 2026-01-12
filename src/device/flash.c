#include <mmio.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "flash.h"

uint8_t *flash_base = NULL;
char *task_file = NULL;

void flash_io_handler(uint32_t offset, int len, int is_write){}

long init_flash(){
	flash_base = (uint8_t*)new_space(0x100000);
	add_mmio_map("flash", FLASH_BASE_ADDR, flash_base, 0x100000, flash_io_handler);
	
	FILE *fp = fopen(task_file, "rb");
        if(!fp) {
                printf("No task_file found\n");
                return 0;
        }


	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	int ret = fread(flash_base, size, 1, fp);
	if(ret == -1){
		printf("read error\n");
	}

	fclose(fp);
	return size;
}
