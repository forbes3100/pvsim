// ****************************************************************************
//
//          PVSim Verilog Simulator Back End, as a Python Extension
//
// Copyright 2012 Scott Forbes
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

#include <Python.h>
#include <time.h>
#include <new>

#include "Utils.h"
#include "Model.h"
#include "VLCompiler.h"
#include "PSignal.h"

// -------- constants --------

const char* titleDisclaimer =
    "\n"
    "PVSim %s Copyright 2004, 2005, 2006, 2012 Scott Forbes\n"
    "PVSim comes with ABSOLUTELY NO WARRANTY; see the Help menu for details.\n"
    "This is free software, and you are welcome to redistribute it under\n"
    "certain conditions; see the Help menu for details.\n\n";

const bool isSolidLevel[] =
{ //  L   S   X   R   F   U   D   H   Q   C   V   W
      1,  1,  0,  0,  0,  0,  0,  1,  0,  0,  1,  1
};

// -------- global variables --------

bool        gSimFileLoaded;
bool        gQuietMode = FALSE;
bool        gTagDebug = FALSE;

char        gLibPath[max_nameLen];
char        gProjName[max_nameLen];
char        gProjFullPathName[max_nameLen];
char        gOrderFileStr[max_nameLen];

PyObject*   gPySignalClass = NULL;
PyObject*   gSigs = NULL;

// -------- local variables --------

int temp;
Tick nTicks;

static PyObject* displayFn = NULL;
static PyObject* readFileFn = NULL;


//-----------------------------------------------------------------------------
// Tell backend which Python functions to use for display, file reading.

static PyObject* pvsim_SetCallbacks(PyObject* self, PyObject* args)
{
    PyObject* result = NULL;
    PyObject* displayFnTemp;
    PyObject* readFileFnTemp;

    if (PyArg_ParseTuple(args, "OO:SetCallbacks",
                          &displayFnTemp, &readFileFnTemp))
    {
        if (!PyCallable_Check(displayFnTemp))
        {
            PyErr_SetString(PyExc_TypeError, "displayFn must be callable");
            return NULL;
        }
        if (!PyCallable_Check(readFileFnTemp))
        {
            PyErr_SetString(PyExc_TypeError, "readFileFn must be callable");
            return NULL;
        }

        Py_XINCREF(displayFnTemp);
        Py_XDECREF(displayFn);
        displayFn = displayFnTemp;

        Py_XINCREF(readFileFnTemp);
        Py_XDECREF(readFileFn);
        readFileFn = readFileFnTemp;

        Py_INCREF(Py_None);
        result = Py_None;
    }
    return result;
}

//-----------------------------------------------------------------------------
// Give backend the Signal class, used to build sigs dictionary.

static PyObject* pvsim_SetSignalType(PyObject* self, PyObject* args)
{
    PyObject* result = NULL;
    PyObject* signalTypeTemp;

    if (PyArg_ParseTuple(args, "O:SetSignalType", &signalTypeTemp))
    {
        if (!PyType_Check(signalTypeTemp))
        {
            PyErr_SetString(PyExc_TypeError, "expected 'Signal' class");
            return NULL;
        }

        Py_XINCREF(signalTypeTemp);
        Py_XDECREF(gPySignalClass);
        gPySignalClass = signalTypeTemp;

        Py_INCREF(Py_None);
        result = Py_None;
    }
    return result;
}

//-----------------------------------------------------------------------------
// Display like printf in the log window.

void display(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    char msg[max_messageLen];
    vsnprintf(msg, max_messageLen-1, format, ap);
    va_end(ap);

    PyObject* args = Py_BuildValue("(s)", msg);
    PyObject* result = PyObject_CallObject(displayFn, args);
    Py_DECREF(args);
    // if (result == NULL)
        // got an error in display function -- do what?
    Py_DECREF(result);
}

//-----------------------------------------------------------------------------
// Display a formatted event message in the log window.

void printfEvt(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    char msg[max_messageLen];
    vsnprintf(msg, max_messageLen-1, format, ap);
    va_end(ap);

    display("%6.3f: %s", (float)gTick/gTicksNS, msg);
}

//-----------------------------------------------------------------------------
// Add a new Signal to gSigs dict.

