/*
 * src/device/gpu.c — VGA / GPU subsystem (parent side)
 *
 * Manages the framebuffer shared memory, forks a GPU child process for
 * asynchronous SDL rendering, and handles keyboard events via a pipe.
 *
 * When the GPU process is active (g_gpu_pid > 0):
 *   - vga_update_screen() posts a semaphore instead of calling SDL directly.
 *   - gpu_kbd_poll() reads keyboard events from the pipe.
 *
 * Fallback: if fork fails, rendering falls back to the original synchronous
 * path (SDL calls happen in-process on the main thread).
 */

#include <common.h>
#include <mmio.h>
#include <gpu.h>
#include <SDL2/SDL.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>

uint8_t *g_fb_base = NULL;
pid_t    g_gpu_pid = 0;
int      g_kbd_fd  = -1;

/* ── Internal globals ─────────────────────────────────────────────────── */
static void       *vmem_ptr      = NULL;   /* mmap'd region or heap */
static uint32_t   *vgactl        = NULL;
static sem_t      *frame_sem     = NULL;
static int         shm_fd        = -1;
static int         kbd_pipe[2]   = {-1, -1};

/* Control flags at FB_SIZE offset in shared memory */
static volatile uint32_t *shm_frame_ready = NULL;
static volatile uint32_t *shm_running     = NULL;

#define CTRL_OFFSET  FB_SIZE

/* ── Fallback SDL rendering (used when GPU process is not active) ─────── */
static SDL_Renderer *fb_renderer = NULL;
static SDL_Texture  *fb_texture  = NULL;

static void init_screen(void)
{
    SDL_Window *window = NULL;
    SDL_Init(SDL_INIT_VIDEO);
    SDL_CreateWindowAndRenderer(SCREEN_W * 2, SCREEN_H * 2, 0,
                                &window, &fb_renderer);
    SDL_SetWindowTitle(window, "SUSTemu");
    fb_texture = SDL_CreateTexture(fb_renderer, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STATIC, SCREEN_W, SCREEN_H);
}

static void update_screen(void)
{
    SDL_UpdateTexture(fb_texture, NULL, vmem_ptr, SCREEN_W * sizeof(uint32_t));
    SDL_RenderClear(fb_renderer);
    SDL_RenderCopy(fb_renderer, fb_texture, NULL, NULL);
    SDL_RenderPresent(fb_renderer);
}

/* ── GPU child process entry (defined in gpu_proc.c) ──────────────────── */
extern void gpu_proc_main(int shm_fd, int kbd_write_fd);

