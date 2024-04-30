// ****************************************************************************
//
//          PVSim Verilog Simulator Signal Compiler
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

#ifdef EXTENSION
#include <Python.h>
#endif
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include "PSignal.h"
#include "Src.h"
#include "SimPalSrc.h"
#include "Model.h"
#include "VLCompiler.h"

// -------- storage space pointers --------

size_t  gMaxSignals;            // storage limits, from initApplication
Signal* gSignals;               // signals array
Signal* gNextSignal;            // next available signal table offset
Signal* signalsLimit;
Space   gSignalSpace =      // signals space
{
    "gSignals",
    0,
    &gSignals,
    sizeof(Signal),
    &gNextSignal,
    &signalsLimit
};

// -------- global variables --------

short   gNSignals;          // number of signals created
int     gTicksNS = 1000;    // scaled-time ticks per NS
int     gNSStart;           // sim start in NS
int     gNSDuration;        // desired simulation run in NS
Signal* gBreakSignal;
Signal* gStopSignal;
bool    gSignalDisplayOn;   // set if following signals to be displayed
unsigned int gNameLenLimit; // warning given for names longer than this
const char* gTracedModel;       // designator of model to be traced
char    gDispBusWidth;  // width of current display-bus
char    gDispBusBitNo;  // next bit to be assigned for a display-bus, or
                        //  -1 if none.

//-----------------------------------------------------------------------------
// Hash a string into a symbol table index, case insensitive.

int hash(const char* s)
{
    int hashVal = 0;
    for (;;)
    {
        char c = *s++;
        if (!c)
            break;
        if (c >= 'a' && c <= 'z')   // convert both name
            c += ('A'-'a');         //   to upper case
        hashVal += c;               // add up characters
    }
    // return sum modulo hash table size
    return (int)(labs(hashVal) % max_hashCodes);
}

//-----------------------------------------------------------------------------
// Look up a name string in the symbol table.  Returns a Symbol pointer,
//  or 0 if not found.

Symbol* lookup(const char* name, bool caseSensitive)
{
    char c;
    char ucName[max_nameLen+50];
    const char* p1 = name;
    char* p2 = ucName;
    do                  // convert name to upper case if allowed
    {
        c = *p1++;
        if (!caseSensitive && c >= 'a' && c <= 'z')
            c += ('A'-'a');
        *p2++ = c;
    } while (c);

    Symbol* sym = gHashTable[hash(ucName)];
    while (sym)
    {
        for (p1 = sym->name, p2 = ucName; ; )   // compare symbol names
        {
            c = *p1++;
            if (!caseSensitive && c >= 'a' && c <= 'z')
                c += ('A'-'a'); // convert sym table name to upper case
            if (c != *p2++)
                break;              // no match: stop this comparison
            if (c == 0)
            {
                gLastSymbol = sym;
                return (sym);   // complete match: return symbol
            }
        }
        sym = sym->next;        // try next symbol in hash list
    }

    return (0);             // no matches: return 0
}

//-----------------------------------------------------------------------------
// Construct a 'New'-allocated holder for a Symbol.

SymObj::SymObj()
{
}

//-----------------------------------------------------------------------------
// Enter a New symbol into the symbol table, and return a pointer to
//  it.  Reuses existing symbol entry if deleted.  Also, creates
//  name string storage if name is scName.

Symbol* newSymbol(const char* name, size_t arg)
{
    Symbol* sym = lookup(name);     // see if name in symbol table
    if (!sym)
    {
        // never been defined: create New symbol entry
        if (gDPUsage != dp_none)
        {                           // in case dictionary space in use
            SymObj* symObj = new SymObj;
            sym = &symObj->symbol;
        }
        else
        {
            needBlock("newSymbol", name, sizeof(Symbol), dp_none);
            sym = (Symbol* )gDP;
            gDP += sizeof (Symbol);
        }

        if (gScToken && name == gScToken->name)
            sym->name = newString(gScToken->name); // just scanned: store name
        else
            sym->name = name;           // else, use existing string
        int hashVal = hash(name);       // hash symbol into gHashTable
        sym->next = gHashTable[hashVal];    // and add to head of chain
        gHashTable[hashVal] = sym;
    }
    sym->arg = arg;
    sym->prevDec = gLastNewSymbol;
    gLastNewSymbol = sym;
    return (sym);
}

//-----------------------------------------------------------------------------
// Add a signal to the signal table, with given initial level.
// Returns a pointer to the New signal. 'name' must be either scName
//   or a stored string.

