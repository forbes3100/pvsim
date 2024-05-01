// ****************************************************************************
//
//          PVSim Verilog Simulator Verilog Module Compiler
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
#include <math.h>

#include "Src.h"
#include "VL.h"
#include "VLSysLib.h"
#include "VLCoder.h"

// Need to build a list of all Nets assigned to in each EvHand, and then
// during linkVariables() see that each Net was assigned somewhere.
// Could have it mark each Net, though Vectors need to have arrays of marks so
// bit ranges can be marked off.

// ------------ Global Global Variables -------------

// time zero reset handler: forces all others to init
EvHand* EvHand::gAssignsReset;

// Verilog reserved keyword strings.
// Must be kept in sync with enum VKeyword in VL.h.
const char* kVKeyStr[128] =
{
    " ", "always", "and", "assign", "begin", "buf",
    "bufif0", "bufif1", "case", "casex", "casez",
    "cmos", "deassign", "default", "defparam", "disable",
    "edge", "else", "end", "endcase", "endfunction", "endgenerate",
    "endmodule", "endprimitive", "endspecify", "endtable", "endtask",
    "event", "for", "force", "forever", "fork", "function",
    "generate", "genvar", "highz0", "highz1", "if", "initial",
    "inout", "input", "integer", "join", "large",
    "macromodule", "medium", "module", "nand", "negedge",
    "nmos", "nor", "not", "notif0", "notif1",
    "or", "output", "parameter", "pmos", "posedge",
    "primitive", "pull", "pull1", "pulldown", "pullup",
    "rcmos", "reg", "release", "repeat", "rnmos",
    "rpmos", "rtran", "rtranif0", "rtranif1", "scalared",
    "small", "specify", "specparam", "strong0", "strong1",
    "supply0", "supply1", "table", "task", "time",
    "tran", "tranif0", "tranif1", "tri", "tri0",
    "tri1", "triand", "trior", "vectored", "wait",
    "wand", "weak0", "weak1", "while", "wire",
    "wor", "xnor", "xor",
    "}",                        // pad out to next power of 2: 128
    "}", "}", "}", "}", "}",
    "}", "}", "}", "}", "}",
    "}", "}", "}", "}", "}",
    "}", "}", "}", "}", "}"
};

// ------------ Local Function Prototypes ------------

void        codeStmtOrNull();
void        codeStmtList();

//-----------------------------------------------------------------------------
// A list of forward-jump instructions to a single location.

void JmpList::add(size_t* jmpAddr)
{
    if (this->next >= this->jmpAddr + max_jmps)
        throw new VError(verr_notYet, "too many cases");
    *this->next++ = jmpAddr;
}

void JmpList::setJmpsToHere()
{
    for (size_t** p = this->jmpAddr; p < this->next; p++)
        setJmpToHere(*p);
}

//-----------------------------------------------------------------------------
// Scan in a declaration's vector range, if any. Returns TRUE if a vector.

bool compileVecRange(Range** rangePtr)
{
    bool hasRange = isToken('[');
    if (hasRange)
    {
        Range* range = new Range();
        range->compile();
        if (range->isScalar)
            throwExpected("vector range, in the form [msb:lsb]");
        if (!(range->left.isConst && range->right.isConst))
            throwExpected("constant vector size, for now");
        range->isFull = TRUE;
        *rangePtr = range;
    }
    return hasRange;
}

//-----------------------------------------------------------------------------
// Scan in a declaration's memory address range, if any.
// Returns TRUE if present.

bool compileMemRange(Range** rangePtr)
{
    bool hasRange = isToken('[');
    if (hasRange)
    {
        Range* range = new Range();
        range->compile();
        if (range->isScalar)
            throwExpected("memory address range, in the form [a:b]");
        if (!(range->left.isConst && range->right.isConst))
            throwExpected("constant memory size, for now");
        range->isFull = TRUE;
        *rangePtr = range;
        if (range->size > 1024)
            throwExpected("memory size <= 1024, for now");
    }
    return hasRange;
}

//-----------------------------------------------------------------------------
// Compile a time value expression, which may be a floating-point constant.
// Resuls is an expression in integer ticks.

Expr* compileTime()
{
    Expr::parmOnly = TRUE;
    Expr* ex = compileExpr();
    if (ex->opcode == op_lit && ex->tyCode == ty_float)
        ex = newConstInt((int)round((ex->data.fValue * gTimeScale)));
    else
    {
        // rescale an integer expression as ticks, if not already
        if (ex->tyCode == ty_int && ex->data.scale != gTimeScale)
            ex = newOp2(op_mul, ex,
                        newConstInt((int)round(gTimeScale/ex->data.scale)));
    }
    return ex;
}

//-----------------------------------------------------------------------------
// Compile an optional delay or parameter specification, returning the number
// of parm expressions stored in the vc.parms array.

int compileParms()
{
    Expr** parm = vc.parms;
    if (isToken('#'))
    {
        NetList* triggersSave = Expr::curTriggers;
        bool gatherSave = Expr::gatherTriggers;
        Expr::gatherTriggers = FALSE;
        Expr::parmOnly = TRUE;
        scan();
        if (isToken('('))
        {
            scan();
            for (;;)
            {
                if (parm >= vc.parms + max_parms)
                    throw new VError(verr_illegal, "too many parameters");
                *parm++ = compileTime();
                if (isToken(')'))
                    break;
                expectSkip(',');
            }
            scan();
        }
        else
            *parm++ = compileTime();

        Expr::curTriggers = triggersSave;
        Expr::gatherTriggers = gatherSave;
        if (debugLevel(4) && parm - vc.parms)
            display("# ");
    }
    return (int)(parm - vc.parms);
}

//-----------------------------------------------------------------------------
// Code loads of all compiled parm expressions.

void codeLoadParms(int nParms)
{
    for (Expr** parm = vc.parms; parm < vc.parms + nParms; parm++)
        codeIntExpr(*parm);
}

//-----------------------------------------------------------------------------
// Compile a task call's argument expression list and 'bind' (really, assign)
// the values to task's argument variables.

void codeBindTaskArgs(NamedTask* task, Variable* taskScopeRef)
{
    Port* port = task->ports;
    if (isToken('('))
    {
        scan();
        for (;;)
        {
            if (!port)
                throw new VError(verr_illegal,
                                 "too many arguments for task call");
            Expr::parmOnly = FALSE;
            Expr* argValue = compileExpr();
            Variable* argVar = port->var;
            switch(argVar->exType.code)
            {
                case ty_int:
                    codeIntExpr(argValue);
                    codeStoInt(argVar->disp, taskScopeRef);
                    break;

                case ty_scalar:
                    codeScalarExpr(argValue);
                    codePostScalar((Scalar*)argVar, taskScopeRef, att_reg, 0);
                    break;

                case ty_vector:
                {
                    Vector* argVec = (Vector*)argVar;
                    codeVectorExpr(argValue, argVec->range->size);
                    codePostVector(argVec, argVec->range, taskScopeRef,
                                   att_reg, 0);
                    break;
                }
                default:
                    throw new VError(verr_illegal, "task arg type: not yet");
            }
            port = port->next;
            if (!isToken(','))
                break;
            scan();
        }
        expectSkip(')');
    }
    if (port)
        throwExpected("more arguments to task call");
}

