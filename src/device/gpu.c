#include <common.h>
#include <mmio.h>
#include <gpu.h>
#include <SDL2/SDL.h>

/*************************************屏幕大小*************************************
* 这里逻辑上看到的宽度和高度固定设置为400和300，但是实际在显示的时候，我们申请了一个800 * 600*
* 的屏幕，这意味着画面会被放大。同时由于一个像素我们用"AARRGGBB"的格式进行存储，所以会占用32个*
* bits。SCREEN_W/SCREEN_H/FB_SIZE 定义在 include/device/gpu.h。                  *
********************************************************************************/

uint8_t *g_fb_base = NULL;  /* direct host pointer to framebuffer; set by init_vga() */

static uint32_t screen_width() {
  return SCREEN_W;
}

static uint32_t screen_height() {
  return SCREEN_H;
}

static uint32_t screen_size() {
  return screen_width() * screen_height() * sizeof(uint32_t);
}

static void *vmem = NULL;
static uint32_t *vgactl_port_base = NULL;

static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;


/*******************************初始化屏幕*****************************************
 * 初始化屏幕，我们设置SDL的VIDEO子系统初始化，设置默认窗口和渲染器，设置标题，为渲染器设置纹理* 
 ********************************************************************************/
static void init_screen() {
  SDL_Window *window = NULL;
  char title[128] = "npc";
  SDL_Init(SDL_INIT_VIDEO);
  SDL_CreateWindowAndRenderer(
      SCREEN_W * 2,
      SCREEN_H * 2,
      0, &window, &renderer);
  SDL_SetWindowTitle(window, title);
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STATIC, SCREEN_W, SCREEN_H);
}


/*****************************更新屏幕*******************************************
 * 更新屏幕分为如下的四步：														   *
 * 第一步，更新纹理中的内容，这个这里函数的第二个参数为NULL，意味着更新整个纹理            * 
 * 第二步，清空渲染器中原来的内容													*
 * 第三步，将全部的纹理拷贝到渲染器中												 *
 * 第四步, 将纹理更新到屏幕上													   *
 ******************************************************************************/
void update_screen() {
  SDL_UpdateTexture(texture, NULL, vmem, SCREEN_W * sizeof(uint32_t));
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);
}

/**************************模拟的vga如何更新屏幕***********************************
 * 模拟器中对设别也进行了模拟，在模拟vga中提供了同步，可以将显存中的内容同步到屏幕上，具体的实*
 * 现方式就是想特定的mmio寄存器中输入1，通知模拟器将模拟的显存中的数据使用SDL的函数更新到模拟*
 * 的屏幕上，同时更新完毕后要将同步位置0。											  *	
 *******************************************************************************/
void vga_update_screen() {
   	if(vgactl_port_base[1] == 1)
	{
		update_screen();
		vgactl_port_base[1] = 0;
	}	
}

/*************************如何初始化模拟的vga************************************
 * 首先申请两份MMIO空间，一个用于作为vga同步寄存器和屏幕大小信息存储，一个用于作为显存    *
 * 注册存储空间完成后，调用初始化屏幕函数，同时初始化显存。     					   *
******************************************************************************/
void init_vga() {
  vgactl_port_base = (uint32_t *)new_space(8);
  vgactl_port_base[0] = (screen_width() << 16) | screen_height();
  vmem = new_space(screen_size());
  g_fb_base = (uint8_t *)vmem;   /* expose direct pointer for fast path */
  add_mmio_map("vgactl", CONFIG_VGA_CTL_MMIO, vgactl_port_base, 8, NULL);
  add_mmio_map("vmem", CONFIG_FB_ADDR, vmem, screen_size(), NULL);
  init_screen();
  memset(vmem, 0, screen_size());
}
