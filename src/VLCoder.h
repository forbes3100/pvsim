// ****************************************************************************
//
//          PVSim Verilog Simulator Coder Back End Interface
//
// Copyright 2004,2005,2006 Scott Forbes
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

#include <time.h>
#include <setjmp.h>
#include "Model.h"
#include "VL.h"

// generic subroutine and function types for code pointers
typedef void (Subr)(...);
typedef size_t (Func)(...);
typedef size_t (VariFunc)(size_t arg, ...);
typedef void (Model::*ModelSubrPtr)(...);
typedef size_t (Model::*ModelFuncPtr)(...);

// Data types

// Data categories:
//-----------------------------------------------------------------------------
// Integer:         size_t bits max.
// Scalar Signal:   A pointer to a signal struct, whose first byte
//                  is a Level.
// Vector Signal:   An array of signal pointers. Register is a pointer
//                  to this array.
// Integer Memory:  An array of size_t integers. Register a pointer.

// a Parameter is only a constant at the time of instantiation, and
// a variable may use a parameter for the width, which means that
// storage allocation isn't fixed until instantiation!
// That's too high a price. The work-around is to always specify
// a constant number of bits large enough for all uses.

class Data
{
public:
    size_t      vDisp;      // or vector loc rel to rSP in stack frame
    union
    {
        size_t  value;      // value for integer constant
        Level   sValue;     // value for scalar constant
        Level*  vValue;     // value pointer for vector constant
    };
    VExType     type;       // data type
    int         scale;      // scaled-integer scale
    const char* name;       // name for debug dump

    void        display();
    bool        isType(VExTyCode t) { return this->type.code == t; }
    void        setIntReg(short nBits = 1, int scale = 1)
                                    { this->type.code = ty_int;
                                      this->type.size = nBits;
                                      this->scale = scale;
                                    }
    void        setScalarReg()
                                    { this->type.code = ty_scalar;
                                      this->scale = 1;
                                    }
    void        setVector(Level* vec, short size, size_t disp)
                                    { this->type.code = ty_vector;
                                      this->type.size = size;
                                      this->scale = 1;
                                      this->vDisp = disp;
                                      this->vValue = vec; }

    friend void codeIntExpr(Expr* ex);
    friend void codeScalarExpr(Expr* ex);
    friend void codeVectorExpr(Expr* ex, int bitWidth);
};

const int max_parms = 9;

// Verilog compiler state

struct VComp
{
    short   nextLocal;  // next local variable offset in module struct
    int*    stmwInstr;  // address of stacking instr

    Data*   dsp;        // data stack pointer: points to top item
    Data*   dataStk;    // data stack: limit of stack growth
    Data*   dataStkEnd; // end of data stack: grows down from here

    int rspMax;     // lowest register used in tmp reg stack
    VError* tooComplexError;
    VError* stackEmpty;
    Instance* mainInstance;         // top instance: 'main'

    Expr*   parms[max_parms];       // array of current compiled parameters
    clock_t startRealTime;          // start time of compilation

    Token*  srcLoc;     // the Verilog line being executed
};

// ------------ Global Variables -------------

extern VComp vc;        // Verilog compiler state

// -------- Global Function Prototypes --------

// VLCoderXXX.c
void initBackEnd();
void initCodeArea();
void codeSubrHead(bool isTask);
void codeSubrTail();
void codeEndModule();
size_t* codeIf();
size_t* codeIfNot();
size_t* codeJmp();
void codeJmp(size_t* offsetAddr);
void setJmpToHere(size_t* offsetAddr);
void setJmpTo(size_t* offsetAddr, size_t* destAddr);
size_t* here();

void codeLitInt(size_t arg, int scale = 1);
void codeLoadInt(size_t disp, Variable* extScopeRef = 0);
void codeStoInt(size_t disp, Variable* extScopeRef = 0);

void codePostNamedEvent(TrigNet* namedEvt, Variable* extScopeRef = 0);

void codeLitLevel(Level arg);
void codeLoadScalar(Net* net, Variable* extScopeRef = 0);
void codeLoadScalar(size_t disp, Variable* extScopeRef = 0);
void codeSampleScalar();
void codePost0Scalar(Net* net, Variable* extScopeRef = 0);
void codePost1Scalar(Net* net, Variable* extScopeRef = 0);
void codePost2Scalar(Net* net, Variable* extScopeRef = 0);
void codePostScalar(Scalar* lVal, Variable* extScopeRef,
                           NetAttr lValAttr, int nParms);

void codeLitLVec(Variable* levVec, Variable* extScopeRef = 0);
void codeLoadVector(Vector* vec, Range* select, Variable* extScopeRef = 0);
void codeSampleVector();
void codePostVector(Vector* vec, Range* select, Variable* extScopeRef,
                           NetAttr lValAttr, int nParms);

void codePost(Net* net, Range* range, Variable* extScopeRef,
              NetAttr attr, int nParms);

void codeLoadMem(Memory* mem, Variable* extScopeRef = 0);
void codeStoMem(Memory* mem, Variable* extScopeRef = 0);

void pushEmpData(int n = 1, const char* name = 0);
void dropData(int n = 1);

void codeDup();     // these check and convert data types before coding
void codePick(int itemNo);
void codePush(int n = 1);
void codeDrop(int n = 1);
void codeSwap();
int popConstInt();
void codeConcat();
void codeExpr(Expr* ex);
void codeIntExpr(Expr* ex);
void codeScalarExpr(Expr* ex);
void codeVectorExpr(Expr* ex, int bitWidth = -1);
void codeSplit(short nBits);
void codeCall(Subr* subr, int nArgs, const char* name = 0, bool isVariadic = FALSE);
void codeWait();
void codeDelay();
void codeCallFn(Func* func, int nArgs);
void codeLoadScopeRef(Variable* extScopeRef);
void codeCallTaskTest(Net* net, ModelFuncPtr taskTestFn, Variable* extScopeRef);
void codeCallTaskTestVec(Vector* vec, ModelFuncPtr taskTestFn,
                         Variable* extScopeRef);
void codeModelCallPrefix();
void codeModelCall(ModelSubrPtr subr, int nArgs);
void codeCallVTask(NamedTask* task, Variable* extScopeRef);
