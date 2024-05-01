// ****************************************************************************
//
//          PVSim Verilog Simulator
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
#include <time.h>
#include "Src.h"
#include "PSignal.h"
#include "Utils.h"
#include "Model.h"

// #define RANGE_CHECKING
#define DEBUG_ADDEVENT
// #define TRACE_ALL_EVENTS

// -------- global variables --------

size_t  gEventHistLen;      // length of 1/2 time line FIFO
#ifdef WRITE_EVENTS
FILE*   gEvFile;
#endif
OpenFile*   gOpenFiles; // list of open data files
const char* gLevelNames = "LSXRFUDHZCVW";
Tick    gTEnd;          // sim & display end tick, adjusted by gStopSignal
Tick    gTickBinSize = 100; // size of each tick bin

// -------- storage space pointers --------

size_t  gMaxEvents;
Event*  events;         // events array
Event*  lastEvent;
Event*  eventLimit;
Space   eventSpace =        // events space
{
    "events",
    0,
    &events,
    sizeof(Event),
    &lastEvent,
    &eventLimit
};

Event** timeLine;           // time line: array of event lists for each tick
Event** timeLineEnd;
Event** timeLineLimit;
Space   timeLineSpace =     // time line space
{
    "time line",
    0,
    &timeLine,
    sizeof(Event* ),
    &timeLineEnd,
    &timeLineLimit
};

Event** tempTL;         // time line temporary array
Event** tempTLEnd;
Event** tempTLLimit;
Space   tempTLSpace =       // time line temporary array space
{
    "time line temp",
    0,
    &tempTL,
    sizeof(Event* ),
    &tempTLEnd,
    &tempTLLimit
};

// -------- function lookup tables --------

const Level Z = LV_L;

const FuncTable funcTable =
{
    { // ANDtable
    //         L     S     X     R     F     U     D     H     Z     C     V     W
    /* L */ {LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, Z, Z, Z, Z},
    /* S */ {LV_L, LV_S, LV_X, LV_X, LV_F, LV_X, LV_X, LV_S, LV_X, LV_X, LV_L, LV_S, Z, Z, Z, Z},
    /* X */ {LV_L, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_L, LV_X, Z, Z, Z, Z},
    /* R */ {LV_L, LV_X, LV_X, LV_R, LV_X, LV_X, LV_X, LV_R, LV_X, LV_X, LV_L, LV_R, Z, Z, Z, Z},
    /* F */ {LV_L, LV_F, LV_X, LV_X, LV_F, LV_X, LV_X, LV_F, LV_X, LV_X, LV_L, LV_F, Z, Z, Z, Z},
    /* U */ {LV_L, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_L, LV_X, Z, Z, Z, Z},
    /* D */ {LV_L, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_L, LV_X, Z, Z, Z, Z},
    /* H */ {LV_L, LV_S, LV_X, LV_R, LV_F, LV_X, LV_X, LV_H, LV_X, LV_X, LV_L, LV_H, Z, Z, Z, Z},
    /* Z */ {LV_L, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_L, LV_X, Z, Z, Z, Z},
    /* C */ {LV_L, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_L, LV_X, Z, Z, Z, Z},
    /* V */ {LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, Z, Z, Z, Z},
    /* W */ {LV_L, LV_S, LV_X, LV_R, LV_F, LV_X, LV_X, LV_H, LV_X, LV_X, LV_L, LV_H, Z, Z, Z, Z}
    },
    { // ORtable
    //         L     S     X     R     F     U     D     H     Z     C     V     W
    /* L */ {LV_L, LV_S, LV_X, LV_R, LV_F, LV_X, LV_X, LV_H, LV_X, LV_X, LV_L, LV_H, Z, Z, Z, Z},
    /* S */ {LV_S, LV_S, LV_X, LV_R, LV_X, LV_X, LV_X, LV_H, LV_X, LV_X, LV_S, LV_H, Z, Z, Z, Z},
    /* X */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_H, LV_X, LV_X, LV_X, LV_H, Z, Z, Z, Z},
    /* R */ {LV_R, LV_R, LV_X, LV_R, LV_X, LV_X, LV_X, LV_H, LV_X, LV_X, LV_R, LV_H, Z, Z, Z, Z},
    /* F */ {LV_F, LV_X, LV_X, LV_X, LV_F, LV_X, LV_X, LV_H, LV_X, LV_X, LV_F, LV_H, Z, Z, Z, Z},
    /* U */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_H, LV_X, LV_X, LV_X, LV_H, Z, Z, Z, Z},
    /* D */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_H, LV_X, LV_X, LV_X, LV_H, Z, Z, Z, Z},
    /* H */ {LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, Z, Z, Z, Z},
    /* Z */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_H, LV_X, LV_X, LV_X, LV_H, Z, Z, Z, Z},
    /* C */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_H, LV_X, LV_X, LV_X, LV_H, Z, Z, Z, Z},
    /* V */ {LV_L, LV_S, LV_X, LV_R, LV_F, LV_X, LV_X, LV_H, LV_X, LV_X, LV_L, LV_H, Z, Z, Z, Z},
    /* W */ {LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, Z, Z, Z, Z}
    },
    { // XORtable
    //         L     S     X     R     F     U     D     H     Z     C     V     W
    /* L */ {LV_L, LV_S, LV_X, LV_R, LV_F, LV_X, LV_X, LV_H, LV_X, LV_X, LV_L, LV_H, Z, Z, Z, Z},
    /* S */ {LV_S, LV_S, LV_X, LV_X, LV_X, LV_X, LV_X, LV_S, LV_X, LV_X, LV_S, LV_S, Z, Z, Z, Z},
    /* X */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, Z, Z, Z, Z},
    /* R */ {LV_R, LV_X, LV_X, LV_F, LV_X, LV_X, LV_X, LV_F, LV_X, LV_X, LV_R, LV_F, Z, Z, Z, Z},
    /* F */ {LV_F, LV_X, LV_X, LV_X, LV_R, LV_X, LV_X, LV_R, LV_X, LV_X, LV_F, LV_R, Z, Z, Z, Z},
    /* U */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, Z, Z, Z, Z},
    /* D */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, Z, Z, Z, Z},
    /* H */ {LV_H, LV_S, LV_X, LV_F, LV_X, LV_X, LV_X, LV_L, LV_X, LV_X, LV_H, LV_L, Z, Z, Z, Z},
    /* Z */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, Z, Z, Z, Z},
    /* C */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, Z, Z, Z, Z},
    /* V */ {LV_L, LV_S, LV_X, LV_R, LV_F, LV_X, LV_X, LV_H, LV_X, LV_X, LV_L, LV_H, Z, Z, Z, Z},
    /* W */ {LV_H, LV_S, LV_X, LV_F, LV_R, LV_X, LV_X, LV_L, LV_X, LV_X, LV_H, LV_L, Z, Z, Z, Z}
    },
    { // ENABLEtable
    // enable: L     S     X     R     F     U     D     H     Z     C     V     W
    /* L */ {LV_Z, LV_X, LV_U, LV_U, LV_U, LV_X, LV_X, LV_L, LV_X, LV_X, LV_Z, LV_L, Z, Z, Z, Z},
    /* S */ {LV_Z, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_S, LV_X, LV_X, LV_Z, LV_S, Z, Z, Z, Z},
    /* X */ {LV_Z, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_Z, LV_X, Z, Z, Z, Z},
    /* R */ {LV_Z, LV_X, LV_X, LV_D, LV_D, LV_X, LV_X, LV_R, LV_X, LV_X, LV_Z, LV_R, Z, Z, Z, Z},
    /* F */ {LV_Z, LV_X, LV_X, LV_U, LV_U, LV_X, LV_X, LV_F, LV_X, LV_X, LV_Z, LV_F, Z, Z, Z, Z},
    /* U */ {LV_Z, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_U, LV_X, LV_X, LV_Z, LV_U, Z, Z, Z, Z},
    /* D */ {LV_Z, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_D, LV_X, LV_X, LV_Z, LV_D, Z, Z, Z, Z},
    /* H */ {LV_Z, LV_X, LV_D, LV_D, LV_D, LV_X, LV_X, LV_H, LV_X, LV_X, LV_Z, LV_H, Z, Z, Z, Z},
    /* Z */ {LV_Z, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_Z, LV_Z, Z, Z, Z, Z},
    /* C */ {LV_Z, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_Z, LV_X, Z, Z, Z, Z},
    /* V */ {LV_Z, LV_X, LV_U, LV_U, LV_U, LV_X, LV_X, LV_L, LV_X, LV_X, LV_Z, LV_L, Z, Z, Z, Z},
    /* W */ {LV_Z, LV_X, LV_D, LV_D, LV_D, LV_X, LV_X, LV_H, LV_X, LV_X, LV_Z, LV_H, Z, Z, Z, Z}
    // ^--input
    },
    { // TStable
    // driver: L     S     X     R     F     U     D     H     Z     C     V     W
    /* L */ {LV_L, LV_C, LV_C, LV_C, LV_C, LV_L, LV_X, LV_C, LV_L, LV_C, LV_L, LV_L, Z, Z, Z, Z},
    /* S */ {LV_C, LV_C, LV_C, LV_C, LV_C, LV_X, LV_X, LV_C, LV_S, LV_C, LV_S, LV_S, Z, Z, Z, Z},
    /* X */ {LV_C, LV_C, LV_C, LV_C, LV_C, LV_X, LV_X, LV_C, LV_X, LV_C, LV_X, LV_X, Z, Z, Z, Z},
    /* R */ {LV_C, LV_C, LV_C, LV_C, LV_C, LV_X, LV_X, LV_C, LV_R, LV_C, LV_R, LV_R, Z, Z, Z, Z},
    /* F */ {LV_C, LV_C, LV_C, LV_C, LV_C, LV_X, LV_X, LV_C, LV_F, LV_C, LV_F, LV_F, Z, Z, Z, Z},
    /* U */ {LV_L, LV_X, LV_X, LV_X, LV_X, LV_U, LV_X, LV_X, LV_U, LV_X, LV_U, LV_U, Z, Z, Z, Z},
    /* D */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_D, LV_H, LV_D, LV_X, LV_D, LV_D, Z, Z, Z, Z},
    /* H */ {LV_C, LV_C, LV_C, LV_C, LV_C, LV_X, LV_H, LV_H, LV_H, LV_C, LV_H, LV_H, Z, Z, Z, Z},
    /* Z */ {LV_L, LV_S, LV_X, LV_R, LV_F, LV_U, LV_D, LV_H, LV_Z, LV_C, LV_V, LV_W, Z, Z, Z, Z},
    /* C */ {LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, Z, Z, Z, Z},
    /* V */ {LV_L, LV_S, LV_X, LV_R, LV_F, LV_U, LV_D, LV_H, LV_V, LV_C, LV_V, LV_Z, Z, Z, Z, Z},
    /* W */ {LV_L, LV_S, LV_X, LV_R, LV_F, LV_U, LV_D, LV_H, LV_W, LV_C, LV_Z, LV_W, Z, Z, Z, Z}
    // ^--tri-state
    },
    { // MINMAXtable
    // min:    L     S     X     R     F     U     D     H     Z     C     V     W
    /* L */ {LV_L, LV_F, LV_F, LV_F, LV_F, LV_X, LV_D, LV_F, LV_D, LV_X, LV_L, LV_F, Z, Z, Z, Z},
    /* S */ {LV_X, LV_S, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, Z, Z, Z, Z},
    /* X */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, Z, Z, Z, Z},
    /* R */ {LV_R, LV_X, LV_X, LV_R, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_R, LV_X, Z, Z, Z, Z},
    /* F */ {LV_X, LV_X, LV_X, LV_X, LV_F, LV_X, LV_X, LV_F, LV_X, LV_X, LV_X, LV_F, Z, Z, Z, Z},
    /* U */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_U, LV_X, LV_X, LV_X, Z, Z, Z, Z},
    /* D */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_D, LV_X, LV_X, LV_X, Z, Z, Z, Z},
    /* H */ {LV_R, LV_R, LV_R, LV_R, LV_R, LV_U, LV_X, LV_H, LV_U, LV_X, LV_R, LV_H, Z, Z, Z, Z},
    /* Z */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_Z, LV_X, LV_X, LV_X, Z, Z, Z, Z},
    /* C */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, Z, Z, Z, Z},
    /* V */ {LV_L, LV_F, LV_F, LV_F, LV_F, LV_X, LV_D, LV_F, LV_D, LV_X, LV_L, LV_F, Z, Z, Z, Z},
    /* W */ {LV_R, LV_R, LV_R, LV_R, LV_R, LV_U, LV_X, LV_H, LV_U, LV_X, LV_R, LV_H, Z, Z, Z, Z}
    // ^--max
    },
    { // BSWALLOWtable
    //         L     S     X     R     F     U     D     H     Z     C     V     W
            LV_L, LV_S, LV_S, LV_H, LV_L, LV_H, LV_L, LV_H, LV_Z, LV_C, LV_V, LV_W
    },
    { // STABLEtable
    //         L     S     X     R     F     U     D     H     Z     C     V     W
            LV_H, LV_H, LV_L, LV_L, LV_L, LV_L, LV_L, LV_H, LV_H, LV_L, LV_H, LV_H
    },
    { // INVERTtable
    //         L     S     X     R     F     U     D     H     Z     C     V     W
            LV_H, LV_S, LV_X, LV_F, LV_R, LV_D, LV_U, LV_L, LV_X, LV_C, LV_H, LV_L
    },
    { // SAMPLEtable
    //         L     S     X     R     F     U     D     H     Z     C     V     W
            LV_L, LV_S, LV_X, LV_X, LV_X, LV_X, LV_X, LV_H, LV_X, LV_X, LV_L, LV_H
    }

