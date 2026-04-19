#include <exec.h>
#include <core.h>
#include <disasm.hpp>
#include <state.h>
#include <vmem.h>
#include <log.h>
#include <watchpoint.h>
#include <reg.h>
#include <elftl.h>
#include <gpu.h>
#include <timer.h>
#include <SDL2/SDL.h>
#include <keyboard.h>
#include <ringbuf.h>
#include <pmem.h>
#include <decode.h>
#include <csr.h>
#include <ir.h>
#include <pipeline.h>
#include <ooo.h>
#include <cache.h>

static bool g_print_step = 0;
int g_inorder_mode = 0;   /* set to 1 by --inorder command-line flag */
int g_bpred_mode   = 0;   /* set to 1 by --bpred command-line flag   */
extern void halt();
char log_buf[128];

/*
 * exec_once — fetch, decode (IR), execute, writeback one instruction.
 *
 * This replaces the old single-pass decode_exec() approach with the
 * three-step IR pipeline introduced in Phase 1:
 *
 *   1. ir_decode    — pattern-match raw bits → IR_Inst (operands + exec_fn)
 *   2. ir_execute   — call exec_fn → compute result / dnpc / side-effects
 *   3. ir_writeback — commit result to GPR file
 *
 * The global `s` (Decode) struct is kept up-to-date so that the SDB
 * monitor and disassembler continue to work without changes.
 */
static void exec_once()
{
	vaddr_t pc   = cpu.pc;
	vaddr_t snpc = pc + 4;

	pmp_check(pc, true, false);
	uint32_t raw = (uint32_t)vaddr_ifetch(pc, 4);

	/* Keep the legacy Decode struct in sync for monitor compatibility */
	s.pc      = pc;
	s.snpc    = snpc;
	s.inst.val = raw;

	IR_Inst ir;
	ir_decode(raw, pc, snpc, &ir);
	ir_execute(&ir, &cpu);
	ir_mem_access(&ir, NULL);    /* MEM stage: actual load/store */
	ir_writeback(&ir, &cpu);

	cpu.pc   = ir.dnpc;
	s.dnpc   = ir.dnpc;
	s.snpc   = ir.dnpc;   /* mirrors old: s.snpc = s.dnpc */

	g_sim_cycles++;   /* functional mode: 1 cycle per instruction */
	g_sim_instret++;

	/* ── Instruction trace (same format as before) ─────────────── */
	if (g_print_step || log_fp) {
		sprintf(log_buf, "%016lx:    ", pc);
		for (int i = 0; i < 4; i++)
			sprintf(log_buf + 21 + 3 * i, "%02x ", *(((uint8_t *)&raw) + 3 - i));
		sprintf(log_buf + 33, "   ");
		disassemble(log_buf + 36, 70, pc, (uint8_t *)(&raw), 4);

		if (g_print_step)
			printf("%s\n", log_buf);
		log_write("%s\n", log_buf);
		add_ringbuf_inst(log_buf);
	}

	/*****************************vga更新***************************************
	 * 每隔一段时间将vga屏幕上的信息进行更新                                         *
	 **************************************************************************/
#ifdef CONFIG_timer
	static uint64_t last = 0;
	uint64_t now = get_time();
	if ((now - last) < 1000000 / 60)
		return;
	last = now;
#endif

#ifdef CONFIG_gpu
	vga_update_screen();
#endif

	/***************************事件********************************************
	 * 用于将事件全部放入到一个环形队列中                                              *
	 **************************************************************************/
#ifdef CONFIG_keyboard
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
		case SDL_QUIT:
			state = NEMU_QUIT;
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP: {
			uint8_t k = event.key.keysym.scancode;
			bool is_keydown = (event.key.type == SDL_KEYDOWN);
			send_key(k, is_keydown);
			break;
		}
		default:
			break;
		}
	}
#endif
}

