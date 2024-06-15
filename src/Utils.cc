// ****************************************************************************
//
//              PVSim Verilog Simulator Utilities
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
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>
#include <time.h>

#include "Model.h"
#include "Utils.h"

const int MAX_OPEN_FILES = 20;
//#define MEM_STATS     // define to display malloc statistics
//#define DEBUG_NEW

// -------- global variables --------

SimObject* SimObject::simObjList;
int     gWarningCount;
int     gFlaggedErrCount;
bool    gFatalLoadErrors;   // true if fatal errors detected while loading
char*   gDP;                // pointer to current free memory space
char*   gDPEnd;             // pointer to end of free memory
DPUsageCode gDPUsage;       // memory usage code
char*   gBlockList;         // linked list of allocated blocks
FILE*   gLogFile;
FILE*   curOpenFiles[MAX_OPEN_FILES];   // array of currently open files
long    gSpacesTotal;       // total space memory used by mallocs
long    gDPTotal;           // total dictionary memory used by mallocs

const char* separatorLine =
"// *********************************************************************";

// -------- global variables --------

int     gLastRand;      // last random number from random()

//-----------------------------------------------------------------------------
// Open a file and remember it in case we have to close it on an error.

FILE* openFile(const char* name, const char* permission, bool createIfMissing)
{
    char* uname = new char[strlen(name) + 1];
    strcpy(uname, name);

    for (int i = 0; i < MAX_OPEN_FILES; i++)
        if (curOpenFiles[i] == 0)
        {
            FILE* file = fopen(uname, permission);
            if (!file)
            {
                if (createIfMissing)
                    file = fopen(uname, "w");
                if (!file)
                    throw new VError(verr_io, "Failed to open file \"%s\"",
                                     uname);
            }
            curOpenFiles[i] = file;
            return (file);
        }
    throw new VError(verr_io, "too many files open at once!");
    return (0);
}

//-----------------------------------------------------------------------------
// Initialize open-files list.

void initFiles()
{
    for (int i = 0; i < MAX_OPEN_FILES; i++)
        curOpenFiles[i] = 0;
}

//-----------------------------------------------------------------------------
// Close a file and remove it from the open-file list.

void closeFile(FILE* file)
{
    for (int i = 0; i < MAX_OPEN_FILES; i++)
        if (curOpenFiles[i] == file)
        {
            fclose(file);
            curOpenFiles[i] = 0;
            return;
        }
    throw new VError(verr_io, "closeFile BUG: file not open!");
}

//-----------------------------------------------------------------------------
// Add given string to the string space, and return a pointer to
//  this copy.

char* newString(const char* s)
{
    size_t len = strlen(s);

    char* sCopy = gNextString;
    if (sCopy < gStrings || sCopy+len > gStrings + gMaxStringSpace)
        throw new VError(verr_memOverflow, "string space overflow");
    gNextString += (len + 1);
    strcpy(sCopy, s);
    return (sCopy);
}

//-----------------------------------------------------------------------------
// Construct a temporary name string variable.

TmpName::TmpName()
{
    this->name[0] = 0;
}

//-----------------------------------------------------------------------------
// Set a temp name to formatted printf-like arguments.

TmpName::TmpName(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(this->name, max_nameLen-1, fmt, ap);
    va_end(ap);
}

//-----------------------------------------------------------------------------
// Construct a throwable error from a printf-like format and args. It assumes
// gScToken location.

VError::VError(VErrCode code, const char* fmt, ...)
{
    this->srcPos = gScToken;
    this->code = code;
    va_list ap;
    va_start(ap, fmt);
    this->message = new char[max_messageLen];
    vsnprintf(this->message, max_messageLen-1, fmt, ap);
    va_end(ap);
}

//-----------------------------------------------------------------------------
// Construct a throwable error from a printf-like format and args. Also
// uses given token for the location of the error in the source, usually from
// a previously-compiled expression.

VError::VError(Token* srcPos, VErrCode code, const char* fmt, ...)
{
    this->srcPos = srcPos;
    this->code = code;
    va_list ap;
    va_start(ap, fmt);
    this->message = new char[max_messageLen];
    vsnprintf(this->message, max_messageLen-1, fmt, ap);
    va_end(ap);
}

//-----------------------------------------------------------------------------
// Display an error message string and its surrounding source context.

