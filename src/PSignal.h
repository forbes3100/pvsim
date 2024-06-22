// ****************************************************************************
//
//          PVSim Verilog Simulator Interface and Signal Class
//
// Copyright (C) 2004,2005,2012 Scott Forbes
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

#ifndef EXTENSION
#define WRITE_EVENTS // define to write event list to .events file
#endif

#include "Utils.h"
#include "Src.h"

#define EVENT_HISTORY
#define SHOW_WARNING                // define to display undefined outputs

#ifdef WRITE_EVENTS
#include <stdio.h>
#endif

enum EqnOp { Base_OP = 0, AND_OP, LOAD_OP, SAVEOR_OP,
        TRUE_OP, FALSE_OP, NEWTS_OP, ENABLE_OP, STORE_OP, NUM_OPS};
// unused:
// OR_OP, MLOR_OP, MHOR_OP, TG_OP, OCDRIVER_OP, ALOAD_OP, AAND_OP, NSTORE_OP, 

typedef union
{
    short   opcode;
    long    operand;
} EqnItem;

enum Level      // signal levels
{
    LV_L,       // low
    LV_S,       // stable
    LV_X,       // changing
    LV_R,       // rising
    LV_F,       // falling
    LV_U,       // low-Z-low
    LV_D,       // high-Z-high
    LV_H,       // high
    LV_Z,       // high impedance
    LV_C,       // tri-state ('bus') conflict
    LV_V,       // weak low
    LV_W        // weak high
};

struct FuncTable
{
    Level   ANDtable[12][16];
    Level   ORtable[12][16];
    Level   XORtable[12][16];
    Level   ENABLEtable[12][16];
    Level   TStable[12][16];
    Level   MINMAXtable[12][16];
    Level   BSWALLOWtable[12];
    Level   STABLEtable[12];
    Level   INVERTtable[12];
    Level   SAMPLEtable[12];
#if 0
    Level   MUXORHtable[12][16];
    Level   MUXORLtable[12][16];
    Level   TGtable[12][16];
    Level   OCtable[12][16];
    Level   ESWALLOWtable[12];
#endif
};

extern const FuncTable funcTable;

const int   max_signals =       9000;   // default
const int   ns_runTime =       10000;

typedef long long   DLong;
//typedef DLong     Tick;
typedef unsigned long Tick;

struct Signal;
class Event;
class Model;

// One node in a linked-list of signals

class SigNode : SimObject
{
    Signal*     mSignal;
    SigNode*    mNext;
public:
                SigNode(Signal* signal, SigNode* next)
                        { mSignal = signal; mNext = next; }
    Signal*     signal()        { return mSignal; }
    SigNode*    next()          { return mNext; }
};

// 'signal is' flags bits:
const int REGISTERED =      0x01;
const int DISPLAYED =       0x02;
const int TRI_STATE =       0x04;
const int TRACED =          0x08;
const int INTERNAL =        0x10;   // _i0, _e0, etc. Affects initialization.
const int C_MODEL =         0x40;

// signal modes
const int MODE_WENT_META =  0x40;   // updateDependents: adjusts preLevel

// signal busOpt bus-display option bits:
const int DISP_BUS =        0x01;
const int DISP_INVERTED =   0x02;
const int DISP_BUS_BIT =    0x04;
const int DISP_STATE =      0x08;

struct Signal8
{
    char    level[8];       // (*8) signal current level
    char    nlevel[8];      // (*8) signal current level, inverted
    char    preLevel[8];    // (*8) signal's (latched) pre-delay level
};

// 'signals' structure: (size is a multiple of 8; note the grouping:)

struct Signal
{
    Level   level:8;        // (*8) signal current level
    Level   lastLevel:8;    // level at last rise, fall
    Level   initLevel:8;    // signal initialization level
    Level   clockPol:8;     // registered signal's clock level after edge
    Level   inLevel:8;      // registered signal's last input level
    Level   floatLevel;     // tri-state signal's pull or float to level
    char    is;             // simulator flags
    char    mode;           // compiler signal-mode flags

    const char* name;       // signal name
    EqnItem* evalCode;      // evaluation code