#if 0
    { // MUXORHtable - Rising+Falling -> High
    //         L     S     X     R     F     U     D     H     Z     C     V     W
    /* L */ {LV_L, LV_S, LV_X, LV_R, LV_F, LV_X, LV_X, LV_H, LV_X, LV_C, LV_L, LV_H, Z, Z, Z, Z},
    /* S */ {LV_S, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_H, LV_X, LV_C, LV_S, LV_H, Z, Z, Z, Z},
    /* X */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_H, LV_X, LV_C, LV_X, LV_H, Z, Z, Z, Z},
    /* R */ {LV_R, LV_X, LV_X, LV_R, LV_H, LV_X, LV_X, LV_H, LV_X, LV_C, LV_R, LV_H, Z, Z, Z, Z},
    /* F */ {LV_F, LV_X, LV_X, LV_H, LV_F, LV_X, LV_X, LV_H, LV_X, LV_C, LV_F, LV_H, Z, Z, Z, Z},
    /* U */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_H, LV_X, LV_C, LV_X, LV_H, Z, Z, Z, Z},
    /* D */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_H, LV_X, LV_C, LV_X, LV_H, Z, Z, Z, Z},
    /* H */ {LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, Z, Z, Z, Z},
    /* Z */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_H, LV_X, LV_C, LV_X, LV_H, Z, Z, Z, Z},
    /* C */ {LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_H, LV_C, LV_C, LV_C, LV_H, Z, Z, Z, Z},
    /* V */ {LV_L, LV_S, LV_X, LV_R, LV_F, LV_X, LV_X, LV_H, LV_X, LV_C, LV_L, LV_H, Z, Z, Z, Z},
    /* W */ {LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, Z, Z, Z, Z}
    },
    { // MUXORLtable - Rising+Falling -> Low
    //         L     S     X     R     F     U     D     H     Z     C     V     W
    /* L */ {LV_L, LV_S, LV_X, LV_R, LV_F, LV_X, LV_X, LV_H, LV_X, LV_C, LV_L, LV_H, Z, Z, Z, Z},
    /* S */ {LV_S, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_H, LV_X, LV_C, LV_S, LV_H, Z, Z, Z, Z},
    /* X */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_H, LV_X, LV_C, LV_X, LV_H, Z, Z, Z, Z},
    /* R */ {LV_R, LV_X, LV_X, LV_R, LV_L, LV_X, LV_X, LV_H, LV_X, LV_C, LV_R, LV_H, Z, Z, Z, Z},
    /* F */ {LV_F, LV_X, LV_X, LV_L, LV_F, LV_X, LV_X, LV_H, LV_X, LV_C, LV_F, LV_H, Z, Z, Z, Z},
    /* U */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_H, LV_X, LV_C, LV_X, LV_H, Z, Z, Z, Z},
    /* D */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_H, LV_X, LV_C, LV_X, LV_H, Z, Z, Z, Z},
    /* H */ {LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, Z, Z, Z, Z},
    /* Z */ {LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_H, LV_X, LV_C, LV_X, LV_H, Z, Z, Z, Z},
    /* C */ {LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_H, LV_C, LV_C, LV_C, LV_H, Z, Z, Z, Z},
    /* V */ {LV_L, LV_S, LV_X, LV_R, LV_F, LV_X, LV_X, LV_H, LV_X, LV_C, LV_L, LV_H, Z, Z, Z, Z},
    /* W */ {LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, LV_H, Z, Z, Z, Z}
    },
    { // TGtable
    // enable: L     S     X     R     F     U     D     H     Z     C     V     W
    /* L */ {LV_Z, LV_X, LV_U, LV_U, LV_U, LV_X, LV_X, LV_L, LV_X, LV_X, LV_Z, LV_L, Z, Z, Z, Z},
    /* S */ {LV_Z, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_S, LV_X, LV_X, LV_Z, LV_S, Z, Z, Z, Z},
    /* X */ {LV_Z, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_Z, LV_X, Z, Z, Z, Z},
    /* R */ {LV_Z, LV_X, LV_X, LV_D, LV_D, LV_X, LV_X, LV_R, LV_X, LV_X, LV_Z, LV_R, Z, Z, Z, Z},
    /* F */ {LV_Z, LV_X, LV_X, LV_U, LV_U, LV_X, LV_X, LV_F, LV_X, LV_X, LV_Z, LV_F, Z, Z, Z, Z},
    /* U */ {LV_Z, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_U, LV_X, LV_X, LV_Z, LV_U, Z, Z, Z, Z},
    /* D */ {LV_Z, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_D, LV_X, LV_X, LV_Z, LV_D, Z, Z, Z, Z},
    /* H */ {LV_Z, LV_X, LV_D, LV_D, LV_D, LV_X, LV_X, LV_H, LV_X, LV_X, LV_Z, LV_H, Z, Z, Z, Z},
    /* Z */ {LV_Z, LV_Z, LV_Z, LV_Z, LV_Z, LV_Z, LV_Z, LV_Z, LV_Z, LV_Z, LV_Z, LV_Z, Z, Z, Z, Z},
    /* C */ {LV_Z, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_C, LV_X, LV_X, LV_Z, LV_C, Z, Z, Z, Z},
    /* V */ {LV_Z, LV_X, LV_U, LV_U, LV_U, LV_X, LV_X, LV_V, LV_X, LV_X, LV_Z, LV_V, Z, Z, Z, Z},
    /* W */ {LV_Z, LV_X, LV_D, LV_D, LV_D, LV_X, LV_X, LV_W, LV_X, LV_X, LV_Z, LV_W, Z, Z, Z, Z}
    // ^--input
    },
    { // OCtable
    // driver: L     S     X     R     F     U     D     H     Z     C     V     W
    /* L */ {LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, LV_L, Z, Z, Z, Z},
    /* S */ {LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_S, LV_S, LV_C, LV_C, LV_S, Z, Z, Z, Z},
    /* X */ {LV_L, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, Z, Z, Z, Z},
    /* R */ {LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_R, LV_R, LV_C, LV_R, LV_R, Z, Z, Z, Z},
    /* F */ {LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_F, LV_F, LV_C, LV_F, LV_F, Z, Z, Z, Z},
    /* U */ {LV_L, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_U, LV_U, LV_X, LV_U, LV_U, Z, Z, Z, Z},
    /* D */ {LV_L, LV_X, LV_X, LV_X, LV_X, LV_X, LV_X, LV_D, LV_D, LV_X, LV_D, LV_D, Z, Z, Z, Z},
    /* H */ {LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_H, LV_H, LV_C, LV_H, LV_H, Z, Z, Z, Z},
    /* Z */ {LV_L, LV_S, LV_X, LV_U, LV_U, LV_X, LV_X, LV_Z, LV_Z, LV_X, LV_V, LV_W, Z, Z, Z, Z},
    /* C */ {LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, LV_C, Z, Z, Z, Z},
    /* V */ {LV_L, LV_S, LV_X, LV_R, LV_F, LV_U, LV_D, LV_V, LV_V, LV_C, LV_V, LV_Z, Z, Z, Z, Z},
    /* W */ {LV_L, LV_S, LV_X, LV_R, LV_F, LV_U, LV_D, LV_W, LV_W, LV_C, LV_Z, LV_W, Z, Z, Z, Z}
    // ^--tri-state
    },
    { // ESWALLOWtable
    //         L     S     X     R     F     U     D     H     Z     C     V     W
            LV_L, LV_S, LV_S, LV_L, LV_H, LV_L, LV_H, LV_H, LV_Z, LV_C, LV_V, LV_W
    },
