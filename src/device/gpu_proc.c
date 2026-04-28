/*
 * src/device/gpu_proc.c — GPU child-process render loop
 *
 * Runs in a forked child process.  Communicates with the parent simulator
 * via shared memory (framebuffer + control flags) and a POSIX semaphore.
 * Keyboard events are piped back to the parent.
 *
 * Shared-memory layout (all offsets from fb_base):
 *   [0         .. FB_SIZE-1]  framebuffer pixels (ARGB8888, 400×300)
 *   [FB_SIZE   .. FB_SIZE+3]  frame_ready  (uint32_t, 1 = new frame)
 *   [FB_SIZE+4 .. FB_SIZE+7]  running      (uint32_t, 0 = exit)
 */

#include <common.h>
#include <gpu.h>
#include <SDL2/SDL.h>
#include <stdatomic.h>
#include <stdio.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>

/* ── Globals (child-process private) ──────────────────────────────────── */
static SDL_Renderer *renderer  = NULL;
static SDL_Texture  *texture   = NULL;
static sem_t        *frame_sem = NULL;
static uint8_t      *fb_shm    = NULL;
static int           kbd_pipe_fd = -1;

/* Pointers into the control area at fb_shm + FB_SIZE */
static volatile uint32_t *g_frame_ready = NULL;
static volatile uint32_t *g_running     = NULL;

#define CTRL_OFFSET  FB_SIZE

/* ── init_sdl ─────────────────────────────────────────────────────────── */
static void init_sdl(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "GPU: SDL_Init failed: %s\n", SDL_GetError());
        _exit(1);
    }
    SDL_Window *window = NULL;
    if (SDL_CreateWindowAndRenderer(SCREEN_W * 2, SCREEN_H * 2,
                                    SDL_WINDOW_SHOWN,
                                    &window, &renderer) != 0) {
        fprintf(stderr, "GPU: SDL_CreateWindowAndRenderer failed: %s\n", SDL_GetError());
        _exit(1);
    }
    SDL_SetWindowTitle(window, "SUSTemu (GPU)");
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STATIC, SCREEN_W, SCREEN_H);
    if (!texture) {
        fprintf(stderr, "GPU: SDL_CreateTexture failed: %s\n", SDL_GetError());
        _exit(1);
    }
}

/* ── send_key_event ───────────────────────────────────────────────────── */
static void send_key_event(uint8_t scancode, int is_keydown)
{
    if (kbd_pipe_fd < 0) return;
    uint8_t buf[2] = { scancode, (uint8_t)is_keydown };
    write(kbd_pipe_fd, buf, 2);   /* best-effort; ignore short write */
}

/* ── gpu_proc_main ────────────────────────────────────────────────────── */
/*
 * Entry point for the forked GPU child process.
 *   shm_fd       — shared memory file descriptor (already open, inherited)
 *   kbd_write_fd — write end of the keyboard-event pipe
 *
 * When the parent sets *g_running = 0 and posts frame_sem, we exit.
 */
void gpu_proc_main(int shm_fd, int kbd_write_fd)
{
    kbd_pipe_fd = kbd_write_fd;

    /* Map shared memory — the parent already sized and mapped it, but the
     * child needs its own mapping for COW safety (MAP_SHARED ensures we
     * see parent writes). */
    fb_shm = (uint8_t *)mmap(NULL, FB_SIZE + 16,
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED, shm_fd, 0);
    if (fb_shm == MAP_FAILED) {
        perror("gpu_proc: mmap");
        _exit(1);
    }
    g_frame_ready = (volatile uint32_t *)(fb_shm + CTRL_OFFSET);
    g_running     = (volatile uint32_t *)(fb_shm + CTRL_OFFSET + 4);

    /* Open the named semaphore */
    frame_sem = sem_open("/sustemu_gpu_sem", 0);
    if (frame_sem == SEM_FAILED) {
        perror("gpu_proc: sem_open");
        _exit(1);
    }

    init_sdl();

    /* ── Main render loop ─────────────────────────────────────────────── */
    *g_frame_ready = 0;
    *g_running     = 1;

    while (*g_running) {
        sem_wait(frame_sem);

        if (!*g_running) break;

        if (*g_frame_ready) {
            SDL_UpdateTexture(texture, NULL, fb_shm,
                              SCREEN_W * sizeof(uint32_t));
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
            *g_frame_ready = 0;
        }

        /* Drain SDL events → pipe keyboard to parent */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                /* Forward quit: send a special scancode the parent recognises */
                send_key_event(0xFF, 0);
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                send_key_event(ev.key.keysym.scancode,
                               ev.key.type == SDL_KEYDOWN);
                break;
            }
        }
    }

    /* Cleanup */
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_Quit();
    sem_close(frame_sem);
    munmap(fb_shm, FB_SIZE + 16);
    close(kbd_pipe_fd);
    _exit(0);
}