//-----------------------------------------------------------------------------
// Code a call to the line-boundary routine if we just crossed a source line.

void codeNewLine()
{
    static int line;

    if (gScToken && gScToken->line != line)
    {
        line = gScToken->line;
        codeLitInt((size_t)gScToken);
        codeCall((Subr*)verLine, 1);
    }
}

//-----------------------------------------------------------------------------
// Compile and code an assignment statement with the expected lvalue type.

void codeAssignment(NetAttr lValueAttr, bool continuous = FALSE)
{
    int nParms = 0;
    if (continuous)
        nParms = compileParms();

    expectNameOf("lvalue");
    Variable* extScopeRef;
    NamedObj* lval = findFullName(&extScopeRef);
    Net* lnet = (Net*) lval;

    Range* vecRange = 0;
    int lValSize = 0;
    VLModule* module = (VLModule*)Scope::local;
    scan();

    switch (lval->type)
    {
        case ty_var:
        {
            Variable* lvar = (Variable*)lval;
            int memSize = 0;
            int indexSize = 0;
            Expr* index;
            Memory* m;

            switch (lvar->exType.code)
            {
                case ty_vector:
                    vecRange = new Range;
                    vecRange->compile();
                    lValSize = vecRange->isFull ? lvar->exType.size :
                                                    vecRange->size;
                    vecRange->size = lValSize;
                    break;
                case ty_memory:
                    if (nParms)
                        throw new VError(verr_illegal,
                                         "no delays for memory yet");
                    expectSkip('[');
                    Expr::parmOnly = FALSE;
                    index = compileExpr();
                    m = (Memory*)lnet;
                    memSize = m->memRange->size;
                    indexSize = 1 << index->nBits;
                    if (indexSize > memSize)
                        throw new VError(verr_illegal,
                               "index %d is larger than %d size of memory %s",
                               indexSize, memSize, m->name);
                    codeIntExpr(index);
                    expectSkip(']');
                    break;
                default:
                    break;
            }

            bool isVector = TRUE;
            bool isNonBlkingAsmt = isToken('<') && isNextToken('=');
            if (isToken('=') ||         // <var> = <expr>
                isNonBlkingAsmt)                // <var> <= <expr>
            {
                scan();
                if (isNonBlkingAsmt)
                {
                    if (continuous)
                        throw new VError(verr_illegal,
                            "continuous assignments can't be non-blocking");
                    scan();
                }

                if (!nParms)
                    nParms = compileParms();
                if (continuous)
                    module->startHandler(k_assign, lvar->name);

                Expr::parmOnly = FALSE;
                Expr* ex = compileExpr();

                switch (lvar->exType.code)
                {
                    case ty_int:
                        if (continuous)
                            throw new VError(verr_illegal,
                                "continuous assignment to an integer");
                        if (nParms)
                            throwExpected("no delay");
                        codeIntExpr(ex);
                        codeStoInt(lvar->disp, extScopeRef);
                        break;

                    case ty_scalar:
                        isVector = FALSE;
                    case ty_vector:
                    {
                        Expr* arg1 = ex->arg[1];
                        Expr* arg2 = ex->arg[2];
                        lnet->assigned = TRUE;

                        if (continuous && ex->opcode == op_sel && 
                            (isConstZ(arg1) || isConstZ(arg2)))
                        {
                            // a tri-state driver equation in either form:
                            //   tri = enable\ ? 'hz : internal
                            //   tri = enable  ? internal : 'hz
                            if (isVector)
                            {
                                if (ex->nBits != lValSize)
                                    throw new VError(verr_illegal,
                                        "%d-bit expression doesn't match"
                                        " %d-bit size of vector %s",
                                        ex->nBits, lValSize, lnet->name);
                            }
                            else if (ex->tyCode != lnet->exType.code)
                                    throw new VError(verr_illegal,
                                                     "type mismatch");

                            // coerce output type from wire to tri, if needed
                            lnet->attr |= att_tri;

                            // determine which select argument is which
                            Expr* enExpr = ex->arg[0];
                            Expr* internExpr = isConstZ(arg1) ? arg2 : arg1;

                            // code the internal equation
                            setTriggers(internExpr);
                            TmpName intName;
                            if (isVector && !vecRange->isFull)
                                intName = TmpName("%s_i%s%ds%d",
                                            lnet->name, module->name,
                                            vecRange->left.bit, lnet->triSrcNo);
                            else
                                intName = TmpName("%s_i%ss%d",
                                                lnet->name, module->name,
                                                lnet->triSrcNo);
                            Net* internal;
                            Range* iRange = 0;
                            if (isVector)
                            {
                                iRange = new Range(ex->nBits);
                                internal = (Net*) new Vector(
                                    newString(intName),
                                    att_none,
                                    Scope::local->newLocal(iRange->size,
                                                            sizeof(Signal*)),
                                    iRange->size,
                                    iRange,
                                    ((Vector*)lnet)->initLevelVec,
                                    (Vector*)lnet,
                                    vecRange);
                            }
                            else
                                internal = (Net*) new Scalar(
                                    newString(intName),
                                    att_none,
                                    Scope::local->newLocal(1, sizeof(size_t)),
                                    (Scalar*)lnet,
                                    vecRange);

                            int nInputParms = nParms;
                            if (nInputParms > 2)
                                nInputParms = 2;
                            codeLoadParms(nInputParms);
                            codeExpr(internExpr);
                            codePost(internal, iRange, 0, att_none,
                                     nInputParms);
                            if (isVector)
                                module->endHandler(internal, iRange);
                            else
                                module->endHandler(internal);

                            // code the enable equation
                            TmpName enName = TmpName("%s_e%ss%d", lnet->name,
                                                 module->name, lnet->triSrcNo);
                            Scalar* enable = new Scalar(
                                newString(enName),
                                att_none,
                                Scope::local->newLocal(1, sizeof(size_t)),
                                (Scalar*)lval,
                                vecRange);

                            module->startHandler(k_assign, enName);
                            setTriggers(enExpr);
                            int nEnParms = nParms - 2;
                            if (nEnParms < 0)
                                nEnParms = 0;
                            for (Expr** parm = vc.parms + 2;
                                 parm < vc.parms + 2 + nEnParms;
                                 parm++)
                                codeIntExpr(*parm);
                            if (isConstZ(arg1))
                                enExpr = newOp1(op_not, enExpr);
                            codeExpr(enExpr);
                            internal->enable = enable;
                            codePostScalar(enable, 0, att_none, nEnParms);
                            module->endHandler(enable);
                            lnet->triSrcNo++;
                        }
                        else
                        {
                            // not a tri-state driver
                            codeLoadParms(nParms);
                            if (isVector)
                            {
                                if (vecRange->isFull)
                                    vecRange = ((Vector*)lval)->range;
                                if (vecRange->isScalar)
                                    codeScalarExpr(ex);
                                else
                                    codeVectorExpr(ex, vecRange->size);
                                if (Expr::conditionedTriggers)
                                    codeSampleVector();
                                codePostVector((Vector*)lval, vecRange,
                                            extScopeRef, lValueAttr, nParms);
                            }
                            else
                            {
                                codeScalarExpr(ex);
                                if (Expr::conditionedTriggers)
                                    codeSampleScalar();
                                codePostScalar((Scalar*)lval, extScopeRef,
                                            lValueAttr, nParms);
                            }
                            if (continuous)
                            {
                                if (isVector)
                                    module->endHandler(lnet, vecRange);
                                else
                                    module->endHandler(lnet);
                            }
                        }
                        break;
                    }
                    case ty_memory:
                    {
                        if (continuous)
                            throw new VError(verr_illegal,
                                "continuous assignment to memory: not yet");
                        Memory* lvalMem = (Memory*)lval;
                        codeIntExpr(ex);
                        codeStoMem(lvalMem, extScopeRef);
                        break;
                    }
                    default:
                        throw new VError(verr_illegal, "lval type: not yet");
                }
                if (debugLevel(4))
                    display("%s-> %s\n", Expr::kExTySym[lvar->exType.code],
                            lnet->name);
                return;
            }
            else
                goto simpleExpr;
            break;
        }
        case ty_task:                   // 'enable' (call) a task
        {
            if (continuous)
                goto simpleExpr;
            NamedTask* task = (NamedTask*)lval;
            // bind (assign) task's args, if any
            codeBindTaskArgs(task, extScopeRef);
            codeCallVTask(task, extScopeRef);
            // all triggers to task subroutine must be triggers for this
            // calling Model
            for (NetList* tp = task->triggers; tp; tp = tp->next)
                addTrigger(tp->net);
            break;
        }
        case ty_func:
        simpleExpr:     // <expr>: not legal in Verilog
            throw new VError(verr_illegal, "illegal statement");
            break;

        default:
            throw new VError(verr_illegal, "illegal use of type");
    }
}

