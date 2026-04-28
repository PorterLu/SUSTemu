#ifndef __GPU_H__
#define __GPU_H__

#include <semaphore.h>

#define CONFIG_FB_ADDR      0xa1000000
#define CONFIG_VGA_CTL_MMIO 0xa0000100
#define SCREEN_W 400
#define SCREEN_H 300
#define FB_SIZE  (SCREEN_W * SCREEN_H * 4)

void init_vga(void);
void vga_update_screen(void);
void gpu_kbd_poll(void);

/* Direct pointer to the host framebuffer — valid after init_vga().
 * Used by paddr_write for a fast path that bypasses MMIO dispatch. */
extern uint8_t *g_fb_base;

/* GPU child process PID (0 if not forked / fallback direct rendering) */
extern pid_t g_gpu_pid;

/* Keyboard pipe: simulator reads events from g_kbd_fd */
extern int g_kbd_fd;

#endif
