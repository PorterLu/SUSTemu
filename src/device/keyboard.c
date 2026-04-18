#include <state.h>
#include <utils.h>
#include <SDL2/SDL.h>
#include <mmio.h>
#include <debug.h>

/* macOS SDK defines _KEY_T as a header guard; undefine it to avoid
 * colliding with our _KEY_NAME(T) → _KEY_T enum token. */
#undef _KEY_T

#define KEYDOWN_MASK 0x8000
#define CONFIG_I8042_DATA_MMIO 0xa0000060
/**********************************Keymap生成********************************************
 * 生存Keymap的过程如下，我们首先有MAP(_KEYS, _KEY_NAME），这里进行宏扩展有_KEYS(_KEY_NAME)    *
 * 这是可以使用_KEYS(f) 进行宏扩展生存键盘表"_KEY_NAME(ESCAPE), ..., _KEY_NAME(PAGEDOWN)"    *
 * _KEY_NAME(k) 也是一个宏扩展，这里进行宏扩展可以直接展开为enum的内容，即 _KEY_ESCAPE,...     	*
 * _KEY_PAGEDOWN,                                                                  	   *
 ***************************************************************************************/

#define _KEYS(f) \
  f(ESCAPE) f(F1) f(F2) f(F3) f(F4) f(F5) f(F6) f(F7) f(F8) f(F9) f(F10) f(F11) f(F12) \
f(GRAVE) f(1) f(2) f(3) f(4) f(5) f(6) f(7) f(8) f(9) f(0) f(MINUS) f(EQUALS) f(BACKSPACE) \
f(TAB) f(Q) f(W) f(E) f(R) f(T) f(Y) f(U) f(I) f(O) f(P) f(LEFTBRACKET) f(RIGHTBRACKET) f(BACKSLASH) \
f(CAPSLOCK) f(A) f(S) f(D) f(F) f(G) f(H) f(J) f(K) f(L) f(SEMICOLON) f(APOSTROPHE) f(RETURN) \
f(LSHIFT) f(Z) f(X) f(C) f(V) f(B) f(N) f(M) f(COMMA) f(PERIOD) f(SLASH) f(RSHIFT) \
f(LCTRL) f(APPLICATION) f(LALT) f(SPACE) f(RALT) f(RCTRL) \
f(UP) f(DOWN) f(LEFT) f(RIGHT) f(INSERT) f(DELETE) f(HOME) f(END) f(PAGEUP) f(PAGEDOWN)

#define _KEY_NAME(k) _KEY_##k,

enum{
	_KEY_NONE = 0,
	MAP(_KEYS, _KEY_NAME)
};

/*******************************Keymap数组生存******************************************
 * 同理可以进行数组的生成，MAP(_KEYS, SDL_KEYMAP) 可以生存_KEYS(SDL_KEY_MAP), 所以可以变成键盘*
 * 表的扩展， SDL_KEY_MAP(ESCAPE)... SDL_KEY_MAP(PAGEDOWN), 使用数组扩展函数，我们有数组赋值 *
 * keymap[SDL_SCANCODE_ESCAPE] = _KEY_ESCAPE; 										  *
 *************************************************************************************/
#define SDL_KEYMAP(k) keymap[concat(SDL_SCANCODE_, k)] = concat(_KEY_, k);
static uint32_t keymap[256] = {};

static void init_keymap() {
	MAP(_KEYS, SDL_KEYMAP)
}

/*******************************键盘队列************************************************
 * 键盘队列使用环形缓冲区的形式，队首指针和队尾指针重合时表示队列为空，这里每接受一个键盘扫描码，总是将*
 * key_r指向的区域赋上键盘扫描码，同时指针向后移动一个单位。如果enqueue的过此中出现key_f等于key_r *
 * 说明队列中每一个区域都存储了键值，将这种情况记为非法。而使用这个队列是在模拟端不断地调用send_key, *
 * sendkey的过程中，会将根据是否为按下，将高位置1。                                          *
 **************************************************************************************/
#define KEY_QUEUE_LEN 1024	
static int key_queue[KEY_QUEUE_LEN] = {};
static int key_f = 0, key_r = 0;

static void key_enqueue(uint32_t am_scancode){
	key_queue[key_r] = am_scancode;
	key_r = (key_r + 1) % KEY_QUEUE_LEN;
	Assert(key_r != key_f, "key queue overflow!");
}

static uint32_t key_dequeue(){
	uint32_t key = _KEY_NONE;
	if(key_f != key_r){
		key = key_queue[key_f];
		key_f = (key_f + 1) % KEY_QUEUE_LEN;
	}
	return key;
}

void send_key(uint8_t scancode, bool is_keydown){
	if(state == NEMU_RUNNING && keymap[scancode] != _KEY_NONE){
		uint32_t am_scancode = keymap[scancode] | (is_keydown ? KEYDOWN_MASK : 0);
		key_enqueue(am_scancode);
	}
}

/********************************程序使用键盘*****************************************
 *  程序仍然通过mmio进行键盘的使用，通过读取对应的寄存器可以回调key_dequeue, 程序在使用时也需要 *
 * 	上面的映射表将键盘值映射回scancode						     						*
 **********************************************************************************/
static uint32_t *i8042_data_port_base = NULL;

static void i8042_data_io_handler(uint32_t offset, int len, bool is_write){
	assert(!is_write);
	assert(offset == 0);
	i8042_data_port_base[0] = key_dequeue();
}

void init_i8042(){
	i8042_data_port_base = (uint32_t*)new_space(4);
	i8042_data_port_base[0] = _KEY_NONE;
	add_mmio_map("keyboard", CONFIG_I8042_DATA_MMIO, i8042_data_port_base, 4, i8042_data_io_handler);
	init_keymap();
}


