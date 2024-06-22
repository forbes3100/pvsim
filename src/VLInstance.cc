// ****************************************************************************
//
//              PVSim Verilog Simulator Verilog Module Instantiation
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
#include "VL.h"
#include "VLSysLib.h"
#include "VLCoder.h"
#include "Model.h"

//-----------------------------------------------------------------------------
// Interface to simulator's existing C-Model event handlers.

// instantiation of an assign statement event handler
class Assign : public Model
{
    Net*        net;

public:
                Assign(Net* net, EvHandCodePtr evHandCode, char* instModule);
    void        reset();
    void        handleEvent();
};

// Construct an assignment statement event handler
Assign::Assign(Net* net, EvHandCodePtr evHandCode, char* instModule):
                    Model(net->name, evHandCode, instModule)
{
    this->net = net;
    modelSignal = 0;
}

// Add an event to cause the signal to evaluate initial value at t=0
void Assign::reset()
{
    if (modelSignal->inputsList)
    {
        // for 'res' global reset signal
        // A trick to force sim1step() to evaluate event:
        // set its pre-zero level to be the inverse of the initial level
        // also insure that we don't init a tri signal more than once
        Signal* signal = modelSignal->inputsList->signal();
        Event* lastEvt = signal->lastEvtPosted;
        Tick t2 = gTick + 1;
        if (signal->level == signal->initLevel &&
            (!lastEvt || lastEvt->tick != t2))
        {
            signal->level = inv(signal);
            addEvent(1, signal, signal->initLevel, CLEAN);
        }
    }
}

void Assign::handleEvent()
{
    executeHandCode();
}

//-----------------------------------------------------------------------------
// Interface to simulator's existing C-Model event handlers.
// 'initial', 'always', and named tasks

class VTask : public Model
{
    Instance*   instance;
public:
                VTask(char* name, EvHandCodePtr evHandCode, Instance* instance);
    void        reset();
    void        start();
    void*       startV(Model* realTask);
};

void* startVTask(void* threadParam);

// Construct an assignment statement event handler
VTask::VTask(char* name, EvHandCodePtr evHandCode, Instance* instance):
            Model(name, evHandCode, instance->instModule)
{
    this->modelSignal = addSignal(name, LV_H);
    // flag this signal as non-event-saving
    this->modelSignal->is = C_MODEL + REGISTERED;
    this->modelSignal->model = this;
    this->modelSignal->srcLoc = 0;
    setEntry(startVTask, (void*)this);
    this->isTask = TRUE;
    this->instance = instance;
}

// Initial VTask thread code: start this task.

void* startVTask(void* threadParam)
{
    Model* vTask = (Model*)threadParam;
    return vTask->startV(vTask);
}

// Add an event to cause the model to start
void VTask::reset()
{
    resetTask();
    addEvent(1, modelSignal, LV_L, WAKEUP);
}

// Start executing compiled task code
void* VTask::startV(Model* realTask)
{
    if (debugLevel(3))
    {
        printf("startV\n");
        fflush(stdout);
    }

    // first local var is task address
    *((Model**)this->instModule) = realTask;
    startVReal();
    return (void*)0;
}

// Dummy start (not used)
void VTask::start()
{
}

//-----------------------------------------------------------------------------
// Construct an event handler

EvHand::EvHand(VKeyword type, EvHandCodePtr code)
{
    this->type = type;
    this->code = code;
    this->net = 0;
    this->triggers = 0;
    this->modNext = 0;
    this->model = 0;
}

//-----------------------------------------------------------------------------
// Create an instance of an event handler

