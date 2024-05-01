// ****************************************************************************
//
//          PVSim Verilog Simulator P-Code Coder Back End
//
// Copyright 2006,2012 Scott Forbes
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

#ifdef EXTENSION
#include <Python.h>
#endif
#include "PSignal.h"
#include "Model.h"

#include "VLCompiler.h"
#include "Src.h"
#include "VL.h"
#include "VLCoder.h"
#include "VLSysLib.h"
#include "PCode.h"
#include "PSignal.h"

// P-code is much slower than native code, but has the big advantage of being
// debuggable.

const int max_dataStack = 100;
const int max_codeLen = 10000;

PCode*  pc;             // current PCode being compiled
PCode*  pcStart;        // compiled code area
PCode*  pcEnd;          // end of compiled code area
int*    sp;             // run-time data stack
VComp   vc;             // other Verilog compiler state

const char* kExTypeName[ty_none+1]; // expression type code names
const char* kPCodeName[p_last];     // P-code names

//-----------------------------------------------------------------------------
// Initialize the compiler back end.

void initBackEnd()
{
    // sanity checks: check compiler settings
    if ((size_t)&((PCode*)0)->n != sizeof(size_t))
        throw new VError(verr_bug,
                "struct PCode offset of 'n' wrong: %d instead of %d bytes\n",
                (size_t)&((PCode*)0)->n, sizeof(size_t));
    if (sizeof(PCode) != 2*sizeof(size_t))
        throw new VError(verr_bug,
                "struct PCode size wrong: %d instead of %d bytes\n",
                sizeof(PCode), 2*sizeof(size_t));
    pcStart = 0;            // code space will be allocated as needed
    pc = 0;
    pcEnd = 0;
    if (!vc.tooComplexError)        // create these only once:
    {
        vc.dataStk = new Data[max_dataStack];
        vc.dataStkEnd = vc.dataStk + max_dataStack;
        vc.tooComplexError = new VError(verr_notYet,
                                        "expression too complex (push)");
        vc.stackEmpty = new VError(verr_bug, "BUG: data stack empty");

        kPCodeName[p_nop] =         "nop";
        kPCodeName[p_dup] =         "dup";
        kPCodeName[p_pick] =        "pick";
        kPCodeName[p_swap] =        "swap";
        kPCodeName[p_rot] =         "rot";
        kPCodeName[p_func] =        "func";
        kPCodeName[p_rts] =         "rts";
        kPCodeName[p_wait] =        "wait";
        kPCodeName[p_del] =         "del";
        kPCodeName[p_leam] =        "leam";
        kPCodeName[p_leai] =        "leai";
        kPCodeName[p_add] =         "add";
        kPCodeName[p_sub] =         "sub";
        kPCodeName[p_mul] =         "mul";
        kPCodeName[p_div] =         "div";
        kPCodeName[p_and] =         "and";
        kPCodeName[p_or] =          "or";
        kPCodeName[p_xor] =         "xor";
        kPCodeName[p_sla] =         "sla";
        kPCodeName[p_sra] =         "sra";
        kPCodeName[p_eq] =          "eq";
        kPCodeName[p_ne] =          "ne";
        kPCodeName[p_gt] =          "gt";
        kPCodeName[p_le] =          "le";
        kPCodeName[p_lt] =          "lt";
        kPCodeName[p_ge] =          "ge";
        kPCodeName[p_not] =         "not";
        kPCodeName[p_nots] =        "nots";
        kPCodeName[p_com] =         "com";
        kPCodeName[p_neg] =         "neg";
        kPCodeName[p_cvis] =        "cvis";
        kPCodeName[p_end] =         "end";

        kPCodeName[p_liw] =         "liw";
        kPCodeName[p_lds] =         "lds";

        kPCodeName[p_bsr0] =        "bsr0";
        kPCodeName[p_bsr1] =        "bsr1";
        kPCodeName[p_bsr2] =        "bsr2";
        kPCodeName[p_bsr3] =        "bsr3";
        kPCodeName[p_bsr4] =        "bsr4";
        kPCodeName[p_bsr5] =        "bsr5";
        kPCodeName[p_bsr] =         "bsr";
        kPCodeName[p_btsk] =        "btsk";
        kPCodeName[p_br] =          "br";
        kPCodeName[p_beq] =         "beq";
        kPCodeName[p_bne] =         "bne";
        kPCodeName[p_drop] =        "drop";
        kPCodeName[p_li] =          "li";
        kPCodeName[p_lea] =         "lea";
        kPCodeName[p_ld] =          "ld";
        kPCodeName[p_ldx] =         "ldx";
        kPCodeName[p_ldbx] =        "ldbx";
        kPCodeName[p_st] =          "st";
        kPCodeName[p_stb] =         "stb";
        kPCodeName[p_stx] =         "stx";
        kPCodeName[p_addi] =        "addi";
        kPCodeName[p_andi] =        "andi";
        kPCodeName[p_sbop] =        "sbop";
    }
}

const int len_codeSpace = 10*max_codeLen;

class CodeSpace : SimObject
{
public:
    PCode code[len_codeSpace];
};

#ifdef MANUAL_DISPLAY
//-----------------------------------------------------------------------------
// Display a temp Data descriptor for debugging.

void Data::display()
{
    int itemNo = (int)(vc.dataStkEnd - this);
    switch (this->type.code)
    {
        case ty_int:
            ::display("i@%d", itemNo);
            break;
        case ty_scopeRef:
            ::display("c@%d", itemNo);
            break;
        case ty_scalar:
            ::display("s@%d", itemNo);
            break;
        case ty_vector:
            ::display("v@%d(%d)", vDisp, type.size);
            break;
        case ty_memory:
            ::display("m@%d[%d]", vDisp, type.size);
            break;
        default:
            throw new VError(verr_bug, "BUG: display: invalid type code");
    }
}
#endif

//-----------------------------------------------------------------------------

void dumpCoderDataStack()
{
    const char* s;
    display("                                            dsp: ");
    for (Data* p = vc.dsp; p < vc.dataStkEnd; p++)
    {
        switch (p->type.code)
        {
            case ty_int:
                s = "i";
                break;
            case ty_scopeRef:
                s = "sr";
                break;
            case ty_scalar:
                s = "sc";
                break;
            case ty_vector:
                s = "v";
                //s = TmpName("v@%d(%d)", p->vDisp, p->type.size);
                break;
            case ty_memory:
                s = "m";
                //s = TmpName("m@%d[%d]", p->vDisp, p->type.size);
                break;
            default:
                throw new VError(verr_bug,
                                 "BUG: dumpCoderDataStack: invalid type code");
        }
        if (p->name)
            display("%s ", p->name);
        else
            display("%s ", s);
    }
    display("\n");
}

//-----------------------------------------------------------------------------
// Code a 0-argument PCode.

void codeOp(PCodeOp op)
{
    if (debugLevel(3))
        display("%p: %s\n", pc, kPCodeName[op]);
    pc->op = op;

    // advance to next PCode-- single-op PCodes leave off 'n' field
    pc = (PCode*)&pc->n;
}

//-----------------------------------------------------------------------------
// Code a 1-integer-argument PCode.

void codeOpI(PCodeOp op, size_t arg)
{
    if (debugLevel(3))
        display("%p: %s 0x%x\n", pc, kPCodeName[op], arg);
    pc->op = op;
    pc->n = arg;
    pc++;
}

//-----------------------------------------------------------------------------
// Code a 1-short-argument PCode.

void codeOpW(PCodeOp op, short arg)
{
    if (debugLevel(3))
        display("%p: %s 0x%x\n", pc, kPCodeName[op], arg);
    pc->op = op;
    pc->w = arg;

    // advance to next PCode-- short PCodes leave off 'n' field
    pc = (PCode*)&pc->n;
}

