#include <decode.h>
#include <reg.h>

Decode s;

void init_decode_info(){
	s.dnpc = s.snpc = 0x80000000;
}

void decode_operand(Decode *s, word_t *dest, word_t *src1, word_t *src2, int type) {
  uint32_t i = s->inst.val;                                                                 //获取指令的值
  int rd  = BITS(i, 11, 7);                                                                     //提前获取rd，rs1，rs2的值，如果存在一定在这个位置
  int rs1 = BITS(i, 19, 15);
  int rs2 = BITS(i, 24, 20);
  destR(rd);                                                                                    //记录目的寄存器的编号
  switch (type) {                                                                               //I型的指令用于短立即数的访问或者访存load操作
    case TYPE_I: src1R(rs1);     src2I(immI(i)); break;                                         //解码出rs1，还有高位的12位立即数并作扩展
    case TYPE_U: src1I(immU(i)); break;                                                         //U型的指令用于长立即数，对于20立即数进行扩展
    case TYPE_S: destI(immS(i)); src1R(rs1); src2R(rs2); break;                                 //用于访问存储的S型指令，需要将高位和低位进行拼接形成12立即数
    case TYPE_J: src1I(immJ(i)); break;
    case TYPE_R: src1R(rs1); src2R(rs2);  break;
    case TYPE_B: src1R(rs1); src2R(rs2); destI(immB(i));
  }
}
