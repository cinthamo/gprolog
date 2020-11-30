/*-------------------------------------------------------------------------*
 * GNU Prolog                                                              *
 *                                                                         *
 * Part  : mini-assembler to assembler translator                          *
 * File  : armhf_any.c                                                     *
 * Descr.: translation file for Linux on aarch64                           *
 * Author: Jasper Taylor and Daniel Diaz                                   *
 *                                                                         *
 * Copyright (C) 1999-2020 Daniel Diaz                                     *
 *                                                                         *
 * This file is part of GNU Prolog                                         *
 *                                                                         *
 * GNU Prolog is free software: you can redistribute it and/or             *
 * modify it under the terms of either:                                    *
 *                                                                         *
 *   - the GNU Lesser General Public License as published by the Free      *
 *     Software Foundation; either version 3 of the License, or (at your   *
 *     option) any later version.                                          *
 *                                                                         *
 * or                                                                      *
 *                                                                         *
 *   - the GNU General Public License as published by the Free             *
 *     Software Foundation; either version 2 of the License, or (at your   *
 *     option) any later version.                                          *
 *                                                                         *
 * or both in parallel, as here.                                           *
 *                                                                         *
 * GNU Prolog is distributed in the hope that it will be useful,           *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU       *
 * General Public License for more details.                                *
 *                                                                         *
 * You should have received copies of the GNU General Public License and   *
 * the GNU Lesser General Public License along with this program.  If      *
 * not, see http://www.gnu.org/licenses/.                                  *
 *-------------------------------------------------------------------------*/

#include <stdio.h>
#include <string.h>




/*---------------------------------*
 * Constants                       *
 *---------------------------------*/

#define STRING_PREFIX              ".LC"
#define DOUBLE_PREFIX              ".LCD"

#define BPW                        8
#define MAX_C_ARGS_IN_C_CODE       32
#define MAX_ARGS_IN_REGS           8
#define MAX_DOUBLES_IN_REGS        8


#define MAX_DOUBLES_IN_PRED        2048


#define CMT(cmt)                   "" // "\t\t// " #cmt
// JAT: enabling the above substitution causes a baffling crash when building
// BipsPl/debugger.s if -fomit-frame-pointer c flag set
#define UN                         "_"
#define R(reg)                     "x" #reg
#define F(reg)                     "d" #reg




/*---------------------------------*
 * Type Definitions                *
 *---------------------------------*/

/*---------------------------------*
 * Global Variables                *
 *---------------------------------*/

double dbl_tbl[MAX_DOUBLES_IN_PRED];
int nb_dbl = 0;
int dbl_lc_no = 0;
int dbl_reg_no;

char asm_reg_bank[20];
char asm_reg_e[20];

int w_label = 0;
int c_label = 0;

	  /* variables for ma_parser.c / ma2asm.c */

int can_produce_pic_code = 0;
char *comment_prefix = "#";
char *local_symb_prefix = ".L";
int strings_need_null = 0;
int call_c_reverse_args = 0;

char *inline_asm_data[] = { NULL };



/*---------------------------------*
 * Function Prototypes             *
 *---------------------------------*/