void newSignalPy(Signal* signal, Level newLevel, bool isBus,
                 int lsub, int rsub)
{
    // create a bus Signal with its file location:
    //    index = signal - gSignals
    //    gSigs[index] = Signal(index, name, [0, level],
    //                          srcFile, srcPos, isBus, lsub, rsub)
    const char* srcName = "-";
    size_t srcPos = 0;
    Token* srcLoc = signal->srcLoc;
    if (srcLoc && srcLoc->tokCode == NAME_TOKEN)
    {
        srcName = srcLoc->src->fileName;
        srcPos = srcLoc->pos - srcLoc->src->base;
    }
    size_t index = signal - gSignals;
    PyObject* args = Py_BuildValue("(ns[ic]sniii)", index,
      signal->name, 0, gLevelNames[newLevel], srcName, srcPos, isBus,
      lsub, rsub);
    PyObject* sig = PyObject_CallObject(gPySignalClass, args);
    Py_DECREF(args);

    if (signal->busOpt & DISP_BUS_BIT && !(signal->is & TRACED))
    {
        // sig.isDisplayed = False
        PyObject* isDisp = PyObject_GetAttrString(sig, "isDisplayed");
        Py_DECREF(isDisp);
        isDisp = Py_False;
        Py_INCREF(isDisp);
        PyObject_SetAttrString(sig, "isDisplayed", isDisp);
    }

    if (gSigs == NULL)
        gSigs = PyDict_New();
    PyObject* key = PyLong_FromSize_t(index);
    if (key == NULL)
        throw new VError(verr_bug, "can't create index key for gSigs");
    if (PyDict_SetItem(gSigs, key, sig))
        throw new VError(verr_bug, "can't add signal to gSigs");
    Py_DECREF(sig);
    Py_DECREF(key);
}

//-----------------------------------------------------------------------------
// Add a python-tuple-format event to signal's events list.

void addEventPy(Signal* signal, Tick tick, PyObject* val)
{
    if (debugLevel(3))
    {
        PyObject* sVal = PyObject_Repr(val);
        display("addEventPy %s %ld %s\n", signal->name, tick,
                PyUnicode_AsUTF8(sVal));
    }
    // sig = sigs[signal-gSignals]
    if (gSigs == NULL || !PyDict_CheckExact(gSigs))
        throw new VError(verr_bug, "gSigs not a Dict");
    PyObject* key = PyLong_FromSize_t(signal - gSignals);
    PyObject* sig = PyDict_GetItem(gSigs, key);
    Py_DECREF(key);
    if (sig == NULL)
        throw new VError(verr_notFound, "Signal %s not in gSigs",
                         signal->name);
    Py_INCREF(sig);

    // sig.events += [tick, val]
    PyObject* events = PyObject_GetAttrString(sig, "events");
    if (events == NULL)
    {
        events = PyList_New(0);
        PyObject_SetAttrString(sig, "events", events);
    }
    else if (!PyList_CheckExact(events))
        throw new VError(verr_illegal, "Signal.events %s not a list",
                         signal->name);
    PyObject* t = Py_BuildValue("l", tick);
    PyList_Append(events, t);
    Py_DECREF(t);
    PyList_Append(events, val);
    Py_DECREF(val);
    Py_DECREF(events);
    Py_DECREF(sig);

    // keep track of latest tick value
    if (gTick > nTicks)
        nTicks = gTick;
}

//-----------------------------------------------------------------------------
// Gather bus-signal events from their bit signals.