//-----------------------------------------------------------------------------
// Set up code area for compiling a new block of code.

void initCodeArea()
{
    if (!pc || pc > pcEnd - max_codeLen)
    {
        CodeSpace* space = new CodeSpace;
        pcStart = (PCode*)space->code;
        pc = pcStart;
        pcEnd = pc + len_codeSpace;
    }
    vc.dsp = vc.dataStkEnd;     // reset data stack pointer

    // TEST ONLY
    // Model::test();
}

//-----------------------------------------------------------------------------
// Compile instructions that begin a subroutine.
//
// For handlers (not Verilog tasks):
// 1st argument to subroutine is Model address.
// 2nd argument to subroutine is module instance base.

void codeSubrHead(bool isTask)
{
    if (debugLevel(3))
        display("            #%2d subrHead\n", vc.dataStkEnd-vc.dsp);
    vc.dsp = vc.dataStkEnd;                     // reset data stack pointer
}

//-----------------------------------------------------------------------------
// Compile instruction that finishes a subroutine: pop return addr.

void codeSubrTail()
{
    if (debugLevel(3))
        display("            #%2d subrTail\n", vc.dataStkEnd-vc.dsp);
    codeOp(p_rts);
}

//-----------------------------------------------------------------------------
// Compile instruction that ends a module and stop execution.

void codeEndModule()
{
    if (debugLevel(3))
        display("            #%2d endModule\n", vc.dataStkEnd-vc.dsp);
    codeOp(p_end);
}

//-----------------------------------------------------------------------------
// General data stack operations.

// Push typed data onto data stack
void pushData(Data* data)
{
    *(--vc.dsp) = *data;
    if (vc.dsp < vc.dataStk)
        throw vc.tooComplexError;
}

// Allocate (but don't fill in) a new stack entry
void pushEmpData(int n, const char* name)
{
    vc.dsp -= n;
    if (vc.dsp < vc.dataStk)
        throw vc.tooComplexError;
    vc.dsp->name = name;
    vc.dsp->type.code = ty_int;
}

// Drop the top n data elements from the data stack.
void dropData(int n)
{
    vc.dsp += n;
    if (vc.dsp > vc.dataStkEnd)
        throw vc.stackEmpty;
    if (pc > pcEnd - 100)
        throw new VError(verr_notYet, "routine or statement too long");
}

// Duplicate the top data on the data stack.
void codeDup()
{
    Data* data = vc.dsp;
    pushData(data);
    switch (data->type.code)
    {
        case ty_int:
        case ty_scalar:
            codeOp(p_dup);
            break;

        case ty_vector:
            break;

        default:
            throw new VError(verr_bug, "BUG: codeDup: unknown data type");
    }
}

// Pick the nth item on the (virtual) data stack and push it.
void codePick(int itemNo)
{
    Data* data = vc.dsp;
    // non-stacked vectors are omitted to get true stack displacement
    int stackDisp = 0;
    for ( ; itemNo > 0; itemNo--, data++)
        if (data->type.code != ty_vector)
            stackDisp++;
    pushData(data);
    switch (data->type.code)
    {
        case ty_int:
        case ty_scalar:
            codeOpW(p_pick, stackDisp);
            break;

        case ty_vector:
            break;

        default:
            throw new VError(verr_bug, "BUG: codePick: unknown data type");
    }
    if (debugLevel(3))
        dumpCoderDataStack();
}

// Code a drop of n items from stack. It looks at their types to determine
// the actual drop count.
void codeDrop(int n)
{
    Data* data = vc.dsp;
    // non-stacked vectors are omitted to get true drop count
    int count = 0;
    for (int i = n ; i > 0; i--, data++)
        if (data->type.code != ty_vector)
            count++;
    if (count)
        codeOpW(p_drop, count);
    dropData(n);
    if (debugLevel(3))
        dumpCoderDataStack();
}

// Code a 2-operand operation.
void codeOp2(PCodeOp op)
{
    codeOp(op);
    dropData();
    if (debugLevel(3))
        dumpCoderDataStack();
}

//-----------------------------------------------------------------------------
// Compile a branch to a C subroutine.
// Takes nArgs arguments from data stack.
// Arguments are expected to be pushed on data stack in calling order,
// but are passed to subroutine in C order.

void codeCall(Subr* subr, int nArgs, const char* name)
{
    if (debugLevel(3))
    {
        if (name)
            display("            #%2d call %s (*%d)\n",
                    vc.dataStkEnd-vc.dsp, name, nArgs);
        else
            display("            #%2d call %p (*%d)\n",
                    vc.dataStkEnd-vc.dsp, subr, nArgs);
    }
    pc->nArgs = nArgs;
    PCodeOp op = p_bsr;
    switch (nArgs)
    {
        case 0: op = p_bsr0; break;
        case 1: op = p_bsr1; break;
        case 2: op = p_bsr2; break;
        case 3: op = p_bsr3; break;
        case 4: op = p_bsr4; break;
        case 5: op = p_bsr5; break;
    }
    codeOpI(op, (size_t)subr);
    dropData(nArgs);
}

//-----------------------------------------------------------------------------
// Compile a wait or delay PCode.

void codeWait()
{
    codeOp(p_wait);
}

void codeDelay()
{
    codeOp(p_del);
}

//-----------------------------------------------------------------------------
// Compile a call to a Model subroutine.
// Arguments are expected to be on stack in in C order
// (Model addr first, then first arg).

void codeCallModel(ModelSubrPtr subr, int nArgs, const char* name = 0)
{
    codeCall(((Subr**)&subr)[0], nArgs, name);
}

//-----------------------------------------------------------------------------
// Compile a branch to a function at given address.
// Takes nArgs arguments from data stack and puts the function's
// return value back on the stack.
// Arguments are expected to be pushed on data stack in calling order, but
// are passed to subroutine in C order (first arg in first reg).

void codeCallFn(Func* func, int nArgs)
{
    codeCall((Subr*)func, nArgs);
    codeOp(p_func);
    pushEmpData();
    vc.dsp->setIntReg();
}

//-----------------------------------------------------------------------------
// Compile a Load Integer of either an external scope address, or if
// zero, the current scope address. This gets passed as an argument
// to task calls.

void codeLoadScopeRef(Variable* extScopeRef)
{
    pushEmpData();
    if (extScopeRef)
        codeOpI(p_ld, extScopeRef->disp);
    else
        codeOp(p_leai);
    vc.dsp->setIntReg();
}

//-----------------------------------------------------------------------------
// Compile a branch; returns address of offset to be filled in

size_t* codeJmp()
{
    size_t* brAdr = &pc->n;
    codeOpI(p_br, 0);
    return brAdr;
}

// Compile an if: a pop and test the integer on stack and branch if 0;
// returns address of offset to be filled in

size_t* codeIf()
{
    size_t* brAdr = &pc->n;
    codeOpI(p_beq, 0);
    dropData();
    return brAdr;
}

// Compile an ifnot: a pop and test the integer on stack and branch if not 0;
// returns address of offset to be filled in

size_t* codeIfNot()
{
    size_t* brAdr = &pc->n;
    codeOpI(p_bne, 0);
    dropData();
    return brAdr;
}

// fill in the branch address from a branch to point to pc

// resolve a forward jump.
void setJmpToHere(size_t* offsetAddr)
{
    *offsetAddr = (size_t)pc;
}

// resolve what could be a backwards (negative) jump.
void setJmpTo(size_t* offsetAddr, size_t* destAddr)
{
    *offsetAddr = (size_t)destAddr;
}

