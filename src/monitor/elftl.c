#include <common.h>
#include <debug.h>
#include <stdlib.h>
#include <elf.h>
#include <string.h>
#include <elftl.h>

int g_ftrace_en = 0;  /* disabled by default; set to 1 to enable ftrace collection */

static char *strtab = NULL;
static Elf64_Sym *symtab = NULL;
static int nr_symtab_entry;

static int depth = 0;

typedef struct ftrace_element{
	char *func_name;
	uint64_t addr;
	bool is_call;
	int depth;
	struct ftrace_element *next;
} ftrace_element;

ftrace_element *head = NULL;
ftrace_element *tail = NULL;

/*********************************ftrace结构初始化*******************************
 * 我们首先打开这个文件，从这个文件读入ELF头，之后可以检查ELF头文件中必要的参数, 之后获取节 *
 * 头表的大小，之后将偏移设置到节头表的位置，我们就可以读入整个节头表，再读入节头字符串表    *
 ******************************************************************************/
void init_elf(const char* elf_file)
{
	int ret;
	assert(elf_file != NULL);

	FILE *fp = fopen(elf_file, "rb");
	Assert(fp, "Can not open elf_file");

	uint8_t buf[sizeof(Elf64_Ehdr)];
	ret = fread(buf, sizeof(Elf64_Ehdr), 1, fp);
	assert(ret == 1);

	Elf64_Ehdr *elf = (Elf64_Ehdr *) buf;
	char magic[] = {ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3};

	assert(memcmp(elf->e_ident, magic, 4) == 0);
	assert(elf->e_ident[EI_CLASS] == ELFCLASS64);
	assert(elf->e_ident[EI_DATA] == ELFDATA2LSB);
	assert(elf->e_ident[EI_VERSION] == EV_CURRENT);
	assert(elf->e_ident[EI_OSABI] == ELFOSABI_SYSV || elf->e_ident[EI_OSABI] == ELFOSABI_LINUX);
	assert(elf->e_ident[EI_ABIVERSION] == 0);
	assert(elf->e_type == ET_EXEC);
	//assert(elf->e_machine == EM_386);
	assert(elf->e_version == EV_CURRENT);

	uint64_t sh_size = elf->e_shentsize * elf->e_shnum;
	Elf64_Shdr *sh = (Elf64_Shdr*)malloc(sh_size);
	fseek(fp, elf->e_shoff, SEEK_SET);
	ret = fread(sh, sh_size, 1, fp);
	assert(ret == 1);

	char *shstrtab =(char*)malloc(sh[elf->e_shstrndx].sh_size);
	fseek(fp, sh[elf->e_shstrndx].sh_offset, SEEK_SET);
	ret = fread(shstrtab, sh[elf->e_shstrndx].sh_size, 1, fp);
	assert(ret == 1);

	/****************************节头表解析********************************
	* 首先遍历所有的节头表表项，找到其中类型被标识为SHT_SYMTAB和SHT_STRTAB的表项  *
	* 之后如果检测到对应的节，则将对应节全部读入								*
	*********************************************************************/
	int i;
	for(i=0; i < elf->e_shnum; i++)
	{
		if(sh[i].sh_type == SHT_SYMTAB && strcmp(shstrtab + sh[i].sh_name, ".symtab") == 0)
		{
			symtab = (Elf64_Sym*)malloc(sh[i].sh_size);
			fseek(fp, sh[i].sh_offset, SEEK_SET);
			ret = fread(symtab, sh[i].sh_size, 1, fp);
			assert(ret == 1);
			nr_symtab_entry = sh[i].sh_size / sizeof(symtab[0]);
		}
		else if(sh[i].sh_type == SHT_STRTAB && strcmp(shstrtab + sh[i].sh_name, ".strtab") == 0)
		{
			strtab = (char*)malloc(sh[i].sh_size);
			fseek(fp,sh[i].sh_offset,SEEK_SET);
			ret = fread(strtab,sh[i].sh_size, 1, fp);
			assert(ret == 1);
		}
	}

	free(sh);
	free(shstrtab);

	assert(strtab != NULL && symtab != NULL);

	fclose(fp);
}

/*********************************通过函数名，找到的地址****************************
 * 通过函数名找到地址，遍历符号表，如果发现符号表项中，某个表项的名字和要找的函数名一致，那么可*
 * 以直接返回这个函数的地址。														*
 *******************************************************************************/
uint64_t findSym(char *name){
	int i;
	for(i=0; i<nr_symtab_entry; i++)
		if(strcmp(&strtab[symtab[i].st_name],name) == 0)
			return symtab[i].st_value;
	return -1;
}

/*********************************通过地址找到对应的函数名***************************
 * 通过地址找到对应的函数名，那么我们可以也同样遍历符号表，之后如果找到某个节的地址区间可以容纳 *
 * 这个我们要找的地址，说明我们要找的地址空间位于这个函数内                              *
 *******************************************************************************/
char* findFunc(uint64_t addr)
{
	int i;
	for(i=0; i<nr_symtab_entry; i++)
	{
		if(addr >= symtab[i].st_value && addr < symtab[i].st_value + symtab[i].st_size)
			return &strtab[symtab[i].st_name];
	}
	return NULL;
}

/*********************************ftrace解析*******************************************
 * ftrace是将函数的调用关系保留下来最后进行输出，我们将这个函数的地址，是call还是ret，函数的名称， *
 * 同时保留现在函数所在的深度，方便输出进行空格的输出										  *
 *************************************************************************************/
void add_ftrace(uint64_t addr, bool is_call)
{
	if (!g_ftrace_en) return;
	ftrace_element *pointer = (ftrace_element*)malloc(sizeof(ftrace_element));
	pointer->addr = addr;
	pointer->is_call = is_call;
	pointer->func_name = findFunc(addr);
	
	if(is_call)
	{
		pointer->depth = depth;
		depth ++;
	}
	else
	{
		depth --;
		pointer->depth = depth;
	}
	
	if(head == NULL && tail == NULL)
	{
		tail = head = pointer;
		pointer->next = NULL;
	}
	else
	{
		tail->next = pointer;
		tail = pointer;
		pointer->next = NULL;
	}

}

void delete_ftrace()
{
	while(head)
	{
		ftrace_element *pointer = head;
		head = head->next;
		free(pointer);
	}
}

void print_ftrace()
{
	ftrace_element * pointer = head;
	while(pointer)
	{
		int i;

		for(i = 0;i < pointer->depth; i++)
			printf("   ");
	
		if(pointer->is_call)
			printf("call ");
		else
			printf("ret ");
		
		printf("%s ",pointer->func_name);
		printf("[%016lx]\n",pointer->addr);
		pointer = pointer->next;
	}
}
