// ****************************************************************************
//
//              PVSim Verilog Simulator Verilog Compiler
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
#include <stdlib.h>

#include "SimPalSrc.h"
#include "Src.h"
#include "VL.h"
#include "VLSysLib.h"
#include "VLCoder.h"
#include "VLCompiler.h"

bool gVerilogInstantiated;

//-----------------------------------------------------------------------------
// Initialize Verilog compiler.

void initVerilog()
{
    vc.tooComplexError = 0;
    vc.startRealTime = clock();
    initBackEnd();
    initExprPool();
    gWarningCount = 0;
    gNameLenLimit = 20;
    gMacros = 0;
    Scope::local = 0;
    // create global symbol scope
    Scope::local = new Scope("global", ty_global, 0);
    Scope::global = Scope::local;
    EvHand::gAssignsReset = 0;
}

//-----------------------------------------------------------------------------
// Compile a Verilog simulation source file.

void loadVerilogFile(const char *fileName)
{
    Src* baseSrcSave = VL::baseSrc;
    Token* scanSave = gScToken;
    gScToken = 0;

    // open Verilog file
    Src* verSrc = new Src(newString(fileName), sm_stripComments+sm_verilog, 0);
    verSrc->tokenize();
    VL::baseSrc = verSrc;

    while (isName("module"))    // main parsing loop
    {
        scan();
        expectNameOf("module");

        // compile a module
        new VLModule(newString(gScToken->name));
    }
    if (gScToken)
        throw new VError(verr_illegal, "extra stuff at end of file");

    VL::baseSrc = baseSrcSave;
    gScToken = scanSave;
}

//-----------------------------------------------------------------------------
// Instantiate all compiled Verilog, creating signals.

void instantiateVerilogIfNeeded()
{
    if (!gVerilogInstantiated)
    {
        if (!gQuietMode)
            display("    instantiating 'main' module...\n");

        vc.mainInstance = new Instance("main", "m", 0, 0, 0);
        if (!gQuietMode)
            display("    'main' created.\n");
        EvHand::gAssignsReset->instantiateAssignsReset(vc.mainInstance);
        if (!gQuietMode)
            display("    Global reset instantiated.\n");
        vc.mainInstance->instantiate();
        if (!gQuietMode)
            display("    'main' instantiated.\n");
        gVerilogInstantiated = TRUE;
        // check for any unassigned variables
        Scope::global->checkVars();
    }
}

//-----------------------------------------------------------------------------
// Scan next symbol as a signal name.

Signal* expectSignalFor(const char* use)
{
    scan();
    expectNameOf(use);
    Symbol* sym = lookup(gScToken->name, TRUE);
    if (!sym)
        throwExpected("signal name");
    return (Signal*)sym->arg;
}

//-----------------------------------------------------------------------------
// Parse a PVSim project file: set duration, compile Verilog files, and go
// simulate.

