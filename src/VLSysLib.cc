// ****************************************************************************
//
//          PVSim Verilog Simulator Verilog System Call Library
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

#ifdef EXTENSION
#include <Python.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

#include "Utils.h"
#include "PSignal.h"
#include "Model.h"

#include "Src.h"
#include "VLCoder.h"
#include "VLSysLib.h"

class ModelSysLib: public Model
{
                    ModelSysLib(char* name, EvHandCodePtr evHandCode,
                                char* instModule):
                        Model(name, evHandCode, instModule) {}
    size_t          scanNumber(size_t base);
public:
    void            readmemmif(char* mifName, Memory* mem);
};


//-----------------------------------------------------------------------------
//                          Verilog Library Routines
//-----------------------------------------------------------------------------

// Load bit i from Vector signal sigVec after checking in range [0,iMax].

size_t loadIndScalar(SignalVec* sigVec, size_t i, size_t iMax)
{
    if (i > iMax)
        return LV_X;
    return sigVec[i]->level;
}

// Load bits starting at i from Vector signal sigVec into a temp LVector,
// width n, after checking bits are in range [0,iMax].

void loadIndVector(SignalVec* sigVec, size_t i, size_t iMax,
                    Level* levVec, size_t nBits)
{
    if (i > iMax-nBits+1)
    {
        for ( ; nBits > 0; nBits--)
            *levVec++ = LV_X;
        return;
    }

    sigVec += i;
    for ( ; nBits > 0; nBits--)
    {
        *levVec++ = (*sigVec)->level;
        sigVec++;
    }
}

// Sample a Vector signal event (Falling or Rising becomes Changing, etc.).
// Cleans signals that change on only certain events, such as a clock edge.

void sampleVector(Level* levVec, size_t nBits)
{
    for ( ; nBits > 0; nBits--)
    {
        *levVec = funcTable.SAMPLEtable[*levVec];
        levVec++;
    }
}

// convert an integer to an LVector (assumes LVector space allocated)

void convIntToLVec(size_t value, Level* levVec, size_t nBits)
{
    levVec += nBits;
    for ( ; nBits > 0; nBits--)
    {
        levVec--;
        *levVec = ((value & 1) ? LV_H : LV_L);
        value >>= 1;
    }
}

// convert an LVector to an integer

size_t convLVecToInt(Level* levVec, size_t nBits)
{
    size_t value = 0;
    for ( ; nBits > 0; nBits--)
        value = (value << 1) + (*levVec++ == LV_H);
    return value;
}

// Do a unary AND on an LVector, returning a single Level.

size_t uandLVec(Level* levVec, size_t nBits)
{
    Level product = LV_H;
    for ( ; nBits > 0; nBits--)
        product = funcTable.ANDtable[*levVec++][product];
    return product;
}

// Do a unary NAND on an LVector, returning a single Level.

size_t unandLVec(Level* levVec, size_t nBits)
{
    Level product = LV_H;
    for ( ; nBits > 0; nBits--)
        product = funcTable.ANDtable[*levVec++][product];
    return funcTable.INVERTtable[product];
}

// Do a unary OR on an LVector, returning a single Level.

size_t uorLVec(Level* levVec, size_t nBits)
{
    Level sum = LV_L;
    for ( ; nBits > 0; nBits--)
        sum = funcTable.ORtable[*levVec++][sum];
    return sum;
}

// Do a unary NOR on an LVector, returning a single Level.

size_t unorLVec(Level* levVec, size_t nBits)
{
    Level sum = LV_L;
    for ( ; nBits > 0; nBits--)
        sum = funcTable.ORtable[*levVec++][sum];
    return funcTable.INVERTtable[sum];
}

// Do a unary XOR on an LVector to get the resulting even parity as a Level.

size_t uxorLVec(Level* levVec, size_t nBits)
{
    Level parity = LV_L;
    for ( ; nBits > 0; nBits--)
        parity = funcTable.XORtable[*levVec++][parity];
    return parity;
}

// Do a unary operation on an LVector into a 2nd LVector. Op uses table.

void unaryOpLVec(Level* src, Level* dest, size_t nBits, Level table[12])
{
    for ( ; nBits > 0; nBits--)
        *dest++ = table[*src++];
}

