// ****************************************************************************
//
//          PVSim Verilog Simulator Model/Task Base Class
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
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <time.h>
#include <stdio.h>
#include "Utils.h"
#include "PSignal.h"
#include "PCode.h"
#include "VL.h"
#include "Model.h"

#if _MSC_VER
#define time_t __time64_t
#define localtime _localtime64
#define time _time64
#endif

Model*  gModelsList;        // list of all models, for initVars
bool    gTracedMode;        // TRUE if current models's signal's being traced
const char* gLastModelName; // model name from newModel() for errors

// Private thread context for model
struct ThreadContext
{
    PCode*  pc;             // PCode program counter
    size_t* sp;             // integer stack pointer
    size_t  tos;            // top of stack
    char*   inst;           // instantiation variables base
    size_t* stack;          // thread private stack
    size_t  arg[10];        // subroutine arguments
    Tick    wakeTime;       // sleep wake tick

    // OLD
    ThreadEntryPtr  threadEntry;    // thread start address
    void*           threadParam;    // ptr to actual object, not Task base class
};

//-----------------------------------------------------------------------------
// Generic model constructor.

Model::Model(const char* name, EvHandCodePtr evHandCode, char* instModule)
{
    next = gModelsList;
    gModelsList = this;
    this->className = "?";
    this->desig = name;
    this->evHandCode = evHandCode;
    this->instModule = instModule;
    gTracedMode = (strcmp(name, gTracedModel) == 0);
    isTraced = gTracedMode;
    if (isTraced)
        display("// *** tracing model '%s'\n", name);
    ThreadContext* ctxt = new ThreadContext;
    this->ctxt = ctxt;
    ctxt->threadEntry = 0;

    // create the new stack
    ctxt->stack = new size_t[size_ThreadStack];
    ctxt->sp = ctxt->stack + size_ThreadStack;
    if (!ctxt->stack)
        throw new VError(verr_memOverflow, "can't allocate a model's stack");
    this->isTask = FALSE;
}

//-----------------------------------------------------------------------------
// Clear the model list, removing all threads from those models with tasks.

Model::~Model()
{
    ThreadContext* ctxt = this->ctxt;
    if (ctxt)
    {
        delete ctxt;
        this->ctxt = 0;
    }
}

//-----------------------------------------------------------------------------
// Remember model name for debugging.

void Model::setLastName(const char* name)
{
    gLastModelName = name;
}

//-----------------------------------------------------------------------------
// Set thread entry point and parameter.

void Model::setEntry(ThreadEntryPtr entry, void* param)
{
    ThreadContext* ctxt = this->ctxt;
    ctxt->threadEntry = (ThreadEntryPtr)this->evHandCode;
    ctxt->threadParam = param;
}

//-----------------------------------------------------------------------------
// Runtime error routine: print a message like printf and return to main
// thread to stop.

void Model::reportRunErr(VError* err)
{
    throw err;
}

//-----------------------------------------------------------------------------
// Go through task list and restart each task thread and reset its
//  variables.

void Model::initVars()
{
    resetRandSeed(0);
    gErrorSignal = 0;

    for (Model* m = gModelsList; m; m = m->next)
        m->init();
}

//-----------------------------------------------------------------------------
// Reset the task list.

void Model::initModels()
{
    gModelsList = 0;
    gTracedMode = FALSE;
}

//-----------------------------------------------------------------------------
// Reset a Model's variables and reset its thread back to the start.

void Model::init()
{
    ThreadContext* ctxt = this->ctxt;
    ctxt->sp = ctxt->stack + size_ThreadStack;
    ctxt->inst = this->instModule;

    if (this->isTask)
    {
        if (debugLevel(3))
            printf("@%2.3f model %s init\n", (float)gTick/gTicksNS,
                   this->desig);

        ctxt->pc = (PCode*)ctxt->threadEntry;

        cancelTimeout();
    }
    reset();
}

//-----------------------------------------------------------------------------
// Default model reset- do nothing.

void Model::reset()
{
}

void Model::start()
{
}

