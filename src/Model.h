// ****************************************************************************
//
//          PVSim Verilog Simulator Model/Task Base Class Interface
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

#include <setjmp.h>
#include <signal.h>
#include "Utils.h"
#include "PSignal.h"
#include "SimPalSrc.h"

typedef Signal* SignalVec;
struct ThreadContext;

// EvHandCodePtr is a pointer to compiled event-handler (evalulation) code
typedef void (*EvHandCodePtr)(Model* vModel, char* instModule);

const int size_ThreadStack = 100000;
typedef void* (*ThreadEntryPtr)(void* arg);


// A Model is an instantiation of an EvHand event handler for an 'always',
// 'assign', or 'task'. It provides the handler with a context for trigger
// events and error messages. All types but 'assign' run as independent threads.

class Model: SimObject
{
protected:
    char*           instModule;

    Model*          next;           // next model in list of all models
    const char*     className;      // name string for this model class
    Model*          model;          // ptr Model base class for higher classes
    const char*     desig;          // model's designator for unique intern sigs
    Signal*         modelSignal;    // dummy signal associated with model
    Signal*         triggerSignal;  // last input event signal
    bool            isTask;         // running as separate thread w/wait(), etc.
    bool            isTraced;       // if this particular model is being traced
    bool            isSleeping;
    bool            isWaiting;
    bool            timeoutsMode;   // TRUE to enable timeout checking

private:
    ThreadContext*  ctxt;           // private thread context
    VError*         error;          // associated error
    EvHandCodePtr   evHandCode;
    Tick            timeoutTime;    // if >0, tick at which to time out at
    Signal*         timeoutSignal;  // signal to report timeout on
    const char*     timeoutMsg;     // timeout error message
    long            timeoutDuration; // duration to report upon error

public:
    void            eval(Signal* eventSig);
    void            eval(Level level);
    void            setModelSignal(Signal* signal)  { modelSignal = signal; }
    static void     setLastName(const char* name);
    inline Signal*  modelSig() { return modelSignal; }
    inline const char*  designator() { return desig; }
    static void     removeAll();
    static void     initVars();
    static void     initModels();       // create a thread pool and all models
    void            addEventV(Tick dt, Signal* signal, Level level,
                              char eventType);
    void            addMinMaxEventV(Tick dtMin, Tick dtMax, Signal* signal,
                        Level level);
    void            addIndEventV(SignalVec* sigVec, int i, int iMax,
                        Tick t, Level level, char eventType);
    void            addIndMinMaxEventV(SignalVec* sigVec, int i, int iMax,
                        Tick dtMin, Tick dtMax, Level level);
    void            postIndVectorV(SignalVec* sigVec, int i, int iMax, Tick dt,
                        Level* levVec, int n);
    void            postIndMinMaxVectorV(SignalVec* sigVec, int i, int iMax,
                        Tick dtMin, Tick dtMax, Level* levVec, int nBits);
    void            wait();                 // wait for input event
    void            delay(int t);           // delay t ticks-- use ns(n) for ns
    static void     test();
    virtual void*   startV(Model* realTask);
protected:
                    Model(const char* name, EvHandCodePtr evHandCode,
                          char* instModule);
                    ~Model();                   // delete model
    void            setEntry(ThreadEntryPtr startVTask, void* param);
    void            executeHandCode();
    void            reportRunErr(VError* err);
    size_t          isTrigger(Signal* signal);
    size_t          isTriggerBus(SignalVec* bus, size_t n);
    size_t          justRisen(Signal* signal);
    size_t          justFallen(Signal* signal);
    void            init();     // (re)create model
    void            resetTask();      // reset task vars; separate from reset()
    void            startVReal();

private:
    virtual void    reset();    // reset local task vars at time 0
    virtual void    start();        // start or restart the task sequence
    void*           startThread();
    virtual void    handleEvent();              // event handler
    void            setTimeout(Tick dt, Signal* errSig, char* errMsg);
    void            cancelTimeout();
    void            switchToMainTask(); // switch back to main thread
    void            returnToMainTask();

    friend void yieldToInit(Model* model) __attribute__((noinline));
    friend void setStackPtr(Model* model, char* newSP)
                    __attribute__((noinline));
    friend void codeStatement();
    friend class VLModule;
    friend class EvHand;
    friend void execCode(Model* model);
};

inline Level level(bool x) { return (x ? LV_H : LV_L); }
inline bool high(Signal* w) { return (w->level == LV_H) || (w->level == LV_W); }
inline bool low(Signal* w) { return (w->level == LV_L) || (w->level == LV_V); }
inline Level inv(Signal* w) { return (low(w) ? LV_H : LV_L); }

void setDependency(Signal* dependent, Signal* signal);
void drawf(Signal* signal, const char* f, ...);
void drawfInFront(Signal* signal, const char* f, ...);
void flagError(Signal* signal, const char* errName, int inFront,
               const char* format, ...);
Tick now();                 // return current simulation time