    Level   nlevel:8;       // (*8) signal current level, inverted
    char    ambDepth;       // ambiguity depth
    short   minTime;        // signal's delay min time
    short   maxTime;        // signal's delay max time
    short   setupTime;      // registered signal's setup time

    Level   preLevel:8;     // (*8) signal's (latched) pre-delay level
    Level   aMetaLevel:8;   // after metastable period level
    Level   initDspLevel:8; // level at start of displayed portion
    char    randDlyCnt;     // random delay counter
    short   holdTime;       // registered signal's hold time
    short   metaTime;       // registered signal's metastable time

    Tick    lastTime;       // time of signal's last event
    Tick    lastInTime;     // time of signal's last input event
    Tick    lastClkTm;      // time of signal's clock's last event
    Event*  firstDispEvt;   // signal's displayed events linked-list
    Event*  scrollEvt;      // first event in scrolled window
    Event*  lastEvtPosted;  // last event posted (end of linked list)

    char    busOpt;         // bus display options
    char    busWidth;       // number of following signals to display as a bus
    char    busBitNo;       // bit-number in a bus: must have MSB first
    char    srcNo;          // source number for tri-state signals
    Event*  dispEvent;      // current event being drawn for bit in bus

    SigNode* inputsList;    // linked-list of signal's input signals
    SigNode* dependList;    // linked-list of signals affected by this one
    Signal* clock;          // signal's clock signal, if registered
    Signal* enable;         // internal (_i0) signal's enable (_e0) signal
    Signal* set;            // signal's ASET signal, if reg, or TG's otherTSSig
    Signal* reset;          // signal's ARESET signal, if registered
    Signal* clockEn;        // signal's CE signal, if registered
    Model*  model;          // C-model object model if C-model
    Event*  floatList;      // list of signal's pending float events
    short   RCminTime;      // pulled-up signal's min RC time constant
    short   RCmaxTime;      // pulled-up signal's max RC time constant
    Token*  srcLoc;         // location in source of signal's definition
    const char* srcLocObjName; // name of object at srcLoc (may be parent's)
    Signal* randTrkSig;     // signal to track random delay counter of, or zero
}  __attribute__((aligned(8)));

// 'event is' flags bits
const int CLEAN =           0x00;   // no flags: clean edge
const int STARTING_AMBIG =  0x01;   // start of rising, falling
const int ENDING_AMBIG =    0x02;   // end of rising, falling
const int SOME_AMBIG =      0x03;   // ambiguity mask
const int FLOATING =        0x04;   // floating or pulling event
const int WAKEUP =          0x08;   // task wakeup event
const int DELETED_FLOAT =   0x10;   // deleted float event
const int ATTACHED_TEXT =   0x20;   // attached-text event
const int FREE =            0x40;   // deleted event

// 'events' structure: ( should be even long length for speed)
const int MAX_ATT_TEXT_LEN = 16;

class Event
{
public:
    Level   level:8;        // signal's new level
    char    is;             // event flags
    Tick    tick;           // event time

    Signal* signal;         // event signal
    Event*  next;           // next event in list for this time
    Event*  nextFloat;      // next float event in list
    Event*  prevInSignal;   // previous event for this signal
    Event*  nextInSignal;   // next event for this signal