void* Model::startV(Model* realTask)
{
    return 0;
}

//-----------------------------------------------------------------------------
// Clear the model list, removing all threads from those models with tasks.

void Model::removeAll()
{
    for (Model* m = gModelsList; m; )
    {
        Model* nextModel = m->next;
        delete m;
        m = nextModel;
    }
    gModelsList = 0;
}

//-----------------------------------------------------------------------------
// Print current data stack contents for debugging.

void dumpDataStack(ThreadContext* ctxt, size_t* sp, size_t tos)
{
    size_t* endsp = ctxt->stack + size_ThreadStack - 1;
    if (sp <= endsp)
    {
        printf(";");
        printf(" %" PRIuPTR, (uintptr_t)tos);
        size_t* p;
        for (p = sp; p < endsp; p++)
            printf(" %" PRIuPTR, (uintptr_t)*p);
    }
    printf("\n");
}

//-----------------------------------------------------------------------------
//  Continue executing model's PCode.

void execCode(Model* model)
{
    ThreadContext* ctxt = model->ctxt;
    // if delay in progress and not done then don't restart task
    // (also when an 'initial' completes)
    if (model->isSleeping && gTick < ctxt->wakeTime)
        return;
    model->isWaiting = FALSE;
    model->isSleeping = FALSE;

    // P-code machine "registers"
    PCode*  pc = ctxt->pc;      // PCode program counter
    size_t* sp = ctxt->sp;      // integer stack pointer
    size_t  tos = ctxt->tos;    // top of stack
    char*   inst = ctxt->inst;  // module variables base
    size_t  arg[10];            // subroutine arguments
    size_t  result = 0;         // function return value
    bool    showPcode = debugLevel(3);
    int     i;
    size_t  temp;

    if (showPcode)
    {
        printf("%8d: execCode(%s pc=%p sp=%p stk=%p int=%p)\n",
            (int)gTick, model->desig, pc, sp, ctxt->stack, inst);
        if (pc->p.op >= first_dualOp)
            printf("%p: %-4s %" PRIuPTR " ", pc, kPCodeName[pc->p.op],
                   (uintptr_t)(pc+1)->n);
        else if (pc->p.op >= first_wordOp)
            printf("%p: %-4s %-8x ", pc, kPCodeName[pc->p.op], pc->p.w);
        else
            printf("%p: %-4s          ", pc, kPCodeName[pc->p.op]);
    }
    while (1)
    {
        size_t n = (pc+1)->n;
        switch (pc->p.op)
        {
            case p_nop:
                throw new VError(verr_bug, "NOP!");
                break;

            case p_bsr0:
                result = (*(Func0*)(n))();
                break;

            case p_bsr1:
                result = (*(Func1*)(n))(tos);
                tos = *sp++;
                break;

            case p_bsr2:
                result = (*(Func2*)(n))(sp[0], tos);
                tos = sp[1];
                sp += 2;
                break;

            case p_bsr3:
                result = (*(Func3*)(n))(sp[1], sp[0], tos);
                tos = sp[2];
                sp += 3;
                break;

            case p_bsr4:
                result = (*(Func4*)(n))(sp[2], sp[1], sp[0], tos);
                tos = sp[3];
                sp += 4;
                break;

            case p_bsr5:
                result = (*(Func5*)(n))(sp[3], sp[2], sp[1], sp[0], tos);
                tos = sp[4];
                sp += 5;
                break;

            case p_bsr:
                i = pc->p.nArgs - 1;
                if (i >= 0)
                    arg[i] = tos;
                for (i--; i >= 0; i--)
                    arg[i] = *sp++;
                result = (*(Func9*)(n))(arg[0], arg[1], arg[2], arg[3],
                                    arg[4], arg[5], arg[6], arg[7], arg[8]);
                if (pc->p.nArgs > 0)
                    tos = *sp++;
                break;

            case p_bsr1v1:
                result = (*(VariFunc1*)(n))(tos);
                tos = *sp++;
                break;

            case p_bsr2v1:
                result = (*(VariFunc1*)(n))(sp[0], tos);
                tos = sp[1];
                sp += 2;
                break;

            case p_bsr3v1:
                result = (*(VariFunc1*)(n))(sp[1], sp[0], tos);
                tos = sp[2];
                sp += 3;
                break;

            case p_bsr4v1:
                result = (*(VariFunc1*)(n))(sp[2], sp[1], sp[0], tos);
                tos = sp[3];
                sp += 4;
                break;

            case p_bsr5v1:
                result = (*(VariFunc1*)(n))(sp[3], sp[2], sp[1], sp[0], tos);
                tos = sp[4];
                sp += 5;
                break;

            case p_bsrv1:
                i = pc->p.nArgs - 1;
                if (i >= 0)
                    arg[i] = tos;
                for (i--; i >= 0; i--)
                    arg[i] = *sp++;
                result = (*(VariFunc1*)(n))(arg[0], arg[1], arg[2], arg[3],
                                    arg[4], arg[5], arg[6], arg[7], arg[8]);
                if (pc->p.nArgs > 0)
                    tos = *sp++;
                break;

            case p_bsr2v2:
                result = (*(VariFunc2*)(n))(sp[0], tos);
                tos = sp[1];
                sp += 2;
                break;

            case p_bsr3v2:
                result = (*(VariFunc2*)(n))(sp[1], sp[0], tos);
                tos = sp[2];
                sp += 3;
                break;

            case p_bsr4v2:
                result = (*(VariFunc2*)(n))(sp[2], sp[1], sp[0], tos);
                tos = sp[3];
                sp += 4;
                break;

            case p_bsr5v2:
                result = (*(VariFunc2*)(n))(sp[3], sp[2], sp[1], sp[0], tos);
                tos = sp[4];
                sp += 5;
                break;

            case p_bsrv2:
                i = pc->p.nArgs - 1;
                if (i >= 0)
                    arg[i] = tos;
                for (i--; i >= 0; i--)
                    arg[i] = *sp++;
                result = (*(VariFunc2*)(n))(arg[0], arg[1], arg[2], arg[3],
                                    arg[4], arg[5], arg[6], arg[7], arg[8]);
                if (pc->p.nArgs > 0)
                    tos = *sp++;
                break;

           case p_btsk:
                *(--sp) = (size_t)inst; // save current module variables base
                inst = (char*)tos;      // new extScopeRef on stack
                tos = (size_t)(pc + 2); // save return addr
                pc = (PCode*)(n);       // jump to verilog task subroutine
                goto nextOp;

            case p_leam:
                *(--sp) = tos;
                tos = (size_t)model;
                break;

            case p_leai:
                *(--sp) = tos;
                tos = (size_t)inst;
                break;

            case p_func:
                *(--sp) = tos;
                tos = result;
                break;

            case p_rts:
                pc = (PCode*)tos;       // pop return addr and module vars base
                inst = (char*)*sp++;
                tos = *sp++;
                goto nextOp;

            case p_wait:    // Suspend task until an input signal event occurs.
                model->isWaiting = TRUE;
                goto pauseThread;

            case p_del:     // Delay a task (sleep) for given number of ticks.
                if (tos > 0)
                {
                    addEvent((Tick)tos, model->modelSignal, LV_L, WAKEUP);
                    ctxt->wakeTime = gTick + tos - 1;
                    model->isSleeping = TRUE;
                    goto pauseThread;
                }
                break;

            case p_br:
                pc = (PCode*)n;
                goto nextOp;

            case p_beq:
                if (!tos)
                {
                    pc = (PCode*)n;
                    tos = *sp++;
                    goto nextOp;
                }
                tos = *sp++;
                break;

            case p_bne:
                if (tos)
                {
                    pc = (PCode*)n;
                    tos = *sp++;
                    goto nextOp;
                }
                tos = *sp++;
                break;

            case p_dup:
                *(--sp) = tos;
                break;

            case p_swap:
                temp = tos;
                tos = *sp;
                *sp = temp;
                break;

            case p_rot:
                temp = tos;
                tos = sp[1];
                sp[1] = sp[0];
                sp[0] = temp;
                break;

            case p_pick:
                *(--sp) = tos;
                tos = sp[pc->p.w];
                break;

            case p_drop:
                sp += pc->p.w;
                tos = sp[-1];
                break;

            case p_li:
                *(--sp) = tos;
                tos = n;
                break;

            case p_liw:
                *(--sp) = tos;
                tos = pc->p.w;
                break;

            case p_lea:
                *(--sp) = tos;
                tos = (size_t)(inst + n);
                break;

            case p_ld:
                *(--sp) = tos;
                tos = *(size_t*)(inst + n);
                break;

            case p_lds:
                *(--sp) = tos;
                tos = *(char*)(*(size_t*)(inst + pc->p.w));
                break;

            case p_ldx:
                tos = *(size_t*)(tos + n);
                break;

            case p_ldbx:
                tos = *(char*)(tos + n);
                break;

            case p_st:
                *(size_t*)(inst + n) = tos;
                tos = *sp++;
                break;

            case p_stb:
                *(char*)(inst + n) = tos;
                tos = *sp++;
                break;

            case p_stx:
                *(size_t*)(tos + n) = *sp++;
                tos = *sp++;
                break;

            case p_addi:
                tos += n;
                break;

            case p_andi:
                tos &= n;
                break;

            case p_add:
                tos += *sp++;
                break;

            case p_sub:
                tos = *sp++ - tos;
                break;

            case p_mul:
                tos *= *sp++;
                break;

            case p_div:
                if (tos == 0)
                    throw new VError(verr_illegal, "Divide by zero");
                tos = *sp++ / tos;
                break;

            case p_and:
                tos &= *sp++;
                break;

            case p_or:
                tos |= *sp++;
                break;

            case p_xor:
                tos ^= *sp++;
                break;

            case p_sla:
                tos = *sp++ << tos;
                break;

            case p_sra:
                tos = *sp++ >> tos;
                break;

            case p_eq:
                tos = *sp++ == tos;
                break;

            case p_ne:
                tos = *sp++ != tos;
                break;

            case p_gt:
                tos = *sp++ > tos;
                break;

            case p_le:
                tos = *sp++ <= tos;
                break;

            case p_lt:
                tos = *sp++ < tos;
                break;

            case p_ge:
                tos = *sp++ >= tos;
                break;

            case p_sbop:
                // 2-operand scalar operation using table at n
                tos = ((Level*)n)[*sp++ + (tos << 4)];
                break;

            case p_not:
                tos = !tos;
                break;

            case p_nots:
                tos = funcTable.INVERTtable[tos];
                break;

            case p_com:
                // shift left & right by count of leading zeros
                // (except LSB)
                // to clear them after complimenting other bits
                i = __builtin_clzll(tos | 1);
                tos = ((tos ^ 0xffffffff) << i) >> i;
                break;

            case p_neg:
                tos = -tos;
                break;

            case p_cvis:
                // turn a 1 into a 7 (LV_H)
                tos &= 1;
                tos = (tos << 2) | (tos << 1) | tos;
                break;

            case p_end:
            {
                size_t spRem = ctxt->stack + size_ThreadStack - sp;
                if (spRem)
                {
                    dumpDataStack(ctxt, sp, tos);
                    throw new VError(verr_bug,
                        "BUG: %d items left on stack at end of %s pcode at %p",
                        spRem, model->desig, pc);
                }
                // done: put task to sleep forever
                ctxt->wakeTime = 0x7fffffff;
                model->isSleeping = TRUE;
                goto pauseThread;
            }
            default:
                printf("*** unknown PCode op %d\n", pc->p.op);
                break;
        }

        // advance to next PCode
        if (pc->p.op >= first_dualOp)
            pc += 2;
        else
            pc++;

      nextOp:
        if (showPcode)
        {
            dumpDataStack(ctxt, sp, tos);
            if (pc->p.op >= first_dualOp)
            {
                size_t n = (pc+1)->n;
                if (n > 1000)
                    printf("%p: %-4s %-9" PRIxPTR " ", pc,
                           kPCodeName[pc->p.op], (uintptr_t)n);
                else
                    printf("%p: %-4s %-9" PRIuPTR " ", pc,
                           kPCodeName[pc->p.op], (uintptr_t)n);
            }
            else if (pc->p.op >= first_wordOp)
                printf("%p: %-4s %-9x ", pc, kPCodeName[pc->p.op], pc->p.w);
            else
                printf("%p: %-4s           ", pc, kPCodeName[pc->p.op]);
        }
    }
  pauseThread:
    if (showPcode)
        dumpDataStack(ctxt, sp, tos);

    // advance to next PCode
    if (pc->p.op >= first_dualOp)
        pc += 2;
    else
        pc++;
    ctxt->pc = pc;
    ctxt->sp = sp;
    ctxt->tos = tos;
    ctxt->inst = inst;
}