//-----------------------------------------------------------------------------
// Look up a string in the Verilog keywords table using a binary search.
// Returns keyword number, or k_notFound.

VKeyword lookupKey(const char* keyName)
{
    int k = n_keys/2;
    int dk = k;
    do
    {
        const char* ip = keyName;
        const char* kp = kVKeyStr[k];
        char ic = *ip++;
        char kc = *kp++;
        dk >>= 1;
        while (ic && kc)    // compare input word to keyword
        {
            if (ic < kc)
            {
                k -= dk;    // if before this, go back by half in table
                break;
            }
            else if (ic > kc)
            {
                k += dk;    // if after this, go forward by half in table
                break;
            }
            kc = *kp++;
            ic = *ip++;
        }
        if (ic == 0)        // if we hit the end of the input word
        {
            if (kc == 0)    // and also reached the end of the keyword,
                return (VKeyword)k; // we have a match, return it.
            k -= dk;        // else, keyword is longer: go back by half
        }
        else if (kc == 0)
            k += dk;        // keyword too short: go forward by half
    } while (dk);

    return k_notFound;
}

//-----------------------------------------------------------------------------
// Compile and code a Verilog behavioral statement.

void codeStatement()
{
    resetExprPool();
#ifdef BREAKPOINTS
    codeNewLine();
#endif
    Data* dspBase = vc.dsp;

    switch (gScToken->tokCode)
    {
        case '#':                           // # <expr> [<stmt>]
        {
            scan();                     // delay value expression
            codeIntExpr(compileTime());
            // Model addr already on bottom of stack
            codeDelay();
            codeDrop();                         // only pop off delay value
            if (debugLevel(4))
                display("# ");
            codeStmtOrNull();
            break;
        }
        case '@':
            scan();
            Expr::conditionedTriggers = TRUE;
            if (isToken('('))           // @ ( <eventexpr> ) [<stmt>]
            {
                scan();
                size_t* waitLoop = here();
                codeWait();
                // push false 'exit' flag on stack
                codeIntExpr(newConstInt(FALSE));
                while (!isToken(')'))
                {
                    VKeyword keyword = lookupKey(gScToken->name);
                    ModelFuncPtr testFn;
                    switch (keyword)
                    {
                        case k_posedge:
                            testFn = (ModelFuncPtr)&Model::justRisen;
                            scan();
                            goto testTrigger;

                        case k_negedge:
                            testFn = (ModelFuncPtr)&Model::justFallen;
                            scan();
                            goto testTrigger;

                        default:
                            testFn = (ModelFuncPtr)&Model::isTrigger;
                        testTrigger:
                        {
                            Variable* extScopeRef;
                            Net* net = (Net*)findFullName(&extScopeRef);
                            if (!net->isType(ty_var))
                                throwExpected("signal or named event");
                            switch (net->exType.code)
                            {
                                // a task test will set 'exit' flag if true
                                case ty_scalar: // includes named events
                                    codeCallTaskTest(net, testFn, extScopeRef);
                                    break;

                                case ty_vector:
                                    if (testFn != (ModelFuncPtr)
                                                    &Model::isTrigger)
                                        throw new VError(verr_illegal,
                                               "not allowed yet for a vector");
                                    codeCallTaskTestVec((Vector*)net,
                                            (ModelFuncPtr)&Model::isTriggerBus,
                                            extScopeRef);
                                    break;

                                default:
                                    throwExpected("signal or named event");
                            }
                            addTrigger(net);
                            break;
                        }
                    }
                    scan();
                    if (!isName("or"))
                        break;
                    scan();
                }
                expectSkip(')');
                setJmpTo(codeIf(), waitLoop); // code a loop-if no 'exit' flag
            }
            else                            // @ <event> [<stmt>]
            {
                expectNameOf("named event");
                Variable* extScopeRef;
                Net* nev = (Net*)findFullName(&extScopeRef);
                if (!nev->isType(ty_var) || !nev->isExType(ty_scalar)
                        || !(nev->attr & att_event))
                    throwExpected("named event");
                size_t* waitLoop = here();
                codeWait();
                // push false 'exit' flag on stack and set flag if trig event
                codeIntExpr(newConstInt(FALSE));
                codeCallTaskTest(nev, (ModelFuncPtr)&Model::isTrigger,
                                 extScopeRef);
                addTrigger(nev);
                setJmpTo(codeIf(), waitLoop);  // code a loop-if no 'exit' flag
                scan();
            }
            if (debugLevel(4))
                display("@ ");
            codeStmtOrNull();
            break;

        case '-':                           // -> <event>
        {
            scan();
            expectSkip('>');
            expectNameOf("named event");
            Variable* extScopeRef;
            Scalar* nev = (Scalar*)findFullName(&extScopeRef);
            if (!nev->isType(ty_var) || !nev->isExType(ty_scalar)
                    || !(nev->attr & att_event))
                throwExpected("named event");
            codePostNamedEvent(nev, extScopeRef);
            if (debugLevel(4))
                display("-> %s\n", nev->name);
            scan();
            expectSkip(';');
            break;
        }
        case ';':                           // ;        (null statement)
            expectSkip(';');
            break;
        
        case '$':                           // system task call
            codeSystemCall();
            expectSkip(';');
            break;
        
        case NAME_TOKEN:
        {
            VKeyword keyword = lookupKey(gScToken->name);
            switch (keyword)
            {
                case k_if:                 // if ( <expr ) <stmt> [else <stmt>]
                {
                    scan();
                    expectSkip('(');
                    Expr::parmOnly = FALSE;
                    codeIntExpr(compileExpr());
                    expectSkip(')');
                    size_t* ifNotJmp = codeIf();
                    codeStmtOrNull();
                    if (isName("else"))
                    {
                        scan();
                        size_t* ifElseJmp = codeJmp();
                        setJmpToHere(ifNotJmp);
                        codeStmtOrNull();
                        ifNotJmp = ifElseJmp;
                    }
                    setJmpToHere(ifNotJmp);
                    break;
                }
                case k_case:             // case ( <expr ) {<caseitem>} endcase
                {
                    scan();
                    expectSkip('(');
                    Expr::parmOnly = FALSE;
                    codeIntExpr(compileExpr());
                    expectSkip(')');
                    size_t caseDisp = Scope::local->newLocal(1, sizeof(size_t));
                    codeStoInt(caseDisp, 0);
                    JmpList exitJmpList;
                    do
                    {
                        if (isName("default"))
                        {                       // default [:] <stmt>
                            scan();
                            if (isToken(':'))
                                scan();
                            codeStmtOrNull();
                            expectName("endcase");
                            break;
                        }
                        else                    // <expr> [, <expr>] : <stmt>
                        {
                            JmpList matchJmpList;
                            size_t* nextJmp;
                            for (;;)
                            {
                                codeLoadInt(caseDisp);
                                Expr* caseEx = newExprNode(op_stacked);
                                caseEx->tyCode = ty_int;
                                Expr::parmOnly = FALSE;
                                codeIntExpr(newOp2(op_eq, caseEx,
                                            compileExpr()));
                                if (isToken(','))
                                {
                                    scan();
                                    matchJmpList.add(codeIfNot());
                                }
                                else
                                {
                                    nextJmp = codeIf();
                                    break;
                                }
                            }
                            expectSkip(':');
                            matchJmpList.setJmpsToHere();
                            codeStmtOrNull();
                            exitJmpList.add(codeJmp());
                            setJmpToHere(nextJmp);
                        }
                    } while (!isName("endcase"));
                    scan();
                    exitJmpList.setJmpsToHere();
                    break;
                }
                case k_forever:                 // forever <stmt>
                {
                    scan();
                    size_t* loopBegin = here();
                    codeStatement();
                    codeJmp(loopBegin);
                    break;
                }
                case k_while:                   // while ( <expr> ) <stmt>
                {
                    scan();
                    size_t* loopBegin = here();
                    expectSkip('(');
                    Expr::parmOnly = FALSE;
                    codeIntExpr(compileExpr());
                    expectSkip(')');
                    size_t* loopExitJmp = codeIf();
                    codeStatement();
                    codeJmp(loopBegin);
                    setJmpToHere(loopExitJmp);
                    break;
                }
                case k_repeat:                  // repeat ( <expr> ) <stmt>
                {
                    scan();
                    expectSkip('(');            // initialize count to expr
                    Expr::parmOnly = FALSE;
                    codeIntExpr(compileExpr());
                    expectSkip(')');
                    size_t countDisp = Scope::local->newLocal(1,
                                                sizeof(size_t));
                    codeStoInt(countDisp);
                    size_t* loopBegin = here();     // tester:
                    codeLoadInt(countDisp);
                    size_t* loopExitJmp = codeIf(); // if count == 0, jump to
                    codeStatement();                //  exit statement

                    codeLoadInt(countDisp);         // count--
                    Expr* countEx = newExprNode(op_stacked);
                    countEx->tyCode = ty_int;
                    codeIntExpr(newOp2(op_sub, countEx, newConstInt(1)));

                    codeStoInt(countDisp);
                    codeJmp(loopBegin);            // jump to tester
                    setJmpToHere(loopExitJmp);     // exit
                    break;
                }
                case k_for:                     // for ( <reg_asmt> ; <expr> ;
                {
                    scan();                     //  <reg_asmt> ) <stmt>
                    expectSkip('(');
                    codeAssignment(att_reg);        // initializer expr
                    expectSkip(';');
                    size_t* tester = here();
                    Expr::parmOnly = FALSE;
                    codeIntExpr(compileExpr());
                    size_t* loopExitJmp = codeIf(); // if == 0, jump to exit
                    size_t* statementJmp = codeJmp();   // else jump to stmt
                    expectSkip(';');
                    size_t* repeater = here();
                    codeAssignment(att_reg);        // repeater expr
                    codeJmp(tester);                // jump to tester
                    expectSkip(')');
                    setJmpToHere(statementJmp);
                    codeStatement();                // statement
                    codeJmp(repeater);              // jump to repeater
                    setJmpToHere(loopExitJmp);      // exit
                    break;
                }
                case k_begin:               // begin <stmt> <stmt> ... end
                    codeStmtList();
                    break;

                case k_disable:             // disable <taskName>
                {
                    scan();
                    Variable* extScopeRef;
                    NamedObj* task = findFullName(&extScopeRef);
                    if ((NamedTask*)task == Scope::local)
                        // disabling self: code a return
                        Scope::local->addRetJmp(codeJmp());
#if 0 // !!! unfinished
                    else if (task->type == ty_begEnd)
                    {
                        // or retrigger another task:
                        // first get VTask address from module instance
                        // stored in first location
                        BegEnd* block = (BegEnd*)task;
                        Model* m = block->model;
                        codeLoadInt(0, extScopeRef);
                        // arg to init() is VTask's Model address
                        codeInit();
                        codeDrop(1);
                    }
                    else
                        throwExpected("named begin-end block");
#endif
                    scan();
                    break;
                }
                default:
                    goto assignment;
            }
            break;
        }

        default:                            // <reg_lvalue> = <expr> ;
        assignment:
            codeAssignment(att_reg);
            expectSkip(';');
            break;
    }

    int dspRem = (int)(dspBase - vc.dsp);
    if (dspRem)
        throw new VError(verr_bug,
            "BUG: codeStatement: %d items left on dsp stack at 0x%x",
            dspRem, here());
}