/* ── init_vga ─────────────────────────────────────────────────────────── */
void init_vga(void)
{
    /* vgactl: synchronisation register (MMIO), always heap-allocated */
    vgactl = (uint32_t *)new_space(8);
    vgactl[0] = (SCREEN_W << 16) | SCREEN_H;
    vgactl[1] = 0;
    add_mmio_map("vgactl", CONFIG_VGA_CTL_MMIO, vgactl, 8, NULL);

    /* GPU child process (Linux only — macOS fork() breaks Cocoa/SDL).
     * On macOS we use the traditional in-process rendering path. */
#ifdef __APPLE__
    goto fallback;
#else
    /* Allocate framebuffer via shared memory for zero-copy GPU access */
    shm_fd = shm_open("/sustemu_gpu_fb", O_CREAT | O_RDWR, 0600);
    if (shm_fd < 0) goto fallback;

    if (ftruncate(shm_fd, FB_SIZE + 16) < 0) { close(shm_fd); goto fallback; }

    vmem_ptr = mmap(NULL, FB_SIZE + 16, PROT_READ | PROT_WRITE,
                    MAP_SHARED, shm_fd, 0);
    if (vmem_ptr == MAP_FAILED) { close(shm_fd); goto fallback; }

    /* Unlink the shm name so it's cleaned up when both processes exit */
    shm_unlink("/sustemu_gpu_fb");

    g_fb_base        = (uint8_t *)vmem_ptr;
    shm_frame_ready  = (volatile uint32_t *)(g_fb_base + CTRL_OFFSET);
    shm_running      = (volatile uint32_t *)(g_fb_base + CTRL_OFFSET + 4);
    *shm_frame_ready = 0;
    *shm_running     = 1;

    add_mmio_map("vmem", CONFIG_FB_ADDR, vmem_ptr, FB_SIZE, NULL);

    /* Create named semaphore */
    sem_unlink("/sustemu_gpu_sem");
    frame_sem = sem_open("/sustemu_gpu_sem", O_CREAT | O_EXCL, 0600, 0);
    if (frame_sem == SEM_FAILED) { munmap(vmem_ptr, FB_SIZE + 16); close(shm_fd); goto fallback; }

    /* Create keyboard pipe */
    if (pipe(kbd_pipe) < 0) { sem_close(frame_sem); sem_unlink("/sustemu_gpu_sem"); munmap(vmem_ptr, FB_SIZE + 16); close(shm_fd); goto fallback; }

    g_gpu_pid = fork();
    if (g_gpu_pid == 0) {
        /* Child */
        close(kbd_pipe[0]);            /* close read end of keyboard pipe */
        gpu_proc_main(shm_fd, kbd_pipe[1]);
        /* never returns */
    }

    if (g_gpu_pid < 0) {
        /* Fork failed */
        close(kbd_pipe[1]); close(kbd_pipe[0]);
        sem_close(frame_sem); sem_unlink("/sustemu_gpu_sem");
        munmap(vmem_ptr, FB_SIZE + 16); close(shm_fd);
        g_gpu_pid = 0;
        goto fallback;
    }

    /* Parent: close write end of pipe, keep read end for keyboard poll */
    close(kbd_pipe[1]);
    g_kbd_fd = kbd_pipe[0];

    /* Clear framebuffer */
    memset(vmem_ptr, 0, FB_SIZE);
    return;
#endif /* !__APPLE__ */

fallback:
    /* Direct (in-process) rendering */
    g_gpu_pid = 0;
    g_kbd_fd = -1;
    vmem_ptr = new_space(FB_SIZE);
    g_fb_base = (uint8_t *)vmem_ptr;
    add_mmio_map("vmem", CONFIG_FB_ADDR, vmem_ptr, FB_SIZE, NULL);
    init_screen();
    memset(vmem_ptr, 0, FB_SIZE);
}

/* ── vga_update_screen ────────────────────────────────────────────────── */
void vga_update_screen(void)
{
    if (vgactl[1] != 1) return;

    if (g_gpu_pid > 0) {
        *shm_frame_ready = 1;
        sem_post(frame_sem);
    } else {
        update_screen();
    }
    vgactl[1] = 0;
}

/* ── gpu_kbd_poll ─────────────────────────────────────────────────────── */
/*
 * Read keyboard events from the GPU process pipe (non-blocking).
 * Called periodically from the simulation main loop.
 */
void gpu_kbd_poll(void)
{
    if (g_gpu_pid > 0) {
        /* GPU process path: read keyboard events from pipe */
        if (g_kbd_fd < 0) return;
        uint8_t buf[2];
        while (1) {
            ssize_t n = read(g_kbd_fd, buf, sizeof(buf));
            if (n < (ssize_t)sizeof(buf)) break;

            uint8_t scancode  = buf[0];
            int     is_down   = buf[1];

            if (scancode == 0xFF) {
                extern int state;
                state = 4;  /* NEMU_QUIT */
                continue;
            }

            extern void send_key(uint8_t, bool);
            send_key(scancode, (bool)is_down);
        }
    } else {
        /* In-process path: poll SDL events directly */
        extern int state;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT: state = 4; break;  /* NEMU_QUIT */
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                extern void send_key(uint8_t, bool);
                send_key(ev.key.keysym.scancode,
                         ev.key.type == SDL_KEYDOWN);
                break;
            }
            }
        }
    }
}