#endif
};

// Table that is TRUE when the two index Levels are not the same approx voltage
bool changedVoltage[12][16] =
{
    //      L  S  X  R  F  U  D  H  Z  C  V  W
    /* L */ {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1},
    /* S */ {1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    /* X */ {1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    /* R */ {1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    /* F */ {1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    /* U */ {1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    /* D */ {1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    /* H */ {1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1},
    /* Z */ {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    /* C */ {1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1},
    /* V */ {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1},
    /* W */ {1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1}
};

// -------- local global variables --------

Tick    gTick;          // current simulation time
Tick    gPrevEvtTick;   // previous event file tick
size_t  timeLineLen;        // length of time line FIFO in ticks
Tick    timeLineBaseTick;   // first tick of time line FIFO
Event** timeLineHead;       // head of time line FIFO (at time gTick)
Event*  freeEventList;      // linked list of free event spaces
int     eventCount;         // event statistics
EqnItem* firstCode;         // signal equation code space start
#ifdef EVENT_HISTORY
Event*  gCurEvent;
#endif
Tick breakTick = 0; // <-- set this to break time for breakpoint below

Signal* gBarSignal;     // if non-zero, signal whose rising edge makes bar
Signal* gErrorSignal;   // the signal that caused the first error
Tick    gErrorTickB;    // start tick of the first error selection
Tick    gErrorTickE;    // end tick of the first error selection
Tick    gDispTStart;    // display start tick
int     gTimeScaleExp;  // absolute time scale, in exponent form
double  gTimeScale;     // time scale factor relative to ticks
int     gTimeRoundExp;  // time internal rounding scale (unused for now)
int     gTimeDispPrec;  // time display precision: number of digits
const char* gTimeSuffixStr; // time display suffix string
int     gTimeMinFieldWid; // time display mimimum field width

int     dummy;

//-----------------------------------------------------------------------------
// Compute and store offset to signal's field: stored after AND_OP, etc.

inline void codeSigAddr(Signal* sig, EqnItem* &nextCode)
{
    (nextCode++)->operand =
        ((char* )sig - (char* )gSignals + OFFSET(Signal8, level));
}

//-----------------------------------------------------------------------------
// We want to add a new event to a signal at the same tick as an existing
// event. Check that the levels are the same (and therefore isn't needed).

void checkEventCollision(Event* evNew, Event* evExist)
{
    if (evNew->level != evExist->level && !(evNew->is & ATTACHED_TEXT))
    {
        Signal* signal = evNew->signal;
        // draw message on signal's waveform
        drawf(signal, "#rcoll");
        Symbol* errFlagSym = lookup("ErrFlag");
        if (errFlagSym)
        {
            Signal* errFlag = (Signal*)errFlagSym->arg;
            if (!gErrorSignal)
            {
                gErrorSignal = signal;
                gErrorTickE = gTick;
                gErrorTickB = gTick;
                //if (gErrorTickB < 0)
                //    gErrorTickB = 0;
                gStopSignal = errFlag;
                addEvent(0, errFlag, LV_H, CLEAN);
                drawf(errFlag, "#rcoll");
                display("// *** Error at ");
                if (gTick/gTicksNS > 1000000) // long run times are in ms
                    display("%7.6f ms ", ((float)gTick/gTicksNS)/1000000.);
                else
                    display("%2.3f ns ", (float)gTick/gTicksNS);
                display(":\n");
                gErrorTickB -= 2*gTicksNS;
                display("// ***       signal %s goes both %c and %c!\n",
                    signal->name, gLevelNames[evExist->level],
                    gLevelNames[evNew->level]);
            }
        }
        gFlaggedErrCount++;
    }
}

//-----------------------------------------------------------------------------
// Print warnings and errors outside of addEvent to keep floating point
// registers out of critical code.

void warnBeyondTimeline(Tick dt, Signal* signal)
{
    warnErr("event time for %s at %2.3fns to future %2.3fns exceeds"
            " %2.3fns timeline!",
                signal->name, (float)gTick/gTicksNS, 
                (float)dt/gTicksNS, (float)gEventHistLen/gTicksNS);
}

void throwWithTime(VErrCode vcode, const char* msg)
{
    throw new VError(vcode, "at %2.3f ns: %s", (float)gTick/gTicksNS, msg);
}

void showPostEvent(Signal* signal, Level level, char eventType, Tick t2)
{
    const char* flagStr;
    switch (eventType)
    {
        case CLEAN:                     flagStr = "";   break;
        case STARTING_AMBIG:            flagStr = "b";  break;
        case ENDING_AMBIG:              flagStr = "e";  break;
        case FLOATING:                  flagStr = "f";  break;
        case STARTING_AMBIG | FLOATING: flagStr = "bf"; break;
        case ENDING_AMBIG | FLOATING:   flagStr = "ef"; break;
        case WAKEUP:                    flagStr = "w";  break;
        case ATTACHED_TEXT:             flagStr = "t";  break;
        default:                        flagStr = "?";
    }
    printfEvt("post Event %12s=%c%s at %2.3f\n",
        signal->name, gLevelNames[level], flagStr, (float)t2/gTicksNS);
}

//-----------------------------------------------------------------------------
// Add an event to the time line at gTick + dt, or return 0 if no more space.

Event* addEvent(    Tick    dt,
                    Signal* signal,
                    Level   level,
                    char    eventType)
{
#ifdef DEBUG_ADDEVENT
    if (!signal)
        throw new VError(verr_bug, "addEvent: event's signal is zero");
    if (signal < gSignals || signal > gNextSignal)
        throw new VError(verr_bug, "BUG: addEvent: bad signal address");
#endif
    if (dt <= 1)
    {
        // if level won't change in next tick: ignore this post
        if (signal->level == level)
            return 0;
        dt = 1;
    }
    Tick t2 = gTick + dt;

    // if similar event just posted: ignore this post
    Event* earlierEv = signal->lastEvtPosted;
#ifdef RANGE_CHECKING
    if (earlierEv && (earlierEv < events || earlierEv >= events+gMaxEvents))
        throwWithTime(verr_bug, "BUG: addEvent: bad earlierEv pointer");
#endif
    if (earlierEv && earlierEv->tick == t2 && earlierEv->level == level)
        return 0;

    if (dt >= (Tick)gEventHistLen)      // don't add event if past timeline tail
    {
        warnBeyondTimeline(dt, signal);
        dt = gEventHistLen - 1;
        t2 = gTick + dt;
    }

    Tick t2EventListBin = (gTick - timeLineBaseTick + dt) / gTickBinSize;
    if (t2EventListBin >= timeLineLen)
        t2EventListBin -= timeLineLen;
    Event** t2EventList = timeLine + t2EventListBin;

    if (signal->is & TRACED)
        showPostEvent(signal, level, eventType, t2);

    Event* event = freeEventList;
    //display("      e=0x%p\n", event);
    if (event == 0)
        throwWithTime(verr_memOverflow, "event space full");
#ifdef DEBUG_ADDEVENT
    if (!(event->is & FREE))
        throwWithTime(verr_bug,
                      "BUG: addEvent: non-free event on freeEventList");
#ifdef RANGE_CHECKING
    if (event < events || event >= events+gMaxEvents)
        throwWithTime(verr_bug,
                      "BUG: addEvent: bad event pointer from freeEventList");
#endif
#endif
    freeEventList = event->next;
    event->signal = signal;    // post event for changing signal level
    event->nextInSignal = 0;
    event->prevInSignal = 0;
    event->nextFloat = 0;
    event->level = level;
    event->is = eventType;
    if (eventType & FLOATING)
    {
        event->nextFloat = signal->floatList;
        signal->floatList = event;
    }
    event->tick = t2;
#ifdef EVENT_HISTORY
    event->cause = gCurEvent;
#endif

    // insert event in timeLine at t2, keeping events in bin chronological
    Event** link = t2EventList;
    Event* e = *link;
    while (e && e->tick < t2)
    {
        link = &e->next;
        e = *link;
    }
    *link = event;
    event->next = e;

    if (!((signal->is & C_MODEL) && (signal->is & REGISTERED)))
                                    // add event to signal's event list
    {                               // (if not a model's event-handler signal)
        // insert new event into signal's list after time t2
        Event* earlierEv = signal->lastEvtPosted;
        while (earlierEv && earlierEv->tick > t2)
            earlierEv = earlierEv->prevInSignal;
        if (earlierEv && earlierEv->tick == t2)
            checkEventCollision(event, earlierEv);
#ifdef DEBUG_ADDEVENT
        if (signal->firstDispEvt && signal->firstDispEvt->signal != signal)
            throw new VError(verr_bug,
                    "addEvent: BUG: firstDispEv not for same signal (%s)",
                signal->name);
#endif
        event->insertS(signal, earlierEv);
#ifdef DEBUG_ADDEVENT
        if (event->signal->firstDispEvt->signal != signal)
            throw new VError(verr_bug,
                    "addEvent: BUG: firstDispEv not for same signal (%s)",
                signal->name);
#endif
    }

#ifdef DEBUG_ADDEVENT
    if (signal == gBreakSignal && gTick >= breakTick)
        dummy = 1;
#endif
    if (!(eventType & ATTACHED_TEXT))
        signal->preLevel = level;   // keep preLevel up-to-date in case this
                              // signal is also being driven by addMinMaxEvent
    return (event);
}

//-----------------------------------------------------------------------------
// Add a pair of events to the time line at gTick + dtMin and + dtMax for
// an ambiguous-time edge event.

void addMinMaxEvent(Tick    dtMin,
                    Tick    dtMax,
                    Signal* signal,
                    Level   level)
{
    if (signal == gBreakSignal && gTick >= breakTick)
        dummy = 2;

    const FuncTable* func = &funcTable;

    // only allow non-ambiguous events to cause ambiguous ones
    // (keeps preLevel from being ambiguous)
//  level = (Level)func->BSWALLOWtable[level];

    char eventType = CLEAN;

    // special case of capacitance type being released is no-op

    if ((signal->is & TRI_STATE) && signal->floatLevel == LV_Z &&
        (level == LV_Z || level == LV_D || level == LV_U))
            return;

    // if signal can float, determine new level (if any) to go to

    if ((signal->is & TRI_STATE) && signal->floatLevel != LV_X)
    {
        if (level == signal->inLevel)
            return;
        signal->inLevel = level;                // input changed
        if (level == LV_Z)
        {
            level = signal->floatLevel;
            eventType = FLOATING;
        }
        else if (level == LV_U)                 // level U = (Z->L or L->Z)
        {
            if (signal->floatLevel == LV_V)     // pulldowns ignore
                return;
            if (signal->level != signal->floatLevel &&
                signal->level != LV_H)
            {
                                                // disabling after pullup time
                if (signal->floatLevel == LV_W)
                    level = LV_R;
                eventType = FLOATING;
            }
            else
            {
                level = LV_F;                   // enabling to low
                dtMin = 1;
                dtMax = 1;
            }
        }
        else if (level == LV_D)                 // level D = (Z->H or H->Z)
        {
            if (signal->floatLevel == LV_W)     // pullups ignore
                return;
            if (signal->level != signal->floatLevel &&
                signal->level != LV_L)
            {
                                               // disabling after pulldown time
                if (signal->floatLevel == LV_V)
                    level = LV_F;
                eventType = FLOATING;
            }
            else
            {
                level = LV_R;                   // enabling to high
                dtMin = 1;
                dtMax = 1;
            }
        }
        else                                    // enabled: no delay
        {
            dtMin = 1;
            dtMax = 1;
        }
    }

    // If this new level is really new,
    // post an event of changing to the new level

    if ((func->BSWALLOWtable[level] != signal->preLevel) &&
                            (level != signal->preLevel))
    {                           // (BSWALLOW makes weak-high match high, etc.)
        // check if new level is going into or out of an ambiguous level
        char ambType;
        if (func->STABLEtable[signal->preLevel] == LV_H)
        {
            if (func->STABLEtable[level] == LV_H)
                ambType = CLEAN;
            else
                ambType = STARTING_AMBIG;
        }
        else
        {
            if (func->STABLEtable[level] == LV_H)
                ambType = ENDING_AMBIG;
            else
                ambType = CLEAN;
        }

        // edge has ambiguity: post starting and ending events
        Level ambLevel = (Level)func->MINMAXtable[level][signal->preLevel];

        if (ambType != ENDING_AMBIG)            // start of ambiguity
            addEvent(dtMin, signal, ambLevel, STARTING_AMBIG + eventType);

        if (ambType != STARTING_AMBIG)          // end of ambiguity
            addEvent(dtMax, signal, ambLevel, ENDING_AMBIG + eventType);
    }
    // save current level for successive tests
    // unless setup or hold violated on previous edge, expect a late meta event
    if (!(signal->mode & MODE_WENT_META))
        signal->preLevel = level;
}

//-----------------------------------------------------------------------------
// Attach a text C-string to a signal's trace at the current time.

void attachSignalText(Signal* signal, char* s, bool inFront, int ticksAllotted)
{
    Event* event = signal->lastEvtPosted;
    if (event)
    {
        if (event->is == ATTACHED_TEXT)
        {
            if (event->tick == gTick + 1)
            {                       // if already an event at this time
                strncpy(event->attText, s, MAX_ATT_TEXT_LEN-1);
                *(event->attText + MAX_ATT_TEXT_LEN-1) = 0;
                return;
            }
            else if (event->tick > gTick - ticksAllotted)
                return;             // if no room for this text, just return
        }
    }
    event = addEvent(2, signal, level(inFront), ATTACHED_TEXT);
    if (event)
    {
        strncpy(event->attText, s, MAX_ATT_TEXT_LEN-1);
        *(event->attText + MAX_ATT_TEXT_LEN-1) = 0;
    }
}

//-----------------------------------------------------------------------------
// Create code for one tri-state signal.

void codeATSSig(Signal* tsSig, Signal* otherSideTG)
{
    EqnItem* nextCode = (EqnItem* )gDP; // prepare to code a tri-state signal
    bool startedTS = FALSE;
                                  // code each source input to tri-state signal
    for (SigNode* node = tsSig->inputsList; node; node = node->next())
    {
        Signal* input = node->signal();
        if (!startedTS && (input->enable))
        {
            gDP = (char* )nextCode;
            needBlock("codeATSSig", "tri-state signal", 300*sizeof(short),
                       dp_palDef);
            nextCode = (EqnItem* )gDP;
            tsSig->evalCode = nextCode;
            nextCode->opcode = NEWTS_OP;    // code a start-of-TS-signal
            nextCode = (EqnItem*)((size_t)nextCode + sizeof(short));
            startedTS = TRUE;
        }
        if (input->enable)         // if source signal has an enable
        {                          // then code an enable onto tri-state signal
            nextCode->opcode = ENABLE_OP;
            nextCode = (EqnItem*)((size_t)nextCode + sizeof(short));
            codeSigAddr(input, nextCode);
            codeSigAddr(input->enable, nextCode);
        }
    }
    if (startedTS)
    {                                    // code a save-TS-signal-result
        nextCode->opcode = STORE_OP;
        nextCode = (EqnItem*)((size_t)nextCode + sizeof(short));
        gDP = (char* )nextCode;
        gDPUsage = dp_none;
        tsSig->minTime = tsSig->RCminTime;
        tsSig->maxTime = tsSig->RCmaxTime;
    }
    else
    {
        tsSig->is &= ~TRI_STATE;    // it really wasn't tri-state!
        if (!tsSig->evalCode)       // if pulled up, but no driver:
        {                           //  just make it stable to floatLevel
            gDP = (char* )nextCode;
            needBlock("codeATSSig", "stable", 4*sizeof(short),
                        dp_palDef);
            nextCode = (EqnItem* )gDP;
            tsSig->evalCode = nextCode;
            nextCode->opcode = LOAD_OP;
            nextCode = (EqnItem*)((size_t)nextCode + sizeof(short));
            codeSigAddr(tsSig, nextCode);
            nextCode->opcode = SAVEOR_OP;
            nextCode = (EqnItem*)((size_t)nextCode + sizeof(short));
            nextCode->opcode = STORE_OP;
            nextCode = (EqnItem*)((size_t)nextCode + sizeof(short));
            gDP = (char* )nextCode;
            gDPUsage = dp_none;
            tsSig->initLevel = tsSig->floatLevel;
        }
    }
}

//-----------------------------------------------------------------------------
// Initialize for a simulation run.

void initSignals()
{
    if (!gQuietMode)
        display("    initializing...\n");
    clock_t startRealTime = clock();

    // Create code for each tri-state signal
    for (Signal* signal = gSignals; signal < gNextSignal; signal++)
    {
        if (signal->is & TRI_STATE)
            codeATSSig(signal, 0);              // code a regular TS signal
    }
    freeTempSpace(&eventSpace);
    size_t signalFactor = (gNextSignal - gSignals) / 1000 + 1;
    int minEvents = 70000;
    size_t spaceForEvents = 1000000000;
    timeLineLen = (spaceForEvents/sizeof(Event) - minEvents) /
                  signalFactor + 500000;
    if (timeLineLen < 2000)
        timeLineLen = 2000;
    timeLineLen = timeLineLen / 1000 * 1000;
    gMaxEvents = timeLineLen * signalFactor + minEvents;
    allocTempSpace(&eventSpace, gMaxEvents);
    allocSpace(&timeLineSpace, timeLineLen);
    if (!gQuietMode)
    {
        display("    Allocated space for %ld events.\n",
                                        spaceForEvents/sizeof(Event));
        display("    Allocated %ld tick timeline.\n", (long)timeLineLen);
        display("    gMaxEvents=%ld.\n", (long)gMaxEvents);
    }
    // time line FIFO is 1/2 event history and 1/2 pending events
    gEventHistLen = timeLineLen >> 1;
    timeLineEnd = timeLine + timeLineLen;
    gTick = 0;
    gPrevEvtTick = -1;
    timeLineBaseTick = 0;
    timeLineHead = timeLine;

    for (int* p = (int* )timeLine; p < (int* )(timeLineEnd); )
        *p++ = 0;
    Event* event;
    for (event = events; event < events+gMaxEvents-1; event++)
    {
        event->next = event+1;
        event->is = FREE;
    }
    event->next = 0;
    event->is = FREE;
    freeEventList = events;

#ifdef WRITE_EVENTS
    // write event file header
    fprintf(gEvFile, "PVSimVersion: %s\n", gPSVersion);
    fprintf(gEvFile, "CompiledDate: %s\n", gPSDate);
    time_t curTime;
    time(&curTime);
    fprintf(gEvFile, "RunDate: %s", ctime(&curTime));
    fprintf(gEvFile, "TicksNS: %d\n\n", gTicksNS);
#endif

    // Initialize each signal to its given level, i.e., HSIGNAL makes a High
    Signal* signal;
    for (signal = gSignals; signal < gNextSignal; signal++)
    {
        Level newLevel = signal->initLevel;
        signal->level = newLevel;
        signal->nlevel = funcTable.INVERTtable[newLevel];
        signal->inLevel = newLevel;
        signal->preLevel = newLevel;
        signal->initDspLevel = newLevel;
        signal->lastTime = -1000;
        signal->lastInTime = -1000;
        signal->lastClkTm = -1000;
        signal->ambDepth = 0;
        signal->floatList = 0;
        signal->firstDispEvt = 0;
        signal->lastEvtPosted = 0;
        signal->randDlyCnt = 0;
#ifdef TRACE_ALL_EVENTS
        signal->is |= TRACED;   // to look at all events
#endif
        if (debugLevel(3))
            display("new signal %s: %s%s\n", signal->name,
                signal->is & DISPLAYED ? "D":"",
                signal->busOpt & DISP_BUS ? "B":"");

        if (signal->is & DISPLAYED)
        {
            if (signal->busOpt & DISP_BUS)
            {
#ifndef WRITE_EVENTS
                // create a display-bus Signal with its file location.
                newSignalPy(signal, newLevel, TRUE,
                (signal+1)->busBitNo,
                (signal+(signal->busWidth)-1)->busBitNo);
#endif
            }
            else
            {
                if ((signal->is & REGISTERED) ||
                   ((signal->is & TRI_STATE) && signal->floatLevel != LV_X))
                    addEvent(0, signal, newLevel, CLEAN);
                if (!(signal->is & REGISTERED))
                {
#ifdef WRITE_EVENTS
                    Token* srcLoc = signal->srcLoc;
                    if (srcLoc && srcLoc->tokCode == NAME_TOKEN)
                    {
                        // write signal with its file location
                        fprintf(gEvFile, "Signal %d=%c: %s %s %ld\n",
                          (int)(signal - gSignals), gLevelNames[newLevel],
                          signal->name, srcLoc->src->fileName,
                          (long)(srcLoc->pos - srcLoc->src->base));
                    }
                    else
                    {
                        // no known location
                        fprintf(gEvFile, "Signal %d=%c: %s - 0\n",
                          (int)(signal - gSignals), gLevelNames[newLevel],
                          signal->name);
                    }
#else
                    // create a Signal with its file location.
                    newSignalPy(signal, newLevel);
#endif
                }
            }
        }
    }
#ifdef EVENT_HISTORY
    gCurEvent = 0;
#endif

    // Now evaluate the equation for each signal to get its true initial level.
    // Pulled-up and Pulled-down signals are excluded.

    for (signal = gSignals; signal < gNextSignal; signal++)
    {
        if (!(signal->is & C_MODEL || signal->busOpt & DISP_BUS) &&
            !((signal->is & TRI_STATE) && signal->floatLevel != LV_X))
        {
            Level newLevel = evalSignal(signal);
            if (signal->is & REGISTERED)
                signal->inLevel = newLevel;
#if 0 // doesn't work.
            else if (signal->is & INTERNAL)
            {
                signal->level = newLevel;
                signal->nlevel = funcTable.INVERTtable[newLevel];
                signal->inLevel = newLevel;
                signal->preLevel = newLevel;
                signal->initDspLevel = newLevel;
            }
#endif
            else
            {
                signal->preLevel = newLevel;
                char ambType;
                if (funcTable.STABLEtable[newLevel] == LV_H)
                    ambType = CLEAN;
                else
                    ambType = STARTING_AMBIG;
                addEvent(0, signal, newLevel, ambType);
            }
        }
    }

    // Initialize local model variables

    Model::initVars();
    clock_t initRealTime = clock() - startRealTime;
    if (!gQuietMode)
        display("      [%3.1f sec]\n", (float)initRealTime/CLOCKS_PER_SEC);
}

//-----------------------------------------------------------------------------
// Remove objects to start a new simulation load.

void newSimulation()
{
    Model::removeAll();
    SimObject::deleteAll();     // recover all memory allocated by 'new'
    freeBlocks();
    freeTempSpace(&eventSpace);
    freeSpace(&timeLineSpace);
}

//-----------------------------------------------------------------------------
// Update all dependents of each changed signal, creating new events.

void updateDependents(Signal* signal)
{
#ifdef RANGE_CHECKING
    if (signal < gSignals || signal >= gNextSignal)
        throw new VError(verr_bug, "updateDependents: bad signal pointer");
#endif
    if (signal == gStopSignal && gTick > 1)
    {
        Tick newTEnd = gTick + (gEventHistLen >> 2);
        if (gTEnd > newTEnd)      // if stop signal true, end 1/4 display later
            gTEnd = newTEnd;
    }
    bool signalChangedVoltage =
                    changedVoltage[signal->level][signal->lastLevel];
    const FuncTable* func = &funcTable;

    // Go through signal's dependents list and possibly change each dependent

    for (SigNode* node = signal->dependList; node; node = node->next())
    {
        Signal* dependent = node->signal();
        if (!dependent)
            throw new VError(verr_bug, "BUG: missing dependent for %s",
                             signal->name);
        int minTime = dependent->minTime;
        int maxTime = dependent->maxTime;
        bool goneMeta = FALSE;
        bool fuzzyClkSetupViolation = FALSE;
        Level newLevel;
        char eventType;

        // for efficient break after time,
        if (dependent == gBreakSignal && gTick >= breakTick)
            dummy = 2;              // <-- set a breakpoint here

        // if a C-model dummy signal: do its task

        if (dependent->is & C_MODEL)
        {
            if (signalChangedVoltage)
            {
                Model* model = dependent->model;
                if (!model)
                    warnErr("Signal %s missing model!", dependent->name);
                else
                {
                    Model::setLastName(model->designator());
                    if (dependent == model->modelSig())
                            // only do a model's main event-handler signal
                        model->eval(signal);
                }
            }
            goto next;
        }

        // Registered signals only change on rising clock

        if (dependent->is & REGISTERED)
        {
            if (dependent->clock == signal)
            {                                // if dependent's clock changed
                if (dependent->clockEn &&
                    func->BSWALLOWtable[dependent->clockEn->level] != LV_H)
                    goto next;               // and clock enabled
                if (func->BSWALLOWtable[signal->level] != dependent->clockPol)
                    goto next;              // if active edge of clock
                if (dependent->reset &&
                    func->BSWALLOWtable[dependent->reset->level] == LV_H)
                    goto next;               // and output isn't being reset
                if (dependent->set &&
                    func->BSWALLOWtable[dependent->set->level] == LV_H)
                    goto next;               // and output isn't being set
                dependent->lastClkTm = gTick;
                if ((int)(gTick - dependent->lastInTime) <
                    dependent->setupTime ||
                    func->STABLEtable[dependent->inLevel] != LV_H)
                {
                    goneMeta = TRUE;       // go metastable if setup not met
                    fuzzyClkSetupViolation =
                            (signal->level != dependent->clockPol);
                    newLevel = LV_S;
                    maxTime += dependent->metaTime;
                }
                else                                      // latch new value
                {
                    newLevel = dependent->inLevel;
                    if (newLevel == dependent->clockPol)
                        newLevel = signal->level;
                    else if (newLevel ==
                            func->INVERTtable[dependent->clockPol])
                        newLevel = (Level)func->INVERTtable[signal->level];
                    else if (signal->level == dependent->clockPol)
                        newLevel = LV_S;
                    else
                        newLevel = LV_X;
                }
                if (dependent->is & TRACED)
                    printfEvt("(clocked: %12s=%c by %s)\n",
                      dependent->name, gLevelNames[newLevel], signal->name);
            }
            else                            // re-evaluate dependent's input
            {

                newLevel = evalSignal(dependent);

                if (dependent->set == signal &&
                    func->BSWALLOWtable[dependent->set->level] == LV_H)
                {
                                               // if dependent's set occured
                    dependent->inLevel = newLevel;
                    if (dependent->is & TRACED)
                        printfEvt("(set    : %12s=%c by %s)\n",
                          dependent->name, gLevelNames[newLevel], signal->name);
                }
                else if (dependent->reset == signal &&
                    func->BSWALLOWtable[dependent->reset->level] == LV_H)
                {
                                             // if dependent's reset occured
                    dependent->inLevel = newLevel;
                    if (dependent->is & TRACED)
                        printfEvt("(reset  : %12s=%c by %s)\n",
                          dependent->name, gLevelNames[newLevel], signal->name);
                }
                else
                {                               // input to register changed
                    if (newLevel == dependent->inLevel)
                        goto next;
                    if (dependent->is & TRACED)
                        printfEvt("(inp chg: %12s=%c by %s)\n",
                          dependent->name, gLevelNames[newLevel], signal->name);
                    dependent->lastInTime = gTick;
                    dependent->inLevel = newLevel;          // input changed
                    if ((int)(gTick - dependent->lastClkTm) >= dependent->holdTime)
                        goto next;

                    goneMeta = TRUE;        // go metastable if hold not met
                    newLevel = LV_S;
                    maxTime += dependent->metaTime;
                }
            }
        }

        // else (not reg'ed), it's a combinatorial input: determine new level

        else
            newLevel = evalSignal(dependent);

        eventType = CLEAN;

        // special case of capacitance type being released is no-op

        if ((dependent->is & TRI_STATE) && dependent->floatLevel == LV_Z &&
            (newLevel == LV_Z || newLevel == LV_D || newLevel == LV_U))
                goto next;

        // if signal can float, determine new level (if any) to go to

        if ((dependent->is & TRI_STATE) && dependent->floatLevel != LV_X)
        {
            if (newLevel == dependent->inLevel)
                goto next;
            dependent->inLevel = newLevel;                  // input changed
            if (newLevel == LV_Z)
            {
                newLevel = dependent->floatLevel;
                eventType = FLOATING;
            }
            else if (newLevel == LV_U)           // level U = (Z->L or L->Z)
            {
                if (dependent->floatLevel == LV_V)       // pulldowns ignore
                    goto next;
                if (dependent->level != dependent->floatLevel &&
                    dependent->level != LV_H)
                {
                                              // disabling after pullup time
                    if (dependent->floatLevel == LV_W)
                        newLevel = LV_R;
                    eventType = FLOATING;
                }
                else
                {
                    newLevel = LV_F;                      // enabling to low
                    minTime = 1;
                    maxTime = 1;
                }
            }
            else if (newLevel == LV_D)          // level D = (Z->H or H->Z)
            {
                if (dependent->floatLevel == LV_W)         // pullups ignore
                    goto next;
                if (dependent->level != dependent->floatLevel &&
                    dependent->level != LV_L)
                {
                                            // disabling after pulldown time
                    if (dependent->floatLevel == LV_V)
                        newLevel = LV_F;
                    eventType = FLOATING;
                }
                else
                {
                    newLevel = LV_R;                     // enabling to high
                    minTime = 1;
                    maxTime = 1;
                }
            }
            else                                        // enabled: no delay
            {
                minTime = 1;
                maxTime = 1;
            }
        }

        // If this new level is really new,
        // post an event of changing to the new level

        if ((func->BSWALLOWtable[newLevel] != dependent->preLevel) &&
                                (newLevel != dependent->preLevel))
        {                      // (BSWALLOW makes weak-high match high, etc.)

            // check if new level is going into or out of an ambiguous level
            char ambType;
            if (func->STABLEtable[dependent->preLevel] == LV_H)
            {
                if (func->STABLEtable[newLevel] == LV_H)
                    ambType = CLEAN;
                else
                    ambType = STARTING_AMBIG;
            }
            else
            {
                if (func->STABLEtable[newLevel] == LV_H)
                    ambType = ENDING_AMBIG;
                else
                    ambType = CLEAN;
            }

            // if metastable and signal is random-after-meta, pick a level
            Level metaVal = dependent->aMetaLevel;
            if (metaVal == LV_C)
                metaVal = (randomRng(2) ? LV_H : LV_L); // 'C' is from randomR

            // else (not random), if edge is solid, post a single event
            else if (minTime == maxTime)
            {
                addEvent(minTime, dependent, newLevel, ambType + eventType);
                if (goneMeta)                 // if metastable: force end to
                {                             // metaVal
                    minTime += 2;
                    if (dependent->metaTime > minTime)
                        minTime = dependent->metaTime;
                    addEvent(minTime, dependent, metaVal,
                        ENDING_AMBIG + eventType);
                }
            }

            // else edge has ambiguity: post starting and ending events
            else
            {
                Level ambLevel =
                       (Level)func->MINMAXtable[newLevel][dependent->preLevel];

                if (ambType != ENDING_AMBIG)            // start of ambiguity
                    addEvent(minTime, dependent, ambLevel,
                        STARTING_AMBIG + eventType);

                if (ambType != STARTING_AMBIG)           // end of ambiguity
                {
                    if (goneMeta)             // if metastable: force end to
                        ambLevel = metaVal;    // metaVal

                    addEvent(maxTime, dependent, ambLevel,
                        ENDING_AMBIG + eventType);
                }
            }

            // save current level for successive tests

            if (goneMeta)
            {
                dependent->preLevel = metaVal;
                if (fuzzyClkSetupViolation)
                    dependent->mode |= MODE_WENT_META;
            }
            // if setup or hold violated on prev edge, expect a late meta event
            else if (!(dependent->mode & MODE_WENT_META))
                dependent->preLevel = newLevel;
        }

    next:
        if (!goneMeta || !fuzzyClkSetupViolation)
            dependent->mode &= ~MODE_WENT_META; // reset flag in other cases
    }
}

#ifdef CHECK_SIGNAL_EVENTS
//-----------------------------------------------------------------------------
// Check a signal's events list.

void checkSignalEvents(Signal* signal)
{
    for (Event* e = signal->firstDispEvt; e; e = e->nextInSignal)
        if (e->signal != signal)
            throw new VError(verr_bug,
                             "BUG: bad event list fwd ptr in %s, E=%08x",
                             signal->name, (int)e);
    for (Event* e = signal->lastEvtPosted; e; e = e->prevInSignal)
        if (e->signal != signal)
            throw new VError(verr_bug,
                             "BUG: bad event list prev ptr in %s, E=%08x",
                             signal->name, (int)e);
}
#endif

//-----------------------------------------------------------------------------
// Remove an event from the event pool and clean up its signal's links.

void removeEvent(Event** link, Event* event)
{
    if (event->is & FREE)
        return;
    Signal* signal = event->signal;
                                // if not a model's dummy signal
    if (signal && !((signal->is & C_MODEL) && (signal->is & REGISTERED)))
    {
#ifdef CHECK_SIGNAL_EVENTS
        if (signal->is & TRACED)
            checkSignalEvents(signal);
#endif
        // remove event from signal's event list
        event->removeFromSignal();
        if (signal->is & TRACED)
            printfEvt("rmEvent: %s E=%08x S=%08x S.F=%08x amb=%d\n",
                signal->name, (size_t)event,
                (size_t)signal, (size_t)signal->firstDispEvt, signal->ambDepth);
#ifdef CHECK_SIGNAL_EVENTS
                checkSignalEvents(signal);
#endif
    }
    if (link)
        *link = event->next;
    event->next = freeEventList;
    freeEventList = event;
    event->is |= FREE;
}

//-----------------------------------------------------------------------------
// Run the simulation for one tick.

void sim1Tick(Event** tickEventLink, Event* lastTickEvent)
{
    // update all changed-signal levels at current time
    Event** link = tickEventLink;
    Event* event;
    for (event = *link; event != lastTickEvent; event = *link)
    {
        Signal* signal = event->signal;
#if 1
        if (signal->is & TRACED)
            printfEvt("Event 0x%08x: %12s=%c at %2.3f nx=0x%08x\n",
                  (size_t)event, signal->name,
                  gLevelNames[event->level], (float)event->tick/gTicksNS,
                  (size_t)event->next);
#ifdef WRITE_EVENTS
        //if (!signal->model)   // (no: kills wire events)
        if (signal->is & DISPLAYED)
        {
            if (gPrevEvtTick != gTick)
            {
                fprintf(gEvFile, "\n%ld:", gTick);
                gPrevEvtTick = gTick;
            }
            fprintf(gEvFile, " %d=%c",
                (int)(signal - gSignals), gLevelNames[event->level]);
        }
        if (event->is & ATTACHED_TEXT)
        {
            fprintf(gEvFile, "\"%s\"", event->attText);
        }
#else
        // Python-extension version: add event to signal's list
        if (signal->is & DISPLAYED)
        {
            PyObject* val = Py_BuildValue("c", gLevelNames[event->level]);
            addEventPy(signal, gTick, val);
        }
        if (event->is & ATTACHED_TEXT)
        {
            PyObject* val = Py_BuildValue("(cs)",
                gLevelNames[event->level], event->attText);
            addEventPy(signal, gTick, val);
        }
#endif
#endif
        if (signal == gBreakSignal && gTick >= breakTick)
            dummy = 1;

        if (event->is & DELETED_FLOAT)  // remove deleted float events
        {
            removeEvent(link, event);
            continue;
        }
        if (event->is & (WAKEUP | ATTACHED_TEXT))
        {
            link = &event->next;
            continue;
        }
        char ambiguity = event->is & SOME_AMBIG;
#ifdef RANGE_CHECKING
        if (event < events || event >= events+gMaxEvents)
            throw new VError(verr_bug,
                             "simulate: bad event pointer, updating %s",
                             signal->name);
        if (signal < gSignals || signal >= gNextSignal)
            throw new VError(verr_bug,
                             "simulate: bad signal pointer, updating %s",
                             signal->name);
#endif
        //
        // when a solid event occurs for a tri-state signal, remove
        // all future events in the signal's floatList.
        //
        if (signal->is & TRI_STATE)
        {
            Event* currentEvent = event;
            if(event->is & FLOATING)
            {
                if (signal->floatList == currentEvent)
                    signal->floatList = 0;
                else
                    for (event = signal->floatList; event;
                                      event = event->nextFloat)
                        if (event->nextFloat == currentEvent)
                        {
                            event->nextFloat = event->nextFloat->nextFloat;
                            break;
                        }
#ifdef RANGE_CHECKING
                if (event == 0)
                {
                    Debugger();
                    throw new VError(verr_bug, "simulate: not in floatList: %s",
                        signal->name);
                }
#endif
            }
            else
            {
                for (event = signal->floatList; event;
                                        event = event->nextFloat)
                {
#ifdef RANGE_CHECKING
                    if (event->signal != signal)
                    {
                        Debugger();
                        throw new VError(verr_bug, 
                          "simulate: bad floatList event, removing %s",
                          signal->name);
                    }
#endif
                    if (event->tick >= gTick)
                    {
                        if (signal->is & TRACED)
                            printfEvt("[***removed %12s=%c at %2.3f, amb=%d]\n",
                              signal->name, gLevelNames[event->level],
                              (float)event->tick/gTicksNS, signal->ambDepth);
                        if ((event->is & SOME_AMBIG) ==
                                        STARTING_AMBIG)
                            event->signal->ambDepth++;
                        else if ((event->is & SOME_AMBIG) ==
                                        ENDING_AMBIG)
                        {
#ifdef NEG_CHECK
                            if (event->signal->ambDepth <= 0)
                                throw new VError(verr_bug, 
                                    "BUG: sim1TickBin: at %2.3fns %s ambDepth"
                                    " negative",
                                    (float)gTick/gTicksNS, event->signal->name);
#endif
                            event->signal->ambDepth--;
                        }
                        event->is |= DELETED_FLOAT;
                    }
                }
                signal->floatList = 0;
            }
            event = currentEvent;
        }
        if (ambiguity == CLEAN)
        {
            if (signal->level != event->level || (signal->busOpt & DISP_STATE))
            {
                if (signal->is & TRACED)
                    printfEvt("(changed: %12s=%c)\n",
                      signal->name, gLevelNames[event->level]);
                signal->lastLevel = signal->level;
                signal->level = event->level;
                signal->nlevel = funcTable.INVERTtable[event->level];
                eventCount++;
                link = &event->next;
            }
            else
            {
                if (signal->is & TRACED)
                    printfEvt("[***removed %12s=%c]\n",
                      signal->name, gLevelNames[event->level]);
                removeEvent(link, event);           // no change: remove
            }
        }
        else if (ambiguity == STARTING_AMBIG)       // min-time edge
        {
            signal->ambDepth++;
            if (signal->level != LV_X &&
                signal->level != event->level)
            {
                if (signal->ambDepth > 1)
                {
                    if (signal->is & TRACED)
                        printfEvt("(changed: %12s=%cb [X] {amb=%d})\n",
                         signal->name, gLevelNames[event->level],
                         signal->ambDepth);
                    signal->lastLevel = signal->level;
                    signal->level = LV_X;   // a mix: show changing
                    event->level = signal->level;   // back-annotate event level
                    signal->nlevel = LV_X;
                }
                else                        // else show rise, etc.
                {
                    if (signal->is & TRACED)
                        printfEvt("(changed: %12s=%cb {amb=%d})\n",
                          signal->name, gLevelNames[event->level],
                          signal->ambDepth);
                    signal->lastLevel = signal->level;
                    signal->level = event->level;
                    event->level = signal->level;   // back-annotate event level
                    signal->nlevel = funcTable.INVERTtable[event->level];
                }
                eventCount++;
                link = &event->next;
            }
            else
            {
                if (signal->is & TRACED)
                    printfEvt("[***removed %12s=%cb {amb=%d}]\n",
                      signal->name, gLevelNames[event->level],
                      signal->ambDepth);
                removeEvent(link, event);           // no change: remove
            }
        }
        else                            // AMB_END:  max-time edge
        {
#ifdef NEG_CHECK
            if (signal->ambDepth <= 0)
                throw new VError(verr_bug,
                    "BUG: sim1TickBin: at %2.3fns %s ambDepth negative",
                    (float)gTick/gTicksNS, signal->name);
#endif
            signal->ambDepth--;
            if (signal->ambDepth == 1 && signal->level != event->level)
            {                         // near end: show rise, etc.
                signal->lastLevel = signal->level;
                signal->level = event->level;
                event->level = signal->level;   // back-annotate event level
                signal->nlevel = funcTable.INVERTtable[event->level];
                if (signal->is & TRACED)
                    printfEvt("(changed: %12s=%ce {amb=%d})\n",
                      signal->name, gLevelNames[event->level],
                      signal->ambDepth);
                eventCount++;
                link = &event->next;
            }
            else if (signal->ambDepth == 0)
            {                                  // end: show stable
                signal->lastLevel = signal->level;
                signal->level = funcTable.BSWALLOWtable[event->level];
                event->level = signal->level;   // back-annotate event level
                signal->nlevel = funcTable.INVERTtable[signal->level];
                if (signal->is & TRACED)
                    printfEvt("(changed: %12s=%ce [%c] {amb=%d})\n",
                      signal->name, gLevelNames[event->level],
                      gLevelNames[signal->level], signal->ambDepth);
                eventCount++;
                link = &event->next;
            }
            else
            {
                if (signal->is & TRACED)
                    printfEvt("[***removed %12s=%c {amb=%d}]\n",
                      signal->name, gLevelNames[event->level],
                      signal->ambDepth);
                removeEvent(link, event);           // no change: remove
            }
        }
#ifdef EVENT_HISTORY
        if (signal->is & TRACED)
        {
            Event* cause = event->cause;
            if (cause)
                printfEvt("           cause: at %2.3f, %12s=%c\n",
                  (float)cause->tick/gTicksNS,
                  cause->signal->name, gLevelNames[cause->level]);
        }
#endif
    }

    for (event = *tickEventLink; event != lastTickEvent; event = event->next)
    {              // update all dependents of each changed signal
        if (event->is & ATTACHED_TEXT || event->tick != gTick)
            continue;
#ifdef RANGE_CHECKING
        if (event < events || event >= events+gMaxEvents)
            throw new VError(verr_bug, "simulate: bad event pointer");
#endif
#ifdef EVENT_HISTORY
        gCurEvent = event;
#endif
        Signal* signal = event->signal;
        if (!signal)
            display("// *** BUG: sim1TickBin: event's signal is zero at %2.3f\n",
                (float)gTick/gTicksNS);
        else
        {
            if (event->is & WAKEUP)   // C-model dummy signal: do its task
            {
                Model::setLastName(signal->model->designator());
                try {
                    signal->model->eval(event->level);
                } catch(VError* err)
                {
                    throw(err);
                } catch(...)
                {
                    throw new VError(verr_illegal, "Runtime error");
                }
            }
            else
                updateDependents(signal);
            if (changedVoltage[signal->level][signal->lastLevel])
                signal->lastTime = gTick;
        }
    }
}

//-----------------------------------------------------------------------------
// Run the simulation for the given duration and leave the resulting events
//  in the time line array, corresponding to ticks gDispTStart to gTEnd.

void simulate()
{
    int nsEnd;
    if (gNSDuration == 0)
    {
        nsEnd = 200000000;
        gNSDuration =  200000000;
    }
    else
        nsEnd = gNSStart + gNSDuration;

    Tick tStart = gNSStart * gTicksNS;
    gTEnd = nsEnd * gTicksNS;

    initSignals();
    gOpenFiles = 0;

    if (!gQuietMode)
        display("    simulating from %2.3f ns to %2.3f ns ...\n\n",
            (float)gNSStart, (float)nsEnd);

    clock_t startRealTime = clock();
    eventCount = 0;
    Event** timeLineTail = timeLineHead + gEventHistLen;

    // Main loop: loop for each tick bin

    Tick startBin = tStart / gTickBinSize;
    Tick endBin =   gTEnd  / gTickBinSize;
    for (Tick tickBin = startBin; tickBin < endBin; tickBin++)
    {
        Event* tailEventList = *timeLineTail;   // free up tail slot in time
                                                // line FIFO
        if (tailEventList)
        {
#ifdef EXTENSION
            throw new VError(verr_memOverflow,
                            "too many events: increase spaceForEvents");
#endif
            Event* nextEvent = NULL;
            for (Event* event = tailEventList; event; event = nextEvent)
            {
                nextEvent = event->next;
                // If removing first displayed event, adjust initial disp level
                // (if not a model's dummy signal)
                Signal* signal = event->signal;
                if (signal && !((signal->is & C_MODEL) &&
                    (signal->is & REGISTERED)) &&
                    signal->firstDispEvt == event)
                    signal->initDspLevel = event->level;

                removeEvent(0, event);
            }
            *timeLineTail = 0;
        }
        if (debugLevel(4))
            display("%2.3f bin\n", (float)tickBin*gTickBinSize/gTicksNS);

        // loop for each different tick time in this bin
        for (Event** link = timeLineHead; *link; )
        {
            Event* e = *link;
            gTick = e->tick;
            // find all events in tick bin at current tick
            if (debugLevel(2))
                display("link=0x%p tick %d e=0x%p", link, (int)gTick, e);
            for ( ; e && e->tick == gTick; e = e->next)
            {
                //display(" e=0x%p %d sig=%p ", e, e->tick, e->signal);
                //if (e->signal)
                //    display("%s\n", e->signal->name);
                //else
                //    display("?\n");
            }

            if (debugLevel(2))
                display(" sim1Tick(link=0x%p, e=0x%p)\n", link, e);
            sim1Tick(link, e);      // simulate events at one tick

            // sim1Tick may have removed some events in the list starting at
            // link, so we have to restart our search for the next tick value
            // from there.
            if (debugLevel(2))
                display(" find next tick\n");
            for (e = *link; e && e->tick == gTick; e = *link)
                link = &e->next;
        }

        timeLineHead++;                 // advance time line pointers
        if (timeLineHead >= timeLineEnd)
        {
            timeLineBaseTick += gTickBinSize;
            timeLineHead -= timeLineLen;
        }
        timeLineTail++;
        if (timeLineTail >= timeLineEnd)
            timeLineTail -= timeLineLen;
    }

    clock_t simRealTime = clock() - startRealTime;
    if (!gFlaggedErrCount)
    {
        if (!gQuietMode)
            display("    [%3.1f sec, %3.1f Kevents/sec]\n",
                (float)simRealTime/CLOCKS_PER_SEC,
                ((float)eventCount/1000)/((float)simRealTime/CLOCKS_PER_SEC));
        if (gWarningCount)
            display("\n*** %d WARNING%s ***\n", gWarningCount,
                (gWarningCount == 1 ? "" : "S"));
        else if (!gQuietMode)
            display("\ndone.\n");
        for (OpenFile* f = gOpenFiles; f; f = f->next)
            fclose(f->file);
    }

    // unwrap time line FIFO so it starts at tick gDispTStart

    gDispTStart = (int)gTEnd - gEventHistLen;
    if (gDispTStart < tStart)
        gDispTStart = tStart;
    size_t timeLineLen = (timeLineEnd - timeLine);
    allocTempSpace(&tempTLSpace, timeLineLen);
    Event** p1 = timeLineHead - (int)(gTEnd - gDispTStart);
    if (p1 < timeLine)
        p1 += timeLineLen;
    Event** p2;
    for (p2 = tempTL; p2 < tempTL + timeLineLen; )
    {
        *p2++ = *p1++;
        if (p1 >= timeLineEnd)
            p1 = timeLine;
    }
    p2 = tempTL;
    for (p1 = timeLine; p1 < timeLineEnd; )
        *p1++ = *p2++;
    freeTempSpace(&tempTLSpace);
    timeLineBaseTick = 0;
    timeLineHead = timeLine;
}
