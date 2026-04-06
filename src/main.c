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
#include <cpu/bpred2.h>
#include <elftl.h>
#include <serial.h>
#include <timer.h>
#include <gpu.h>
#include <keyboard.h>
#include <mmio.h>
#include <decode.h>
#include <flash.h>
#include <cache.h>
#include <core.h>

char* elf_file = NULL;
int   g_trace_en = 0;   /* --trace: per-cycle pipeline trace to log file */

extern int g_inorder_mode;  /* defined in exec.c */
extern int g_bpred_mode;    /* defined in exec.c */
extern int g_ooo_mode;      /* defined in ooo.c  */

void halt()
{
	state = NEMU_ABORT;
	return;
}

/* Default mode flags — set by parse_args, used after init to create cores */
static CoreMode g_default_mode   = CORE_MODE_FUNCTIONAL;
static int      g_dual_flag      = 0;  /* --dual: enable 2-core simulation */

int parse_args(int argc, char *argv[])
{
	const struct option table[] = {
		{"batch",	no_argument			, NULL, 'b'},
		{"elf"	,	required_argument	, NULL, 'e'},
		{"log"	,	required_argument	, NULL, 'l'},
		{"task"	, 	required_argument	, NULL, 't'},
		{"inorder",	no_argument			, NULL, 'i'},
		{"bpred",	no_argument			, NULL, 'p'},
		{"bpred2",	no_argument			, &g_bpred2_mode, 1},
		{"ooo",		no_argument			, NULL, 'o'},
		{"dual",	no_argument			, NULL, 'd'},
		{"trace",	no_argument			, &g_trace_en, 1},
		{0		, 	0					, NULL,  0 }
	};

	int o;
	while((o=getopt_long(argc, argv, "-bs:e:l:t:ipod", table, NULL))!=-1)
	{
		switch(o)
		{
			case 'b': set_batch_mode(); break;
			case 'l': log_file = optarg;break;
			case 'e': elf_file = optarg;break;
			case 't': task_file = optarg; break;
			case 'i': g_inorder_mode = 1; g_default_mode = CORE_MODE_INORDER; break;
			case 'p': g_bpred_mode = 1; break;
			case 'o': g_ooo_mode = 1;   g_default_mode = CORE_MODE_OOO; break;
			case 'd': g_dual_flag = 1; break;
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
	init_regs();

	printf(ANSI_FMT("     _____  _    _   _____  _______  ______  __  __  _    _\n\
    / ____|| |  | | / ____||__   __||  ____||  \\/  || |  | |\n\
   | (___  | |  | || (___     | |   | |__   | \\  / || |  | |\n\
    \\___ \\ | |  | | \\___ \\    | |   |  __|  | |\\/| || |  | |\n\
    ____) || |__| | ____) |   | |   | |____ | |  | || |__| |\n\
   |_____/  \\____/ |_____/    |_|   |______||_|  |_| \\____/\n\n", ANSI_FG_BLUE));

	init_cache_system();

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

	/* Create core(s) after all init is done (init_regs sets cpu, load_img
	 * sets memory, so architectural state is ready to snapshot). */
	g_num_cores = g_dual_flag ? 2 : 1;
	for (int i = 0; i < g_num_cores; i++)
		core_create(i, g_default_mode, g_bpred_mode);

	sdbloop();

	return status();
}