//-----------------------------------------------------------------------------
// Execute task code.
// Note that the task code must get this task address var before
// the first call to wait, etc. when task is swapped.

void Model::executeHandCode()
{
    ThreadContext* ctxt = this->ctxt;
    ctxt->pc = (PCode*)this->evHandCode;
    ctxt->inst = this->instModule;
    resetTask();
    execCode(this);
}

//-----------------------------------------------------------------------------
//  Evaluate a change of an input signal.

void Model::eval(Signal* eventSig)
{
    // set model's trigger-event signal
    this->triggerSignal = eventSig;

    if (this->isTask)
    {
        // and switch to task execution thread
        execCode(this);
    }
    else
        // execute model's event handler
        handleEvent();
}

//-----------------------------------------------------------------------------
//  Evaluate a change of an input signal or a wakeup event.

void Model::eval(Level level)
{
    if (!this->isTask)              // if an 'initial' task has completed
        return;                     //   don't restart task
    triggerSignal = 0;              // clear task's trigger signal
                                    // and switch to task execution thread
    if (debugLevel(3))
    {
        printf("eval\n");
        fflush(stdout);
    }
    execCode(this);
}

//-----------------------------------------------------------------------------
// Standard task reset.

void Model::resetTask()
{
    isSleeping = FALSE;
    isWaiting = FALSE;
}