void loadProjectFile(const char *fileName, const char* testChoice)
{
    initVerilog();
    gScToken = 0;

    Src* projSrc = new Src(newString(fileName), sm_forth + sm_stripComments, 0);

    new Macro("SIMULATOR", "1", projSrc);
    if (testChoice)
    {
        new Macro(testChoice, "", projSrc);
        new Macro("TEST_CHOICE", TmpName("\"%s\"", testChoice), projSrc);
    }
    else
        new Macro("TEST_CHOICE", "", projSrc);

    projSrc->tokenize();
    VL::baseSrc = projSrc;
    gVerilogInstantiated = FALSE;

    do                  // main parsing loop
    {
        if (isName("duration"))
        {
            // set simulation duration
            scan();
            expect(NUMBER_TOKEN);
            gNSDuration = gScToken->number;
        }
        else if (isName("debug"))
        {
            // set debug level
            scan();
            expect(NUMBER_TOKEN);
            VL::debugLevel = gScToken->number;
        }
        else if (isName("load"))
        {
            // load a Verilog file
            scan();
            if (!(isToken(NAME_TOKEN) || isToken(STRING_TOKEN)))
                expectNameOf("Verilog file");
            loadVerilogFile(gScToken->name);
        }
        else if (isName("shell"))
        {
            // execute a shell command (e.g., "make" for sc.py compiles)
            scan();
            if (!(isToken(NAME_TOKEN) || isToken(STRING_TOKEN)))
                expectNameOf("shell command");
            display("executing \"%s\"\n", gScToken->name);
#if 0
            int result = system(gScToken->name);
#else
            putenv((char*)"PYTHONEXECUTABLE=");    // ensure we're using the default
                                            // python in tools such as sc.py
            FILE* fout = popen(TmpName("%s 2>&1", gScToken->name), "r");
            if (fout == NULL)
                throw new VError(verr_illegal, "shell command failed:");
            {
                char line[max_nameLen];
                //sleep(1);
                while (fgets(line, max_nameLen, fout) != NULL)
                    display(line);
            }
            int result = pclose(fout);
#endif
            if (result)
                throw new VError(verr_illegal,
                                "shell command returned error code %d", result);
        }
        else if (isName("trace"))
        {
            // set up to display events for a signal
            instantiateVerilogIfNeeded();
            Signal* signal = expectSignalFor("signal to trace");
            signal->is |= TRACED + DISPLAYED;
            display("// *** tracing signal '%s'\n", signal->name);
        }
        else if (isName("hide"))
        {
            // undisplay a signal
            instantiateVerilogIfNeeded();
            Signal* signal = expectSignalFor("signal to hide");
            signal->is &= ~DISPLAYED;
            // if it's a bus signal ("Foo[7:0]"), hide its sub-signals
            const char* p = signal->name + strlen(signal->name) - 1;
            if (*p == ']')
            {
                const char* pR = 0;
                for (; p > signal->name && *p != '['; p--)
                {
                    if (*p == ':')
                    {
                        pR = p + 1;
                    }
                }
                if (*p == '[' && pR)
                {
                    int iL = atoi(p+1);
                    int iR = atoi(pR);
                    if (iR > iL)
                    {
                        int i = iR;
                        iR = iL;
                        iL = i;
                    }
                    char* name = newString(signal->name);
                    char* pSub = name + (p + 1 - signal->name);
                    for (int i = iR; i <= iL; i++)
                    {
                        snprintf(pSub, sizeof(pSub), "%d]", i);
                        Symbol* sym = lookup(name, TRUE);
                        if (!sym)
                            throwExpected("bus sub-signal name");
                        signal = (Signal*)sym->arg;
                        signal->is &= ~DISPLAYED;
                    }
                }
            }
        }
        else if (isName("hideOn"))
        {
            // undisplay following-defined signals
            gSignalDisplayOn = FALSE;
        }
        else if (isName("hideOff"))
        {
            // display following-defined signals
            gSignalDisplayOn = TRUE;
        }
        else if (isName("breakOnSignal"))
        {
            // set up break on each evaluation of a signal
            instantiateVerilogIfNeeded();
            gBreakSignal = expectSignalFor("break signal");
        }
        else if (isName("testChoice"))
        {
            // choice from Simulation menu has been passed to Simulate()
            scan();
        }
        else if (!gScToken)
            throw new VError(verr_notFound, "empty psim file");
        else
        {
            if (isToken(NAME_TOKEN))
                throw new VError(verr_notFound, "unknown psim command \"%s\"",
                                 gScToken->name);
            else
                throw new VError(verr_notFound, "psim command expected");
        }
    } while (gScToken && scan());

    instantiateVerilogIfNeeded();

    clock_t compileRealTime = clock() - vc.startRealTime;
    if (!gQuietMode)
        display("      [Verilog: %5.3f sec.]\n",
                (float)compileRealTime/CLOCKS_PER_SEC);
}

