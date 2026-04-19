#ifndef __ELFTL_H__
#define __ELFTL_H__

extern int g_ftrace_en;  /* set to 1 to enable ftrace collection */

void init_elf(const char* elf_file);

//通过函数名，找到的地址
uint64_t findSym(char *name);

//在64位地址空间可以使用指针
//findFunc通过地址找到对应的函数名
char* findFunc(uint64_t addr);

void add_ftrace(uint64_t addr, bool is_call);

void delete_ftrace();

void print_ftrace();

#endif