// Do a binary operation on 2 LVectors into a 3rd LVector. Op uses table.

void binaryOpLVec(Level* src0, Level* src1, Level* dest, size_t nBits,
                  Level table[12][16])
{
    for ( ; nBits > 0; nBits--)
        *dest++ = table[*src0++][*src1++];
}

//-----------------------------------------------------------------------------
//                          System Library Routines
//-----------------------------------------------------------------------------

// Set the signal upon whose rising edges blue bars will be drawn.

void verBarClock(Signal* barSignal)
{
    gBarSignal = barSignal;
}

//-----------------------------------------------------------------------------
// Return the current simulation time.

size_t verTime()
{
    return gTick;
}

//-----------------------------------------------------------------------------
// Specify the time display format.

void verTimeFormat(size_t scaleExp, size_t precision, char* suffixStr,
                   size_t minFieldWid)
{
    gTimeScaleExp = (int)scaleExp;
    gTimeScale = gTicksNS * pow(10, gTimeScaleExp + 9);
    gTimeDispPrec = (int)precision;
    gTimeSuffixStr = suffixStr;
    gTimeMinFieldWid = (int)minFieldWid;
}

//-----------------------------------------------------------------------------
// Format a time value, returning a string.

size_t verFormatTime(size_t time, size_t haveWidPrec, size_t width,
                     size_t precision)
{
    size_t suffixWid = strlen(gTimeSuffixStr);
    if (debugLevel(2))
        display(
            "verFormatTime: time=%d hvWidPrec=%d width=%d prec=%d scale=%f\n",
            time, haveWidPrec, width, precision, gTimeScale);
    return (size_t)newString(TmpName("%*.*f%s",
        ((haveWidPrec & 2) ? width : gTimeMinFieldWid - suffixWid),
        ((haveWidPrec & 1) ? precision : gTimeDispPrec),
        time / gTimeScale, gTimeSuffixStr));
}

//-----------------------------------------------------------------------------
// Return a random integer between start, end. A nonzero seed is passed
// to srand().

size_t verDistUniform(size_t seed, size_t start, size_t end)
{
    if (seed)
        resetRandSeed((int)seed);
    return randomRng((int)(end - start)) + start;
}

//-----------------------------------------------------------------------------
// Stop the simulation and print the current time and given number.

void verStop(size_t n)
{
    if (gTick/gTicksNS > 1000000) // int run times are in milliseconds
        display("\nSTOP #%d at %7.6f ms\n", n,
                ((float)gTick/gTicksNS)/1000000.);
    else
        display("\nSTOP #%d at %2.1f ns\n", n, (float)gTick/gTicksNS);
    gTEnd = gTick;  // force end of simulation loop
}

//-----------------------------------------------------------------------------
// Drop into the debugger.

int gDebugErrCode;

void verDebug(size_t n)
{
    gDebugErrCode = (int)n;     // set CodeWarrior breakpoint here
#if 0
    Debugger();
#endif

#if 0 // POWERPC ONLY
    // cause a system trap: use this tag to start amber's trace using:
    if (gTagDebug)
        gDebugErrCode = __mfspr(1023);
#endif
}

//-----------------------------------------------------------------------------
// Verilog breakpoint: show location in Alpha source window.

void verBreakpoint(size_t iSrcLoc)
{
    Token* srcLoc = (Token*)iSrcLoc;
    if (srcLoc)
        breakInEditor(srcLoc);
    else
        Debugger();
}

//-----------------------------------------------------------------------------
// At a code line boundary: Check for a breakpoint or single-step.

void verLine(size_t iSrcLoc)
{
    vc.srcLoc = (Token*)iSrcLoc;
//  if (a breakpoint)
//      verBreakpoint(defFile);

//  throw new VError(verr_break, "testing throw from verLine %d", vc.curLine);
}

//-----------------------------------------------------------------------------
// fopen(fName): Open a file for writing to by fdisplay(), returning the
// file pointer.

size_t verFOpen(char* fName)
{
    FILE* file = fopen(fName, "w");
    //gOpenFiles = new OpenFile(file, gOpenFiles);  // not unless we remove
                                                    // it upon $fclose()
    return (size_t)file;
}

//-----------------------------------------------------------------------------
// Read an Altera .mif memory initialization file into a memory element.