/*-------------------------------------------------------------------------*
 * ASM_START                                                               *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Asm_Start(void)
{
#ifdef MAP_REG_BANK
  sprintf(asm_reg_bank, R(%s), MAP_REG_BANK);
#else
  strcpy(asm_reg_bank, R(10));
#endif

#ifdef MAP_REG_E
  sprintf(asm_reg_e, R(%s), MAP_REG_E);
#else
  strcpy(asm_reg_e, R(25));
#endif

  Label_Printf(".text");


  Label("fail");
  Pl_Fail();
}




/*-------------------------------------------------------------------------*
 * ASM_STOP                                                                *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Asm_Stop(void)
{
#ifdef __ELF__
  Inst_Printf(".section", ".note.GNU-stack,\"\"");
#endif
}




/*-------------------------------------------------------------------------*
 * CODE_START                                                              *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Code_Start(char *label, int prolog, int global)
{
  int i;
  int x = dbl_lc_no - nb_dbl;

  Inst_Printf(".align", "3");
  for (i = 0; i < nb_dbl; i++)
    {
      Label_Printf("%s%d:", DOUBLE_PREFIX, x++);
      Inst_Printf(".double", "0d%1.20e", dbl_tbl[i]);
    }

  nb_dbl = 0;

  Label_Printf("");
  Inst_Printf(".align", "2");
  Inst_Printf(".type", "%s, %%function", label);

  if (global)
    Inst_Printf(".global", "%s", label);

  Label(label);

  if (!prolog)
    {
      // x30 = lr but on old assemblers lr is not recognized (eg. 'as' binutils 2.28 debian)
      Inst_Printf("stp", "x15, x30, [sp, -16]!"); 
      // Inst_Printf("add", "fp, sp, #4"); make space in stack
      Inst_Printf("sub", "sp, sp, #%d", (MAX_C_ARGS_IN_C_CODE-MAX_ARGS_IN_REGS)*BPW);
      // Inst_Printf("ldr", R(0) ", =.plus");
      // Inst_Printf("bl", "printf");
    }

}




/*-------------------------------------------------------------------------*
 * CODE_STOP                                                               *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Code_Stop(void)
{
}




/*-------------------------------------------------------------------------*
 * LABEL                                                                   *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Label(char *label)
{
  Label_Printf("\n%s:", label);
}

// only certain values can be used for immediate addition, break into
// separate steps consisting of these values (its good enough for gcc...)
int nearest_immed(int tgt) {
  int wrk = tgt;
  if (tgt<0) wrk = -wrk;
  int exp = 0;
  while (wrk > 255) {
    exp += 2;
    wrk = wrk >> 2;
  }
  wrk = wrk << exp;
  if (tgt<0) wrk = -wrk;
  return wrk;
}

void decrement_reg(char* r, int int_val) {
  int slice, shift=0;
  while (int_val) {
    slice = int_val & 0xfff;
    if (slice)
      Inst_Printf("sub", "%s, %s, #%d, LSL %d", r, r, slice, shift);
    int_val >>= 12;
    shift += 12;
  }
}

void increment_reg(char* r, int int_val) {
  int slice, shift=0;
  while (int_val) {
    slice = int_val & 0xfff;
    if (slice)
      Inst_Printf("add", "%s, %s, #%d, LSL %d", r, r, slice, shift);
    int_val >>= 12;
    shift += 12;
  }
}

// Direct address loading only works over a certain range -- to avoid
// limits we have to include address as a literal...

void load_contents(char* r, char* addr) {
  Inst_Printf("adrp", "%s, :got:%s", r, addr);
  Inst_Printf("ldr", "%s, [%s, #:got_lo12:%s]", r, r, addr);
}

void load_address(char* r, char* addr) {
  Inst_Printf("adrp", "%s, %s", r, addr);
  Inst_Printf("add", "%s, %s, :lo12:%s", r, r, addr);
}

// perhaps keep track of
// when we have pl_reg_bank in r3 so avoiding too many hops?

void load_reg_bank(void) {
#ifndef MAP_REG_BANK
  load_contents(asm_reg_bank, "pl_reg_bank");
  Inst_Printf("ldr", "%s, [%s]", asm_reg_bank, asm_reg_bank);
#endif
}
/*-------------------------------------------------------------------------*
 * RELOAD_E_IN_REGISTER                                                    *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Reload_E_In_Register(void)
{
#ifndef MAP_REG_E
  load_reg_bank();
  Inst_Printf("ldr", "%s, [%s, #%d]" CMT(REIR), asm_reg_e, asm_reg_bank, MAP_OFFSET_E);
  // increment_reg(asm_reg_e, MAP_OFFSET_E);
#endif
}




/*-------------------------------------------------------------------------*
 * PL_JUMP                                                                 *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Pl_Jump(char *label)
{
  Inst_Printf("b", "%s", label);
}




/*-------------------------------------------------------------------------*
 * PREP_CP                                                                 *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Prep_CP(void)
{
  char cont_lbl[16];
  
  sprintf(cont_lbl, ".Lcont%d" CMT(PCP), c_label);
#ifdef MAP_REG_CP
  char r[4];
  sprintf(r, R(%s), MAP_REG_CP);
  load_address(r, cont_lbl);
#else
  load_address(R(2), cont_lbl);
  load_reg_bank();
  Inst_Printf("str", R(2) ", [%s, #%d]", asm_reg_bank, MAP_OFFSET_CP);
#endif
}




/*-------------------------------------------------------------------------*
 * HERE_CP                                                                 *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Here_CP(void)
{
  Label_Printf(".Lcont%d:" CMT(HCP), c_label++);
}




/*-------------------------------------------------------------------------*
 * PL_CALL                                                                 *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Pl_Call(char *label)
{
  Prep_CP();
  Pl_Jump(label);
  Here_CP();
}




/*-------------------------------------------------------------------------*
 * PL_FAIL                                                                 *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Pl_Fail(void)
{
#ifdef MAP_REG_B
  Inst_Printf("ldr", R(30) ", [" R(%s) ", #-8]" CMT(FAIL), MAP_REG_B);
#else
  load_reg_bank();
  Inst_Printf("ldr", R(30) ", [%s, #%d]", asm_reg_bank, MAP_OFFSET_B);
  Inst_Printf("ldr", R(30) ", [" R(30) ", #-8]" CMT(FAIL));
#endif
  Inst_Printf("ret", "");
}




/*-------------------------------------------------------------------------*
 * PL_RET                                                                  *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Pl_Ret(void)
{
#ifdef MAP_REG_CP
  Inst_Printf("ret", R(%s) CMT(RET), MAP_REG_CP);
#else
  load_reg_bank();
  Inst_Printf("ldr", "x30, [%s, #%d]" CMT(RET), asm_reg_bank, MAP_OFFSET_CP);
  Inst_Printf("ret", "");
#endif
}




/*-------------------------------------------------------------------------*
 * JUMP                                                                    *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Jump(char *label)
{
  Inst_Printf("b", "%s", label);
}




/*-------------------------------------------------------------------------*
 * MOVE_FROM_REG_X                                                         *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_From_Reg_X(int index)
{
  load_reg_bank();
  Inst_Printf("ldr", R(2) ", [%s, #%d]" CMT(MFRX), asm_reg_bank, index*BPW);
}




/*-------------------------------------------------------------------------*
 * MOVE_FROM_REG_Y                                                         *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_From_Reg_Y(int index)
{
  Inst_Printf("mov", R(2) ", %s", asm_reg_e);
  decrement_reg(R(2), -Y_OFFSET(index));
  Inst_Printf("ldr", R(2) ", [" R(2) "]" CMT(MFRY));
}




/*-------------------------------------------------------------------------*
 * MOVE_TO_REG_X                                                           *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_To_Reg_X(int index)
{
  load_reg_bank();
  Inst_Printf("str", R(2) ", [%s, #%d]" CMT(M2RX), asm_reg_bank, index*BPW);
}




/*-------------------------------------------------------------------------*
 * MOVE_TO_REG_Y                                                           *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_To_Reg_Y(int index)
{
  Inst_Printf("mov", R(7) ", %s", asm_reg_e);
  decrement_reg(R(7), -Y_OFFSET(index));
  Inst_Printf("str", R(2) ", [" R(7) "]" CMT(M2RY));
}




/*-------------------------------------------------------------------------*
 * CALL_C_START                                                            *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Call_C_Start(char *fct_name, int fc, int nb_args, int nb_args_in_words,
	     char **p_inline)
{
  dbl_reg_no = 0;
}




#define STACK_OFFSET(offset)   (offset_excl_doubles-MAX_ARGS_IN_REGS)*BPW
#define DBL_RET_WORDS          2

#define BEFORE_ARG				\
{						\
 char r[4];                                     \
 int offset_excl_doubles = offset-2*dbl_reg_no; \
   						\
  if (offset_excl_doubles < MAX_ARGS_IN_REGS)	\
    sprintf(r, R(%d), offset_excl_doubles + 0);	\
  else						\
    strcpy(r, R(9));




#define AFTER_ARG				\
  if (offset_excl_doubles >= MAX_ARGS_IN_REGS)	{       \
    Inst_Printf("str", "%s, [sp, #%d]", r,		\
		STACK_OFFSET(offset_excl_doubles));	\
  }                                             \
}


#define AFTER_ARG_DBL						\
}

void make_value(char* r, unsigned long int int_val) {
  char* acts;
  int slice, shift=0, wipe=1;
  while (int_val || wipe) {
    slice = int_val & 0xffff;
    if (slice || (!int_val) && wipe)
      if (wipe) {
	Inst_Printf("movz", "%s, #%d, LSL %d", r, slice, shift);
	wipe = 0;
      } else
	Inst_Printf("movk", "%s, #%d, LSL %d", r, slice, shift);
    int_val >>= 16;
    shift += 16;
  }
}

/*-------------------------------------------------------------------------*
 * CALL_C_ARG_INT                                                          *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Call_C_Arg_Int(int offset, PlLong int_val)
{
  BEFORE_ARG;

  make_value(r, int_val);

  AFTER_ARG;

  return 1;
}




/*-------------------------------------------------------------------------*
 * CALL_C_ARG_DOUBLE                                                       *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Call_C_Arg_Double(int offset, double dbl_val)
{
  BEFORE_ARG;

  char dbl_locn[12], dbl_reg[8];

  sprintf(dbl_locn, "%s%d", DOUBLE_PREFIX, dbl_lc_no++);
  sprintf(dbl_reg, F(%d), dbl_reg_no++);
  dbl_tbl[nb_dbl++] = dbl_val;

  load_address(r, dbl_locn);
  Inst_Printf("ldr", "%s, [%s]" CMT(CAD), dbl_reg, r);

  AFTER_ARG_DBL;

  return DBL_RET_WORDS;
}




/*-------------------------------------------------------------------------*
 * CALL_C_ARG_STRING                                                       *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Call_C_Arg_String(int offset, int str_no)
{
  BEFORE_ARG;

  char labl[8];
  sprintf(labl, "%s%d", STRING_PREFIX, str_no);
  load_address(r, labl);
  AFTER_ARG;

  return 1;
}


int in_offset_range(int off) {
  return off>-4096 && off<4096;
}

/*-------------------------------------------------------------------------*
 * CALL_C_ARG_MEM_L                                                        *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Call_C_Arg_Mem_L(int offset, int adr_of, char *name, int index)
{
  BEFORE_ARG;

  load_address(r, name);
  increment_reg(r, index*BPW);
  if (!adr_of)
    Inst_Printf("ldr", "%s, [%s]" CMT(CAML), r, r);
  AFTER_ARG;

  return 1;
}




/*-------------------------------------------------------------------------*
 * CALL_C_ARG_REG_X                                                        *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Call_C_Arg_Reg_X(int offset, int adr_of, int index)
{
  BEFORE_ARG;

  load_reg_bank();
  Inst_Printf("mov", "%s, %s" CMT(CARX), r, asm_reg_bank);

  if (adr_of)
    increment_reg(r, index*BPW);
  else
    Inst_Printf("ldr", "%s, [%s, #%d]", r, r, index*BPW);
  AFTER_ARG;

  return 1;
}




/*-------------------------------------------------------------------------*
 * CALL_C_ARG_REG_Y                                                        *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Call_C_Arg_Reg_Y(int offset, int adr_of, int index)
{
  BEFORE_ARG;

  Inst_Printf("mov", "%s, %s" CMT(CARY), r, asm_reg_e);
  decrement_reg(r, -Y_OFFSET(index));
  if (!adr_of)
    Inst_Printf("ldr", "%s, [%s]", r, r);

  AFTER_ARG;

  return 1;
}




/*-------------------------------------------------------------------------*
 * CALL_C_ARG_FOREIGN_L                                                    *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Call_C_Arg_Foreign_L(int offset, int adr_of, int index)
{
  // Surely it's just...
  return Call_C_Arg_Mem_L(offset, adr_of, "pl_foreign_long", index);
}




/*-------------------------------------------------------------------------*
 * CALL_C_ARG_FOREIGN_D                                                    *
 *                                                                         *
 *-------------------------------------------------------------------------*/
