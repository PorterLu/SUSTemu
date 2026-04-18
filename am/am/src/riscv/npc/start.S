.section entry, "ax"		// 表示该节可以执行，ax是allocaiotn execute的缩写
.globl _start				// 设置入口函数
.type _start, @function

_start:
  mv s0, zero				//设置堆栈
  la sp, _stack_pointer
  jal _trm_init