//-----------------------------------------------------------------------------
// Master-task event handler. Does nothing but allow wait() to continue.

void Model::handleEvent()
{
}

//-----------------------------------------------------------------------------
// Switch back to the main thread.

void Model::returnToMainTask()
{
    if (debugLevel(3))
    {
        printf("returnToMainTask\n");
        fflush(stdout);
    }

    // check for a timeout upon returning
    if (this->timeoutsMode && this->timeoutTime)
    {
        if (gTick >= this->timeoutTime) // if timeout has expired
        {
            flagError(timeoutSignal, "TO", FALSE,
                "Timeout waiting for %s since %2.3f ns", this->timeoutMsg,
                (float)(now() - this->timeoutDuration)/gTicksNS);
            this->timeoutTime = 0;
            throw merr_retry;       // escape and reset sequencer
        }
        else                        // not expired yet: wait some more
        {
            Tick dt = this->timeoutTime - gTick;
            if (dt >= (Tick)gEventHistLen)
                dt = gEventHistLen - 1;
            // 'LV_H' means TIMEOUT event
            addEvent(dt, this->modelSignal, LV_H, WAKEUP);
        }
    }
}

//-----------------------------------------------------------------------------
// Switch back to the main thread and throw an error.

void Model::switchToMainTask()
{
    //if (debugLevel(3))
    {
        printf("switchToMainTask\n");
        fflush(stdout);
    }
}