Signal* addSignal(const char* name, Level initLevel)
{
    for (const char* p = name; *p; p++)
        if (*p == '.' || *p == '_') // a dot or _ in a name means it's internal
            goto internalName;  // and exempt from the length limit check

    if (strlen(name) > gNameLenLimit && name[0] != 'f')
        warnErr("Signal name \"%s\" is too int. Limit is %d characters.",
            name, gNameLenLimit);
internalName:
    Signal* newSignal;
    Symbol* sym = lookup(name, TRUE);
    if (sym)
        newSignal = (Signal*)sym->arg;
    else
    {
        newSignal = gNextSignal;
        if (newSignal >= gSignals+gMaxSignals)
            throw new VError(verr_memOverflow,
                                "too many signals. Enlarge gMaxSignals.");
        sym = newSymbol(name, (size_t)newSignal);
        newSignal->name = sym->name;
        newSignal->initLevel = initLevel;
        newSignal->floatLevel = LV_X;
        newSignal->dependList = 0;
        newSignal->inputsList = 0;
        newSignal->RCminTime = 1;
        newSignal->RCmaxTime = 1;
        newSignal->mode = 0;
        newSignal->evalCode = 0;
        newSignal->randTrkSig = 0;
        if (gDispBusBitNo >= 0)
        {
            newSignal->busOpt = DISP_BUS_BIT;
            newSignal->busWidth = gDispBusWidth;
            newSignal->busBitNo = gDispBusBitNo;
            gDispBusBitNo--;
        }
        gNSignals++;
        gNextSignal++;
    }
    // Set signal's status to reflect current settings of 'displayedSignals'
    // and 'monteSignals'.
    if (gSignalDisplayOn)
        newSignal->is |= DISPLAYED;
    return (newSignal);
}

//-----------------------------------------------------------------------------
// newSignals   Initialize the signal table.

void newSignalsS()
{
    if (gNSignals)      // don't init if already have some signals
        return;

    gNextSignal = gSignals;

    int* endp = (int* )(gSignals + gMaxSignals);
    for (int* p = (int* )gSignals; p < endp; )  // clear signal table
        *p++ = 0;
    gSignalDisplayOn = FALSE;
    addSignal("__signal0__", LV_L);     // dummy signal #0, used to
                                        //  check for unassigned pins
                                        // and =L because it is used
                                        // to AND with lone inputs
    needBlock("newSignalsS", "", sizeof(short), dp_palDef);
    gSignals[0].evalCode = (EqnItem*)gDP;
    *((short* )gDP) = STORE_OP;
    gDP += sizeof(short);
    gDPUsage = dp_none;
    gSignalDisplayOn = TRUE;
    gDispBusBitNo = -1;
}

//-----------------------------------------------------------------------------
// Add a dependent signal to another signal's dependents list, and
//  add that signal to dependent's input signals list.

void setDependency(Signal* dependent, Signal* signal)
{
    if (!dependent)
        throw new VError(verr_notFound,
                "setDependency: missing dependent for %s", signal->name);
    if (!signal)
        throw new VError(verr_notFound,
                "setDependency: missing signal for %s", dependent->name);
    for (SigNode* dnode = signal->dependList; dnode; dnode = dnode->next())
        if (dnode->signal() == dependent)
            return;

    signal->dependList    = new SigNode(dependent, signal->dependList);
    dependent->inputsList = new SigNode(signal, dependent->inputsList);
}

//-----------------------------------------------------------------------------
// Initialize simulator spaces and add Simulator keywords to symbol
//  table.

void initSimulator()
{
    allocSpace(&gStringsSpace, gMaxStringSpace);

    for (int i = 0; i < max_hashCodes; i++)
        gHashTable[i] = 0;                  // clear symbol hash table
    gLastNewSymbol = 0;

    gWarningCount = 0;

    // TODO -- are these requirements still needed?
    if (sizeof(Signal) & 7)
        throw new VError(verr_bug,
                    "Signal structure size must be a multiple of 8 bytes");
#ifdef FULL_DEBUG
    if ((OFFSET(Signal8, level) & 7) | (OFFSET(Signal8, nlevel) & 7) |
        (OFFSET(Signal8, preLevel) & 7))
        throw new VError(verr_bug,
           "level, nlevel, and preLevel in Signal must be on 8 byte boundries");
#endif
    if (gMaxSignals & 7)
        throw new VError(verr_bug, "gMaxSignals must be a multiple of 8");

    // first clean up any previous signal's linked lists

    allocSpace(&gSignalSpace, gMaxSignals);

    gNSignals = 0;

#ifdef RANGE_CHECKING
    firstCode = (EqnItem*)gDP;
#endif
    newSignalsS();
    gTracedModel = "~";
    gBarSignal = 0;
    gBreakSignal = 0;
    gStopSignal = 0;
    gNameLenLimit = 12;
    addSignal("ErrFlag", LV_L);

    // set defaults
    gTimeScaleExp = -9;             // 1ns timeunit
    gTimeScale = 10.0;              // timeunit / timeprecision
    gTimeRoundExp = gTimeScaleExp;  
    gTimeDispPrec = 0;              // time format decimal places
    gTimeSuffixStr = "";            // no time suffix
    gTimeMinFieldWid = 20;          // 20 places for time field
}
