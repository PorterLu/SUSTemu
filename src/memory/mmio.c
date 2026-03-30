#include <mmio.h>
#include <stdlib.h>
#include <log.h>
#include <pmem.h>

#define NR_MAP 16
#define IO_SPACE_MAX (16 * 1024 * 1024)

static IOMap maps[NR_MAP] = {};
static int nr_map = 0;
static uint8_t *p_space = NULL;
static uint8_t *io_space = NULL;

/******************************************map地址空间*********************************
 * 按地址类型进行分类， 返回mmio设备的下标，所有的设备都被注册到了队列上					    *
 ************************************************************************************/
static inline bool map_inside(IOMap *maps, int size, paddr_t addr){
	return (addr >= maps->low && addr <= maps->high);
}

static inline int find_mapid_by_addr(IOMap *maps, int size, paddr_t addr){
	int i;
	for(i = 0; i < size; i++){
		if(map_inside(maps + i, size, addr)){
			//difftest_skip_ref();
			return i;
		}
	}
	return -1;
}

static void invoke_callback(io_callback_t c, paddr_t offset, int len, bool is_write){
	if(c != NULL) { c(offset, len, is_write);}
}

static IOMap* fetch_mmio_map(paddr_t addr){
	int mapid = find_mapid_by_addr(maps, nr_map, addr);
	if(mapid != -1)
		return maps + mapid;
	else
		return NULL;
}

/***********************************添加MMIO设备***************************************
 * 在mmio队列上添加一个设备，信息包括这个设备的名字，地址，所用的空间，所占的地址长度，回调函数    *
 ************************************************************************************/
void add_mmio_map(const char *name, paddr_t addr, void *space, uint32_t len, io_callback_t callback){
	assert(nr_map < NR_MAP);
	maps[nr_map] = (IOMap){ .name = name, .low = addr, .high = addr + len - 1,
							.space = space, .callback = callback };
	Log("Add mmio map '%s' at [ %lx , %lx ]",
		maps[nr_map].name, maps[nr_map].low, maps[nr_map].high);
	
	nr_map ++;
}

word_t mmio_read(paddr_t addr, int len){
	IOMap *map = fetch_mmio_map(addr);
	if (map == NULL) {
		panic("MMIO read: no device mapped at addr %lx\n", addr);
	}
	return map_read(addr, len, map);
}

void mmio_write(paddr_t addr, int len, word_t data){
	IOMap *map = fetch_mmio_map(addr);
	if (map == NULL) {
		panic("MMIO write: no device mapped at addr %lx\n", addr);
	}
	map_write(addr, len, data, map);
}

uint8_t* new_space(int size){
	uint8_t *p = p_space;
	size = (size + PAGE_SIZE - 1) & (~PAGE_MASK);
	p_space += size;
	return p;
}

void init_map(){
	io_space = (uint8_t *)malloc(IO_SPACE_MAX);
	assert(io_space);
	p_space = io_space;
}

/*********************************访问外设的过程*****************************************
 * 通过 Map中的设备基地址，可以直接得到访问的设备地址空间中的偏移，这个偏移很可能代表了访问了不同的设 *
 * 备，我们调用回调函数，通知设备我们对其进行了访问，这样可以让设备对我们的行为做出响应，之后我们直接读*
 * 对应地址上设备更新的值，就成功读写了设备                                                  *
 **************************************************************************************/
word_t map_read(paddr_t addr, int len, IOMap *map){
	paddr_t offset = addr - map->low;
	invoke_callback(map->callback, offset, len, false);
	word_t ret = host_read((uint8_t*)(map->space) + offset, len);
	return ret;
}

void map_write(paddr_t addr, int len, word_t data, IOMap *map){
	paddr_t offset = addr - map->low;
	host_write((uint8_t*)(map->space) + offset, len, data);
	invoke_callback(map->callback, offset, len, true);
}