void EvHand::instantiate(Instance* parent, const char* parentName, int taskNum)
{
    Model* model;
    VLModule* mod = parent->module;
    char* imod = parent->instModule;
    switch (this->type)
    {
        case k_parameter:
            // 'parameter' handlers are called here during instantiation.
            model = new Model("param", this->code, parent->instModule);
            if (debugLevel(2))
                display("parameter: t=%p m=%p c=%p i=%p\n",
                        model, mod, this->code, imod);
            if (this->parm->hasOverride)
                *(size_t*)(imod + this->parm->disp) = this->parm->value;
            else
                //(this->code)((Assign*)model, imod);
                model->executeHandCode();
            break;

        case k_assign:
            // 'assign' event handlers are called from the main thread-- each
            // event runs through its handler without wait() calls.
            model = (Model*) new Assign(this->net, this->code, imod);
            if (debugLevel(2))
                display("Assign: %s.%s @%p t=0x%08x m=%p c=%p i=%p\n",
                            parentName, (char*)this->net->name, this->net,
                            model, mod, this->code, imod);
            if (this->net->isExType(ty_vector))
            {
                Vector* vec = (Vector*)this->net;
                Model** modelVec = vec->modelVec + this->vecBaseBit;
                if (debugLevel(2))
                    display("EvHand::inst: Vector %s.%s bit=%d size=%d"
                            " model=%p\n", parentName, vec->name,
                            this->vecBaseBit, this->vecSize, model);
                for (int n = this->vecSize; n > 0; n--)
                    *modelVec++ = model;
            }
            else if (debugLevel(2))
                display("EvHand::inst: Scalar %s.%s model=%p\n",
                    parentName, net->name, model);
            this->net->model = model;
            break;

        case k_initial:
        case k_always:
        {
            // 'initial' and 'always' handlers are started in their own thread
            // and contain wait() calls.
            const char* typeName = kVKeyStr[this->type];
            TmpName taskName = TmpName("%s._%s%d", parentName, typeName,
                                       taskNum);
            model = (Model*) new VTask(newString(taskName), this->code, parent);
            const char* blockName = Scope::local->lastBlockName;
            // if it has a named begin-end block, point name to task.
            if (blockName)
                new BegEnd(blockName, model);
            if (debugLevel(2))
                display("VTask: %s (%s) t=0x%08x m=%p c=%p i=%p\n",
                            (char*)taskName, blockName, model, mod,
                            this->code, imod);
            break;
        }
        default:
            throw new VError(verr_bug, "BUG: instantiate");
    }
    this->model = model;
}

//-----------------------------------------------------------------------------
// Set dependencies from event handler's input signals to its model signal,
// now that all Signals have been created.

void EvHand::setDependencies(Instance* instance)
{
    if (this->type == k_parameter)
        return;

    Signal* dependent = this->model->modelSig();
    const char* dependName = this->model->designator();
    if (!dependent)
        throw new VError(this->net->srcLoc, verr_illegal,
                        "missing or improper definition for signal '%s'",
                        dependName);
    if (debugLevel(2))
        display("setDependencies for EvHand %s, dependent %s\n",
                    dependName, dependent->name);

#if 1
    // every signal depends on gAssignsReset, to force initialization at t=0
    if (!gAssignsReset || !gAssignsReset->model ||
        !gAssignsReset->model->modelSig())
        throw new VError(verr_bug, "no global reset!");
    setDependency(dependent, gAssignsReset->model->modelSig());
#endif

    for (NetList* n = this->triggers; n; n = n->next)
    {
        Net* trigger = n->net;
        switch (trigger->exType.code)
        {
            case ty_scalar:
            {
                Signal* triggerSig = ((Scalar*)trigger)->signal;
                if (!triggerSig)
                    goto errorMissingDef;
                setDependency(dependent, triggerSig);
                break;
            }
            case ty_vector:
            {
                Vector* trigVec = (Vector*)trigger;
                SignalVec* trigSigPtr = trigVec->signalVec;
                for (int i = trigVec->exType.size; i > 0; i--)
                {
                    Signal* triggerSig = *trigSigPtr++;
                    if (!triggerSig)
                        goto errorMissingDef;
                    setDependency(dependent, triggerSig);
                }
                break;
            }
            case ty_memory:
            {
                Signal* triggerSig = ((Memory*)trigger)->signal;
                if (!triggerSig)
                    goto errorMissingDef;
                setDependency(dependent, triggerSig);
                break;
            }
            errorMissingDef:
            {
                TmpName msg = TmpName("missing definition for %s",
                                      trigger->name);

                Instance* inst = instance;
                const char* sigName = trigger->name;
                for (Conn* conn = inst->conns; conn; conn = conn->next)
                    if (strcmp(conn->portName, sigName) == 0)
                    {
                        // show this connection node
                        sigName = conn->var->name;
                        inst = conn->instance;
                        TmpName varRefMsg = TmpName(" from %s:%s",
                                               inst->module->name, sigName);
                        msg += varRefMsg;
                        // now search next farther-away inst for the connection
                        conn = inst->conns;
                    }

                throw new VError(trigger->srcLoc, verr_illegal, msg);
                break;
            }
            default:
                throw new VError(trigger->srcLoc, verr_illegal,
                                 "setDependencies type for '%s': not yet",
                                 trigger->name);
        }
    }
}


//-----------------------------------------------------------------------------

void EvHand::instantiateAssignsReset(Instance* parent)
{
    this->instantiate(parent, "__resI__", 0);

    Model* model = this->model;
    Scalar* res = (Scalar*)this->net;
    Signal* signal = addSignal("__res__", LV_L);
    res->signal = signal;
    signal->is &= ~DISPLAYED;
    signal->is |= C_MODEL;
    signal->model = model;
    model->setModelSignal(signal);
}