void exec(uint64_t n)
{
	g_print_step = (n < MAX_INST_TO_PRINT);
	switch (state) {
	case NEMU_END:
	case NEMU_ABORT:
		Log("Program execution has ended.\n");
		return;
	default:
		state = NEMU_RUNNING;
	}

	if (g_num_cores > 1) {
		/* ── Multi-core mode: use Core abstraction ───────────────────── */
		/* Initialise each core's engine on first run */
		static int cores_initialised = 0;
		if (!cores_initialised) {
			cores_initialised = 1;
			for (int i = 0; i < g_num_cores; i++) {
				Core *c = &cores[i];
				/* Install this core's context before init so cache globals are valid */
				g_current_hartid = c->core_id;
				cpu   = c->cpu;
				csr   = c->csr;
				L1I_cache = c->l1i;
				L1D_cache = c->l1d;
				if (c->mode == CORE_MODE_INORDER)
					pipeline_init_core(c);
				else if (c->mode == CORE_MODE_OOO)
					ooo_init_core(c);
			}
		}

		uint64_t total = 0;
		while (total < n && state == NEMU_RUNNING) {
			for (int i = 0; i < g_num_cores; i++) {
				core_cycle(&cores[i]);
				total++;
				if (wp_any_active() && check_wp() && state != NEMU_ABORT)
					state = NEMU_STOP;
				if (state != NEMU_RUNNING) break;
			}
		}
		for (int i = 0; i < g_num_cores; i++)
			core_report(&cores[i]);
	} else if (g_ooo_mode) {
		/* ── Out-of-order (Tomasulo) mode ──────────────────────── */
		ooo_init();
		while (ooo_stats.insts < n && state == NEMU_RUNNING) {
			ooo_cycle();
			if (wp_any_active() && check_wp() && state != NEMU_ABORT)
				state = NEMU_STOP;
#ifdef CONFIG_timer
			static uint64_t ooo_last = 0;
			static uint64_t ooo_cycle_ctr = 0;
			if ((++ooo_cycle_ctr & 4095) == 0) {
				uint64_t ooo_now = get_time();
				if ((ooo_now - ooo_last) >= 1000000 / 60) {
					ooo_last = ooo_now;
#ifdef CONFIG_gpu
					vga_update_screen();
#endif
#ifdef CONFIG_keyboard
					SDL_Event event;
					while (SDL_PollEvent(&event)) {
						switch (event.type) {
						case SDL_QUIT: state = NEMU_QUIT; break;
						case SDL_KEYDOWN:
						case SDL_KEYUP: {
							uint8_t k = event.key.keysym.scancode;
							send_key(k, event.key.type == SDL_KEYDOWN);
							break;
						}
						default: break;
						}
					}
#endif
				}
			}
#endif
		}
		ooo_report();
	} else if (g_inorder_mode) {
		/* ── In-order pipeline mode ────────────────────────────── */
		pipeline_init();
		while (pipe_stats.insts < n && state == NEMU_RUNNING) {
			pipeline_cycle();
			if (wp_any_active() && check_wp() && state != NEMU_ABORT)
				state = NEMU_STOP;
#ifdef CONFIG_timer
			static uint64_t pip_last = 0;
			static uint64_t pip_cycle_ctr = 0;
			if ((++pip_cycle_ctr & 4095) == 0) {
				uint64_t pip_now = get_time();
				if ((pip_now - pip_last) >= 1000000 / 60) {
					pip_last = pip_now;
#ifdef CONFIG_gpu
					vga_update_screen();
#endif
#ifdef CONFIG_keyboard
					SDL_Event event;
					while (SDL_PollEvent(&event)) {
						switch (event.type) {
						case SDL_QUIT: state = NEMU_QUIT; break;
						case SDL_KEYDOWN:
						case SDL_KEYUP: {
							uint8_t k = event.key.keysym.scancode;
							send_key(k, event.key.type == SDL_KEYDOWN);
							break;
						}
						default: break;
						}
					}
#endif
				}
			}
#endif
		}
		pipeline_report();
	} else {
		/* ── Functional simulation mode (default) ────────── */
		for (; n > 0; n--) {
			exec_once();
			if (wp_any_active() && check_wp() && state != NEMU_ABORT)
				state = NEMU_STOP;
			if (state != NEMU_RUNNING)
				break;
		}
	}

	switch (state) {
	case NEMU_ABORT:
		if (!status())
			printf(ANSI_FMT("HIT GOOG TRAP\n", ANSI_FG_GREEN));
		else
			printf(ANSI_FMT("HIT BAD TRAP\n", ANSI_FG_RED));
	}

}