void codeJmp(size_t* offsetAddr)
{
    setJmpTo(codeJmp(), offsetAddr);
}

// Return current code pointer.
size_t* here()
{
    return (size_t*)pc;
}

//-----------------------------------------------------------------------------
// Integer operations.

// Code a load immediate size_t integer.
void codeLitInt(size_t n, int scale)
{
    pushEmpData();
    if (n >= 0x8000 || (int)n <= -0x8000)
        codeOpI(p_li, n);
    else
        codeOpW(p_liw, n);
    vc.dsp->setIntReg(1, scale);
    if (debugLevel(3))
    {
        vc.dsp->name = newString(TmpName("%d", n));
        dumpCoderDataStack();
    }
}

// Code a load effective address, given displacement.
void codeLoadAdr(size_t disp, Variable* extScopeRef = 0)
{
    pushEmpData();
    if (debugLevel(3))
        display("            #%2d lea inst[%d]\n", vc.dataStkEnd-vc.dsp, disp);
    if (extScopeRef)
    {
        codeOpI(p_ld, extScopeRef->disp);
        codeOpI(p_addi, disp);
    }
    else
        codeOpI(p_lea, disp);
    vc.dsp->setIntReg();
}

// Compile a Load Integer
void codeLoadInt(size_t disp, Variable* extScopeRef)
{
    pushEmpData();
    if (debugLevel(3))
        display("            #%2d loadint inst[%d]\n",
                vc.dataStkEnd-vc.dsp, disp);
    if (extScopeRef)
    {
        codeOpI(p_ld, extScopeRef->disp);
        codeOpI(p_ldx, disp);
    }
    else
        codeOpI(p_ld, disp);
    vc.dsp->setIntReg();
    if (debugLevel(3))
    {
        vc.dsp->name = 0;
        dumpCoderDataStack();
    }
}

// Compile a Store Integer
void codeStoInt(size_t disp, Variable* extScopeRef)
{
    if (debugLevel(3))
        display("            #%2d stoint inst[%d]\n",
                vc.dataStkEnd-vc.dsp, disp);
    if (extScopeRef)
    {
        codeOpI(p_ld, extScopeRef->disp);
        codeOpI(p_stx, disp);
    }
    else
        codeOpI(p_st, disp);
    dropData();
}

//-----------------------------------------------------------------------------
// Compile a select (Sel ? True : False) on integer or scalar values.

void codeSelect()
{
    if (debugLevel(3))
        display("            #%2d select\n", vc.dataStkEnd-vc.dsp);

    codeOp(p_rot);
    vc.dsp[0] = vc.dsp[2];
    size_t* brAdr = codeIf();
    codeOpW(p_drop, 1);
    size_t* elseAdr = codeJmp();
    setJmpToHere(brAdr);
    codeOp(p_swap);
    codeOpW(p_drop, 1);
    setJmpToHere(elseAdr);

    vc.dsp[1] = vc.dsp[0];
    dropData(1);
}

//-----------------------------------------------------------------------------
// Code a copy of an LVector from one displacement to another in local var
// space. Returns address of destDisp instr.

size_t* codeCopyLVec(size_t srcDisp, size_t destDisp, size_t size)
{
    if (debugLevel(3))
        display(
            "            #%2d copyLVec(src=I+0x%x, dest=I+0x%x, size=0x%x)\n",
            vc.dataStkEnd-vc.dsp, srcDisp, destDisp, size);
    size_t* destDispInstr = &pc->n;
    codeLoadAdr(destDisp);
    codeLoadAdr(srcDisp);
    codeLitInt(size);
    codeCall((Subr*)memcpy, 3, "memcpy");
    return destDispInstr;
}

//-----------------------------------------------------------------------------
// Compile an LVector select (Sel ? True : False).

void codeSelectLVec(int nBits)
{
    Data dFalse = vc.dsp[0];
    Data dTrue = vc.dsp[1];
    dropData(2);
    size_t* brAdr = codeIf();
    codeCopyLVec(dTrue.vDisp, dFalse.vDisp, dFalse.type.size);
    setJmpToHere(brAdr);
    pushEmpData();
    vc.dsp->setVector(dFalse.vValue, dFalse.type.size, dFalse.vDisp);
}

//-----------------------------------------------------------------------------
// Operations on signal levels.

// Push a literal signal level.
void codeLitLevel(Level arg)
{
    pushEmpData();
    codeOpW(p_liw, arg);
    vc.dsp->setScalarReg();
    if (debugLevel(3))
    {
        vc.dsp->name = newString(TmpName("%c", gLevelNames[arg]));
        dumpCoderDataStack();
    }
}

// Load a signal at given displacement (used for vector bit selects).
void codeLoadScalar(int disp, Variable* extScopeRef)
{
    pushEmpData();
    if (extScopeRef)
    {
        codeOpI(p_ld, extScopeRef->disp);
        codeOpI(p_ldx, disp);
        codeOpI(p_ldbx, 0);
    }
    else if (disp >= 0x8000)
    {
        codeOpI(p_ld, disp);
        codeOpI(p_ldbx, 0);
    }
    else
        codeOpW(p_lds, disp);
    vc.dsp->setScalarReg();
    if (debugLevel(3))
        dumpCoderDataStack();
}

// Load a signal.
void codeLoadScalar(Net* net, Variable* extScopeRef)
{
    codeLoadScalar(net->disp, extScopeRef);
}

//-----------------------------------------------------------------------------
// 'Sample' a scalar signal (Falling or Rising becomes Changing, etc.).
// Cleans signals that change on only certain events, such as a clock edge.

void codeSampleScalar()
{
    codeOpI(p_addi, (size_t)&funcTable.SAMPLEtable);
    codeOpI(p_ldbx, 0);
}

//-----------------------------------------------------------------------------
// 'Sample' a scalar signal (Falling or Rising becomes Changing, etc.).

void codeSampleVector()
{
    size_t levelVecDisp = vc.dsp->vDisp;
    size_t levelVecSize = vc.dsp->type.size;
    codeLoadAdr(levelVecDisp);
    codeLitInt(levelVecSize);
    codeCall((Subr*)sampleVector, 2, "sampleVector");
}

//-----------------------------------------------------------------------------
// Post a signal-change event with no delay time. New level is on the stack.
void codePost0Scalar(Net* net, Variable* extScopeRef)
{
    // code: task->addEventV(dt, signal, level, CLEAN);
    codeModelCallPrefix();                  // model address
    codeLitInt(0);                          // dt = 0
    codeLoadInt(net->disp, extScopeRef);    // signal addr
    codePick(3);                            // level
    codeLitInt(CLEAN);                      // CLEAN
    codeCallModel((ModelSubrPtr)&Model::addEventV, 5, "addEventV");
    codeDrop(1);
}

// Post a signal-change event. Delay time and New level are on the stack.
void codePost1Scalar(Net* net, Variable* extScopeRef)
{
    // code: task->addEventV(dt, signal, level, CLEAN);
    codeModelCallPrefix();                  // model address
    codePick(2);                            // dt
    codeLoadInt(net->disp, extScopeRef);    // signal addr
    codePick(3);                            // level
    codeLitInt(CLEAN);                      // CLEAN
    codeCallModel((ModelSubrPtr)&Model::addEventV, 5, "addEventV");
    codeDrop(2);
}