//-----------------------------------------------------------------------------
// Construct an instantiation or instance template.

Instance::Instance(const char* moduleName, const char* designator,
                   ParmVal* parmVals, Instance* parent, Instance* next) :
                                    Scope(designator, ty_instance, 0)
{
    this->moduleName = moduleName;
    this->module = 0;
    this->parmVals = parmVals;
    this->conns = 0;
    this->instModule = 0;
    this->parent = parent;
    this->nextTmpl = next;
}

//-----------------------------------------------------------------------------
// Bind the given port signal to the connection's parent signal by copying
// its Signal address into this instance's local var.

Port* Conn::bind(Port* port, VLModule* module, char* instModule)
{
    Variable* portVar = 0;
    if (port)
        portVar = port->var;
    if (this->portName)
    {
        portVar = (Variable*)module->find(this->portName, module, TRUE);
        if (!portVar)
        {
            // if an unconnected port doesn't exist, ignore it
            //  (may not exist in sim model only)
            if (strcmp(this->var->name, "_") == 0)
                goto bindDone;
            throw new VError(module->srcLoc, verr_notFound,
                             "port '%s' not found", this->portName);
        }
        if (!portVar->isType(ty_var))
            throw new VError(portVar->srcLoc,
                             verr_illegal, "named port '%s' not really a port",
                             this->portName);
    }
    else if (!port)
        throw new VError(module->srcLoc,
                         verr_illegal, "too many ports given");
    if (debugLevel(2))
        display("Bind %s port %s to %d(r30)=%s\n", module->name, portVar->name,
                this->var->disp, this->var->name);

    // at this point we have the parent module's var with its assigned Signal,
    // and we know which internal port to connect it to
    // so copy signal and set local var pointer to it
    if (this->var->isExType(ty_scalar) || this->var->isExType(ty_vector))
    {
        Net* net = (Net*)this->var;
        Net* portNet = (Net*)portVar;

        if ((net->attr & att_copy) && (portNet->attr & att_output))
            throw new VError(portVar->srcLoc, verr_illegal,
                        "can't connect output port %s of %s to non-net",
                        portVar->name, module->name);

        if (net->isExType(ty_scalar) || this->range->isScalar)
        {
            Scalar* portNet = (Scalar*)portVar;
            if (!portNet->isExType(ty_scalar))
                throw new VError(portVar->srcLoc,
                        verr_illegal,
                        "attempt to bind scalar %s to non-scalar port %s",
                        this->var->name, portVar->name);

            Signal* signal = 0;
            if (net->isExType(ty_scalar))
            {
                Scalar* scalar = (Scalar*)this->var;
                signal = scalar->signal;
            }
            else
            {
                Vector* vec = (Vector*)this->var;
                Range* fullRange = vec->range;
                signal = *(vec->signalVec +
                      (this->range->left.bit -
                       fullRange->left.bit) * fullRange->incr);
            }
            portNet->signal = signal;
            *((Signal**)(instModule + portVar->disp)) = signal;
            if (debugLevel(2))
                display("  Bound Signal %s (m=%p) to %s (m=%p, i=%p)\n",
                    portVar->name, portNet->model, this->var->name, net->model,
                    instModule);

            // promote parent's wire to a reg if driven by a reg output from
            // this module
            if ((portNet->attr & att_reg) &&
                !(net->attr & (att_reg | att_tri | att_inout)))
                net->attr |= att_reg;
        }
        else
        {
            Vector* vec = (Vector*)this->var;
            Vector* portVec = (Vector*)portVar;
            if (!vec->isExType(ty_vector))
                throw new VError(portVar->srcLoc,
                        verr_illegal,
                        "attempt to bind vector %s to non-vector port %s",
                        vec->name, portVar->name);
            if (portVar->exType.size != this->range->size)
                throw new VError(portVar->srcLoc,
                            verr_illegal,
                            "vector %s[%d] doesn't match size of port %s[%d]",
                            vec->name, this->range->size,
                            portVar->name, portVar->exType.size);

            // copy in Vector's (subrange of) bit Signals
            Range* fullRange = vec->range;
            Signal** signalVec = vec->signalVec +
                      (this->range->left.bit -
                       fullRange->left.bit) * fullRange->incr;
            portVec->signalVec = signalVec;
            Signal** instSigVec = (Signal**)(instModule + portVar->disp);
            for (int i = this->range->size; i > 0; i--)
            {
                Signal* signal = *signalVec++;
                *instSigVec++ = signal;
                if (!signal)
                    throw new VError(portVar->srcLoc,
                                verr_illegal,
                                "output vector .%s(%s) is missing a definition",
                                portVar->name, vec->name);
            }
            if (debugLevel(2))
                display("  Bound Vec %s (m=%p) to %s[%d:%d] (m=%p)\n",
                    portVar->name, portVec->model, this->var->name,
                    this->range->left.bit, this->range->right.bit, vec->model);

            // promote parent's wire to a reg if driven by a reg output from
            // this module, if not driving a subrange of the parent vector
            if ((portNet->attr & att_reg) &&
                this->range->size == fullRange->size &&
                !(net->attr & (att_reg | att_tri | att_inout)))
                net->attr |= att_reg;
        }

        // nonstandard: can't promote an internal wire output to parent's tri
        if ((net->attr & att_output) && !(portNet->attr & att_tri) &&
            (net->attr & att_tri))
            throw new VError(portVar->srcLoc,
                        verr_illegal,
                        "not yet: can't connect tri %s to wire port %s",
                        this->var->name, portVar->name);
    }
    else
        throw new VError(this->var->srcLoc,
                         verr_illegal, "bind type of %s to %s: not yet",
                         this->var->name, (port ? port->name : this->portName));

    portVar->bound = TRUE;

    bindDone:
    if (!port || this->portName)
        return 0;
    else
        return port->next;      // return pointer to next port
}

