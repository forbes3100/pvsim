// ****************************************************************************
//
//          PVSim Verilog Simulator Verilog System Call Library Interace
//
// Copyright 2004,2005,2006,2012 Scott Forbes
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

#include "Utils.h"
#include "Model.h"
#include "Src.h"
#include "VL.h"

// A system call table entry

struct SysCall
{
    char*   name;
    char*   argTypes;
    bool    isFn;
    void*   func;
};

// table of system calls in library

extern SysCall gSysCalls[];

// Verilog library routines

size_t loadIndScalar(SignalVec* sigVec, size_t i, size_t iMax);
void loadIndVector(SignalVec* sigVec, size_t i, size_t iMax,
                    Level* levVec, size_t n);
void sampleVector(Level* levVec, size_t nBits);
void codeConcat(Expr* ex);
void convIntToLVec(size_t value, Level* levVec, size_t nBits);
size_t convLVecToInt(Level* levVec, size_t nBits);
size_t uandLVec(Level* levVec, size_t nBits);
size_t unandLVec(Level* levVec, size_t nBits);
size_t uorLVec(Level* levVec, size_t nBits);
size_t unorLVec(Level* levVec, size_t nBits);
size_t uxorLVec(Level* levVec, size_t nBits);
void unaryOpLVec(Level* src, Level* dest, size_t nBits, Level table[12]);
void binaryOpLVec(Level* src0, Level* src1, Level* dest, size_t nBits,
                  Level table[12][16]);
void verLine(size_t isrcLoc);
void verBreakpoint(size_t ircLoc);

// system call/function compiling

void codeSystemCall();
Expr* compileSystemFn();
void codeFunction(Expr* ex);
void codeNetRef(Net* net, Variable* extScopeRef);