//-----------------------------------------------------------------------------
// Compile and code an optional statement".

void codeStmtOrNull()
{
    if (isToken(';'))
        scan();
    else
        codeStatement();
}

//-----------------------------------------------------------------------------
// Compile and code a statement list: "begin <stmt> <stmt> ... end".

void codeStmtList()
{
    char* blockName = 0;
    scan(); // skip 'begin'
    if (isToken(':'))
    {
        scan();
        blockName = newString(gScToken->name);
        scan();
    }
    while (!isName("end"))
        codeStatement();
    scan();
    Scope::local->lastBlockName = blockName;
}

//-----------------------------------------------------------------------------
// Code a post of a named event.
void codePostNamedEvent(TrigNet* namedEvt, Variable* extScopeRef)
{
    // code 'event = !event;' to toggle event each time
    codeScalarExpr(newOp1(op_not, newLoadTrigNet(namedEvt, extScopeRef)));
    codePost0Scalar(namedEvt, extScopeRef);
    namedEvt->assigned = TRUE;
}

//-----------------------------------------------------------------------------
// Compile and code a variable or event declaration.

void VLModule::codeNetDeclaration(NetAttr attr)
{
    scan();
    Range* vecRange = 0;                // get optional vector-range
    bool isVector = compileVecRange(&vecRange);
#ifdef NOTYET   // !!! no net delays yet
    int minDelay, maxDelay;             // get optional net-delay
    compileMinMaxDelay(&minDelay, &maxDelay);
#endif
    for (;;)                            // assign type to each net in list
    {
        expectNameOf("variable");
        char* netName = newString(gScToken->name);
        Port* port;
        Token* saveLoc = gScToken;

        for (port = Scope::local->ports; port; port = port->next)
            if (strcmp(port->name, netName) == 0)
                break;
        if (port)       // port signal exists: add this attribute to it
        {
#if 0 // apparently legal...
            if (attr == att_none)
                throw new VError(verr_illegal, "already a port");
#endif
            Variable* portVar = port->var;
            switch (portVar->exType.code)
            {
                case ty_scalar:
                    if (isVector)
                        throw new VError(verr_illegal,
                                         "mismatch: port is a vector");
                    break;
                case ty_vector:
                    if (!isVector)
                        throw new VError(verr_illegal,
                                         "mismatch: port is a scalar");
                    if (vecRange->size != portVar->exType.size)
                        throw new VError(verr_illegal, "mismatch: port size");
                    break;
                default:
                    throwExpected("vector or scalar port name");
            }
            Net* portNet = (Net*)portVar;
            portNet->attr |= attr;
            scan();
        }
        else            // else create a new variable
        {
            if (Scope::local->find(netName, 0, TRUE))
                throw new VError(verr_illegal, "net already declared");

            scan();
            Range* memRange;            // any memory address range?
            bool isMemory = compileMemRange(&memRange);
            if (isMemory)
            {
                size_t maxSize = sizeof(Signal*) +
                                memRange->size * sizeof(size_t);
                new Memory(netName, attr,
                            Scope::local->newLocal(memRange->size + 1,
                                                   sizeof(Signal*)),
                            maxSize, memRange, vecRange);
            }
            else
            {
                gScToken = saveLoc;
                if (isVector)
                    new Vector(netName, attr,
                            Scope::local->newLocal(vecRange->size,
                                                   sizeof(Signal*)),
                                vecRange->size, vecRange);
                else
                    new Scalar(netName, attr,
                            Scope::local->newLocal(1, sizeof(size_t)));
                scan();
            }
        }

        if (isToken('='))
        {
            gScToken = saveLoc;
            codeContAssignStmt(TRUE);
        }

        if (!isToken(','))
            break;
        scan();
    }
    expectSkip(';');
}

