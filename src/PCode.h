// ****************************************************************************
//
//          PVSim Verilog Simulator P-Code Coder Back End
//
// Copyright 2006 Scott Forbes
//
// This file is part of PVSim.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with PVSim; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
// ****************************************************************************

#pragma once

#include <limits.h>
#include "VLCoder.h"

const int bit_wordOp = 0x20;
const int bit_dualOp = 0x40;

// Pseudo-Codes

enum PCodeOp {
    p_nop = 0,
    p_dup,      //      duplicate TOS
    p_swap,     //      swap TOS, TOS+1
    p_rot,      //      rotate TOS, TOS+1, TOS+2
    p_func,     //      push function return value
    p_rts,      //      return from task subroutine
    p_wait,     //      wait for next model event
    p_del,      //      delay for TOS ticks
    p_leam,     //      load address of model
    p_leai,     //      load address of instance
    p_add,      //      add TOS+1 to TOS
    p_sub,      //      subtract TOS+1 from TOS
    p_mul,      //      multiply TOS+1 by TOS to TOS
    p_div,      //      divide TOS+1 by TOS to TOS
    p_and,      //      and TOS+1 to TOS
    p_or,       //      or TOS+1 to TOS
    p_xor,      //      xor TOS+1 to TOS
    p_sla,      //      shift TOS+1 left by TOS bits to TOS
    p_sra,      //      shift TOS+1 right by TOS bits to TOS
    p_eq,       //      integer compare TOS+1 to TOS, 1 if ==
    p_ne,       //      integer compare TOS+1 to TOS, 1 if !=
    p_gt,       //      integer compare TOS+1 to TOS, 1 if >
    p_le,       //      integer compare TOS+1 to TOS, 1 if <=
    p_lt,       //      integer compare TOS+1 to TOS, 1 if <
    p_ge,       //      integer compare TOS+1 to TOS, 1 if >=
    p_not,      //      integer !TOS to TOS
    p_nots,     //      scalar !TOS to TOS
    p_com,      //      integer ~TOS to TOS
    p_neg,      //      integer negate TOS to TOS
    p_cvis,     //      convert integer to scalar
    p_end,      //      return from pcode execution
                // (p_end MUST BE LAST 0-OP)
    p_liw = bit_wordOp, // w    load immediate 16-bit integer w to TOS
    p_lds,      // w    load byte from variable at address at inst[w]
    p_pick,     // w    pick wth item from stack
    p_drop,     // w    drop w items

    p_bsr0 = bit_dualOp, // n   branch to C subroutine at n with no args
    p_bsr1,     // n    branch to C subroutine at n with 1 arg
    p_bsr2,     // n    branch to C subroutine at n with 2 args
    p_bsr3,     // n    branch to C subroutine at n with 3 args
    p_bsr4,     // n    branch to C subroutine at n with 4 args
    p_bsr5,     // n    branch to C subroutine at n with 5 args
    p_bsr,      // n    branch to C subroutine at n with nArgs args
    p_btsk,     // n    branch to task subroutine at n
    p_br,       // n    branch to n
    p_beq,      // n    pop TOS and branch to n if its equal to zero
    p_bne,      // n    pop TOS and branch to n if its not equal to zero
    p_li,       // n    load immediate size_t integer n to TOS
    p_lea,      // n    load effective address of variable at inst[n]
    p_ld,       // n    load a variable from inst[n]
    p_ldx,      // n    load a variable from TOS[n]
    p_ldbx,     // n    load a byte from TOS[n]
    p_st,       // n    store a variable at inst[n]
    p_stb,      // n    store a byte at inst[n]
    p_stx,      // n    store a variable at TOS[n]
    p_addi,     // n    add immediate n to TOS
    p_andi,     // n    and immediate n to TOS
    p_sbop,     // n    scalar op TOS+1 with TOS, using table at n
    p_last };

struct PCode
{
    // single-op subset
    PCodeOp     op;     // op-code (includes bit_wordOp, bit_dualOp)
    char        nArgs;  // number of subroutine args for p_bsr
    short       w;      // short-word arg, if bit_wordOp set

    size_t      n;      // dual-op stackable operand, may be a pointer
};

extern const char* kPCodeName[];        // P-code names

extern PCode*   pc;         // current PCode being compiled

void codeOp(PCodeOp op);
void codeOpI(PCodeOp op, size_t arg);