void buildBusSignals()
{

    for (Signal* busSig = gSignals; busSig < gNextSignal; busSig++)
    {
        if (!(busSig->busOpt & DISP_BUS && busSig->is & DISPLAYED))
            continue;

        Signal* msbSig = busSig + 1;
        Signal* lsbSig = busSig + msbSig->busWidth + 1;
        int busInv = ((busSig->busOpt & DISP_INVERTED) != 0);
        Signal* bitSig;
        if (debugLevel(3) || busSig->is & TRACED)
            display("buildBusSignals signal %s [ %s : %s ]\n",
                    busSig->name, msbSig->name, lsbSig->name);

        // start all bit-signals at the beginning of display time
        for (bitSig = msbSig; bitSig < lsbSig; bitSig++)
        {
            bitSig->dispEvent = bitSig->firstDispEvt;
            bitSig->level = bitSig->initDspLevel;
            bitSig->lastLevel = bitSig->initDspLevel;
            if (busSig->is & TRACED)
                display("bbs bit signal %s start %ld\n",
                        bitSig->name, bitSig->firstDispEvt->tick);
        }
        Tick curTick = 0;

        // loop for each event on any bit-signal, in chronological order.
        int loopCount = 0;
        bool lastPass = FALSE;
        Level curLevel = LV_L;
        for (;;)
        {
            // get all "current" bits levels, and look for the next event time
            const Tick infinity = 0x7fffffff;
            Tick nextTick = infinity;
            Signal* bitSig;
            for (bitSig = msbSig; bitSig < lsbSig; bitSig++)
            {
                Event* event = bitSig->dispEvent;
                while (event && (event->is & ATTACHED_TEXT))
                    event = event->nextInSignal;
                if (event)
                {
                    if (event->tick <= curTick)         // if event is current
                        bitSig->level = event->level;   // use its level
                    if (event->tick < nextTick)         // find next event
                        nextTick = event->tick;
                }
            }
            if (busSig->is & TRACED)
                display("bbs %s: curTick=%ld nextTick=%ld\n",
                        busSig->name, curTick, nextTick);

            if (nextTick == infinity)
            {
                nextTick = gTEnd + 100;
                lastPass = TRUE;
            }

            // merge "current" bits into one bus signal level
            curLevel = msbSig->level;
            size_t busValue = 0;
            bool busValueValid = TRUE;
            for (bitSig = msbSig; bitSig < lsbSig; bitSig++)
            {
                Level bitLevel = bitSig->level;
                if (curLevel != bitLevel)
                {
                    if (curLevel == LV_C || bitLevel == LV_C)
                        curLevel = LV_C;
                    else
                    {
                        if (isSolidLevel[curLevel] && isSolidLevel[bitLevel])
                            curLevel = LV_S;
                        else
                            curLevel = LV_X;
                    }
                }
                if (busValueValid)
                {
                    if (isSolidLevel[bitSig->lastLevel] &&
                         bitSig->lastLevel != LV_S)
                        busValue = (busValue << 1) |
                            ((bitSig->lastLevel == LV_H ||
                            bitSig->lastLevel == LV_W) ^ busInv);
                    else
                        busValueValid = FALSE;
                }
                Event* event = bitSig->dispEvent;
                if (event)
                {
                    if (event->tick <= curTick)
                    {
                        bitSig->dispEvent = event->nextInSignal;
                        if (event->is & ATTACHED_TEXT)
                        {
                            PyObject* val = Py_BuildValue("(cs)",
                                gLevelNames[bitSig->lastLevel],
                                event->attText);
                            addEventPy(busSig, event->tick, val);
                        }
                        else
                            bitSig->lastLevel = bitSig->level;
                    }
                }
            }
            if (busSig->is & TRACED)
                display(" bbs %s: busValue=%ld\n",
                        busSig->name, (long)busValue);

            // store the bus event
            PyObject* val = PyLong_FromSize_t(busValue);
            addEventPy(busSig, curTick, val);

            curTick = nextTick;

            // quit loop when no future events found
            if (lastPass)
                break;

            // quit loop if stuck
            if (loopCount++ > gNSDuration*gTicksNS + 100)
            {
                display("\n// *** While locating bit events for bus %s",
                        busSig->name);
                throw new VError(verr_bug,
                    "stuck in infinite loop in buildBusSignals()");
            }
        }
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

void breakInEditor(Token* srcLoc)
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

static PyObject* pvsim_Simulate(PyObject* self, PyObject* args)
{
    PyObject* result = NULL;
    const char* projFullPathNameTemp;
    const char* testChoice;
    if (!PyArg_ParseTuple(args, "ss", &projFullPathNameTemp, &testChoice))
    {
        return NULL;
    }

    strncpy(gProjFullPathName, projFullPathNameTemp, max_nameLen-1);
    char* p;
    char* start = gProjFullPathName;
    for (p = gProjFullPathName; *p; p++)
        if (*p == '/')
            start = p + 1;
    char* p2 = gProjName;
    for (p = start; *p && *p != '.'; p++)
        *p2++ = *p;
    *p2 = 0;

    Py_XDECREF(gSigs);
    gSigs = PyDict_New();
    gBarSignal = gSignals;
    nTicks = 0;

    try
    {
        // Load the simulation source

        time_t t = time(0);
        if (!gQuietMode)
        {
            display("Log started %s\n", ctime(&t));
            display("PVSim Verilog Simulator %s, compiled %s\n\n",
                    gPSVersion, gPSDate);
#ifdef __VERSION__
            display("(GCC %s ", __VERSION__);
#endif
#ifdef __OPTIMIZE__
            display("opt ");
#endif
#ifdef __OPTIMIZE_SIZE__
            display("opt-size ");
#endif
            display("size_t=%d)\n", sizeof(size_t));
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
            loadProjectFile(projFileName, testChoice);
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
        if (gNSStart < 0)
            gNSStart = 0;
        if (gNSDuration < 10)
            gNSDuration = 10;

        simulate();

        // post-sim: gather bus-signal events from their bit signals
        buildBusSignals();

        PyObject* barSig = Py_None;
        Py_INCREF(barSig);
        if (gBarSignal)
        {
            Py_DECREF(barSig);
            barSig = PyLong_FromSize_t(gBarSignal - gSignals);
        }
        result = Py_BuildValue("OiS", gSigs, nTicks, barSig);
    }
    catch (VError* err)
    {
        err->display();
    }
    catch (MainErrorCode errNo)
    {
        display("\n*** ERROR: pvsim_Simulate: code %d\n", errNo);
    }
    catch (...)
    {
        display("\n*** ERROR: pvsim_Simulate: unknown\n");
    }

    return result;
}

//-----------------------------------------------------------------------------
// Initialize back end and set operating modes.

static PyObject* pvsim_Init(PyObject* self, PyObject* args)
{
    // debug levels:
    //      1   include internal signals in timing window
    //      2   also log instantiations
    //      3   also log generated PCode
    //      4   also log compiled expressions in Forth-like syntax
    VL::debugLevel = 0;
    gQuietMode = FALSE;
    gTagDebug = FALSE;
    if (!PyArg_ParseTuple(args, "|iii", &debugLevel, &gQuietMode, &gTagDebug))
    {
        return NULL;
    }

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

    SimObject::simObjList = 0;
    gNextSignal = gSignals; // for newSignals startup
    freeBlocks();
    Model::initModels();

    // Allocate application free memory for signals, bitmap, etc.
    gMaxSignals = max_signals;
    // !!! readPrefsFile();
    gMaxSignals = gMaxSignals & 0xfffffff8;
    gMaxStringSpace = 100 * gMaxSignals;

    gSimFileLoaded = FALSE;

    Py_INCREF(Py_None);
    return Py_None;
}

//-----------------------------------------------------------------------------
// Return version string.

static PyObject* pvsim_GetVersion(PyObject* self, PyObject* args)
{
    if (!PyArg_ParseTuple(args, ""))
        return NULL;
    return Py_BuildValue("s", gPSVersion);
}

//-----------------------------------------------------------------------------

static PyMethodDef PVSimMethods[] = {
    {"Init",  pvsim_Init, METH_VARARGS, "Initialize back end."},
    {"GetVersion",  pvsim_GetVersion, METH_VARARGS, "Get version string."},
    {"SetCallbacks",  pvsim_SetCallbacks, METH_VARARGS,
                                                "Set callback functions."},
    {"SetSignalType",  pvsim_SetSignalType, METH_VARARGS, "Set class Signal."},
    {"Simulate",  pvsim_Simulate, METH_VARARGS, "Run simulation."},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef PVSimModule = {
    PyModuleDef_HEAD_INIT,
    "pvsimu",           /* m_name */
    "Documentation for pvsimu module",  /* m_doc */
    -1,                 /* m_size */
    PVSimMethods,       /* m_methods */
    NULL,               /* m_reload */
    NULL,               /* m_traverse */
    NULL,               /* m_clear */
    NULL,               /* m_free */
};

//-----------------------------------------------------------------------------

PyMODINIT_FUNC PyInit_pvsimu(void)
{
    PyObject* m = PyModule_Create(&PVSimModule);
    if (m == NULL)
        return NULL;

    //PVSimError = PyErr_NewException("pvsim.error", NULL, NULL);
    //Py_INCREF(PVSimError);
    //PyModule_AddObject(m, "error", PVSimError);

    return m;
}