//-----------------------------------------------------------------------------
// Compile and code a port definition or declaration.

void codePortDeclaration(NetAttr baseAttr, bool isPort, char termChar)
{
    for (;;)
    {
        // Verilog 2001: gather attributes
        bool haveAttr = FALSE;
        NetAttr attr = baseAttr;
        while (isToken(NAME_TOKEN)) 
        {
            VKeyword keyword = lookupKey(gScToken->name);
            switch (keyword)
            {
                case k_input:
                    attr |= att_input;
                    haveAttr = TRUE;
                    break;

                case k_output:
                    attr |= att_output;
                    haveAttr = TRUE;
                    break;

                case k_inout:
                    attr |= (att_inout | att_tri);
                    haveAttr = TRUE;
                    break;

                case k_wire:
                    haveAttr = TRUE;
                    break;

                case k_reg:
                    attr |= att_reg;
                    haveAttr = TRUE;
                    break;

                case k_tri:
                    attr |= att_tri;
                    haveAttr = TRUE;
                    break;

                default:
                    goto nameDecl;
            }
            scan();
        }
        nameDecl:

        // get optional vector-range
        Range* vecRange;
        bool isVector = FALSE;
        if (haveAttr)
            isVector = compileVecRange(&vecRange);

        // assign type to each port in list
        for (;;)
        {
            expectNameOf("variable");
            Port* port;
            for (port = Scope::local->ports; port; port = port->next)
                if (strcmp(port->name, gScToken->name) == 0)
                    break;
            if (isPort)
            {
                // a task, function, or port: define ports
                if (port)
                    throw new VError(verr_illegal, "port already declared");
                port = new Port(newString(gScToken->name));
                Scope::local->addPort(port);
            }
            else
            {
                // a module item: expect all ports from portnames list
                if (!port)
                    throwExpected("port signal name");
                if (port->var)
                    throw new VError(verr_illegal,
                                     "port signal already declared");
            }

            // create actual Scalar or Vector for port
            if (haveAttr)
            {
                if (isVector)
                    port->var = (Variable*)new Vector(port->name, attr,
                                   Scope::local->newLocal(vecRange->size,
                                                sizeof(Signal*)),
                                                vecRange->size, vecRange);
                else
                    port->var = (Variable*)new Scalar(port->name, attr,
                                   Scope::local->newLocal(1, sizeof(size_t)));
            }
            scan();
            if (isPort || !isToken(','))
                break;
            scan();
        }
        if (!isToken(','))
            break;
        scan();
    }
    expectSkip(termChar);
}

//-----------------------------------------------------------------------------
// Compile and code a parameter assignment declaration.
// Parameter is stored as a size_t integer value and width (width=-1 for none).

void VLModule::codeParmDeclaration(bool isDefParm, char termChar)
{
    scan();
    Range* vecRange = 0;                // get optional vector-range
    bool isVector = compileVecRange(&vecRange);
    if (isVector && !(vecRange->left.isConst && vecRange->right.isConst &&
            vecRange->left.bit < sizeof(size_t) && vecRange->right.bit == 0))
        throwExpected("size_t bit size max, if given");

    for (;;)
    {
        char* parmName;
        if (isDefParm)
        {
            // get defparam's future instance name
            if (!isNextToken('.'))
                throwExpected("future instance name");
            TmpName refName("%s.", gScToken->name);
            scan();
            scan();
            expectNameOf("parameter");
            refName += gScToken->name;
            parmName = newString(refName);
        }
        else
        {
            expectNameOf("parameter");
            parmName = newString(gScToken->name);
        }
        Variable* parm = new Variable(parmName, ty_int,
                             Scope::local->newLocal(1, 2*sizeof(size_t)));
        parm->isParm = TRUE;
        parm->isDefParm = isDefParm;
        addParm(parm);
        scan();
        expectSkip('=');

        startHandler(k_parameter, parmName);
        this->evHandsE->parm = parm;
        Expr::gatherTriggers = FALSE;
        Expr::parmOnly = TRUE;
        Expr* ex = compileExpr();
        codeIntExpr(ex);
        parm->scale = vc.dsp->scale;
        codeStoInt(parm->disp, 0);
        codeIntExpr(newConstInt(isVector ? vecRange->left.bit + 1 : -1));
        codeStoInt(parm->disp + sizeof(size_t), 0);
        endHandler();

        if (!isToken(','))
            break;
        scan();
    }
    expectSkip(termChar);
}