int
Call_C_Arg_Foreign_D(int offset, int adr_of, int index)
{
  // pointers are same
  if (adr_of)
    return Call_C_Arg_Mem_L(offset, adr_of, "pl_foreign_double", index);
  BEFORE_ARG;

  load_address(R(7), "pl_foreign_double");
  Inst_Printf("ldr", F(%d) ", [" R(7) ", %d]" CMT(CAFD),
	      dbl_reg_no++, index*BPW);
  AFTER_ARG_DBL;

  return DBL_RET_WORDS;
}




/*-------------------------------------------------------------------------*
 * CALL_C_INVOKE                                                           *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Call_C_Invoke(char *fct_name, int fc, int nb_args, int nb_args_in_words)
{
  Inst_Printf("bl", "%s" CMT(CI), fct_name);
}




/*-------------------------------------------------------------------------*
 * CALL_C_STOP                                                             *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Call_C_Stop(char *fct_name, int nb_args, char **p_inline)
{
#ifndef MAP_REG_E
  if (p_inline && INL_ACCESS_INFO(p_inline))
    reload_e = 1;
#endif
}




/*-------------------------------------------------------------------------*
 * JUMP_RET                                                                *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Jump_Ret(void)
{
  Inst_Printf("ret", R(0) CMT(JR));
}




/*-------------------------------------------------------------------------*
 * FAIL_RET                                                                *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Fail_Ret(void)
{
  Inst_Printf("cmp", R(0) ", #0" CMT(FR));
  Inst_Printf("bne", ".Lcont%d", c_label);
  Inst_Printf("b", "fail");
  Label_Printf(".Lcont%d:", c_label++);
  // was this needed because b could jump further than beq? Yep...
}




/*-------------------------------------------------------------------------*
 * MOVE_RET_TO_MEM_L                                                       *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_Ret_To_Mem_L(char *name, int index)
{
  load_address(R(7), name);
  increment_reg(R(7), index*BPW);
  Inst_Printf("str", R(0) ", [" R(7) "]" CMT(R2ML));
}




/*-------------------------------------------------------------------------*
 * MOVE_RET_TO_REG_X                                                       *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_Ret_To_Reg_X(int index)
{				/* similar to Move_To_Reg_X */
  load_reg_bank();
  Inst_Printf("str", R(0) ", [%s, #%d]" CMT(R2RX), asm_reg_bank, index*BPW);
}




