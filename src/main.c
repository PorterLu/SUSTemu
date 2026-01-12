#include <vmem.h>
#include <assert.h>
#include <getopt.h>
#include <debug.h>
#include <log.h>
#include <vmem.h>
#include <string.h>
#include <img.h>
#include <reg.h>
#include <sdb.h>
#include <state.h>
#include <expr.h>
#include <exec.h>
#include <watchpoint.h>
#include <disasm.hpp>
#include <elftl.h>
#include <serial.h>
#include <timer.h>
#include <gpu.h>
#include <keyboard.h>
#include <mmio.h>
#include <decode.h>
#include <flash.h>

char* elf_file = NULL;

void halt()
{
	state = NEMU_ABORT;
	return;
}

int parse_args(int argc, char *argv[])
{
	const struct option table[] = {
		{"batch",	no_argument			, NULL, 'b'},
		{"elf"	,	required_argument	, NULL, 'e'},
		{"log"	,	required_argument	, NULL, 'l'},
		{"task"	, 	required_argument	, NULL, 't'},
		{0		, 	0					, NULL,  0 }
	};
	
	int o;
	while((o=getopt_long(argc, argv, "-bs:e:l:t:", table, NULL))!=-1)
	{
		switch(o)
		{
			case 'b': set_batch_mode(); break;		
			case 'l': log_file = optarg;break;
			case 'e': elf_file = optarg;break;
			case 't': task_file = optarg; break;
			case 1: img_file = optarg;return 0;
		}
	}

	return 0;
}

int main(int argc, char *argv[]){
	parse_args(argc, argv);
	init_log(log_file);
	init_disasm("riscv64-pc-linux-gnu");
	init_regex();
	init_wp_pool();
	init_default_program();
	load_img();
	init_elf(elf_file);
	init_map();
	init_decode_info();

	printf(ANSI_FMT("     _____  _    _   _____  _______  ______  __  __  _    _\n\
    / ____|| |  | | / ____||__   __||  ____||  \\/  || |  | |\n\
   | (___  | |  | || (___     | |   | |__   | \\  / || |  | |\n\
    \\___ \\ | |  | | \\___ \\    | |   |  __|  | |\\/| || |  | |\n\
    ____) || |__| | ____) |   | |   | |____ | |  | || |__| |\n\
   |_____/  \\____/ |_____/    |_|   |______||_|  |_| \\____/\n\n", ANSI_FG_BLUE));

#ifdef CONFIG_flash
	init_flash();
#endif

#ifdef CONFIG_serial
	init_serial();
#endif

#ifdef CONFIG_timer
	init_timer();
#endif

#ifdef CONFIG_gpu
	init_vga();
#endif

#ifdef CONFIG_keyboard
	init_i8042();
#endif

	sdbloop();

	return status();
}	
