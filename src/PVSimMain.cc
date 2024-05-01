// ****************************************************************************
//
//          PVSim Verilog Simulator Back End, as an Application
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

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <new>

#include "Utils.h"
#include "Model.h"
#include "VLCompiler.h"
#include "PSignal.h"

// -------- constants --------

int temp;

const char* titleDisclaimer =
    "\n"
    "PVSim %s Copyright 2004, 2005, 2006, 2012 Scott Forbes\n"
    "PVSim comes with ABSOLUTELY NO WARRANTY; see the Help menu for details.\n"
    "This is free software, and you are welcome to redistribute it under\n"
    "certain conditions; see the Help menu for details.\n\n";

// -------- global variables --------

bool        gSimFileLoaded;
bool        gQuietMode = FALSE;
bool        gTagDebug = FALSE;

char        gLibPath[max_nameLen];
char        gProjName[max_nameLen];
char        gProjFullPathName[max_nameLen];
char        gOrderFileStr[max_nameLen];

//-----------------------------------------------------------------------------
// Display like printf in the log window.

void display(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    char msg[max_messageLen];
    printf("format=%lu\n", (size_t)format);
    vsnprintf(msg, max_messageLen-1, format, ap);
    va_end(ap);

    if (gProjName[0])
    {
        if (!gLogFile)
        {
            char logFileName[max_nameLen];
            snprintf(logFileName, sizeof(logFileName), "%s.log", gProjName);
            gLogFile = fopen(logFileName, "w");
            if (!gLogFile)
                reportErrDialog("creating log file");
        }
        fprintf(gLogFile, "%s", msg);
    }
    printf("%s", msg);
}

//-----------------------------------------------------------------------------
// Write a formatted event message to the console and events file.

void printfEvt(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    char msg[max_messageLen];
    vsnprintf(msg, max_messageLen-1, format, ap);
    va_end(ap);

    if (!gQuietMode)
    {
        display("%6.3f: %s", (float)gTick/gTicksNS, msg);
#ifdef WRITE_EVENTS
        fprintf(gEvFile, "%6.3f: %s", (float)gTick/gTicksNS, msg);
#endif
    }
}

//-----------------------------------------------------------------------------
// Stubs.

void Debugger()
{
}

void showMsgInEditor(const char*, const char*, int)
{
}

//-----------------------------------------------------------------------------
// Free allocated temporary memory.

void freeTempSpace(Space* space)
{
    if (space->mallocBase != NULL)
    {
        free(space->mallocBase);
        space->mallocBase = NULL;
    }
}

//-----------------------------------------------------------------------------
// Allocate a large memory space sized to numElems elements. It uses
//  temporary memory, outside of all application heaps.
//  The space's pointers will be initialized to point into the new space.

void allocTempSpace(Space* space, long numElems)
{
    assert(space->mallocBase == NULL);
    long size = space->elemSize * numElems + 4;
    char* mallocBase = (char*)malloc(size);
    if (mallocBase == NULL)
        throw new VError(verr_memOverflow,
                        "Out of non-application memory for %s", space->name);

    space->mallocBase = mallocBase;
    char* base = (char* )(((unsigned long)mallocBase + 3) & ~3);
    *(char**)space->base = base;
    *(char**)space->end = base;
    *(char**)space->limit = base + size;
}

//-----------------------------------------------------------------------------

void breakInEditor(Token* srcLoc)
{
}

//-----------------------------------------------------------------------------
// Report an error in a dialog box. Takes printf-like arguments.

void reportErrDialog(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    throw merr_reportError;
}

//-----------------------------------------------------------------------------
// 'new' ran out of memory.

void handleNewErr()
{
    throw new VError(verr_memOverflow, "New: out of memory");
}


//-----------------------------------------------------------------------------
// Compile and simulate thread.