// Post a min-max signal-change event. dtMin, dtMax times and New level
// are on the stack.
void codePost2Scalar(Net* net, Variable* extScopeRef)
{
    // code: task->addMinMaxEventV(dtMin, dtMax, signal, level);
    codeModelCallPrefix();                  // model address
    codePick(3);                            // dtMin
    codePick(3);                            // dtMax
    codeLoadInt(net->disp, extScopeRef);    // signal addr
    codePick(4);                            // level
    codeCallModel((ModelSubrPtr)&Model::addMinMaxEventV, 5, "addMinMaxEventV");
    codeDrop(3);
}

//-----------------------------------------------------------------------------
// Expects a scalar expression on the data stack.

void codePostScalar(Scalar* lVal, Variable* extScopeRef,
                           NetAttr lValAttr, int nParms)
{
    if (lValAttr == att_reg)
    {
        if (!(lVal->attr & att_reg))
            throwExpected("reg lvalue");
    }
    else
    {
        if ((lVal->attr & att_reg))
            throwExpected("wire lvalue");
    }
    switch (nParms)
    {
        case 0:
            codePost0Scalar((Net*)lVal, extScopeRef);
            break;
        case 1:
            codePost1Scalar((Net*)lVal, extScopeRef);
            break;
        case 2:
            codePost2Scalar((Net*)lVal, extScopeRef);
            break;
        default:
            throw new VError(verr_illegal, "too many delay parms");
            break;
    }
}

//-----------------------------------------------------------------------------
// Code a load of an LVector.

void codeLitLVec(Variable* levVec, Variable* extScopeRef)
{
    // Copy an LVector from variable to a temp local var space.
    size_t levVecDisp = Scope::local->newLocal(levVec->exType.size,
                                               sizeof(Level));
    codeLoadAdr(levVecDisp);                // destination
    codeLoadAdr(levVec->disp, extScopeRef); // source
    codeLitInt(levVec->exType.size);        // length
    codeCall((Subr*)memcpy, 3, "memcpy");

    pushEmpData();
    vc.dsp->setVector(0, levVec->exType.size, levVecDisp);
}

//-----------------------------------------------------------------------------

void checkInRange(Range* vecRange, Range* select)
{
    if (vecRange->isScalar)
        throw new VError(verr_bug, "BUG: checkInRange: 'scalar' vector range");
    if (!select->isScalar && vecRange->incr != select->incr)
        throw new VError(verr_illegal, "range pair in wrong order");

    int low = vecRange->left.bit;
    int high = vecRange->right.bit;
    if (vecRange->incr < 0)
    {
        int temp = low;
        low = high;
        high = temp;
    }
    int bit = select->left.bit;
    if (select->left.isConst && (bit < low || bit > high))
        goto outOfRange;
    bit = select->right.bit;
    if (!select->isScalar && select->right.isConst && (bit < low || bit > high))
        goto outOfRange;
    return;

outOfRange:
    throw new VError(verr_illegal, "vector bit %d out of range [%d:%d]",
                     bit, vecRange->left.bit, vecRange->right.bit);
}

//-----------------------------------------------------------------------------
// Code a load of one (possibly general) index in a Range.
// The resulting integer is left on the stack (return value to be ignored).

int RangeIndex::codeIntExpr()
{
    if (this->isConst)
        codeLitInt(this->bit);
    else if (isDisp)
        codeLoadInt(this->disp);
    else // variable expression
        ::codeIntExpr(this->expr);
    return 0;
}

//-----------------------------------------------------------------------------
// Code the bit range selection part of a Vector signal access.
// Compiled code will push 3 values: sigVec, i, iMax.

static void codeVecRange(Vector* vec, Range* select, Variable* extScopeRef)
{
    if (debugLevel(3))
        display("            #%2d vecRange(%s[\n",
                vc.dataStkEnd-vc.dsp, vec->name);
    Range* vecRange = vec->range;
    checkInRange(vecRange, select);

    // get sigVec: Signal ref base address (add in module base address) 
    if (debugLevel(3))
        display("            #%2d vecRange base\n", vc.dataStkEnd-vc.dsp);
    codeLoadAdr(vec->disp, extScopeRef);

    // i = (select->left - vecRange->left) * vecRange->incr;
    if (debugLevel(3))
        display("            #%2d vecRange scaling\n", vc.dataStkEnd-vc.dsp);
    if (select->left.isConst && vecRange->left.isConst)
        codeLitInt((select->left.bit - vecRange->left.bit) * vecRange->incr);
    else
    {
        select->left.codeIntExpr();
        vecRange->left.codeIntExpr();
        codeOp2(p_sub);
        codeLitInt(vecRange->incr);
        codeOp2(p_mul);
    }

    // iMax = (vecRange->right - vecRange->left) * vecRange->incr;
    if (vecRange->right.isConst && vecRange->left.isConst)
        codeLitInt((vecRange->right.bit - vecRange->left.bit) * vecRange->incr);
    else
    {
        vecRange->right.codeIntExpr();
        vecRange->left.codeIntExpr();
        codeOp2(p_sub);
        codeLitInt(vecRange->incr);
        codeOp2(p_mul);
    }
}

//-----------------------------------------------------------------------------
// Code a load of all or part of a vector signal, either onto reg-stack (if
// 1 bit selected) or into a new LVector in local space.

void codeLoadVector(Vector* vec, Range* select, Variable* extScopeRef)
{
    if (debugLevel(3))
        display("            #%2d loadVector(%s\n",
                vc.dataStkEnd-vc.dsp, vec->name);
    // get address and bit index(es) of source Vector into registers
    codeVecRange(vec, select, extScopeRef);
    // now on stack: sigVec, i, iMax

    // either code a bit select and load it
    if (select->isScalar)   // (implies non-full range)
    {
        // code call: Level loadIndScalar(SignalVec* sigVec, int i, int iMax)
        codeCallFn((Func*)loadIndScalar, 3);
        vc.dsp->setScalarReg();
    }
    else // or code a part select and load vector into temp area
    {
        int nBits = select->size;
        size_t levVecDisp = Scope::local->newLocal(nBits, sizeof(Level));
        codeLoadAdr(levVecDisp);
        codeLitInt(nBits);
        // code call: void loadIndVector(SignalVec* sigVec, int i, int iMax,
        //                               Level* levVecDisp, int nBits)
        if (debugLevel(3))
            display("            #%2d bl loadIndVector #sv=I+0x%x,"
                    "lv=I+0x%x,size=%d\n", vc.dataStkEnd-vc.dsp, vc.dsp->vDisp,
                    levVecDisp, select->size);
        codeCall((Subr*)loadIndVector, 5, "loadIndVector");
        pushEmpData();
        vc.dsp->setVector(0, nBits, levVecDisp);
    }
}

//-----------------------------------------------------------------------------
// Code a post of a signal vector, new levels are in an LVector (or Level if
// isScalar) referred to on the stack.