//-----------------------------------------------------------------------------
// Initial task thread code: start task and restart when reset.

void* Model::startThread()
{
    for (;;)
    {
        try
        {
            start();
        }
        catch (MainErrorCode errNo)
        {
            if (errNo < 0)      // a real error returns to event loop
            {
                switchToMainTask();
                return (void*)errNo;
            }
        }                       // else restart sequencer upon catching Reset
        returnToMainTask();
    }
    return (void*)0;                // for threadProc return value
}

//-----------------------------------------------------------------------------
// Start executing compiled task code.

void Model::startVReal()
{
    try
    {
        // execute task code
        // note that the task code must get this task address var before
        // the first call to wait, etc. when task is swapped.
        executeHandCode();

        // an 'initial' block has finished. Hang the thread up here
        while (1)
            wait();
    }
    catch (MainErrorCode errNo)
    {
        // a real error returns to event loop
        // this->error is for Model::eval() after setjmp(gMainContext)
        switchToMainTask(); 
    }
}

//-----------------------------------------------------------------------------
// TRUE if is current trigger signal.

size_t Model::isTrigger(Signal* signal)
{
    bool answer = (triggerSignal == signal);
    return answer;
}

//-----------------------------------------------------------------------------
// TRUE if trigger signal is in this bus.

size_t Model::isTriggerBus(SignalVec* bus, size_t n)
{
    for (int i = 0; i < (int)n; i++)
        if (triggerSignal == bus[i])
            return TRUE;
    return FALSE;
}