size_t ModelSysLib::scanNumber(size_t base)
{
    const char* name = gScToken->name;
    char anum[20];
    if (isToken(NUMBER_TOKEN))
    {
        snprintf(anum, 19, "%d", gScToken->number);
        name = anum;
    }
    else
        expect(NAME_TOKEN);

    char* nameEnd;
    errno = 0;
    size_t value = (size_t)strtoll(name, &nameEnd, (int)base);
    if (errno || *nameEnd)
        throw new VError(verr_illegal, "bad base-%d value: %s",
                        base, gScToken->name);
    scan();
    return value;
}

//-----------------------------------------------------------------------------
// Read an Altera .mif memory initialization file into a memory element.

void ModelSysLib::readmemmif(char* mifName, Memory* mem)
{
    int adrBase = 10;
    int dataBase = 10;

    Src* mif = new Src(mifName, sm_stripComments + sm_vhdl, 0);
    mif->tokenize();
    try
    {
        while (gScToken)
        {
            if (isName("WIDTH"))
            {
                scan();
                expectSkip('=');
                if (gScToken->number != (int)mem->elemRange->size)
                    throw new VError(verr_illegal, "wrong width");
                scan();
                expectSkip(';');
            }
            else if (isName("DEPTH"))
            {
                scan();
                expectSkip('=');
                if (gScToken->number != (int)mem->memRange->size)
                    throw new VError(verr_illegal, "wrong depth");
                scan();
                expectSkip(';');
            }
            else if (isName("ADDRESS_RADIX"))
            {
                scan();
                expectSkip('=');
                if (isName("HEX"))
                    adrBase = 16;
                else if (!isName("UNS"))
                    throw new VError(verr_illegal,
                            "only UNS and HEX address radices supported");
                scan();
                expectSkip(';');
            }
            else if (isName("DATA_RADIX"))
            {
                scan();
                expectSkip('=');
                if (isName("HEX"))
                    dataBase = 16;
                else if (!isName("UNS"))
                    throw new VError(verr_illegal,
                            "only UNS and HEX data radices supported");
                scan();
                expectSkip(';');
            }
            else if (isName("CONTENT"))
            {
                // skips over trigger signal pointer
                size_t* memArray = (size_t*)(this->instModule + mem->disp +
                                             sizeof(Signal*));
                scan();
                expectName("BEGIN");
                scan();
                while (gScToken && !isName("END"))
                {
                    if (isToken('['))
                    {
                        scan();
                        long adr, endAdr;
                        if (isToken(NUMBER_TOKEN))
                        {
                            adr = gScToken->number;
                            scan();
                            expectSkip('.');
                            expectSkip('.');
                            expect(NUMBER_TOKEN);
                            endAdr = gScToken->number;
                        }
                        else
                        {
                            expect(NAME_TOKEN);
                            char* end;
                            errno = 0;
                            adr = strtol(gScToken->name, &end, adrBase);
                            if (errno || *end != '.' || end[1] != '.')
                                throw new VError(verr_illegal,
                                                 "[N..N] form expected");
                            endAdr = strtol(end + 2, &end, adrBase);
                        }
                        scan();
                        expectSkip(']');
                        expectSkip(':');
                        size_t value = scanNumber(dataBase);
                        expectSkip(';');
                        for ( ; adr <= endAdr; adr++)
                            memArray[adr] = value;
                    }
                    else
                    {
                        long adr = scanNumber(adrBase);
                        expectSkip(':');
                        size_t value = scanNumber(dataBase);
                        expectSkip(';');
                        memArray[adr] = value;
                    }
                }
                expectName("END");
                scan();
                expectSkip(';');
            }
            else
                throw new VError(verr_illegal, "syntax error");
        }
    } catch(VError* err)
    {
        reportRunErr(new VError(err->code, "%s\nin readmemmif(%s, %s) line %d",
            err->message, mifName, mem->name, gScToken->line));
    }
}

//-----------------------------------------------------------------------------
// Code a load of a Scalar or Vector-bit signal address.

