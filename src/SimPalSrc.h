// ****************************************************************************
//
//          PVSim Verilog Simulator Signal Compiler Interface
//
// Copyright (C) 2004,2005 Scott Forbes
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
#include "PSignal.h"
#include "Src.h"

struct Symbol
{
    const char* name;       // symbol name, mixed case
    size_t      arg;        // argument to pass to definition, if any
    Symbol*     prevDec;    // pointer to previous declared symbol
    Symbol*     next;       // pointer to next symbol in hash list
};

// Encapsulates a Symbol object in a way that will be deleted with deleteAll

class SymObj: SimObject
{
public:
    Symbol symbol;

        SymObj();
};

// -------- storage space pointers --------

extern size_t   gMaxSignals;            // storage limits, from initApplication
extern Space    gSignalSpace;

// -------- global variables --------

extern short    gNSignals;          // number of signals created
extern int      gTicksNS;           // scaled-time ticks per NS
extern int      gNSStart;           // sim start in NS
extern int      gNSDuration;        // desired simulation run in NS
extern Signal*  gBreakSignal;
extern Signal*  gStopSignal;
extern bool     gSignalDisplayOn;   // set if following signals to be displayed
extern unsigned int gNameLenLimit;  // warning given for names longer than this
extern const char*  gTracedModel;   // designator of model to be traced

extern char     gDispBusWidth;      // width of current display-bus
extern char     gDispBusBitNo;      // next bit to be assigned for a
                                    //  display-bus, or -1 if none.
extern Symbol** gHashTable;
extern Symbol*  gLastSymbol;        // last symbol parsed
extern Symbol*  gLastNewSymbol;     // last symbol declared
    
// -------- global function prototypes --------

int hash(const char* s);
Symbol* lookup(const char* name, bool caseSensitive = FALSE);
Symbol* newSymbol(const char* name, int arg);
void setDependency(Signal* dependent, Signal* signal);
Signal* addSignal(const char* name, Level initLevel);
void initSimulator();