void VError::display()
{
    const char* fileName = 0;
    size_t pos = 0;
    Token* srcPos = this->srcPos;

    if (srcPos)
    {
        // if inside macro text, pop out of it
        Src* errSrc = srcPos->src;
        while (errSrc  && !errSrc->fileName)
                errSrc = errSrc->parent;
        if (errSrc)
        {
            // print 5 lines of source text before and including error line
            ::display("\n%s\n", separatorLine);         // print top separator
            char *p = errSrc->base;
            char* pline = p;
            for (int i = max(srcPos->line - 5, 0); *p && i > 0; i--)
            {
                while (*p && *p != '\n')
                    p++;
                if (*p)
                    p++;
            }
            for (int i = min(srcPos->line, 5); *p && i > 0; i--)
            {
                pline = p;
                while (*p && *p != '\n')
                {
                    if (*p == '%')      // remove those nasty '%'s so printf
                        *p = 'p';       // doesn't choke
                    p++;
                }
                *p++ = 0;
                ::display("%s\n", pline);
            }

            // print marker under error character position
            for (p = pline; p < srcPos->pos; p++)
            {
                if (*p == '\t')
                    ::display("\t");
                else
                    ::display(" ");
            }
            fileName = errSrc->fileName;
            pos = srcPos->pos - errSrc->base;
            ::display("^\n// *** ERROR in file '%s', line %d:\n// ***   %s\n",
                    fileName, srcPos->line, this->message);
        }
        else
            ::display("\n// *** ERROR: %s\n", this->message);
    }
    else
        ::display("\n// *** ERROR: %s\n", this->message);

    ::display(separatorLine);
    ::display("\n");

    if (fileName)
        showMsgInEditor(this->message, fileName, (int)pos);
}

//-----------------------------------------------------------------------------
// Memory allocation error: print error message showing which routine
//  failed to allocate how much memory and what it was needed for.

void reportMemErr(const char* procName, const char* itemName, long neededBytes)
{
    throw new VError(verr_memOverflow,
                        "Out of memory in %s: %d bytes needed for %s.",
                        procName, neededBytes, itemName);
}

//-----------------------------------------------------------------------------
// Re-allocate a memory space, keeping the data in the space intact. The
//  space's pointers will be updated to point into the New space.

void reAllocSpace(Space* space, long newNumElems)
{
    long maxElems = (*(char**)space->limit - *(char**)space->base) /
                    space->elemSize;
    long curElems = (*(char**)space->end   - *(char**)space->base) /
                    space->elemSize;
    long size = space->elemSize * newNumElems;

    if (space->mallocBase != 0)
    {
        if (newNumElems > maxElems)
        {
            char* mallocBase = (char* )realloc(space->mallocBase, size + 4);
            if (mallocBase == 0)
                reportMemErr("reAllocSpace", space->name, size);
            char* base = (char* )((unsigned long)mallocBase +
                                ((unsigned long)*(char**)space->base -
                                 (unsigned long)space->mallocBase));
            space->mallocBase = mallocBase;
            *(char**)space->base = base;
            *(char**)space->end = base + (curElems * space->elemSize);
            *(char**)space->limit = base + size;
        }
    }
    else
    {
#ifdef MEM_STATS
        gSpacesTotal += size + 4;
        display("malloc(%4ldK) for %5s, total=%5ldK\n",
                size/1024, space->name, gSpacesTotal/1024);
#endif
        char* mallocBase = (char* )malloc(size + 4);
        if (mallocBase == 0)
            reportMemErr("reAllocSpace", space->name, size);
        space->mallocBase = mallocBase;
        char* base = (char* )(((unsigned long)mallocBase + 3) & ~3);
        *(char**)space->end = *(char**)space->base = base;
        *(char**)space->limit = base + size;
    }
}


//-----------------------------------------------------------------------------
// Free the memory occupied by a space, and clear all of its pointers.

void freeSpace(Space* space)
{
    *(char**)space->end = *(char**)space->base;
}

//-----------------------------------------------------------------------------
// Allocate a memory space, sized to numElems elements. The space's pointers
//  will be initialized to point into the New space.

void allocSpace(Space* space, long numElems)
{
    freeSpace(space);
    reAllocSpace(space, numElems);
}

//-----------------------------------------------------------------------------
// Check to see that we have at least 'neededBytes' more space after
//  gDP.  It will try to allocate the needed memory and print an error
//  message if it can't. If both usageCode and the stored gDPUsage are
//  not dp_none, then they must match.  A block of a given usage type
//  will be made contiguous.

void needBlock(const char* procName, const char* itemName, long neededBytes,
               DPUsageCode usageCode)
{
    if (usageCode != gDPUsage)
    {
        if (gDPUsage != dp_none)
            throw new VError(verr_memOverflow,
                    "%s for %s: dictionary space in use\n'var xxx ...'"
                    " must have a matching 'endBlock'", procName, itemName);
    }
    if (gDP + neededBytes > gDPEnd)
    {
        if (gDPUsage != dp_none)
            throw new VError(verr_memOverflow,
                    "%s for %s: exhausted chunk space", procName, itemName);
        long size = (neededBytes & ~3) + size_memChunk + sizeof(char* );
#ifdef MEM_STATS
        gDPTotal += size;
        display("malloc(%4ldK) for dict , total=%5ldK\n",
                size/1024, gDPTotal/1024);
#endif
        gDP = (char*)malloc(size);
        if (gDP == 0)
            reportMemErr(procName, itemName, neededBytes);
        *(char** )gDP = gBlockList;
        gBlockList = gDP;
        gDP += sizeof(char* );
        gDPEnd = gDP + size_memChunk;
    }
    gDPUsage = usageCode;
}