//-----------------------------------------------------------------------------
// Compile and code an integer variable declaration.

void codeIntDeclaration()
{
    scan();
    for (;;)
    {
        expectNameOf("variable");
        new Variable(newString(gScToken->name), ty_int,
                     Scope::local->newLocal(1, sizeof(size_t)));
        scan();
        if (!isToken(','))
            break;
        scan();
    }
    expectSkip(';');
}

//-----------------------------------------------------------------------------
// Create and code the head and tail of an event handler subroutine.
// curTriggers arg is for when an expression has already been compiled.

void VLModule::startHandler(VKeyword type, const char* desig,
                            NetList* curTriggers)
{
    if (debugLevel(3))
        display("0x%08x: *** EvHand %s:\n", here(), desig);
    this->addEvHand(new EvHand(type, (EvHandCodePtr)here()));
    Expr::curTriggers = curTriggers;
    Expr::gatherTriggers = TRUE;
    Expr::conditionedTriggers = FALSE;
    Expr::parmOnly = FALSE;
    codeSubrHead(FALSE);
}

void VLModule::endHandler()
{
    codeEndModule();
    this->evHandsE->triggers = Expr::curTriggers;
    Expr::gatherTriggers = FALSE;
    Expr::parmOnly = FALSE;
}

void VLModule::clearExprTriggers()
{
    Expr::curTriggers = 0;
    Expr::gatherTriggers = TRUE;
    Expr::conditionedTriggers = FALSE;
}

#if 0 // BROKEN
//-----------------------------------------------------------------------------
// Create a new internal Net.

Net* VLModule::newInternNet(char idLet, VExType exType, Net* output,
                            char* gateName, Range** range)
{
    TmpName intName = TmpName("%s_%c%s", output->name, idLet, gateName);
    Net* net;
    switch (exType.code)
    {
        case ty_int:
            throwExpected("scalar or vector");
            break;

        case ty_scalar:
            net = (Net*) new Scalar(newString(intName), att_none,
                                  Scope::local->newLocal(1, sizeof(size_t)),
                                  (Scalar*)output);
            break;

        case ty_vector:
            *range = new Range();
            range->compile();
            if (range->isFull)
                *range = *((Vector*)output)->range;
            if (!range->isFull && !(range->left.isConst &&
                    (range->isScalar || range->right.isConst)))
                throwExpected("constant vector size, for now");
            Range* rangeInst = new Range(range);
            net = (Net*) new Vector(newString(intName),
                                    att_none,
                                    Scope::local->newLocal(rangeInst->size,
                                                           sizeof(Signal*)),
                                    rangeInst->size,
                                    rangeInst,
                                    ((Vector*)output)->initLevelVec,
                                    (Vector*)output);
            break;
    }
    return net;
}

//-----------------------------------------------------------------------------
// Compile and code an enable buffer, with given polarities.
//
//   bufif [(minProp, maxProp, minEO, maxEO)] {<name> ( <output>, <input> )} ;
//
// Limitations: only one output per gate.

enum { minPropParm = 0, maxPropParm, minEOParm, maxEOParm };

void VLModule::codeBufIf(bool outPol, bool enPol)
{
    scan();
    int nParms = compileParms();
    if (nParms > 4)
        throwExpected("minProp, maxProp, minEO, maxEO");

    for (;;)
    {
        expectNameOf("gate");
        TmpName gateName = TmpName(gScToken->name);
        scan();
        expectSkip('(');

        expectNameOf("output");                     // get output
        Variable* outputExtScopeRef;
        Net* output = (Net*)findFullName(&outputExtScopeRef);
        if (!output->isType(ty_var) || !(output->attr & att_tri))
            throwExpected("tri-state output signal");
        scan();

        expectSkip(',');
        Range* iRange;
        Net* internal = newInternNet('i', output->exType, output, gateName,
                                     &iRange);
        startHandler(k_assign, internal->name);     // compile input equation
        int nInputParms = nParms;
        if (nInputParms > 2)
            nInputParms = 2;
        codeLoadParms(nInputParms);
        Expr::parmOnly = FALSE;
        Expr* internExpr = compileExpr();
        if (outPol == 0)
            internExpr = newOp1(op_not, internExpr);
        codeExpr(internExpr);
        codePost(internal, iRange, 0, att_none, nInputParms);
        endHandler(internal);

        expectSkip(',');
        VExType enType = {ty_scalar, 0};
        Scalar* enable = (Scalar*)newInternNet('e', enType, output,
                                               gateName, 0);
        startHandler(k_assign, enable->name);       // compile enable equation
        int nEnParms = nParms - 2;
        if (nEnParms < 0)
            nEnParms = 0;
        for (Expr** parm = vc.parms + 2; parm < vc.parms + 2 + nEnParms; parm++)
            codeIntExpr(*parm);
        Expr::parmOnly = FALSE;
        Expr* enExpr = compileExpr();
        if (enPol == 0)
            enExpr = newOp1(op_not, enExpr);
        codeExpr(enExpr);
        internal->enable = enable;
        codePostScalar(enable, 0, att_none, nEnParms);
        endHandler(enable);

        expectSkip(')');
        if (!isToken(','))
            break;
        scan();
    }
    expectSkip(';');
}
#endif // BROKEN

//-----------------------------------------------------------------------------
// Compile and code a continuous-assigment statement.
//
// assign  {#p} sol = {#p} ex;
// assign  {#p} tri = {#p} enex ? iex : 'hz;
// assign  {#p} tri = {#p} enex ? 'hz : iex;
//
// wire sol = ex;

void VLModule::codeContAssignStmt(bool oneAssign)
{
    if (!oneAssign)
        scan();

    for (;;)
    {
        codeAssignment(att_none, TRUE);
        if (oneAssign)
            return;

        if (!isToken(','))
            break;
        scan();
    }
    expectSkip(';');
}

//-----------------------------------------------------------------------------
// Code a module instance.
// For each connection, it either compiles an expression (which can only be
// connected to an input), or just notes the Net and Range for later binding.

