#include <watchpoint.h>
#include <expr.h>
#include <stdio.h>
#include <reg.h>
#include <string.h>

static WP wp_pool[NR_WP];
static WP *head, *free_;
WP *g_wp_head = NULL;  /* public alias of head — checked inline by wp_any_active() */

void init_wp_pool(){
	int i;
	for(i = 0; i < NR_WP; i++){
		wp_pool[i].NO = i;
		wp_pool[i].next = &wp_pool[i+1];
	}

	wp_pool[NR_WP - 1].next = NULL;

	head = NULL;
	g_wp_head = NULL;
	free_ = wp_pool;
}

//we get a wp from the head of the free_, and put it before the head of the head_
void new_wp(char *expression)
{
	WP *pointer = free_;
	bool success = true;
	if(pointer != NULL){
		free_ = pointer->next;
		pointer->next = head;
		head = pointer;
		g_wp_head = head;
		strcpy(pointer->expr, expression);
		pointer->value = expr(expression, &success);
	}
}

//get the wp which num is NO from head queue and put it to the head of free_
void free_wp(int NO)
{
	WP* pointer = head;
	WP* pre = NULL;
	while(pointer != NULL){
		if(pointer->NO == NO){
			if(pre == NULL)
				head = pointer->next;
			else{
				pre->next = pointer->next;
			}

			pointer->next = free_;
			free_ = pointer;
			g_wp_head = head;
			return;
		}
		pre = pointer;
		pointer = pointer->next;
	}
}

void print_wp(){
	printf("watchpoint list:\n");
	WP* pointer = head;
	while(pointer != NULL){
		printf(" NO:%d, ", pointer->NO);
		printf(" expr: %s 0x%lx\n", pointer->expr, pointer->value);
		pointer = pointer->next;
	}
}

bool check_wp(){
	bool success = true, record = false;
	WP *pointer = head;
	while(pointer != NULL){
    	uint64_t value = expr(pointer->expr,&success);
		success = true;		//we don't check the correctness of expr
		if(value != pointer->value){
			record = true;
			pointer->value = value;
      		printf("w%d,Program stop before 0x%08lx new value = %lx\n",pointer->NO,cpu.pc, pointer->value);
      		break;
		}
		pointer = pointer->next;
	}
	return record;	
}
