#ifndef __WATCHPOINT_H__
#define __WATCHPOINT_H__
#include <stdint.h>
#include <common.h>
#include <stddef.h>

#define NR_WP 32
#define expr_maxl 100

typedef struct watchpoint{
	int NO;
	struct watchpoint *next;
	char expr[expr_maxl];
	uint64_t value;
} WP;

void init_wp_pool();
void new_wp(char *expression);
void free_wp(int NO);
void print_wp();
bool check_wp();

/* Fast inline guard — avoids the check_wp() function call when no
 * watchpoints are active (the common case during -b batch runs). */
extern WP *g_wp_head;
static inline bool wp_any_active(void) { return g_wp_head != NULL; }

#endif