void VLModule::codeModuleInstance()
{
    TmpName modName;
    scanFullName(&modName);
    scan();
    ParmVal* pvList = 0;
    ParmVal* nextPV = 0;
    int scale = 1;
    if (isToken('#'))   // get a list of parm values, if any
    {
        scan();
        expectSkip('(');
        while (!isToken(')'))
        {
            char* name = 0;
            size_t value = 0;

            // Verilog 2001: get optional parameter port name
            if (isToken('.'))
            {
                scan();
                expectNameOf("parameter port");
                name = newString(gScToken->name);
                scan();
                expectSkip('(');
                if (!isToken(')'))
                    value = getConstIntExpr(&scale);
                expectSkip(')');
            }
            else
                value = getConstIntExpr(&scale);

            ParmVal* pv = new ParmVal(name, value, scale);
            if (nextPV)
                nextPV->next = pv;
            else
                pvList = pv;
            nextPV = pv;
            if (!isToken(','))
                break;
            scan();
        }
        expectSkip(')');
    }
    for (;;)
    {
        expectNameOf("module instance");
        if (local->find(gScToken->name, 0, TRUE))
            throw new VError(verr_illegal, "already defined");
        char* imodName = newString(gScToken->name);

        // assign instance's defparam values to named parameters
        for (Variable* parm = this->parms; parm; parm = parm->next)
        {
            if (parm->isDefParm)
            {
                TmpName pathName(parm->name);
                char* lastDot = 0;
                for (char* p = pathName; *p; p++)
                    if (*p == '.')
                        lastDot = p;
                if (!lastDot)
                    throw new VError(verr_bug,
                        "BUG: defparam %s missing module name?", parm->name);
                *lastDot = 0;
                char* parmSubName = lastDot + 1;
                if (strcmp(imodName, pathName) == 0)
                {
                    ParmVal* pv = new ParmVal(newString(parmSubName), parm);
                    if (nextPV)
                        nextPV->next = pv;
                    else
                        pvList = pv;
                    nextPV = pv;
                }
            }
        }

        Scope* localSave = local;
        Instance* instance = new Instance(newString(modName),
                                          imodName,
                                          pvList,
                                          0,
                                          instTmpls);
        this->instTmpls = instance;
        local = localSave;
        VLModule* module = (VLModule*)localSave->findScope(modName, TRUE);
        if (module)
        {
            if (!module->isType(ty_module))
                throw new VError(verr_illegal, "'%s' not a module",
                                 newString(modName));
            instance->module = module;
        }
        scan();
        expectSkip('(');
        Conn* connList = 0;
        Conn* nextConn = 0;
        while (!isToken(')'))
        {
            Net* net = 0;
            Range* vecRange = 0;
            char* portName = 0;
            Expr* ex = 0;
            clearExprTriggers();

            if (isToken('.'))
            {
                scan();
                expectNameOf("port");
                portName = newString(gScToken->name);
                scan();
                expectSkip('(');
                if (!isToken(')'))
                {
                    Expr::parmOnly = FALSE;
                    ex = compileExpr();
                }
                expectSkip(')');
            }
            else
            {
                Expr::parmOnly = FALSE;
                ex = compileExpr();
            }

            // if connected
            if (ex)
            {
                // if a scalar or vector, conn. notes the net and (sub)range
                if ((ex->opcode == op_load &&
                        (ex->tyCode == ty_scalar || ex->tyCode == ty_vector)) ||
                        (ex->opcode == op_conv && ex->tyCode == ty_scalar &&
                        ex->arg[0]->opcode == op_load &&
                        ex->arg[0]->tyCode == ty_vector))
                {
                    if (ex->opcode == op_conv)
                    {
                        // connecting one bit of a vect (ex got conv to Scalar)
                        ex = ex->arg[0];
                    }
                    if (ex->tyCode == ty_vector)
                    {
                        // connecting a (slice of a) vector: get its range
                        vecRange = new Range(ex->data.range);
                        if (!(vecRange->left.isConst &&
                            (vecRange->isScalar || vecRange->right.isConst)))
                            throwExpected("constant vector bit range");
                    }
                    net = (Net*) ex->data.var;
                }
                else
                {
                    // connecting an expression to port: make an internal
                    // temp net and flag it with att_copy to only allow it to
                    // connect to an input

                    char* intName = newString(TmpName("i%s_%s_%x",
                                        this->name,
                                        (portName ? portName : "_"),
                                        this->localSize));

                    startHandler(k_assign, "port", Expr::curTriggers);
                    if (ex->tyCode == ty_vector)
                    {
                        codeVectorExpr(ex, ex->nBits);
                        vecRange = new Range(ex->nBits);
                        Vector* vec = new Vector(intName, att_copy,
                                        Scope::local->newLocal(ex->nBits,
                                                               sizeof(Signal*)),
                                        ex->nBits, vecRange);
                        codePostVector(vec, vecRange, 0, 0, 0);
                        net = (Net*) vec;
                        endHandler(net, vecRange);
                    }
                    else
                    {
                        codeScalarExpr(ex);
                        Scalar* scalar = new Scalar(intName, att_copy,
                                   Scope::local->newLocal(1, sizeof(Signal*)));
                        codePostScalar(scalar, 0, 0, 0);
                        net = (Net*) scalar;
                        endHandler(net);
                    }
                }
            }
            else
            {
                // no connecting variable given: make up one to keep module's
                // output driver some signal to drive
                net = new Scalar("_", 0, Scope::local->newLocal(1,
                                                            sizeof(size_t)));
            }
            net->assigned = TRUE;

            // !!! should be new Conn(net, portName, extScopeRef) ?
            Conn* conn = new Conn(net, vecRange, portName);
            if (nextConn)
                nextConn->next = conn;
            else
                connList = conn;
            nextConn = conn;

            if (!isToken(','))
                break;
            scan();
        }
        instance->conns = connList;
        expectSkip(')');
        if (!isToken(','))
            break;
        scan();
    }
    expectSkip(';');
}

//-----------------------------------------------------------------------------
// Compile and code a module item.