//-----------------------------------------------------------------------------
// Copy back model handler assignments from output signals to the parent's
// to unbind the given port signal from the connection's parent signal.

Port* Conn::unbind(Port* port, VLModule* module, char* parentName)
{
    Variable* portVar = 0;
    if (port)
        portVar = port->var;
    // error checking has already been done by bind()
    if (this->portName)
    {
        portVar = (Variable*)module->find(this->portName, portVar, TRUE);
        if (!portVar)
        {
            // if an unconnected port doesn't exist, ignore it
            //  (may not exist in sim model only)
            if (strcmp(this->var->name, "_") == 0)
                goto unbindDone;
            throw new VError(module->srcLoc, verr_notFound,
                             "port '%s' not found", this->portName);
        }
    }

    if (portVar->isExType(ty_scalar) || portVar->isExType(ty_vector))
    {
        Net* portNet = (Net*) portVar;
        Net* net = (Net*) this->var;
        if ((portNet->attr & att_output) && !(net->attr & att_copy))
        {
            // copy back handler module instance
            if (portVar->isExType(ty_vector))
            {
                Vector* vec = (Vector*)net;
                Range* fullRange = vec->range;
                Model** modelVec = vec->modelVec +
                          (this->range->left.bit -
                           fullRange->left.bit) * fullRange->incr;
                // Model** modelPortVec = ((Vector*)portNet)->modelVec;
                if (debugLevel(2))
                    display("Unbind: Vector %s.%s size=%d modelVec=%p\n",
                        parentName, net->name, this->range->size,
                        ((Vector*)portNet)->modelVec);
                // copy back Vector's (subrange of) bit model pointers
                for (int n = this->range->size; n > 0; n--)
                {
#if 0
                    Model* m = *modelPortVec++; // why copy back? not always 0?
                    *modelVec++ = m;
                    if (debugLevel(2))
                    {
                        char* mName = "?";
                        if (m)
                        {
                            Signal* mSig = m->modelSig();
                            if (mSig)
                                mName = mSig->name;
                        }
                        display("        bit model 0x%x %s\n", (int)m, mName);
                    }
#else
                    // assigned (by module), but never to be used
                    *modelVec++ = (Model*)-1;
                    if (debugLevel(2))
                        display("        bit model dummy\n");
#endif
                }
                net->attr |= portNet->attr & att_tri;
                if (this->range->size == fullRange->size)
                    net->attr |= portNet->attr & att_reg;
            }
            else
            {
                net->model = portNet->model;
                // and its basic attributes
                net->attr |= portNet->attr & (att_reg | att_tri);
            }
        }
    }

    unbindDone:
    if (!port || this->portName)
        return 0;
    else
        return port->next;  // return pointer to next port
}

//-----------------------------------------------------------------------------
// Bind a parameter value to the module's parameter or defparam value,
// and mark that it has a preset value so that it won't get evaluated.

Variable* ParmVal::bind(Variable* parm, VLModule* module, Instance* inst)
{
    if (this->name)
    {
        parm = (Variable*)module->find(this->name, module);
        if (!parm->isType(ty_var))
            throw new VError(parm->srcLoc, verr_illegal,
                             "named parameter '%s' not really a parameter",
                             this->name);
    }
    else if (!parm)
        throw new VError(module->srcLoc, verr_illegal,
                            "too many parameter values given");

    if (this->isDefParm)
    {
        // defparm'ed: get parent's instantiated parameter value
        parm->value = *(size_t *)(inst->parent->instModule + defparm->disp);
        parm->scale = defparm->scale;
    }
    else
    {
        parm->value = this->value;
        parm->scale = this->scale;
    }
    parm->hasOverride = TRUE;

    if (this->name)
        return 0;
    else
        return parm->next;
}