void codeNetRef(Net* net, Variable* extScopeRef)
{
    size_t disp;
    switch (net->exType.code)
    {
        case ty_scalar:     // code a signal fetch
            disp = ((Scalar*)net)->disp;
            break;

        case ty_vector:     // code a vector signal fetch
        {
            Vector* vec = (Vector*)net;
            Range range = Range();
            range.compile();
            if (!(range.isFull || (range.isScalar && range.left.isConst)))
                throwExpected("constant vector bit selector");
            disp = vec->disp + ((range.left.bit - vec->range->left.bit) *
                                        vec->range->incr) * sizeof(Signal*);
            break;
        }
        case ty_memory:     // code a memory variable reference
            codeLitInt((size_t)net);
            return;

        default:
            throw new VError(verr_illegal, "variable ref type: not yet");
    }
    codeLoadInt(disp, extScopeRef);
    if (Expr::gatherTriggers)
        addTrigger(net);
}

//-----------------------------------------------------------------------------
// Scan a name and code a load of a Scalar or Vector-bit signal address.

void scanCodeNetRef()
{
    Variable* extScopeRef;
    NamedObj* sym = findFullName(&extScopeRef);
    if (!sym->isType(ty_var))
        throwExpected("variable name");
    scan();
    codeNetRef((Net*)sym, extScopeRef);
}

//-----------------------------------------------------------------------------
// Code a list of integer arguments.

void codeIntArgs(int nArgs)
{
    expectSkip('(');
    codeIntExpr(compileExpr());
    for (nArgs-- ; nArgs > 0; nArgs--)
    {
        expectSkip(',');
        codeIntExpr(compileExpr());
    }
    expectSkip(')');
}

//-----------------------------------------------------------------------------
// Compile a linked-list of argument expressions. Uses op_func expression
// nodes for the list.

void compileIntArgs(int nArgs, Expr* mainExprNode)
{
    Expr* ex = mainExprNode;            // first arg goes in main node
    ex->func.arg = compileExpr();
    for (nArgs--; nArgs > 0; nArgs--)
    {
        expectSkip(',');
        Expr* nextEx = newExprNode(op_func);    // add nodes for other args
        ex->func.nextArgNode = nextEx;          // and link them in a list
        ex = nextEx;
        ex->func.arg = compileExpr();
    }
    ex->func.nextArgNode = 0;
}

//-----------------------------------------------------------------------------
// Compile and code a $display() or $fdisplay() call, starting at format
// argument. argEx is the last argument expression.
// If format string is preceeded by a net argument, return that in an "op_load"
// expression as the net to be annotated.

Expr* compileDisplay(Expr* argEx, bool addNL)
{
    Expr* annotated = 0;
    Expr* fmtExpr = compileExpr();

    // format string may be preceeded by an annotated-net argument
    if (fmtExpr->opcode == op_load ||
        (fmtExpr->opcode == op_conv && fmtExpr->arg[0]->opcode == op_load))
    {
        annotated = fmtExpr;
        expectSkip(',');
        fmtExpr = compileExpr();
    }

    argEx->func.arg = fmtExpr;
    char* fmt = (char*)fmtExpr->data.value;
    if (fmtExpr->tyCode != ty_int || fmt < gStrings || fmt > gNextString)
        throwExpected("format string");
    TmpName sfmt = TmpName("%s", fmt);
    if (addNL)
        sfmt += "\n";
    char* p;
    for (p = (char*)sfmt; *p && isToken(','); p++)
        if (*p == '%')
        {
            // compile corresponding argument
            scan();
            Expr* nextEx = newExprNode(op_func);
            Expr* convEx = newExprNode(op_conv);
            nextEx->func.arg = convEx;
            convEx->tyCode = ty_int;
            convEx->arg[0] = compileExpr();

            // convert Verilog format type to C type, if needed
            p++;
            char* widthStr = p;
            char* precStr = (char *)"";
            while (*p && isdigit(*p))
                p++;
            if (*p == '.')
            {
                p++;
                precStr = p;
                while (*p && isdigit(*p))
                    p++;
            }
            if (*p == 't')
            {
                // a time argument:
                int haveWidPrec = ((isdigit(widthStr[0]) != 0) << 1) |
                                   (precStr[0] != 0);
                *p = 's';
                // remove any precision arg to %s, leaving only width, if any
                int prec = atoi(precStr);
                if (precStr[0])
                {
                    char* pp = precStr;
                    while (*pp && isdigit(*pp))
                        pp++;
                    strcpy(precStr-1, pp);
                }
                
                // code call to verFormatTime(int time, int haveWidPrec,
                //   int width, int precision)
                Expr* timeEx = newExprNode(op_func);
                nextEx->func.arg = timeEx;
                timeEx->tyCode = ty_int;
                timeEx->func.code = sf_formatTime;
                timeEx->func.arg = convEx;
                Expr* timeHaveEx = newExprNode(op_func);
                timeEx->func.nextArgNode = timeHaveEx;
                timeHaveEx->func.arg = newConstInt(haveWidPrec);
                Expr* timeWidEx = newExprNode(op_func);
                timeHaveEx->func.nextArgNode = timeWidEx;
                timeWidEx->func.arg = newConstInt(atoi(widthStr));
                Expr* timePrecEx = newExprNode(op_func);
                timeWidEx->func.nextArgNode = timePrecEx;
                timePrecEx->func.arg = newConstInt(prec);
                timePrecEx->func.nextArgNode = 0;
            }
            else if (*p == 'h')
                *p = 'x';

            argEx->func.nextArgNode = nextEx;
            argEx = nextEx;
        }
    if (*p && !isToken(')'))
        throw new VError(verr_illegal, "too few arguments for format string");
    if (isToken(','))
        throw new VError(verr_illegal, "too many arguments for format string");
    fmtExpr->data.value = (size_t)newString(sfmt);
    argEx->func.nextArgNode = 0;

    scan();
    return annotated;
}

