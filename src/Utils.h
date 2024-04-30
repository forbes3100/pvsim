// ****************************************************************************
//
//          PVSim Verilog Simulator Utilities Interface
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

#include <stdio.h>
#include <string.h>
#ifdef DO_PROFILE
#include <profile.h>
#endif

// #define RANGE_CHECKING
// #define TRAP_ERRORS
// #define SHOW_SOURCE

const int size_memChunk =          200000;
const int max_hashCodes =           10000;
const unsigned int max_nameLen =    100;
const unsigned int max_messageLen = 1000;

//  field offsets, e.g. "move.w  d0,OFFSET(Rect,bottom)(a2)"
#define OFFSET(type, field)     ((size_t) &((type* ) 0)->field)

const bool TRUE =  1;
const bool FALSE = 0;

extern inline int min(int a, int b) { return a < b ? a : b; }
extern inline int max(int a, int b) { return a > b ? a : b; }

// Base class for all objects that are removed before the next compile

class SimObject
{
    SimObject* prevObj;     // doubly-linked list of all simulator objects
    SimObject* nextObj;
    int     signature;      // proof-of-validity
    void    verifyList();

public:
    static SimObject* simObjList;

            SimObject();
    virtual ~SimObject();       // virtual so 'delete' gets passed actual size
    static void deleteAll();
    friend class Src;
};

// Memory space structure: used by allocSpace, etc.

struct Space
{
    const char* name;       // space's name, for error reporting
    char*   mallocBase;     // real base address of space
    void*   base;           // pointer to even-longword base address
    int     elemSize;       // size of each element in space
    void*   end;            // pointer to current end pointer
    void*   limit;          // pointer to limit pointer
    void*   handle;         // Mac temp space handle
};

// Linked list of multiple spaces, used for storing a growing group of
// fixed-size objects that get deleted between runs.

class SpaceList
{
    char*       base;
    char*       end;
    char*       limit;
    Space       space;
    SpaceList*  nextToBeFilled;
    SpaceList*  nextFilled;

public:
                SpaceList(SpaceList* prev, char* name, short elemSize,
                          long numElems);
    SpaceList*  free();
    friend class SigNode;
};

struct SrcFile: SimObject
{
    FILE*   fp;     // file pointer, if file open
    //Handle    handle; // handle to text in temp memory, if any
    int     len;
};

typedef int AEErrorCode;

enum MainErrorCode
{
    merr_retry = 1,
    merr_reportError = -2
};

enum VErrCode
{
    verr_notFound,
    verr_illegal,
    verr_notYet,
    verr_memOverflow,
    verr_io,
    verr_bug,
    verr_break,
    verr_stop
};


class Token;

class VError : SimObject
{
public:
    VErrCode    code;
    Token*      srcPos;
    char*       message;

                VError(VErrCode code, const char* fmt, ...);
                VError(Token* srcPos, VErrCode code, const char* fmt, ...);
    bool        is(VErrCode code)       { return this->code == code; }
    void        display();
};

struct VL
{
    static class Src*   baseSrc;        // base Verilog file source
    static int  debugLevel;     // debugging display detail level
};

extern inline bool debugLevel(int level)
{
    return VL::debugLevel >= level;
}

// Memory usage codes for gDPUsage

enum DPUsageCode
{
    dp_none = 0,
    dp_palDef
};

//-----------------------------------------------------------------------------
// Temporary name string class

class TmpName
{
    char    name[max_nameLen];

public:
    TmpName&    operator = (const char* name)
                                { strncpy(this->name, name, max_nameLen);
                                  return *this; }
    TmpName&    operator += (const char* s)
                                { strncat(this->name, s, max_nameLen);
                                  return *this; }
                TmpName();
                TmpName(const char* fmt, ...);
                operator char* ()       { return this->name; }
};

// -------- global variables --------

extern int      gWarningCount;
extern int      gFlaggedErrCount;
extern bool     gQuietMode;
extern bool     gTagDebug;
extern bool     gFatalLoadErrors;   // true if fatal errors while loading
extern char*    gDP;                // pointer to current free memory space
extern char*    gDPEnd;             // pointer to end of free memory
extern DPUsageCode gDPUsage;        // memory usage code
extern char*    gBlockList;         // linked list of allocated blocks
extern FILE*    gLogFile;
extern char     gLibPath[];         // ~/Library path
extern char     gProjName[];        // project file (.psim) base name
extern char     gProjFullPathName[]; // project file name with path
extern int      gStopRequest;       // key interrupted compiling or simulating

extern int      gLastRand;          // last random number from random()

// -------- global function prototypes --------

// I/O
void display(const char* f, ...);   // display formatted text in log window
void printfEvt(const char* f, ...); // print formatted event message to
                                    //   console and event file
void initFiles();
FILE* openFile(const char* name, const char* permission,
               bool createIfMissing = FALSE);
void closeFile(FILE* file);

// Error handling
void reportErrDialog(const char* fmt, ...);
void reportMemErr(const char* procName, const char* itemName, long neededBytes);
void showMsgInEditor(const char* msg, const char* fileName, int line);
void breakInEditor(Token* srcLoc);

// Memory allocation
void reAllocSpace(Space* space, long newNumElems);
void allocSpace(Space* space, long numElems);
void freeSpace(Space* space);
void allocTempSpace(Space* space, long numElems);
void freeTempSpace(Space* space);
void needBlock(const char* procName, const char* itemName, long neededBytes,
               DPUsageCode usage);
void freeBlocks();

// Misc
void resetRandSeed(int seed);
int randomRng(int range);           // return random number between 0-range

extern "C" { void Debugger(); }