//-----------------------------------------------------------------------------
// TRUE if current trigger signal is rising.

size_t Model::justRisen(Signal* signal)
{
    if (debugLevel(3) && triggerSignal)
        printf("@%2.3f %s justRisen if (%d && %d)\n",
               (float)gTick/gTicksNS, triggerSignal->name,
               triggerSignal == signal, high(signal));
    return (size_t)(triggerSignal == signal && high(signal));
}

//-----------------------------------------------------------------------------
// TRUE if current trigger signal is falling.

size_t Model::justFallen(Signal* signal)
{
    if (debugLevel(3) && triggerSignal)
        printf("@%2.3f %s justFallen if (%d && %d)\n",
               (float)gTick/gTicksNS, triggerSignal->name,
               triggerSignal == signal, low(signal));
    return (size_t)(triggerSignal == signal && low(signal));
}

//-----------------------------------------------------------------------------
// Add an event to the time line at gTick + dt, or return 0 if no more space.
// Catches any thrown errors and jumps around compiled verilog code since
// exception handler doesn't handle that.
//
// The exception code algorithm is rather complex, but I think it's failing
// because the PVSim-compiled Verilog code layer doesn't include the needed
// context tables that the C++ compiler builds for each file. So when the
// algorithm is unwinding the stack, it can't find the Verilog routine in
// any of its tables and gives up.

void Model::addEventV(Tick dt, Signal* signal, Level level, char eventType)
{
    try
    {
        addEvent(dt, signal, level, eventType);
    } catch(VError* err)
    {
        reportRunErr(err);
    }
}

//-----------------------------------------------------------------------------
// Add a pair of events to the time line at gTick + dtMin and + dtMax for
// an ambiguous-time edge event.
// Catches any thrown errors and jumps around compiled verilog code.

void Model::addMinMaxEventV(Tick dtMin, Tick dtMax, Signal* signal,
                            Level level)
{
    try
    {
        addMinMaxEvent(dtMin, dtMax, signal, level);
    } catch(VError* err)
    {
        reportRunErr(err);
    }
}

//-----------------------------------------------------------------------------
// Add an indexed event: post an event for bit i of sigVec, after checking
// that the bit is in range [0,iMax].
// Catches any thrown errors and jumps around compiled verilog code.

void Model::addIndEventV(SignalVec* sigVec, int i, int iMax,
                         Tick t, Level level, char eventType)
{
    try
    {
        if (i < 0 || i > iMax)
            return;

        addEvent(t, sigVec[i], level, eventType);
    } catch(VError* err)
    {
        reportRunErr(err);
    }
}

//-----------------------------------------------------------------------------
// Add an indexed min-max event: post an event for bit i of sigVec, after
// checking that the bit is in range [0,iMax].
// Catches any thrown errors and jumps around compiled verilog code.