//-----------------------------------------------------------------------------
//  Free all blocks allocated by needBlock.

void freeBlocks()
{
    char* nextDP;

#ifdef MEM_STATS
        display("free(%4ldK) for dict\n", gDPTotal/1024);
        gDPTotal = 0;
#endif
    for (gDP = gBlockList; gDP; gDP = nextDP)
    {
        nextDP = *(char** )gDP;
        free(gDP);
    }
    gBlockList = 0;
    gDP = gDPEnd = 0;
    gDPUsage = dp_none;
}

//-----------------------------------------------------------------------------
// Create another space in a list of spaces.

SpaceList::SpaceList(SpaceList* prev, char* name, short elemSize, long numElems)
{
    space.name = name;
    space.mallocBase = 0;
    space.elemSize = elemSize;
    space.base = &base;
    space.end = &end;
    space.limit = &limit;
    allocSpace(&space, numElems);
    nextFilled = prev;
    if (prev)
        prev->nextToBeFilled = this;
    nextToBeFilled = 0;
}

//-----------------------------------------------------------------------------
// Free up space inside existing storage space(s) and return tail space.

SpaceList* SpaceList::free()
{
    freeSpace(&space);
    if (nextFilled)
        return nextFilled->free();
    else
        return this;
}

//-----------------------------------------------------------------------------
// Contstruct a New object that can be deleted later by deleteAll.

SimObject::SimObject()
{
#ifdef DEBUG_NEW
    verifyList();
#endif
    prevObj = 0;
    nextObj = simObjList;
    if (nextObj)
        nextObj->prevObj = this;
    simObjList = this;
    signature = 0x534f414a;
#ifdef DEBUG_NEW
    verifyList();
#endif
}

//-----------------------------------------------------------------------------
// Verify that the object linked-list is intact.

void SimObject::verifyList()
{
    int i = 0;
    for (SimObject* sp = simObjList; sp; sp = sp->nextObj)
    {
        if (sp->signature != 0x534f414a)
            Debugger();
            // throw new VError(verr_bug,
            //                  "BUG: invalid signature in SimObject #%d", i);
        if (sp->nextObj)
        {
            if (sp->nextObj->prevObj != sp)
            {
                Debugger();
                // display("// *** BUG: bad backlink in SimObjects list"
                //         " for 0x%08lx\n", (long)nextObj);
            }
        }
        i++;
        if (i > 100000)
            Debugger();
            // throw new VError(verr_bug,
            //      "BUG: infinite loop in SimObjects, 1st at 0x%08lx",
            //      simObjList);
    }
}

//-----------------------------------------------------------------------------
// Remove one object from the doubly-linked simObjList.

SimObject::~SimObject()
{
#ifdef DEBUG_NEW
    verifyList();
#endif
    if (prevObj)
        prevObj->nextObj = nextObj;
    if (nextObj)
        nextObj->prevObj = prevObj;
    if (simObjList == this)
        simObjList = nextObj;
#ifdef DEBUG_NEW
    verifyList();
#endif
}

//-----------------------------------------------------------------------------
// Delete all objects in the simObjList.

void SimObject::deleteAll()
{
    for (SimObject* sp = simObjList; sp; )
    {
        SimObject* nextsp = sp->nextObj;
#ifdef DEBUG_NEW
        simObjList->verifyList();
#endif
        delete sp;
        sp = nextsp;
    }
    simObjList = 0;
}

//-----------------------------------------------------------------------------
// Stubs to resolve external references.

void operator delete(void*) throw()
{
}

//-----------------------------------------------------------------------------
// Reset the random-sequence seed for this simulation run to the value set
//  by _srand_.

void resetRandSeed(int seed)
{
    if (seed)
        gLastRand = seed;
    if (gLastRand == 0)
        gLastRand = (int)clock();
}

//-----------------------------------------------------------------------------
// Return a random positive number from 0 to range-1.

int randomRng(int range)
{
    if (range == 0)
        return 0;
    // not as random as rand(), but portable: gives same seq on all machines
    gLastRand = (gLastRand * 1103515245 + 12345) & 0x7fffffff;

    int val = gLastRand % range;
#ifdef DEBUG_RANDOM
    // include for debugging 'random' failures
    display("rand(%d)=%d [%d]\n", range, val, gLastRand);
#endif
    return val;
}
