.macro reg_save base
	sd ra, 0(\base)
	sd sp, 8(\base)
	sd gp, 16(\base)
	sd tp, 24(\base)
	sd t0, 32(\base)
	sd t1, 40(\base)
	sd t2, 48(\base)
	sd s0, 56(\base)
	sd s1, 64(\base)
	sd a0, 72(\base)
	sd a1, 80(\base)
	sd a2, 88(\base)
	sd a3, 96(\base)
	sd a4, 104(\base)
	sd a5, 112(\base)
	sd a6, 120(\base)
	sd a7, 128(\base)
	sd s2, 136(\base)
	sd s3, 144(\base)
	sd s4, 152(\base)
	sd s5, 160(\base)
	sd s6, 168(\base)
	sd s7, 176(\base)
	sd s8, 184(\base)
	sd s9, 192(\base)
	sd s10, 200(\base)
	sd s11, 208(\base)
	sd t3, 216(\base)
	sd t4, 224(\base)
	sd t5, 232(\base)
.endm

.macro reg_restore base
	ld ra, 0(\base)
	ld sp, 8(\base)
	ld gp, 16(\base)
	ld tp, 24(\base)
	ld t0, 32(\base)
	ld t1, 40(\base)
	ld t2, 48(\base)
	ld s0, 56(\base)
	ld s1, 64(\base)
	ld a0, 72(\base)
	ld a1, 80(\base)
	ld a2, 88(\base)
	ld a3, 96(\base)
	ld a4, 104(\base)
	ld a5, 112(\base)
	ld a6, 120(\base)
	ld a7, 128(\base)
	ld s2, 136(\base)
	ld s3, 144(\base)
	ld s4, 152(\base)
	ld s5, 160(\base)
	ld s6, 168(\base)
	ld s7, 176(\base)
	ld s8, 184(\base)
	ld s9, 192(\base)
	ld s10, 200(\base)
	ld s11, 208(\base)
	ld t3, 216(\base)
	ld t4, 224(\base)
	ld t5, 232(\base)
	ld t6, 240(\base)
.endm

.data 
.global context_buffer
.align 3
context_buffer:
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0
.8byte 0x0

.text
.extern trap_handler
.global trap_vector
.align 4
trap_vector:
#	sd		t6, 0(sp)
#	la		t6, context_buffer
#	csrw	mscratch, t6
#	ld		t6, 0(sp)
	csrrw	t6, mscratch, t6			//将mscratch中的上下文指针存在t6
	la		t6, context_buffer
	reg_save t6							//保存除了t6外的上下文

	mv		t5, t6						//单独存储t6
	csrr	t6, mscratch				
	sd		t6, 240(t5)

#	csrr	a0, mepc					//存储mepc
#	sd		a0, 248(t5)

	csrw	mscratch, t5				//恢复上下文指针

#	csrr	a0, mepc					//将异常地址和异常原因作为参数
#	csrr	a1, mcause
	call	trap_handler				//调用陷入服务例程
	csrr	t6, mepc
	addi 	t6, t6, 4
	csrw	mepc, t6
#	csrw	mepc, a0					//返回的地址
	
	csrr	t6, mscratch				//将原来的上下文地址输入t6，调用上下文恢复的宏
	reg_restore t6

	mret								//返回原来的上下文


.global switch_to
.align 4
switch_to:
	
	csrw	mscratch, a0
	ld		a1, 248(a0)
	csrw	mepc, a1	

	mv	t6, a0
	reg_restore t6
	
	mret

.end