void codePostVector(Vector* lVal, Range* select, Variable* extScopeRef,
                           NetAttr lValAttr, int nParms)
{
    if (debugLevel(3))
        display("            #%2d postVector(%s\n",
                vc.dataStkEnd-vc.dsp, lVal->name);
    if (lValAttr == att_reg)
    {
        if (!(lVal->attr & att_reg))
            throwExpected("reg lvalue");
    }
    else
    {
        if ((lVal->attr & att_reg))
            throwExpected("net lvalue");
    }

    // new level's data
    Data* dLevel = vc.dsp;
    codeModelCallPrefix();                  // model address
    // get address and bit index(es) of dest Vector into registers
    codeVecRange(lVal, select, extScopeRef);
    // now on stack: <parms>, level, (empty), model, sigVec, i, iMax

    ModelSubrPtr vtaskSubr;
    const char* vtaskSubrName;
    int nArgs = 7;
    switch (nParms)
    {
        case 0:
            // Post a vector-change event with no delay. New levels are in an
            // LVector (or Level if isScalar) referred to on the stack.

            // code: task->addIndEventV(SignalVec* sigVec, int i, int iMax,
            //                           0, level, CLEAN)
            codeLitInt(0);                  // dt = 0
            goto postCleanVector;

        case 1:
            // Post a vector-change event. Delay time and new levels are in an
            // LVector (or Level if isScalar) referred to on the stack.
            codePick(5);                    // dt
        postCleanVector:
            switch (dLevel->type.code)
            {
                case ty_scalar:
                    // code: task->addIndEventV(SignalVec* sigVec, int i,
                    //                          int iMax, dt, level, CLEAN)
                    if (debugLevel(3))
                        display("            #%2d bl addIndEventV CLEAN\n",
                                vc.dataStkEnd-vc.dsp);
                    codePick(nParms+5);     // level
                    codeLitInt(CLEAN);      // CLEAN
                    vtaskSubr = (ModelSubrPtr)&Model::addIndEventV;
                    vtaskSubrName = "addIndEventV";
                    break;
                case ty_vector:
                    // code: task->postIndVectorV(SignalVec* sigVec, int i,
                    //                            int iMax, dt, levelVec, size)
                    if (debugLevel(3))
                        display("            #%2d bl postIndVectorV"
                                " #disp=0x%x,size=%d\n", vc.dataStkEnd-vc.dsp,
                                dLevel->vDisp, select->size);
                    codeLoadAdr(dLevel->vDisp); // levelVec
                    codeLitInt(select->size);   // size
                    vtaskSubr = (ModelSubrPtr)&Model::postIndVectorV;
                    vtaskSubrName = "postIndVectorV";
                    break;
                default:
                    throw new VError(verr_bug,
                                     "BUG: codePostVector: invalid type code");
            }
            break;

        case 2:
            // Post a min-max vector-change event. dtMin, dtMax times and new
            // levels are in an LVector (or Level if isScalar) referred to on
            // the stack.
            codePick(5);                    // dtMin
            codePick(5);                    // dtMax

            switch (dLevel->type.code)
            {
                case ty_scalar:
                    // code: task->addIndMinMaxEventV(SignalVec* sigVec, int i,
                    //                           int iMax, dtMin, dtMax, level)
                    codePick(7);            // level
                    vtaskSubr = (ModelSubrPtr)&Model::addIndMinMaxEventV;
                    vtaskSubrName = "addIndMinMaxEventV";
                    nParms++; // to drop scalar when done
                    break;
                case ty_vector:
                    // code: task->postIndMinMaxVectorV(SignalVec* sigVec,
                    //                   int i, int iMax, int dtMin, int dtMax,
                    //                   Level* levVec, int nBits)
                    codeLoadAdr(dLevel->vDisp); // levelVec
                    codeLitInt(select->size);   // size
                    vtaskSubr = (ModelSubrPtr)&Model::postIndMinMaxVectorV;
                    vtaskSubrName = "postIndMinMaxVectorV";
                    nArgs = 8;
                default:
                    throw new VError(verr_bug,
                                     "BUG: codePostVector: invalid type code");
            }
            break;

        default:
            throw new VError(verr_illegal, "too many delay parms");
            break;
    }
    codeCallModel(vtaskSubr, nArgs, vtaskSubrName);
    codeDrop(nParms+1);     // drop parms and given level
    //if (dLevel->type.code != ty_scalar)
    //    dropData(1);
}

//-----------------------------------------------------------------------------
// Post an event for either a scalar or vector.

void codePost(Net* net, Range* range, Variable* extScopeRef,
              NetAttr attr, int nParms)
{
    switch (net->exType.code)
    {
        case ty_int:
            throwExpected("scalar or vector");
            break;
        case ty_scalar:
            codePostScalar((Scalar*)net, extScopeRef, attr, nParms);
            break;
        case ty_vector:
            codePostVector((Vector*)net, range, extScopeRef, attr, nParms);
            break;

        default:
            throw new VError(verr_bug, "BUG: codePost: invalid type code");
            break;
    }
}

//-----------------------------------------------------------------------------
// Code a load of a memory element as an integer to the stack.
// Index is expected on the stack.

void codeLoadMem(Memory* mem, Variable* extScopeRef)
{
    // *4 for int index, +4 to skip over trigger signal pointer
    codeLitInt(sizeof(size_t) == 8 ? 3 : 2);
    codeOp2(p_sla);
    if (extScopeRef)
        codeOpI(p_ld, extScopeRef->disp);
    else
        codeOp(p_leai);
    codeOp(p_add);
    codeOpI(p_ldx, mem->disp + sizeof(size_t));
    vc.dsp->setIntReg(mem->elemRange ? mem->elemRange->size : sizeof(size_t)*8);
}

//-----------------------------------------------------------------------------
// Compile a write of an integer value to a memory.
// The memory address was pushed first on the dsp stack;
//
// !!! size_t integer width only for now

void codeStoMem(Memory* mem, Variable* extScopeRef)
{
    // *4 for int index, +4 to skip over trigger signal pointer
    codeOp(p_swap);
    codeLitInt(sizeof(size_t) == 8 ? 3 : 2);
    codeOp2(p_sla);
    if (extScopeRef)
        codeOpI(p_ld, extScopeRef->disp);
    else
        codeOp(p_leai);
    codeOp(p_add);
    codeOpI(p_stx, mem->disp + sizeof(size_t));
    dropData(2);

    // now post an event to the memory's model signal to notify readers
    // of a possible data change.
    codePostNamedEvent(mem, extScopeRef);
}

//-----------------------------------------------------------------------------
// Code a unary operation on an LVector into a 2nd LVector. Op uses table.

void codeVectorUnaryOp(const Level* table)
{
    Data* arg = vc.dsp;
    int nBits = arg->type.size;
    size_t levVecDisp = Scope::local->newLocal(nBits, sizeof(Level));
    codeLoadAdr(arg->vDisp);        // src vector
    codeLoadAdr(levVecDisp);        // dest vector
    codeLitInt(nBits);              // size
    codeLitInt((size_t)table);      // Level op table
    codeCall((Subr*)unaryOpLVec, 4, "unaryOpLVec");
    vc.dsp->setVector(0, nBits, levVecDisp);
}

//-----------------------------------------------------------------------------
// Code a binary logical operation on two scalars.

void codeScalarBinaryOp(const Level* table)
{
    codeOpI(p_sbop, (size_t)table);
    dropData();
}

void codeVectorBinaryOp(const Level* table)
{
    Data* arg0 = vc.dsp;
    Data* arg1 = vc.dsp + 1;
    int nBits = arg0->type.size;
    size_t levVecDisp = Scope::local->newLocal(nBits, sizeof(Level));
    // void binaryOpLVec(Level* src0, Level* src1, Level* dest, int nBits,
    //                  Level table[12][16])
    codeLoadAdr(arg0->vDisp);       // src 0 integer
    codeLoadAdr(arg1->vDisp);       // src 1 vector
    codeLoadAdr(levVecDisp);        // dest vector
    codeLitInt(nBits);              // size
    codeLitInt((size_t)table);      // Level op table
    codeCall((Subr*)binaryOpLVec, 5, "binaryOpLVec");
    dropData();
    vc.dsp->setVector(0, nBits, levVecDisp);
}

//-----------------------------------------------------------------------------
// Code a vector-concatenation function. Expression is in the form of a 
// system function call, with an argument list of expressions to be
// concatenated.

