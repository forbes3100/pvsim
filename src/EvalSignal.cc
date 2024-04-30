// ****************************************************************************
//
//          PVSim Verilog Simulator Signal Code Evaluator
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

#ifdef EXTENSION
#include <Python.h>
#endif
#include "PSignal.h"

//-----------------------------------------------------------------------------
// Grab a 32-bit operand, a signal's offset into the gSignals array, add it
// to the gSignals base, and return the character there.
// Increments the code pointer.

inline int SigOpr(EqnItem* &ic)
{
    return *((char* )gSignals + ((ic++)->operand));
}

//-----------------------------------------------------------------------------
// Evaluate a signal's equation and return the new signal level.
//  The simulator spends most of its time here, so this function should
//  be optimized for speed.

Level evalSignalCode(Signal* signal, Signal* sigs, const FuncTable* func)
{
    EqnItem* ic;
    Level   andAcc, orAcc;
    int     opr1, opr2;

#ifdef RANGE_CHECKING
    if (signal < gSignals || signal >= gNextSignal)
        throw new VError(verr_bug, "evalSignal: bad signal pointer");
#endif
    func = &funcTable;
    ic = signal->evalCode;
    andAcc = LV_L;
    orAcc = LV_L;
    if (ic == 0)
        return warnUnused(signal);
#ifdef RANGE_CHECKING
    if (ic < firstCode || ic >= (EqnItem*)gDP)
        throw new VError(verr_bug, "evalSignal: bad ic pointer");
#endif
    for (;;)
    {
        // grab the next opcode and dispatch on it
        short opcode = ic->opcode;
        ic = (EqnItem*)((size_t)ic + sizeof(short));
        switch(opcode)
        {
            case AND_OP:
                andAcc = func->ANDtable[SigOpr(ic)][andAcc];
                break;
            case LOAD_OP:
                andAcc = (Level)SigOpr(ic);
                break;
            case SAVEOR_OP:
                orAcc = andAcc;
                break;
            case TRUE_OP:
                andAcc = LV_H;
                break;
            case FALSE_OP:
                andAcc = LV_L;
                break;
            case NEWTS_OP:
                orAcc = LV_Z;
                break;
            case ENABLE_OP:
                opr1 = SigOpr(ic);
                opr2 = SigOpr(ic);
                andAcc = func->ENABLEtable[opr1][opr2];
                orAcc = func->TStable[orAcc][andAcc];
                break;
            case STORE_OP:
                return (orAcc);
#if 0
        // unused
            case OR_OP:
                orAcc = func->ORtable[orAcc][andAcc];
                break;
            case MLOR_OP:
                orAcc = func->MUXORLtable[orAcc][andAcc];
                break;
            case MHOR_OP:
                orAcc = func->MUXORHtable[orAcc][andAcc];
                break;
            case TG_OP:
                opr1 = SigOpr(ic);
                opr2 = SigOpr(ic);
                andAcc = func->TGtable[opr1][opr2];
                orAcc = func->TStable[orAcc][andAcc];
                break;
            case OCDRIVER_OP:
                orAcc = func->OCtable[orAcc][SigOpr(ic)];
                break;
            case ALOAD_OP:
                andAcc = func->ESWALLOWtable[SigOpr(ic)];
                break;
            case AAND_OP:
                andAcc = func->ANDtable
                    [(int)func->ESWALLOWtable[SigOpr(ic)]][andAcc];
                break;
            case NSTORE_OP:
                return func->INVERTtable[orAcc];
#endif
            default:
                ic = (EqnItem*)((size_t)ic - sizeof(short));
                throw new VError(verr_bug,
                    "bad opcode 0x%x at %p encountered while evaluating signal",
                    ic->opcode, ic, signal->name);
        }
    }
}

//-----------------------------------------------------------------------------

Level warnUnused(Signal* sig)
{
#ifdef SHOW_WARNING
    if (sig->dependList)
        display("// *** WARNING: signal '%s' declared & used but no source"
                "defined\n", sig->name);
    else
        display("// *** WARNING: remove \"%cSIGNAL %s\"\n",
            gLevelNames[sig->initLevel], sig->name);
#endif
    gWarningCount++;
    return (LV_C);
}