void Model::addIndMinMaxEventV(SignalVec* sigVec, int i, int iMax,
                                Tick dtMin, Tick dtMax, Level level)
{
    try
    {
        if (i < 0 || i > iMax)
            return;

        addMinMaxEvent(dtMin, dtMax, sigVec[i], level);
    } catch(VError* err)
    {
        reportRunErr(err);
    }
}

//-----------------------------------------------------------------------------
// Post a Vector signal event: post an event for each bit of an LVector,
// starting at bit i in sigVec, after checking bits are in range [0,iMax].
// Catches any thrown errors and jumps around compiled verilog code.

void Model::postIndVectorV(SignalVec* sigVec, int i, int iMax, Tick dt,
                           Level* levVec, int nBits)
{
    SignalVec* sv = sigVec;
    try
    {
        if (i < 0 || i > iMax-nBits+1)
            return;

        sv += i;
        for ( ; nBits > 0; nBits--)
        {
            addEvent(dt, *sv, *levVec, CLEAN);
            sv++;
            levVec++;
        }
    } catch(VError* err)
    {
        Vector* vec = Scope::global->findVector(sigVec);
        char* vRep = vec ? vec->repr(sigVec + i, nBits) : newString("?");
        reportRunErr(new VError(err->code,
           "%s\n// in postIndVectorV(vec@%p=%s bit=%d max=%d dt=%d nBits=%d)\n",
           err->message, sigVec, vRep, i, iMax, dt, nBits));
    }
}

//-----------------------------------------------------------------------------
// Post a min-max Vector signal event: post a pair of events for each bit
// of an LVector, starting at bit i in sigVec, after checking bits are in
// range [0,iMax].
// Catches any thrown errors and jumps around compiled verilog code.

void Model::postIndMinMaxVectorV(SignalVec* sigVec, int i, int iMax,
                                 Tick dtMin, Tick dtMax, Level* levVec,
                                 int nBits)
{
    SignalVec* sv = sigVec;
    try
    {
        if (i < 0 || i > iMax-nBits+1)
            return;

        sv += i;
        for ( ; nBits > 0; nBits--)
        {
            addMinMaxEvent(dtMin, dtMax, *sv, *levVec);
            sv++;
            levVec++;
        }
    } catch(VError* err)
    {
        Vector* vec = Scope::global->findVector(sigVec);
        char* vRep = vec ? vec->repr(sigVec + i, nBits) : newString("?");
        reportRunErr(new VError(err->code,
           "%s\n// in postIndMinMaxVectorV(vec@%p=%s bit=%d max=%d nBits=%d)\n",
           err->message, sigVec, vRep, i, iMax, nBits));
    }
}

//-----------------------------------------------------------------------------
// Suspend task until an input signal event occurs.

// !!! called from VTask::startV(): needs to exit that?

void Model::wait()
{
    if (debugLevel(3))
        printf("@%2.3f model %s: wait()\n",
                (float)gTick/gTicksNS, this->desig);
}

//-----------------------------------------------------------------------------
// Set up for a timeout at time now + dt, which will be flagged the given
//  signal with the a "Timeout waiting for <errMsg>" error message.

void Model::setTimeout(Tick dt, Signal* errSig, char* errMsg)
{
    timeoutsMode = (dt > 0);
    if (timeoutsMode)
    {
        timeoutTime = gTick + dt;
        timeoutSignal = errSig;
        timeoutMsg = errMsg;
        timeoutDuration = dt;
        if (dt >= (Tick)gEventHistLen)
            dt = gEventHistLen - 1;
        addEvent(dt, modelSignal, LV_H, WAKEUP);   // 'LV_H' means TIMEOUT event
    }
}

//-----------------------------------------------------------------------------
// Cancel the pending timeout.

void Model::cancelTimeout()
{
    timeoutTime = 0;
}

//-----------------------------------------------------------------------------
// Return current simulated time.

Tick now()
{
    return gTick;
}

//-----------------------------------------------------------------------------
// Draw a message in the timing window, on the given signal.  Standard
//  printf-type arguments.