void codeConcat(Expr* ex)
{
    // code each of the source expressions, keeping track of the current
    // result vector bit total and locations of each result store instr
    if (debugLevel(3))
        display("            #%2d concat(\n", vc.dataStkEnd-vc.dsp);
    int bit = 0;
    Expr* ap;
    for (ap = ex; ap; ap = ap->func.nextArgNode)
    {
        Expr* argEx = ap->func.arg;
        if (argEx->tyCode == ty_scalar)
        {
            codeScalarExpr(argEx);
            ap->func.catStoInstr = &pc->n;
            codeOpI(p_stb, bit);
            bit++;
        }
        else
        {
            codeVectorExpr(argEx);
            int argSize = vc.dsp->type.size;
            ap->func.catStoInstr = codeCopyLVec(vc.dsp->vDisp, bit, argSize);
            bit += argSize;
        }
        dropData();
    }

    // allocate local storage for result vector (dest) and push it on stack
    size_t destDisp = Scope::local->newLocal(bit, sizeof(Level));
    if (debugLevel(3))
        display("            #%2d concat alloc disp=0x%x\n",
                vc.dataStkEnd-vc.dsp, destDisp);
    pushEmpData();
    vc.dsp->setVector(0, bit, destDisp);
    // add vector base displacement to each of the result store instructions
    for (ap = ex; ap; ap = ap->func.nextArgNode)
        *(ap->func.catStoInstr) += destDisp;
}

//-----------------------------------------------------------------------------
// Code an expression returning an integer as stacked data.

void codeIntExpr(Expr* ex)
{
    if (debugLevel(3))
        display("            #%2d intExpr(\n", vc.dataStkEnd-vc.dsp);

    int scale = 1;
    if (ex->opcode == op_lit)
    {
        size_t value;
        switch (ex->tyCode)
        {
            case ty_int:
                value = ex->data.value;
                break;

            case ty_float:
                // a float value is assumed to be a time in ns, so convert it
                // to integer ticks
                scale = gTicksNS;
                value = (int)(ex->data.fValue * gTicksNS);
                break;

            case ty_scalar: 
                if (ex->data.sValue != LV_L && ex->data.sValue != LV_H)
                    goto cantConvert;
                value = (ex->data.sValue == LV_H);
                break;

            case ty_vector:
                if (ex->nBits > sizeof(size_t)*8)
                    goto tooLarge;
                value = convLVecToInt(ex->data.var->levelVec, ex->nBits);
                break;

            default:
                throw new VError(verr_bug,
                                 "BUG: codeIntExpr: invalid type code");
                break;
        }
        codeLitInt(value, scale);
    }
    else
    {
        codeExpr(ex);
        if (!vc.dsp->isType(ty_int))
        {
            size_t disp, size;
            switch (vc.dsp->type.code)
            {
                case ty_scalar: 
                    codeOpI(p_andi, 1);
                    break;

                case ty_vector:
                    if (vc.dsp->type.size > sizeof(size_t)*8)
                        goto tooLarge;
                    disp = vc.dsp->vDisp;
                    size = vc.dsp->type.size;
                    dropData();
                    codeLoadAdr(disp);                          // src vector
                    codeLitInt(size);                           // size
                    codeCallFn((Func*)convLVecToInt, 2);        // dest integer
                    break;

                default:
                cantConvert:
                    throw new VError(ex->srcLoc, verr_notYet,
                                    "can't convert to int yet");
                tooLarge:
                    throw new VError(ex->srcLoc, verr_notYet,
                        "can't convert >%d bits to int yet", sizeof(size_t));
                    break;
            }
            vc.dsp->setIntReg();
        }
    }
}

//-----------------------------------------------------------------------------
// Code an expression returning a scalar as stacked data.

void codeScalarExpr(Expr* ex)
{
    if (debugLevel(3))
        display("            #%2d scalarExpr(\n", vc.dataStkEnd-vc.dsp);
    if (ex->opcode == op_lit)
    {
        switch (ex->tyCode)
        {
            case ty_int:
                codeLitLevel(((ex->data.value / ex->data.scale) & 1)
                                ? LV_H : LV_L);
                break;
            case ty_scalar:
                codeLitLevel(ex->data.sValue);
                break;
            case ty_vector:
                throwExpected(ex->srcLoc, "scalar");

            default:
                throw new VError(verr_bug,
                                 "BUG: codeScalarExpr: invalid type code");
                break;
        }
    }
    else
    {
        codeExpr(ex);
        if (!vc.dsp->isType(ty_scalar))
        {
            Expr* arg;
            switch (vc.dsp->type.code)
            {
                case ty_int:        // turn a 1 into a 7 (LV_H)
                    codeOp(p_cvis);
                    break;
                case ty_vector:
                    arg = ex->arg[0];
                    if (ex->data.range.isScalar && arg->opcode == op_load)
                    {       // vector bit select becomes a scalar load
                        int disp = arg->data.var->disp;
                        disp += arg->data.range.left.bit;
                        codeLoadScalar(disp, arg->data.extScopeRef);
                    }
                    else
                        goto cantConvert;
                    break;
                default:
                cantConvert:
                    throw new VError(ex->srcLoc, verr_notYet,
                                    "can't convert to scalar");
                    break;
            }
            vc.dsp->setScalarReg();
        }
    }
}

//-----------------------------------------------------------------------------
// Code an expression returning a vector of given size as stacked data.
// A bitWidth of -1 means unknown width.

void codeVectorExpr(Expr* ex, int bitWidth)
{
    if (debugLevel(3))
        display("            #%2d vectorExpr(\n", vc.dataStkEnd-vc.dsp);
    if (ex->opcode == op_lit)
    {
        switch (ex->tyCode)
        {
            case ty_int:
            {
                if (bitWidth == -1)
                    bitWidth = 1;
                size_t value = ex->data.value / ex->data.scale;
                Level* levVec = new Level[bitWidth];
                convIntToLVec(value, levVec, bitWidth);
                size_t levVecDisp = Scope::local->newLocal(bitWidth,
                                                           sizeof(Level));
                new Variable("_", ty_vecConst, levVecDisp, (size_t)levVec,
                             bitWidth);
                            // and push pointer to it onto data stack
                pushEmpData();
                vc.dsp->setVector(levVec, bitWidth, levVecDisp);
                break;
            }
            case ty_scalar:
                throw new VError(ex->srcLoc, verr_illegal,
                                "no scalar-to-vector yet");
                break;
            case ty_vector:
                if (bitWidth != -1 && ex->nBits != bitWidth)
                    throw new VError(ex->srcLoc, verr_illegal,
                                     "%d-bit data size doesn't match %d-bit"
                                     " destination size", ex->nBits, bitWidth);
                codeLitLVec(ex->data.var, ex->data.extScopeRef);
                break;

            default:
                throw new VError(verr_bug,
                                 "BUG: codeVectorExpr: invalid type code");
                break;
        }
    }
    else
    {
        codeExpr(ex);
        if (vc.dsp->isType(ty_vector))
        {
            if (bitWidth != -1 && (int)vc.dsp->type.size != bitWidth)
                throw new VError(ex->srcLoc, verr_illegal,
                            "%d-bit data size doesn't match %d-bit"
                            " destination size", vc.dsp->type.size, bitWidth);
        }
        else
        {
            switch (vc.dsp->type.code)
            {
                case ty_int:
                {
                    if (bitWidth == -1)
                        bitWidth = vc.dsp->type.size;
                    size_t levVecDisp = Scope::local->newLocal(bitWidth,
                                                               sizeof(Level));
                                                    // src integer
                    codeLoadAdr(levVecDisp);        // dest vector
                    codeLitInt(bitWidth);           // size
                    codeCall((Subr*)convIntToLVec, 3, "convIntToLVec");
                    pushEmpData();
                    vc.dsp->setVector(0, bitWidth, levVecDisp);
                    break;
                }
                default:
                    throw new VError(ex->srcLoc, verr_illegal,
                                "can't convert to vector");
                    break;
            }
        }
    }
}