//-----------------------------------------------------------------------------
// Unbind a parameter value from the instance's parameter: unmark it
// as being overridden.

Variable* ParmVal::unbind(Variable* parm, VLModule* module, Instance* inst)
{
    // error checking has already been done by bind()
    if (this->name)
        parm = (Variable*)module->find(this->name, module);

    parm->hasOverride = FALSE;

    if (this->name)
        return 0;
    else
        return parm->next;
}

//-----------------------------------------------------------------------------
// If an error occurred during an instantiation the scanner will be
// pointing to the end of the file, so we must prepend location details

void Instance::labelAndThrow(VError* error, char* desig)
{
    char* msg = new char[max_messageLen];
    if (this->module)
        snprintf(msg, max_messageLen-1,
                 "in module %s instance '%s':\n//   %s",
                 this->module->name, newString(desig), error->message);
    else
        snprintf(msg, max_messageLen-1, "in instance '%s':\n//   %s",
                 newString(desig), error->message);
    error->message = msg;
    throw error;
}

//-----------------------------------------------------------------------------
// Create a scopes's internal signals and initialize integers.

void Scope::initVariables(char* instModule, char* fullDesig)
{
    TmpName sigFD;
    if (fullDesig[0] == 'm' && (fullDesig[1] == '.' || fullDesig[1] == 0))
    {
        if (fullDesig[1] == 0)
            sigFD = TmpName("");
        else
            sigFD = TmpName("%s.", fullDesig + 2);
    }
    else
        sigFD = TmpName("%s.", fullDesig);

    for (NamedObj* sym = this->names; sym; sym = sym->namesNext)
        if (sym->isType(ty_var))
        {
            Variable* var = (Variable*)sym;
            switch (var->exType.code)
            {
                case ty_int:        // integer: plug in initial value
                    if (!var->isParm)
                        *((size_t*)(instModule + var->disp)) = var->value;
                    break;
                    
                case ty_scopeRef:   // non-local scope: do nothing
                    break;

                case ty_scalar:     // scalar signal: create Signal if internal
                {
                    Scalar* scalar = (Scalar*)var;
                    if (!(scalar->attr & att_inout))
                    {
                        TmpName sigName = TmpName("%s%s",
                                                  (char*)sigFD, scalar->name);
                        Signal* signal = addSignal(newString(sigName), LV_L);
                        signal->model = 0;   // model filled in later for wires
                        signal->srcLoc = scalar->srcLoc;
                        signal->srcLocObjName = scalar->srcLocObjName;
                        if ((sigName[0] == '_' && sigName[1] == '_') ||
                            !scalar->isVisible)
                            signal->is &= ~DISPLAYED;
                        if (scalar->attr & att_tri)
                            signal->is |= TRI_STATE;
                        else
                            signal->is |= C_MODEL;
                        if (scalar->attr & (att_pull0 | att_pull1))
                        {
                            signal->is |= TRI_STATE;
                            signal->floatLevel = (scalar->attr & att_pull0 ?
                                                  LV_L : LV_H);
                            signal->RCminTime = 5 * gTicksNS;
                            signal->RCmaxTime = signal->RCminTime;
                        }
                        scalar->signal = signal;
                        // local var is pointer to signal
                        *((Signal**)(instModule + scalar->disp)) = signal;
                    }
                    break;
                }
                case ty_vector:   // vector signal: create Signals if internal
                {
                    Vector* vec = (Vector*)var;
                    if (!(vec->attr & att_inout))
                    {
                        int bit = vec->range->left.bit;
                        int endBit = vec->range->right.bit;
                        if (!(vec->range->left.isConst &&
                              vec->range->right.isConst))
                        {
                            bit = sizeof(size_t);
                            endBit = 0;
                        }
                        // create a 'bus' display-only signal first
                        TmpName sigName = TmpName("%s%s[%d:%d]", (char*)sigFD,
                                                    vec->name, bit, endBit);
                        Signal* signal = addSignal(newString(sigName), LV_S);
                        signal->srcLoc = vec->srcLoc;
                        signal->srcLocObjName = vec->srcLocObjName;
                        signal->busOpt = DISP_BUS;
//                      if (inverted)
//                          signal->busOpt |= DISP_INVERTED;
                        gDispBusWidth = vec->range->size;
                        gDispBusBitNo = gDispBusWidth - 1;
                        NetAttr attr = vec->attr;

                        // now create the individual bit signals
                        SignalVec* sigPtr = (SignalVec*)(instModule +
                                                         vec->disp);
                        vec->signalVec = sigPtr;
#ifdef WHY
                        if (vec->attr & att_copy)
                            break;
#endif
                        int bitInc = vec->range->incr;
                        endBit += bitInc;
                        for ( ; bit != endBit; bit += bitInc)
                        {
                            TmpName sigName = TmpName("%s%s[%d]", (char*)sigFD,
                                                      vec->name, bit);
                            Signal* signal = addSignal(newString(sigName),
                                                       LV_L);
                            // model filled in later for wires
                            signal->model = 0;
                            signal->srcLoc = vec->srcLoc;
                            signal->srcLocObjName = vec->srcLocObjName;
                            if (!vec->isVisible && !debugLevel(1))
                                signal->is &= ~DISPLAYED;
                            if (attr & att_tri)
                                signal->is |= TRI_STATE;
                            else
                                signal->is |= C_MODEL;
                            if (attr & (att_pull0 | att_pull1))
                            {
                                signal->is |= TRI_STATE;
                                signal->floatLevel = (attr & att_pull0 ?
                                                      LV_L : LV_H);
                                signal->RCminTime = 5 * gTicksNS;
                                signal->RCmaxTime = signal->RCminTime;
                            }
                            // local var is array of pointers to signals
                            *sigPtr++ = signal;
                        }
                    }
                    break;
                }
                case ty_memory:     // memory: create trigger signal
                {
                    Memory* mem = (Memory*)var;
                    TmpName sigName = TmpName("%s%s", (char*)sigFD, mem->name);
                    Signal* signal = addSignal(newString(sigName), LV_L);
                    signal->model = 0;      // model filled in later
                    signal->srcLoc = mem->srcLoc;
                    signal->srcLocObjName = mem->srcLocObjName;
                    signal->is = C_MODEL;
                    if (mem->isVisible || debugLevel(1))
                        signal->is |= DISPLAYED;
                    mem->signal = signal;
                    // first 4 bytes of local var is pointer to trigger signal
                    *((Signal**)(instModule + mem->disp)) = signal;
                    break;
                }
                case ty_vecConst:   // vector constant: copy into local vars
                {
                    Level* svp = var->levelVec;
                    Level* dvp = (Level*)(instModule + var->disp);
                    for (int i = var->exType.size; i > 0; i--)
                        *dvp++ = *svp++;
                    break;
                }
                default:
                    throw new VError(verr_bug, "BUG: initVariables");
            }
        }
}