/*-------------------------------------------------------------------------*
 * MOVE_RET_TO_REG_Y                                                       *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_Ret_To_Reg_Y(int index)
{				/* similar to Move_To_Reg_Y */
  Inst_Printf("mov", R(7) ", %s", asm_reg_e);
  decrement_reg(R(7), -Y_OFFSET(index));
  Inst_Printf("str", R(0) ", [" R(7) "]" CMT(R2RY));
}




/*-------------------------------------------------------------------------*
 * MOVE_RET_TO_FOREIGN_L                                                   *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_Ret_To_Foreign_L(int index)
{
  Move_Ret_To_Mem_L("pl_foreign_long", index);
}




/*-------------------------------------------------------------------------*
 * MOVE_RET_TO_FOREIGN_D                                                   *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Move_Ret_To_Foreign_D(int index)
{
  load_address(R(7), "pl_foreign_double"),
  Inst_Printf("str", "d0, [" R(7) ", %d]" CMT(R2FD), index*BPW);
}




/*-------------------------------------------------------------------------*
 * CMP_RET_AND_INT                                                         *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Cmp_Ret_And_Int(PlLong int_val)
{
  if (nearest_immed(int_val) == int_val)
    Inst_Printf("cmp", R(0) ", #%ld" CMT(CRAI), int_val);
  else {
    make_value(R(7), int_val);
    Inst_Printf("cmp", R(0) ", " R(7) CMT(CRAI));
  }
}




/*-------------------------------------------------------------------------*
 * JUMP_IF_EQUAL                                                           *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Jump_If_Equal(char *label)
{
  Inst_Printf("beq", "%s", label);
}




/*-------------------------------------------------------------------------*
 * JUMP_IF_GREATER                                                         *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Jump_If_Greater(char *label)
{
  Inst_Printf("bgt", "%s", label);
}




/*-------------------------------------------------------------------------*
 * C_RET                                                                   *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
C_Ret(void)
{
  // Inst_Printf("ldr", R(0) ", =.minus");
  // Inst_Printf("bl", "printf");
  Inst_Printf("add", "sp, sp, #%d", (MAX_C_ARGS_IN_C_CODE-MAX_ARGS_IN_REGS)*BPW);
  Inst_Printf("ldp", "x15, x30, [sp], 16");
  Inst_Printf("ret", CMT(CR));
}




/*-------------------------------------------------------------------------*
 * DICO_STRING_START                                                       *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Dico_String_Start(int nb_consts)
{
}




/*-------------------------------------------------------------------------*
 * DICO_STRING                                                             *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Dico_String(int str_no, char *asciiz)
{
  Label_Printf("%s%d:", STRING_PREFIX, str_no);
#if defined(M_powerpc_linux) || defined(M_powerpc_bsd)
  Inst_Printf(".string", "%s", asciiz);
#else
  Inst_Printf(".asciz", "%s", asciiz);
#endif
}




/*-------------------------------------------------------------------------*
 * DICO_STRING_STOP                                                        *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Dico_String_Stop(int nb_consts)
{
}




/*-------------------------------------------------------------------------*
 * DICO_LONG_START                                                         *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Dico_Long_Start(int nb_longs)
{
  Label_Printf(".data");
  Inst_Printf(".align", "4");
}




/*-------------------------------------------------------------------------*
 * DICO_LONG                                                               *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Dico_Long(char *name, int global, VType vtype, PlLong value)
{
  switch (vtype)
    {
    case NONE:
      value = 1;		/* then in case ARRAY_SIZE */
    case ARRAY_SIZE:
      if (!global)
	Inst_Printf(".lcomm", "%s,%ld", name, value*BPW);
      else
	Inst_Printf(".comm", UN "%s,%ld,8", name, value*BPW);
      break;

    case INITIAL_VALUE:
      if (global)
	Inst_Printf(".global", "%s", name);
      Label_Printf("%s:", name);
      Inst_Printf(".xword", "%ld", value);
      break;
    }
}




/*-------------------------------------------------------------------------*
 * DICO_LONG_STOP                                                          *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Dico_Long_Stop(int nb_longs)
{
}




/*-------------------------------------------------------------------------*
 * DATA_START                                                              *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Data_Start(char *initializer_fct)
{
  if (initializer_fct == NULL)
    return;

  Inst_Printf(".section", ".init_array,\"aw\"");
  Inst_Printf(".align", "3");
  Inst_Printf(".xword", "%s", initializer_fct);

  Inst_Printf(".data", "");
  // Inst_Printf(".plus:", ".asciz \"+\"");
  // Inst_Printf(".minus:", ".asciz \"-\"");
    
}




/*-------------------------------------------------------------------------*
 * DATA_STOP                                                               *
 *                                                                         *
 *-------------------------------------------------------------------------*/
void
Data_Stop(char *initializer_fct)
{
  if (initializer_fct == NULL)
    return;

#if 0
  Label_Printf(".data");
  Label_Printf(UN "obj_chain_stop:");

  Inst_Printf(".long", UN "obj_chain_start");
#endif
}