//-----------------------------------------------------------------------------
// Code an integer expression, returning a scalar as stacked data, where the
// scalar is zero if the integer is all zero.

void codeReducedIntExpr(Expr* ex)
{
    codeIntExpr(ex);
    codeOp(p_cvis);
    vc.dsp->setScalarReg();
}

//-----------------------------------------------------------------------------
// Code a vector expression, returning a scalar as stacked data, where the
// scalar is zero if the vector is all zero.

void codeReducedVectExpr(Expr* ex)
{
    codeVectorExpr(ex);
    Data dVec = *vc.dsp;
    dropData();

    // code a call to "Level u..LVec(Level* levVec, int nBits)"
    codeLoadAdr(dVec.vDisp);
    codeLitInt(dVec.type.size);
    codeCallFn((Func*)uorLVec, 2);
    vc.dsp->setScalarReg();
}

//-----------------------------------------------------------------------------
// Code an expression returning stacked data.

void codeExpr(Expr* ex)
{
    const Level* table;
    size_t (*unaryFn)(Level*, size_t);
    ExOpCode opcode = ex->opcode;
    if (debugLevel(3))
        display("            #%2d expr(op=%d\n",
                vc.dataStkEnd-vc.dsp, (int)opcode);
#if 0
    if (opcode < 0 || opcode >= num_ExOpCodes)
        throw new VError(ex->srcLoc, verr_illegal,
                    "codeExpr: invalid opcode %d", opcode);
#endif
    if (opcode < ops_dual)
    {       // Single opcodes:
        if (opcode == op_stacked)   // expression already stacked -- do nothing
            return;
        else if (opcode == op_lit)  // load a literal constant
        {
            switch (ex->tyCode)
            {
                case ty_int:
                    codeLitInt(ex->data.value, ex->data.scale);
                    break;
                case ty_scalar:
                    codeLitLevel(ex->data.sValue);
                    break;
                case ty_vector:
                    codeLitLVec(ex->data.var, ex->data.extScopeRef);
                    break;

                default:
                    throw new VError(verr_bug,
                                     "BUG: codeExpr lit: invalid type code");
                    break;
            }
        }
        else if (opcode == op_load) // load a variable
        {
            switch (ex->tyCode)
            {
                case ty_int:
                    codeLoadInt(ex->data.var->disp, ex->data.extScopeRef);
                    break;
                case ty_scalar:
                    codeLoadScalar((Scalar*)ex->data.var, ex->data.extScopeRef);
                    break;
                case ty_vector:
                    codeLoadVector((Vector*)ex->data.var, &ex->data.range,
                                    ex->data.extScopeRef);
                    break;
                case ty_memory:
                {
                    codeIntExpr(ex->data.index);
                    Memory* mem = (Memory*)ex->data.var;
                    codeLoadMem(mem, ex->data.extScopeRef);
                    break;
                }
                default:
                    throw new VError(verr_bug,
                                     "BUG: codeExpr load: invalid type code");
                    break;
            }
        }
        else if (opcode == op_conv) // type conversion
        {
            switch (ex->tyCode)
            {
                case ty_int:
                case ty_float:
                case ty_memory:
                    codeIntExpr(ex->arg[0]);
                    break;
                case ty_scalar:
                    codeScalarExpr(ex->arg[0]);
                    break;
                case ty_vector:
                    codeVectorExpr(ex->arg[0], ex->nBits);
                    break;

                default:
                    throw new VError(verr_bug,
                                     "BUG: codeExpr conv: invalid type code");
                    break;
                    }
        }
        else if (opcode == op_lea)  // load effective address (a net reference)
        {
            codeNetRef((Net*)ex->data.var, ex->data.extScopeRef);
        }
        else if (opcode == op_func) // function call
        {
            if (ex->func.code == sf_concat)
                codeConcat(ex);     // bit concatenation is special
            else
                codeFunction(ex);
        }
        else
        {
            Expr* arg0 = ex->arg[0];
            switch (opcode)
            {
                case op_not:
                {
                    // unary logical not -- examples:
                    // !0 -> 1, !1234 -> 0, !1'b1 -> 1'b0, !3'b101 -> 1'b0
                    switch (arg0->tyCode)
                    {
                        case ty_int:
                        {
                            codeExpr(arg0);
                            codeOp(p_not);
                            break;
                        }

                        case ty_scalar:
                            codeExpr(arg0);
                        invertScalar:
                            codeOp(p_nots);
                            break;

                        case ty_vector:
                            codeReducedVectExpr(arg0);
                            goto invertScalar;

                        default:
                            throw new VError(verr_bug,
                                "BUG: codeExpr unary not: invalid type code");
                            break;
                    }
                    break;
                }

                case op_com:
                {
                    // bitwise unary compliment -- examples:
                    // ~0 -> 1, ~1234 -> 813, ~16'h1234 -> 16'hEDCB
                    table = funcTable.INVERTtable;
                    codeExpr(arg0);
                    switch (arg0->tyCode)
                    {
                        case ty_int:
                            codeOp(p_com);
                            break;

                        case ty_scalar:
                            codeOp(p_nots);
                            break;

                        case ty_vector:
                            codeVectorUnaryOp(table);
                            break;

                        default:
                            throw new VError(verr_bug,
                                "BUG: codeExpr unary com: invalid type code");
                            break;
                    }
                    break;
                }

                case op_neg:
                    codeIntExpr(ex->arg[0]);
                    codeOp(p_neg);
                    break;

                case op_uand:
                    unaryFn = uandLVec;
                    goto unaryLogical;

                case op_unand:
                    unaryFn = unandLVec;
                    goto unaryLogical;

                case op_uor:
                    unaryFn = uorLVec;
                    goto unaryLogical;

                case op_unor:
                    unaryFn = unorLVec;
                    goto unaryLogical;

                case op_uxor:
                    unaryFn = uxorLVec;
                unaryLogical:
                {
                    codeVectorExpr(ex->arg[0]);
                    Data dVec = *vc.dsp;
                    dropData();
                    // code a call to "Level u..LVec(Level* levVec, int nBits)"
                    codeLoadAdr(dVec.vDisp);
                    codeLitInt(dVec.type.size);
                    codeCallFn((Func*)unaryFn, 2);
                    vc.dsp->setScalarReg();
                    break;
                }
                default:
                    throw new VError(ex->srcLoc, verr_illegal,
                                     "codeExpr: no '%s' op yet",
                                     Expr::Expr::kOpSym[opcode]);
            }
        }
    }
    else if (opcode < ops_triple)
    {   // Dual opcodes:
        Expr* arg0 = ex->arg[0];
        Expr* arg1 = ex->arg[1];
        PCodeOp op;
        switch (opcode)
        {
            case op_add:
                op = p_add;
                goto binaryInts;
            case op_sub:
                op = p_sub;
                goto binaryInts;
            case op_mul:
                op = p_mul;
                goto binaryInts;
            case op_div:
                op = p_div;
                goto binaryInts;
            case op_sla:
                op = p_sla;
                goto binaryInts;
            case op_sra:
                op = p_sra;
                goto binaryInts;
            case op_eq:
                op = p_eq;
                goto binaryInts;
            case op_ne:
                op = p_ne;
                goto binaryInts;
            case op_gt:
                op = p_gt;
                goto binaryInts;
            case op_le:
                op = p_le;
                goto binaryInts;
            case op_lt:
                op = p_lt;
                goto binaryInts;
            case op_ge:
                op = p_ge;
            binaryInts:
                codeIntExpr(arg0);
                codeIntExpr(arg1);
                codeOp2(op);
                break;
            
            case op_band:
                op = p_and;
                table = &funcTable.ANDtable[0][0];
                goto binary;
            case op_bor:
                op = p_or;
                table = &funcTable.ORtable[0][0];
                goto binary;
            case op_bxor:
                op = p_xor;
                table = &funcTable.XORtable[0][0];
                goto binary;

            binary:
                switch (arg0->tyCode)
                {
                    case ty_int:
                        switch (arg1->tyCode)
                        {
                            case ty_int:
                                codeExpr(arg0);
                                codeExpr(arg1);
                                codeOp2(op);                // i op i
                                break;

                            case ty_scalar:
                                codeScalarExpr(arg0);
                                codeExpr(arg1);
                                codeScalarBinaryOp(table);  // (i->s) op s
                                break;

                            case ty_vector:
                            case ty_memory:
                                codeVectorExpr(arg0, arg1->nBits);
                                codeVectorExpr(arg1, arg1->nBits);
                                codeVectorBinaryOp(table); // (i->v) op (v,m->v)
                                break;

                            default:
                                throw new VError(verr_bug,
                                    "BUG: codeExpr ints: invalid type code");
                                break;
                        }
                        break;

                    case ty_scalar:
                        switch (arg1->tyCode)
                        {
                            case ty_int:
                            case ty_scalar:
                                codeExpr(arg0);
                                codeScalarExpr(arg1);
                                codeScalarBinaryOp(table);  // s op (i->s)
                                break;

                            case ty_vector:
                            case ty_memory:
                                codeVectorExpr(arg0, arg1->nBits);
                                codeVectorExpr(arg1, arg1->nBits);
                                codeVectorBinaryOp(table); // (s->v) op (v,m->v)
                                break;

                            default:
                                throw new VError(verr_bug,
                                    "BUG: codeExpr scalars: invalid type code");
                                break;
                        }
                        break;

                    case ty_vector:
                    case ty_memory:
                            codeVectorExpr(arg0, arg1->nBits);
                            codeVectorExpr(arg1, arg1->nBits);
                            codeVectorBinaryOp(table); // (v,m->v) op (i,s,m->v)
                        break;

                    default:
                        throw new VError(verr_bug,
                                    "BUG: codeExpr binary: invalid type code");
                        break;
                }
                break;

            // logical and, or: reduce vector operands to scalars
            case op_and:
                table = &funcTable.ANDtable[0][0];
                goto binaryReduc;
            case op_or:
                table = &funcTable.ORtable[0][0];
                goto binaryReduc;

            binaryReduc:
                switch (arg0->tyCode)
                {
                    case ty_int:
                        codeReducedIntExpr(arg0);
                        break;
                    case ty_scalar:
                        codeScalarExpr(arg0);
                        break;
                    case ty_vector:
                        codeReducedVectExpr(arg0);
                        break;
                    default:
                        throw new VError(verr_bug, "BUG: codeExpr binaryReduc"
                                         " arg0: invalid type code");
                        break;
                }
                switch (arg1->tyCode)
                {
                    case ty_int:
                        codeReducedIntExpr(arg1);
                        break;
                    case ty_scalar:
                        codeScalarExpr(arg1);
                        break;
                    case ty_vector:
                        codeReducedVectExpr(arg1);
                        break;
                    default:
                        throw new VError(verr_bug, "BUG: codeExpr binaryReduc"
                                         " arg1: invalid type code");
                        break;
                }
                codeScalarBinaryOp(table);          // s op s
                break;

            default:
                throw new VError(ex->srcLoc, verr_illegal,
                            "codeExpr: no '%s' op yet", Expr::kOpSym[opcode]);
        }
    }
    else
    {   // Triple opcodes:
        Expr* arg0 = ex->arg[0];
        Expr* arg1 = ex->arg[1];
        Expr* arg2 = ex->arg[2];
        switch (opcode)
        {
            case op_sel:                // Select: (arg0 ? arg1 : arg2)
            {
                codeScalarExpr(arg0);
                VExTyCode trueType = arg1->tyCode;
                VExTyCode falseType = arg2->tyCode;
                if (trueType == ty_vector && falseType == ty_vector &&
                    (arg1->nBits != arg2->nBits))
                {
                    char msg[200];
                    snprintf(msg, sizeof(msg), "true and false vectors have different sizes:"
                            " %d vs %d (nonstandard)",
                        arg1->nBits, arg2->nBits);
                    throw new VError(verr_illegal, msg);
                }
                if (trueType == ty_vector || falseType == ty_vector)
                {
                    codeVectorExpr(arg1, arg1->nBits);
                    codeVectorExpr(arg2, arg1->nBits);
                    codeSelectLVec(arg1->nBits);
                }
                else if (trueType == ty_scalar || falseType == ty_scalar)
                {
                    codeScalarExpr(arg1);
                    codeScalarExpr(arg2);
                    codeSelect();
                }
                else
                {
                    codeIntExpr(arg1);
                    codeIntExpr(arg2);
                    codeSelect();
                }
                break;
            }
            default:
                throw new VError(ex->srcLoc, verr_illegal,
                            "codeExpr: no '%s' op yet", Expr::kOpSym[opcode]);
        }
    }
}