//-----------------------------------------------------------------------------
// Set scope's internal signal's model (eval code) pointers and resolve
// external scope references.

void Scope::linkVariables(char* instModule, char* fullDesig)
{
    for (NamedObj* sym = this->names; sym; sym = sym->namesNext)
        if (sym->isType(ty_var))
        {
            Variable* var = (Variable*)sym;
            switch (var->exType.code)
            {
                case ty_scopeRef:   // non-local scope: plug in address
                {
                    Scope* scope = var->externScope;
                    switch (scope->type)
                    {
                        case ty_instance:
                        {
                            Instance* inst = (Instance*)scope;
                            *((char**)(instModule + var->disp)) =
                                                        inst->instModule;
                            break;
                        }
                        default:
                            throw new VError(var->srcLoc,
                                verr_illegal, "not this type of ref yet");
                    }
                    break;
                }
                case ty_scalar:
                {
                    Scalar* scalar = (Scalar*)var;
                    Signal* signal = scalar->signal;
                    if (!(scalar->attr & (att_reg | att_input | att_tri)))
                    {           // a 'wire' internal or output-only signal:
                                // associate it with its 'assign' code
                        Model* model = scalar->model;
                        if (!model || !signal)
                        {
                            // ignore unconnected missing ports
                            if (strcmp(scalar->name, "_") == 0)
                                continue;
                            throw new VError(var->srcLoc, verr_illegal,
                               "wire '%s' never assigned (missing port?)",
                               scalar->name);
                        }
                        signal->model = model;
                        if (debugLevel(3))
                            display("Link Scalar %s.%s @0x%x: Sig %s @%p,"
                                    " t=%p msig=%p\n", fullDesig, scalar->name,
                                    scalar, signal->name, signal,
                                model, model->modelSig());
                        model->setModelSignal(signal);
                    }
                    else if (!(scalar->attr & att_input))
                    {
                        if (!scalar->assigned && !scalar->isNamed("ErrFlag"))
                            throw new VError(var->srcLoc, verr_illegal,
                                        "'%s' never assigned", scalar->name);
                    }
                    Scalar* tri = (Scalar*)scalar->tri;
                    if (tri)
                    {       // a tri-state internal signal: tri depends on it
                        if (VL::debugLevel < 2)
                            signal->is |= INTERNAL;
                        
                        Signal* triSig;
                        if (tri->isExType(ty_vector))
                        {
                            Vector* vTri = (Vector*)scalar->tri;
                            int i = vTri->range->incr *
                                    (scalar->triRange->left.bit -
                                     vTri->range->left.bit);
                            triSig = vTri->signalVec[i];
                        }
                        else
                            triSig = tri->signal;

                        if (!triSig)
                        {
                            // ignore unconnected missing ports
                            if (strcmp(tri->name, "_") == 0)
                                continue;
                            throw new VError(var->srcLoc, verr_illegal,
                                        "tri-state wire '%s' never assigned"
                                        " (missing port?)", tri->name);
                        }
                        if (!(triSig->is & TRI_STATE))
                            throw new VError(tri->srcLoc, verr_illegal,
                                    "wire '%s' should be a tri (nonstandard!)",
                                    triSig->name);
                        setDependency(triSig, signal);

                        if (scalar->enable) // and an enable: tell signal
                        {
                            Signal* enSig = scalar->enable->signal;
                            signal->enable = enSig;
                            setDependency(triSig, enSig);
                        }
                    }
                    break;
                }
                case ty_vector:
                {
                    Vector* vec = (Vector*)var;
                    if (!(vec->attr & (att_reg | att_input | att_tri)))
                    {           // a 'wire' internal or output-only signal:
                                // associate it with its 'assign' code
                        Model** modelPtr = vec->modelVec;
                        Signal** sigPtr = vec->signalVec;
                        Range* range = vec->range;
                        int end = range->right.bit + range->incr;
                        for (int i = range->left.bit;
                             i != end;
                             i += range->incr, modelPtr++, sigPtr++)
                        {
                            Model* model = *modelPtr;
                            if (model == (Model*)-1)
                                continue;     // skip bit if driven by a module

                            if (!model || !sigPtr || !*sigPtr)
                            {
                                modelPtr = vec->modelVec;
                                bool anyBit = FALSE;
                                for (int j = range->left.bit;
                                     j != end;
                                     j += range->incr, modelPtr++)
                                    if (*modelPtr)
                                    {
                                        anyBit = TRUE;
                                        break;
                                    }
                                if (anyBit)
                                    throw new VError(var->srcLoc, verr_illegal,
                                        "wire '%s[%d]' never assigned",
                                        vec->name, i);
                                throw new VError(var->srcLoc, verr_illegal,
                                    "vector wire '%s' never assigned",
                                    vec->name);
                            }
                            (*sigPtr)->model = model;
                            if (debugLevel(3))
                                display("Link Vector %s.%s @%p: SigV %s @%p,"
                                        " size=%d t=%p msig=%p\n",
                                        fullDesig, vec->name, vec,
                                        (*sigPtr)->name, sigPtr,
                                        vec->range->size, model,
                                        model->modelSig());
                            if (!model->modelSig())
                            {
                                model->setModelSignal(*sigPtr);
                                if (debugLevel(3))
                                    display(
                                        "Link Vector %s set modelSignal %s\n",
                                        vec->name, model->modelSig()->name);
                            }
                        }
                    }
                    Vector* vTri = (Vector*)vec->tri;
                    if (vTri)
                    {       // a tri-state internal vector: tri depends on it
                        int i = 0;
                        if (!vec->triRange->isFull)
                            i = vTri->range->incr *
                                (vec->triRange->left.bit -
                                 vTri->range->left.bit);
                        Signal** sigPtr = vec->signalVec;
                        Signal** sigTriPtr = vTri->signalVec + i;
                        Signal* enSig = vec->enable->signal;

                        for (int n = vec->triRange->size; n > 0; n--)
                        {
                            Signal* signal = *sigPtr++;
                            Signal* triSig = *sigTriPtr++;
                            signal->is |= INTERNAL;
                            signal->enable = enSig;
                            setDependency(triSig, signal);
                            setDependency(triSig, enSig);
                        }
                    }
                    break;
                }
                case ty_memory:
                {
                    Memory* mem = (Memory*)var;
                    Signal* signal = mem->signal;
                    if (!(mem->attr & (att_reg | att_input)))
                    {           // a 'wire' internal or output-only signal:
                                // associate it with its 'assign' code
                        Model* model = mem->model;
                        if (!model)
                            throw new VError(var->srcLoc, verr_illegal,
                                        "wire '%s' never assigned", mem->name);
                        signal->model = model;
                        model->setModelSignal(signal);
                    }
                    break;
                }
                case ty_int:        // do nothing for these
                case ty_vecConst:
                    break;

                default:
                    throw new VError(verr_bug, "BUG: linkVariables");
            }
        }
}