void VLModule::codeModuleItem()
{
    expectNameOf("module item");
    initCodeArea();
    resetExprPool();
    NetAttr pull;

    VKeyword keyword = lookupKey(gScToken->name);
    switch (keyword)
    {
        case k_input:               // port declarations
        case k_output:
        case k_inout:
            codePortDeclaration(att_none, FALSE, ';');
            break;

        case k_parameter:           // parameter assignment
            codeParmDeclaration(FALSE, ';');
            break;

        case k_integer:             // integer declaration
        case k_genvar:
            codeIntDeclaration();
            break;

        case k_wire:                // variable and event declarations
            codeNetDeclaration(att_none);
            break;

        case k_reg:
            codeNetDeclaration(att_reg);
            break;

        case k_tri:
            codeNetDeclaration(att_tri);
            break;

        case k_tri0:
            codeNetDeclaration(att_tri + att_pull0);
            break;

        case k_tri1:
            codeNetDeclaration(att_tri + att_pull1);
            break;

        case k_wor:
        case k_trior:
            codeNetDeclaration(att_wor);
            break;

        case k_event:
            codeNetDeclaration(att_event);
            break;

        case k_supply0:
        case k_supply1:
        case k_time:
        case k_wand:
        case k_triand:
            throw new VError(verr_illegal, "not this type yet");
            break;

        case k_assign:                  // continuous-assigment statement
            codeContAssignStmt();
            break;

        case k_initial:                 // initialization behavioral code
            scan();
            startHandler(k_initial, "initial");
            codeStatement();
            endHandler();
            break;

        case k_always:                  // repeating behavioral code
        {
            scan();
            startHandler(k_always, "always");
            Expr::gatherTriggers = FALSE;
            size_t* alwaysBegin = here();
            codeStatement();
            codeJmp(alwaysBegin);
            endHandler();
            break;
        }
        case k_task:                    // task declaration
        {
            scan();
            NamedTask* task = new NamedTask(newString(gScToken->name),
                                            this->enclModule, this->localSize);
            this->localSize = task->localSize;
            break;
        }
        case k_defparam:                // defparam statement
            codeParmDeclaration(TRUE, ';');
            break;

        case k_generate:                // generate section
        case k_endgenerate:
            scan();
            // ignored for now
            break;

        case k_function:
        case k_primitive:
        case k_specify:
            throw new VError(verr_illegal, "module item: not yet");
            break;

        case k_pullup:                  // pullup, pulldown resistors
            pull = att_pull1;
            goto pullGate;
        case k_pulldown:
            pull = att_pull0;
        pullGate:
            do
            {
                scan();
                if (isToken(NAME_TOKEN))
                    scan();
                expectSkip('(');
                Variable* extScopeRef;
                NamedObj* sym = findFullName(&extScopeRef);
                if (!sym->isType(ty_var))
                    throwExpected("terminal net name");
                scan();
                ((Net*)sym)->attr |= pull;
                expectSkip(')');
            } while (isToken(','));
            expectSkip(';');
            break;

#if 0 // BROKEN
        case k_bufif0:                  // enable gates
            codeBufIf(1, 0);
            break;
        case k_bufif1:
            codeBufIf(1, 1);
            break;
        case k_notif0:
            codeBufIf(0, 0);
            break;
        case k_notif1:
            codeBufIf(0, 1);
            break;
#else
        case k_bufif0:                  // enable gates
        case k_bufif1:
        case k_notif0:
        case k_notif1:
#endif
        case k_and:                     // n-input gate
        case k_nand:
        case k_nor:
        case k_or:
        case k_xnor:
        case k_xor:

        case k_buf:                     // n-output gate
        case k_not:

        case k_nmos:                    // mos switch
        case k_pmos:
        case k_rnmos:
        case k_rpmos:

        case k_tran:                    // pass switch
        case k_rtran:

        case k_tranif0:                 // pass-enable switch
        case k_tranif1:
        case k_rtranif0:
        case k_rtranif1:

        case k_cmos:                    // cmos switch
        case k_rcmos:
            throw new VError(verr_illegal,
                             "no built-in gates: define as a module");
            break;

        case k_notFound:                // other: a module instantiation
                codeModuleInstance();
            break;

        default:
            throw new VError(verr_illegal, "unknown module item: not yet");
            break;
    }
}

//-----------------------------------------------------------------------------
// Compile a module or task port list.

void VLModule::getPorts()
{
    scan();
    if (isToken('#'))
    {
        // Verilog 2001: Parameter support
        scan();
        expectSkip('(');
        expectName("parameter");
        codeParmDeclaration(FALSE, ')');
    }
    if (isToken('('))
    {
        scan();
        codePortDeclaration(att_none, TRUE, ')');
    }
    expectSkip(';');
}

//-----------------------------------------------------------------------------
// Construct a VLModule by compiling source Verilog.

VLModule::VLModule(char* name) : Scope(name, ty_module, this)
{
    this->ports = 0;                        // clear all module item lists
    this->portsE = 0;
    this->parms = 0;
    this->parmsE = 0;
    this->localSize= 0;
    this->evHands = 0;
    this->evHandsE = 0;
    this->instTmpls = 0;
    initCodeArea();
    resetExprPool();
    // reserve space for first var: task pointer
    Scope::local->newLocal(1, sizeof(size_t));

    // create global reset signal if first module:
    //   all signals will depend on this
    if (!EvHand::gAssignsReset)
    {
        Scalar* res = new Scalar("__ResS__", att_none,
                                 Scope::local->newLocal(1, sizeof(size_t)),
                                 0);
        startHandler(k_assign, res->name);
        codeLitLevel(LV_H);
        codePost(res, 0, 0, att_none, 0);
        endHandler();

        // move global reset handler from module's handler list to gAssignsReset
        EvHand::gAssignsReset = this->evHandsE;
        if (this->evHands == this->evHandsE)
        {
            this->evHands = 0;
            this->evHandsE = 0;
        }
        else
            for (EvHand* eh = this->evHands; eh; eh = eh->modNext)
                if (eh->modNext == this->evHandsE)
                {
                    eh->modNext = 0;
                    this->evHandsE = eh;
                    break;
                }
        res->evHand = EvHand::gAssignsReset;
        EvHand::gAssignsReset->net = res;
    }

    getPorts();                     // build port list

    while (!isName("endmodule"))
        codeModuleItem();               // code each module item

    scan();
    
    Scope::local = enclScope;   // restore scope
}

//-----------------------------------------------------------------------------
// Construct a NamedTask by compiling a Verilog 'task' definition.

NamedTask::NamedTask(const char* name, VLModule* enclModule,
                     size_t localsStart) : Scope(name, ty_task, enclModule)
{
    scan();
    expectSkip(';');
    this->ports = 0;    
    this->portsE = 0;
    this->localSize= localsStart; // start locals at parent module's locals ptr

    while (isToken(NAME_TOKEN)) // code task item declarations
    {
        VKeyword keyword = lookupKey(gScToken->name);
        switch (keyword)
        {
            case k_input:               // port declarations
            case k_output:
            case k_inout:
                codePortDeclaration(att_reg, TRUE, ';');
                break;

            case k_integer:             // integer declaration
                codeIntDeclaration();
                break;

            case k_wire:                // variable and event declarations
                enclModule->codeNetDeclaration(att_none);
                break;

            case k_reg:
                enclModule->codeNetDeclaration(att_reg);
                break;

            case k_event:
                enclModule->codeNetDeclaration(att_event);
                break;

            default:
                goto bodyStmt;
        }
    }
bodyStmt:
    // remove input and output attributes from task ports since they are really
    // separate variables and thus are not initialized by the parent module
    for (Port* port = this->ports; port; port = port->next)
    {
        Variable* var = port->var;
        if (var->exType.code == ty_scalar || var->exType.code == ty_vector)
        {
            Net* net = (Net*)var;
            net->attr &= ~att_inout;
            net->assigned = TRUE;
        }
    }
    initCodeArea();
    Expr::curTriggers = 0;
    Expr::gatherTriggers = TRUE;
    Expr::conditionedTriggers = FALSE;
    Expr::parmOnly = FALSE;
    this->code = (void*)here();
    codeSubrHead(TRUE);
// the following clobbers Macro::"SIMULATOR" data value!
//  vc.dsp->setIntReg();            // (for all task calls)

    codeStmtOrNull();               // body statement

    this->disableJmps.setJmpsToHere();  // 'disable' stmts jump to end of subr
    codeSubrTail();
    this->triggers = Expr::curTriggers;
    Expr::gatherTriggers = FALSE;
    expectName("endtask");
    scan();

    Scope::local = enclScope;           // restore scope
}