//-----------------------------------------------------------------------------
// Code a test of the given net against the current task's trigger signal.
// This is used in event expressions. taskTestFn is 'justRisen', etc.

void codeCallTaskTest(Net* net, ModelFuncPtr taskTestFn, Variable* extScopeRef)
{
    // bool Model::justRisen(Signal* signal)
    codeModelCallPrefix();                  // model address
    codeLoadInt(net->disp, extScopeRef);
    codeCallFn(((Func**)&taskTestFn)[0], 2);
    codeOp2(p_or);          // OR result into val on stack
}

//-----------------------------------------------------------------------------
// Code a test of the given net against the current task's trigger signal.
// This is used in event expressions. taskTestVecFn is 'isTriggerBus', etc.

void codeCallTaskTestVec(Vector* vec, ModelFuncPtr taskTestVecFn,
                        Variable* extScopeRef)
{
    // bool Model::isTriggerBus(SignalVec* bus, int n)
    codeModelCallPrefix();                  // model address
    codeLoadAdr(vec->disp, extScopeRef);
    codeLitInt(vec->exType.size);
    codeCallFn(((Func**)&taskTestVecFn)[0], 3);
    codeOp2(p_or);          // OR result into val on stack
}

//-----------------------------------------------------------------------------
// Start a compile of a branch to Model routine.

void codeModelCallPrefix()
{
    pushEmpData(1, "model");
    codeOp(p_leam);
}

//-----------------------------------------------------------------------------
// Compile a branch to Model routine whose UCP address is given.
// Takes nArgs arguments from register stack.
// Arguments are expected to be pushed on reg stack in calling order,
// but are passed to subroutine in C order (first arg in first reg).

void codeModelCall(ModelSubrPtr subr, int nArgs)
{
    // void ModelSysLib::readmemmif(char* mifName, Memory* mem)
    codeCallModel(subr, nArgs + 1);
}

//-----------------------------------------------------------------------------
// Compile a branch to a Verilog module task.

void codeCallVTask(NamedTask* task, Variable* extScopeRef)
{
    if (debugLevel(3))
        display("            #%2d callVTask %s (%p)\n",
            vc.dataStkEnd-vc.dsp, task->name, task->code);

    codeLoadScopeRef(extScopeRef);    // arg to call is module instance address
    codeOpI(p_btsk, (size_t)task->code);
    dropData();
}