    char    attText[MAX_ATT_TEXT_LEN]; // optional attached-text
#ifdef EVENT_HISTORY
    Event*  cause;          // event that caused this event
#endif
    void    insertS(Signal* sig, Event* after)
            {
                if (after)
                {
                    Event* next = after->nextInSignal;
                    this->nextInSignal = next;
                    this->prevInSignal = after;
                    after->nextInSignal = this;
                    if (next)
                        next->prevInSignal = this;
                    else
                        sig->lastEvtPosted = this;
                    if (sig->firstDispEvt == 0)
                        sig->firstDispEvt = this;
                    if (sig->is & TRACED)
                        printfEvt("   insertS %s after\n", sig->name);
                }
                else
                {
                    // after nothing: insert at beginning
                    this->nextInSignal = sig->firstDispEvt;
                    this->prevInSignal = 0;
                    if (sig->firstDispEvt)
                        sig->firstDispEvt->prevInSignal = this;
                    sig->firstDispEvt = this;
                    if (!sig->lastEvtPosted)
                        sig->lastEvtPosted = this;
                    if (sig->is & TRACED)
                        printfEvt("   insertS %s at beginning\n", sig->name);
                }
            }
    void    removeFromSignal()
            {
                Signal* sig = this->signal;
                Event* next = this->nextInSignal;
                Event* prev = this->prevInSignal;
                if (sig->firstDispEvt == this)
                {
                    sig->firstDispEvt = next;
                    if (sig->is & TRACED)
                        display("remove %s @%ld -> %ld\n",
                                sig->name, this->tick, next->tick);
                }
                if (sig->lastEvtPosted == this)
                    sig->lastEvtPosted = prev;
                if (next)
                    next->prevInSignal = prev;
                if (prev)
                    prev->nextInSignal = next;
            }
};

class OpenFile
{
    FILE*   file;
    OpenFile* next;
public:
            OpenFile(FILE* file, class OpenFile* next)
                { this->file = file; this->next = next; }
    friend void simulate();
};

// -------- global variables --------

extern size_t   gMaxSignals;    // storage limits, from initApplication
extern size_t   gMaxEvents;
extern Signal*  gSignals;       // gSignals array

extern Signal*  gNextSignal;    // next available signal table offset
extern int      gTicksNS;       // number of ns per simulation step
extern Tick     gTick;          // current simulation time
extern Tick     gPrevEvtTick;   // previous tick written to event file
extern Tick     gTEnd;          // sim & display end tick, adj by gStopSignal
extern const char*  gPSVersion; // version strings
extern const char*  gPSDate;

extern Signal*  gBarSignal;     // if <>0, signal who's rising edge makes bar
extern Signal*  gErrorSignal;   // the signal that caused the first error
extern Tick     gErrorTickB;    // start tick of the first error selection
extern Tick     gErrorTickE;    // end tick of the first error selection
extern Tick     gDispTStart;    // display start tick
extern int      gTimeScaleExp;  // absolute time scale, in exponent form
extern double   gTimeScale;     // ticks per timescale unit
extern int      gTimeRoundExp;  // time intern rounding scale (unused for now)
extern int      gTimeDispPrec;  // time display precision: number of digits
extern const char*  gTimeSuffixStr; // time display suffix string
extern int      gTimeMinFieldWid; // time display mimimum field width

#ifdef WRITE_EVENTS
extern Event*   gCurEvent;
extern const char*  gEvFileName;
extern FILE*    gEvFile;
#else
extern PyObject* gPySignalClass; // Python 'Signal' class
extern PyObject* gSigs;         // Python list of all displayed signals
#endif
extern OpenFile* gOpenFiles;        // list of open data files
extern const char* gLevelNames;
extern size_t   gEventHistLen;  // length of 1/2 time line FIFO
extern Event**  timeLine;       // time line: array of event lists per tick

// -------- global function prototypes --------

// Simulator.cc
void newSimulation();
Event* addEvent(Tick t, Signal* signal, Level level, char eventType);
void addMinMaxEvent(Tick    dtMin,
                    Tick    dtMax,
                    Signal* signal,
                    Level   level);
void removeEvent(Event** link, Event* event);
void attachSignalText(Signal* signal,
                      char* s,
                      bool inFront = FALSE,
                      int ticksAllotted = 600);
void simulate();

// EvalSignal.cc
Level evalSignalCode(Signal* signal, Signal* sigs, const FuncTable* func);
Level warnUnused(Signal* signal);

inline Level evalSignal(Signal* sig)
{
    if (sig->evalCode)
        return evalSignalCode(sig, gSignals, &funcTable);
    else
        return warnUnused(sig);
}

#ifdef EXTENSION
// PVSimExtension.c
void newSignalPy(Signal* signal, Level newLevel, bool isBus = FALSE,
                 int lbit = 0, int rbit = 0);
void addEventPy(Signal* sig, Tick tick, PyObject* val);
#endif