void* compileAndSimulate()
{
    try
    {
        // Load the simulation source

        time_t t = time(0);
        if (!gQuietMode)
        {
            display("Log started %s\n", ctime(&t));
            display("PVSim Verilog Simulator %s, compiled %s\n\n",
                    gPSVersion, gPSDate);
            display("size_t=%d bytes\n", sizeof(size_t));
        }

        gWarningCount = 0;
        gFlaggedErrCount = 0;
        gFatalLoadErrors = FALSE;
        if (!gSimFileLoaded)
        {
            newSimulation();
            initSimulator();
            gWarningCount = 0;
            clock_t startRealTime = clock();
            char projFileName[max_nameLen];
            snprintf(projFileName, sizeof(projFileName), "%s.psim", gProjName);
            loadProjectFile(projFileName);
            clock_t compileRealTime = clock() - startRealTime;
            if (!gQuietMode)
                display("      [%ld signals, %5.3f sec]\n",
                   gNextSignal-gSignals, (float)compileRealTime/CLOCKS_PER_SEC);
            gSimFileLoaded = TRUE;
        }
        if (gFatalLoadErrors)
            throw new VError(verr_stop,
                             "fatal errors encountered-- see log above");

        // Run the simulation

#ifdef WRITE_EVENTS
        char eventsFileName[max_nameLen];
#ifdef USE_LIBRARY_LOGS
        strncpy(eventsFileName, gLibPath, max_nameLen-1);
        strncat(eventsFileName, "Logs/PVSim.events", max_nameLen-1);
        eventsFileName[max_nameLen-1] = 0;
#else
        snprintf(eventsFileName, sizeof(eventsFileName), "%s.events", gProjName);

#endif
        gEvFile = openFile(eventsFileName, "w");
#endif
        if (gNSStart < 0)
            gNSStart = 0;
        if (gNSDuration < 10)
            gNSDuration = 10;

        simulate();

#ifdef WRITE_EVENTS
        fprintf(gEvFile, "\n");
        if (gBarSignal)
            fprintf(gEvFile, "BarSignal: %d\n", (int)(gBarSignal - gSignals));
        closeFile(gEvFile);
#endif
    }
    catch (VError* err)
    {
        err->display();
        exit(-1);
    }
    catch (MainErrorCode errNo)
    {
        exit(errNo);
    }
    catch (...)
    {
        throw;
    }
    return 0;
}

//-----------------------------------------------------------------------------
// Compile and simulate the current project.

void doFirstSimulation()
{
    Model::initModels();

    // Allocate application free memory for signals, bitmap, etc.
    gMaxSignals = max_signals;
    // !!! readPrefsFile();
    gMaxSignals = gMaxSignals & 0xfffffff8;
    gMaxStringSpace = 100 * gMaxSignals;

    gSimFileLoaded = FALSE;

    compileAndSimulate();
}

//-----------------------------------------------------------------------------
// Initialize this application.

void initApplication()
{
    SimObject::simObjList = 0;

    gNextSignal = gSignals; // for newSignals startup

#ifdef USE_LIBRARY_LOGS
    char* homeDir = getenv("HOME");
    if (!homeDir)
        reportErrDialog("getting HOME directory");
    strncpy(gLibPath, homeDir, max_nameLen-1);
    strncat(gLibPath, "/Library/", max_nameLen-1);
    gLibPath[max_nameLen-1] = 0;
#endif
}

//-----------------------------------------------------------------------------
// Show command line usage and quit.

void usage()
{
    printf("usage: pvsim [ -d<level> -q -t -v ] file.psim\n");
    exit(-1);
}

//-----------------------------------------------------------------------------
// Main routine: parse command line arguments and dispatch.

int main(int argc, char* argv[])
{
    // debug levels:
    //      1   include internal signals in timing window
    //      2   also log instantiations
    //      3   also log generated PCode
    //      4   also log compiled expressions in Forth-like syntax
    VL::debugLevel = 0;
    bool haveFile = FALSE;
    int i;

    // parse any command line arguments
    for (i = 1; i < argc; i++)
    {
        char* arg = argv[i];
        if (arg[0] == '-')
        {
            if (strlen(arg) < 2)
                usage();
            switch(arg[1])
            {
                case 'd':
                    VL::debugLevel = atoi(arg + 2);
                    break;

                case 'q':
                    gQuietMode = TRUE;
                    break;

                case 't':
                    gTagDebug = TRUE;
                    break;

                case 'v':
                    printf("%s\n", gPSVersion);
                    return 0;

                default:
                    usage();
            }
        }
        else
        {
            if (haveFile)
                usage();
            strcpy(gProjFullPathName, arg);
            char* p;
            char* start = gProjFullPathName;
            for (p = gProjFullPathName; *p; p++)
                if (*p == '/')
                    start = p + 1;
            char* p2 = gProjName;
            for (p = start; *p && *p != '.'; p++)
                *p2++ = *p;
            *p2 = 0;
            haveFile = TRUE;
        }
    }
    if (!haveFile)
        usage();

    // initialization
    gDP = gDPEnd = 0;                   // init memory space
    gBlockList = 0;
    gNSStart = 0;
    gNSDuration = ns_runTime;
    gLogFile = 0;

    initFiles();

    std::set_new_handler(handleNewErr);
    gHashTable = (Symbol**)malloc(max_hashCodes*sizeof(Symbol*));
    if (gHashTable == 0)
    {
        reportErrDialog("can't allocate hash table");
        throw;
    }

    initApplication();
    freeBlocks();

    if (!gQuietMode)
        printf(titleDisclaimer, gPSVersion);

    doFirstSimulation();
    return 0;
}