//-----------------------------------------------------------------------------
// Instantiate a VLModule with actual signals and local variable space.

void Instance::instantiate(const char* parentName, Instance* parent)
{
    TmpName fullDesig;
    VLModule* mod = 0;
    char* imod = 0;

    if (!gQuietMode)
        display("    instantiating %s %s\n", this->moduleName, this->name);
    this->parent = parent;
    try
    {
        // create a complete designator ("ecom1.u506")
        if (parentName)
            fullDesig = TmpName("%s.%s", parentName, this->name);
        else
            fullDesig = TmpName(this->name);

        // locate module, if not already found at compile time
        if (!this->module)
        {
            NamedObj* obj = Scope::global->findScope(this->moduleName, TRUE);
            if (!obj)
            {
                char* catModName = newString(TmpName("%s_%s",
                                                this->moduleName, this->name));
                obj = Scope::global->findScope(catModName, TRUE);
                if (!obj)
                    throw new VError(verr_notFound, "'%s' nor '%s' found",
                                this->moduleName, catModName);
                this->moduleName = catModName;
            }
            if (!obj->isType(ty_module))
                throw new VError(this->srcLoc,
                                verr_illegal, "%s: '%s' is not a module",
                                this->name, this->moduleName);
            this->module = (VLModule*)obj;
        }
        mod = this->module;

        // allocate module's local storage
        this->instModule = new char[mod->localSize];
        imod = this->instModule;

        // bind parameter values to parameters
        Variable* parm = mod->parms;
        for (ParmVal* pv = this->parmVals; pv; pv = pv->next)
            parm = pv->bind(parm, mod, this);

        // create all module's event handlers (assign, always, task, etc.)
        int taskNum = 1;
        for (EvHand* eh = mod->evHands; eh; eh = eh->modNext)
        {
            eh->instantiate(this, newString(fullDesig), taskNum);
            taskNum++;
        }

        // bind port signals to parent instance's corresponding signals
        Port* port = mod->ports;
        Port* p;
        for (p = port; p; p = p->next)
        {
            if (p->var)
                p->var->bound = FALSE;
        }
        for (Conn* conn = this->conns; conn; conn = conn->next)
        {
            conn->instance = this;
            port = conn->bind(port, mod, imod);
        }
        if (port)
            throw new VError(this->srcLoc, verr_illegal, "too few ports given");
        for (p = mod->ports; p; p = p->next)
        {
            Net* net = (Net*) p->var;
            if (!net || (!net->bound && (net->attr & att_input)))
                throw new VError(this->srcLoc, verr_illegal,
                                 "input port '%s' not connected", p->name);
        }

        // create module's internal signals and initialize integers
        mod->initVariables(imod, fullDesig);

        // create any module function or task internal signals, init integers
        for (Scope* scope = mod->scopes; scope; scope = scope->scopesNext)
            scope->initVariables(imod, fullDesig);
    }
    catch (VError* error)
    {
        labelAndThrow(error, fullDesig);
    }

    // instantiate all module's sub-instances
    for (Instance* iTmpl = mod->instTmpls; iTmpl; iTmpl = iTmpl->nextTmpl)
        iTmpl->instantiate(fullDesig, this);

    try
    {
        // Set scope's internal signal's model (eval code) pointers and resolve
        // external scope references.
        mod->linkVariables(imod, fullDesig);

        // for any module function or tasks: resolve external scope references
        for (Scope* scope = mod->scopes; scope; scope = scope->scopesNext)
            scope->linkVariables(imod, fullDesig);

        // add dependencies for event handlers to their input signals
        for (EvHand* eh = mod->evHands; eh; eh = eh->modNext)
            eh->setDependencies(this);

#if 0
        // unbind parameter values from parameters
        Variable* parm = mod->parms;
        for (ParmVal* pv = this->parmVals; pv; pv = pv->next)
            parm = pv->unbind(parm, mod, this);
#endif

        // unbind port signals from parent instance's corresponding signals
        // this 'copies out' output signal's assignments
        Port* port = mod->ports;
        for (Conn* conn = this->conns; conn; conn = conn->next)
            port = conn->unbind(port, mod, fullDesig);

    }
    catch (VError* error)
    {
        labelAndThrow(error, fullDesig);
    }
}