void drawf(Signal* signal, const char* fmt, ...)
{
    va_list ap;
    char    s[200];

    if (gNextString+max_nameLen < gStrings+gMaxStringSpace)
    {
        va_start(ap, fmt);
        vsnprintf(s, max_nameLen-1, fmt, ap);
        va_end(ap);
        attachSignalText(signal, s);
    }
}

//-----------------------------------------------------------------------------
// Draw a message in the timing window, on the given signal.  Standard
//  printf-type arguments.  Message goes in front of last event.

void drawfInFront(Signal* signal, const char* fmt, ...)
{
    va_list ap;
    char    s[200];

    if (gNextString+max_nameLen < gStrings+gMaxStringSpace)
    {
        va_start(ap, fmt);
        vsnprintf(s, max_nameLen-1, fmt, ap);
        va_end(ap);
        attachSignalText(signal, s, TRUE);
    }
}

//-----------------------------------------------------------------------------
// $flagError(signal, errName, inFront, format, ...):
// Flag an error by setting the ErrFlag signal and drawing the errName
// on the given signal, and displaying a full description
// of the error, given in printf-style format.

void flagError(Signal* signal, const char* errName, int inFront,
               const char* format, ...)
{
    // create full message for printf-style format and args
    va_list ap;
    va_start(ap, format);
    char msg[max_messageLen];
    if (!signal || !errName || !format)
        strncpy(msg, "missing args to flagError()", max_messageLen-1);
    else
        vsnprintf(msg, max_messageLen-1, format, ap);
    va_end(ap);

    // draw message on signal's waveform
    if (inFront)
        drawfInFront(signal, "#r%s", errName);
    else
        drawf(signal, "#r%s", errName);

    Symbol* errFlagSym = lookup("ErrFlag");
    if (errFlagSym)
    {
        Signal* errFlag = (Signal*)errFlagSym->arg;
        if (!gErrorSignal)
        {
            gErrorSignal = signal;
            gErrorTickE = gTick;
            gErrorTickB = gTick;
            gStopSignal = errFlag;
            addEvent(0, errFlag, LV_H, CLEAN);
            drawf(errFlag, "#r%s", errName);
            if (gTick/gTicksNS > 1000000)
            {
                char date[100];         // print time & date for int runs
                time_t now = time(NULL);
                strcpy(date, asctime(localtime(&now)));
                date[strlen(date) - 1] = '\0';
                display("// *** (%s)\n", date);
            }
            display("// *** Error at ");
            if (gTick/gTicksNS > 1000000) // int run times are in milliseconds
                display("%7.6f ms ", ((float)gTick/gTicksNS)/1000000.);
            else
                display("%2.3f ns ", (float)gTick/gTicksNS);
            display(" on %s:\n", signal->name);
            gErrorTickB -= 2*gTicksNS;
            display("// ***       %s: %s\n", errName, msg);
        }
    }
    gFlaggedErrCount++;
}

//-----------------------------------------------------------------------------
// Test PCode.

void Model::test()
{
    PCode* testCode = pc;

    codeLitInt(1234);
    codeOpI(p_addi, 2);
    //codeWait();
    size_t* ifjmp = codeIf();
    codeLitInt((size_t)"testing %d\n");
    codeLitInt(1);
    codeCall((Subr*)&printf, 2, "printf", 1);
    setJmpToHere(ifjmp);
    codeLitInt(1234);
    codeLitInt(1234);
    codeOp(p_add);
    dropData();
    codeOpI(p_st, 4);
    dropData();
    codeLoadInt(4);
    codeLoadInt(4);
    codeOp(p_add);
    codeSampleScalar();
    codeOp(p_end);

    char* inst = new char(100);
    Model* m = new Model("test", (EvHandCodePtr)testCode, inst);
    Signal* s = addSignal("test", LV_H);
    m->modelSignal = s;
    s->is = C_MODEL + REGISTERED;
    s->model = m;
    s->srcLoc = 0;
    m->isTask = TRUE;
    m->init();
    m->executeHandCode();
}