//-----------------------------------------------------------------------------
// Compile a net reference, given as an argument to a system function.

Expr* compileNetRef()
{
    Expr* ex = newExprNode(op_lea);
    Variable* var = (Variable*)findFullName(&ex->data.extScopeRef);
    if (!var->isType(ty_var) || !(var->isExType(ty_scalar) ||
        var->isExType(ty_vector)))
        throwExpected("scalar or vector name");
    ex->data.var = var;
    scan();
    return ex;
}

//-----------------------------------------------------------------------------
// Compile (but don't code) a system function call. On entry, '$' has been
// scanned. Returns pointer to function expression node with optional
// linked-list of argument expressions.

Expr* compileSystemFn()
{
    scan();
    expectNameOf("system function");
    TmpName sysFnCallName = TmpName(gScToken->name);
    scan();
    Expr* ex = newExprNode(op_func);
    ex->func.arg = 0;
    ex->func.nextArgNode = 0;
    ex->tyCode = ty_int;
    // $time(): return current simulation time
    if (strcmp(sysFnCallName, "time") == 0)
        ex->func.code = sf_time;
    // $random(limit): return a random integer between 0 and limit
    else if (strcmp(sysFnCallName, "random") == 0)
    {
        ex->func.code = sf_random;
        expectSkip('(');
        compileIntArgs(1, ex);
        expectSkip(')');
    }
    // $dist_uniform(seed, start, end): random integer between start, end
    else if (strcmp(sysFnCallName, "dist_uniform") == 0)
    {
        ex->func.code = sf_dist_uniform;
        expectSkip('(');
        compileIntArgs(3, ex);
        expectSkip(')');
    }
    // $fopen(filename, mode)
    else if (strcmp(sysFnCallName, "fopen") == 0)
    {
        ex->func.code = sf_fopen;
        expectSkip('(');
        compileDisplay(ex, FALSE);
    }
    else
        throwExpected("system function name");
    return ex;
}

//-----------------------------------------------------------------------------
// Code a system (or user) function call.

void codeFunction(Expr* ex)
{
    Expr* ap = ex;
    int nArgs = 0;
    while (ap && ap->func.arg)
    {
        codeExpr(ap->func.arg);
        nArgs++;
        ap = ap->func.nextArgNode;
    }
    switch (ex->func.code)
    {
        case sf_time:
            codeCallFn((Func*)verTime, 0);
            break;
        case sf_formatTime:
            codeCallFn((Func*)verFormatTime, 4);
            break;
        case sf_random:
            codeCallFn((Func*)randomRng, 1);
            break;
        case sf_dist_uniform:
            codeCallFn((Func*)verDistUniform, 3);
            break;
        case sf_fopen:
            codeCallFn((Func*)verFOpen, 1);
            break;
        case sf_flagError:
            codeCall((Subr*)flagError, nArgs);
            break;
        case sf_display:
            codeCall((Subr*)display, nArgs);
            break;
        case sf_fdisplay:
            codeCall((Subr*)fprintf, nArgs);
            break;
        case sf_annotate:
            codeCall((Subr*)drawf, nArgs);
            break;
        case sf_readmemmif:
            codeModelCall((ModelSubrPtr)&ModelSysLib::readmemmif, 2);
            break;
        default:
            throw new VError(verr_bug, "BUG: can't code function yet");
    }
}

//-----------------------------------------------------------------------------
// Code a system function call. On entry, function name has been scanned.

void codeSystemCall()
{
    scan();
    TmpName sysCallName = TmpName(gScToken->name);
    scan();
    Expr::parmOnly = FALSE;
    bool displayF = (strcmp(sysCallName, "display") == 0);
    bool writeF = (strcmp(sysCallName, "write") == 0);
    bool fdisplayF = (strcmp(sysCallName, "fdisplay") == 0);
    bool fwriteF = (strcmp(sysCallName, "fwrite") == 0);

    // $barClock(signal)
    if (strcmp(sysCallName, "barClock") == 0)
    {
        expectSkip('(');
        scanCodeNetRef();
        expectSkip(')');
        codeCall((Subr*)verBarClock, 1);
    }

    // $debug(code)
    else if (strcmp(sysCallName, "debug") == 0)
    {
        codeIntArgs(1);
        codeCall((Subr*)verDebug, 1);
    }

    // $bp()
    else if (strcmp(sysCallName, "bp") == 0)
    {
        codeLitInt((size_t)gScToken);
        codeCall((Subr*)verBreakpoint, 1);
    }

    // $flagError(net, errName, miss, fmt, ...)
    else if (strcmp(sysCallName, "flagError") == 0)
    {
        expectSkip('(');

        Expr* ex = newExprNode(op_func);    // 'flagError' function
        ex->tyCode = ty_int;
        ex->func.code = sf_flagError;

        Expr* argEx = compileNetRef();      // arg 1: 'signal' net reference
        Expr* nextEx = newExprNode(op_func);
        ex->func.arg = argEx;
        ex->func.nextArgNode = nextEx;
        expectSkip(',');

        compileDisplay(nextEx, 0);
        codeExpr(ex);
    }

    // $display(fmt, ...) or $display(net, fmt, ...)
    else if (displayF || writeF)
    {
        expectSkip('(');

        Expr* ex = newExprNode(op_func);    // 'display' function
        ex->tyCode = ty_int;
        ex->func.code = sf_display;
        Expr* annotated = compileDisplay(ex, displayF);
        if (annotated)
        {
            // $display(net, fmt, ...) translates into either:
            //      $annotate(net, fmt, ...)
            // or if fmt starts with "*** ":
            //      $flagError(net, netName, 0, fmt, ...)

            // arg 1: 'signal' net ref: insert in front of func argument list
            Variable* var = annotated->data.var;
            if (annotated->opcode == op_conv)
            {
                var = annotated->arg[0]->data.var;
                annotated->data.var = var;
            }
            if (!var->isType(ty_var) || !(var->isExType(ty_scalar) ||
                var->isExType(ty_vector)))
                throwExpected("scalar or vector annotated-net name for"
                              " $display(net, fmt...)");
            annotated->opcode = op_lea;
            Expr* fmtExpr = ex->func.arg;
            Expr* afterFmtFEx = ex->func.nextArgNode;
            Expr* fmtFEx = newExprNode(op_func);
            fmtFEx->func.arg = fmtExpr;
            fmtFEx->func.nextArgNode = afterFmtFEx;
            ex->func.arg = annotated;
            ex->func.nextArgNode = fmtFEx;
            char* fmt = (char*)fmtExpr->data.value;
            if (strncmp(fmt, "*** ", 4) == 0)
            {
                fmtExpr->data.value = (size_t)(fmt + 4);
                ex->func.code = sf_flagError;
                Expr* netNameExpr = newExprNode(op_lit);
                netNameExpr->tyCode = ty_int;
                netNameExpr->data.value = (size_t)annotated->data.var->name;
                Expr* netNameFEx = newExprNode(op_func);
                netNameFEx->func.arg = netNameExpr;
                ex->func.nextArgNode = netNameFEx;

                Expr* missExpr = newExprNode(op_lit);
                missExpr->tyCode = ty_int;
                missExpr->data.value = 0;
                Expr* missFEx = newExprNode(op_func);
                missFEx->func.arg = missExpr;
                netNameFEx->func.nextArgNode = missFEx;
                missFEx->func.nextArgNode = fmtFEx;
            }
            else
                ex->func.code = sf_annotate;
        }
        codeExpr(ex);
    }

    // $fdisplay(file, fmt, ...)
    else if (fdisplayF || fwriteF)
    {
        expectSkip('(');

        Expr* ex = newExprNode(op_func);    // 'fdisplay' function
        ex->tyCode = ty_int;
        ex->func.code = sf_fdisplay;
        Expr* argEx = compileExpr();
        Expr* nextEx = newExprNode(op_func);
        ex->func.arg = argEx;
        ex->func.nextArgNode = nextEx;
        expectSkip(',');
        compileDisplay(nextEx, fdisplayF);
        codeExpr(ex);
    }

    // $fclose(file)
    else if (strcmp(sysCallName, "fclose") == 0)
    {
        codeIntArgs(1);
        codeCall((Subr*)fclose, 1);
    }

    // annotate(signal, fmt, ...)
    else if (strcmp(sysCallName, "annotate") == 0)
    {
        expectSkip('(');

        Expr* ex = newExprNode(op_func);    // 'annotate' function
        ex->tyCode = ty_int;
        ex->func.code = sf_annotate;

        ex->func.arg = compileNetRef();     // arg 1: 'signal' net reference
        Expr* nextEx = newExprNode(op_func);
        ex->func.nextArgNode = nextEx;
        Expr* argEx = nextEx;
        expectSkip(',');

        Expr* fmtExpr = compileExpr();      // arg 2: 'fmt' string
        char* fmt = (char*)fmtExpr->data.value;
        if (fmtExpr->tyCode != ty_int || fmt < gStrings || fmt > gNextString)
            throwExpected("format string");
        nextEx->tyCode = ty_int;
        nextEx->func.arg = fmtExpr;
        argEx->func.nextArgNode = nextEx;
        argEx = nextEx;

        while (isToken(','))            // optional args 3..n:
        {
            scan();
            Expr* nextEx = newExprNode(op_func);
            nextEx->tyCode = ty_int;
            nextEx->func.arg = compileExpr();
            argEx->func.nextArgNode = nextEx;
            argEx = nextEx;
        }
        argEx->func.nextArgNode = 0;

        expectSkip(')');
        codeExpr(ex);
    }

    // $readmemmif("filename", memoryname)
    else if (strcmp(sysCallName, "readmemmif") == 0)
    {
        expectSkip('(');
        codeModelCallPrefix();

        // 'readmemmif' function
        Expr* arg1Ex = newExprNode(op_func);
        arg1Ex->tyCode = ty_int;
        arg1Ex->func.code = sf_readmemmif;

        // arg 1: filename string
        arg1Ex->func.arg = compileExpr();
        Expr* arg2Ex = newExprNode(op_func);
        arg1Ex->func.nextArgNode = arg2Ex;
        expectSkip(',');

        // arg 2: memory variable reference
        Expr* ex = newExprNode(op_lea);
        Variable* var = (Variable*)findFullName(&ex->data.extScopeRef);
        if (!var->isType(ty_var) || !(var->isExType(ty_memory)))
            throwExpected("memory variable name");
        ex->data.var = var;
        scan();
        arg2Ex->func.arg = ex;
        arg2Ex->func.nextArgNode = 0;
        expectSkip(')');

        codeExpr(arg1Ex);
    }

    // stop(code)
    else if (strcmp(sysCallName, "stop") == 0)
    {
        codeIntArgs(1);
        codeCall((Subr*)verStop, 1);
    }

    // $timeformat(scaleExp, roundExp, suffixStr, minFieldWid)
    else if (strcmp(sysCallName, "timeformat") == 0)
    {
        codeIntArgs(4);
        codeCall((Subr*)verTimeFormat, 4);
    }

    else
        throwExpected("system routine name");
}

