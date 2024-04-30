// ****************************************************************************
//
//      PVSim Verilog Simulator Verilog Objects Classes
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

#pragma once

#include "Utils.h"
#include "SimPalSrc.h"
#include "Model.h"

// Verilog object types
enum VType
{
    ty_var,
    ty_func,
    ty_task,
    ty_begEnd,
    ty_module,
    ty_instance,
    ty_global,
    ty_macro
};

// forward references
class Net;
class EvHand;
class Instance;
class VLModule;

// Verilog Named Objects (Symbols)

class NamedObj : SimObject
{
public:
    const char* name;
    VType       type;
    NamedObj*   namesNext;      // next name in current scope
    Token*      srcLoc;         // source position for error messages

                NamedObj();
                NamedObj(const char* name, VType type);
    bool        isType(VType type)  { return (this->type == type); }
    bool        isNamed(const char* name) { return strcmp(this->name, name) == 0; }
    friend class Scope;
};

// expression type codes -- must match Expr::kExTySym[]
enum VExTyCode
{
    ty_int,         // int integer
    ty_float,       // floating-point parameter
    ty_vecConst,    // Level-vector constant
    ty_scalar,      // single-bit signal
    ty_vector,      // multi-bit signals, bitWidth used
    ty_memory,      // integer memory array of ints
    ty_scopeRef,    // non-local scope reference
    ty_none,        // a voided item
    num_types
};

// expression types
struct VExType
{
    VExTyCode   code;           // type code
    unsigned int size;          // width of a signal vector
};                              // or length of integer memory array

class Variable : public NamedObj
{
public:
    VExType     exType;         // net/integer type/size, func return type
    int         disp;           // storage location relative to rSP
    union
    {
        size_t  value;          // integer initial value or parameter default
        class Scope*    externScope;    // scope ref: pointer to external scope
        Level*  levelVec;       // vector constant: pointer to Levels array
    };
    int         scale;          // scaled-integer scale
    bool        bound;          // flag used to detect unbound ports
    bool        isParm;         // TRUE if a parameter
    bool        isDefParm;      // TRUE if a defparam'ed parameter
    bool        hasOverride;    // TRUE if parameter value from parent
    Variable*   next;           // next parameter in module's list

                Variable(const char* name, VExTyCode type, short disp,
                        size_t value = 0, int size = 0):
                                                    NamedObj(name, ty_var)
                                        {   this->exType.code = type;
                                            this->exType.size = size;
                                            this->disp = disp;
                                            this->value = value;
                                            this->scale = 1;
                                            this->isParm = FALSE;
                                            this->hasOverride = FALSE;
                                            this->next = 0; }
                ~Variable();
    bool        isExType(VExTyCode t)   { return (exType.code == t); }
    void        setType(VExTyCode type, int size)
                                        {   this->exType.code = type;
                                            this->exType.size = size; }

    friend class Vector;
    friend class Memory;
};

// a named begin-end block
class BegEnd : public NamedObj
{
public:
    Model*      model;

                BegEnd(const char* name, Model* model):
                                    NamedObj(name, ty_begEnd)
                                        {   this->model = model; }
};

//-----------------------------------------------------------------------------

// Net attributes
typedef short NetAttr;  // not enumerated so that they may be combined

const short att_none    = 0x0000;
const short att_reg     = 0x0001;       // net is a register
const short att_tri     = 0x0002;       // net is tri-state
const short att_supply  = 0x0004;       // net is a supply
const short att_input   = 0x0008;       // net is an input
const short att_output  = 0x0010;       // net is an output
const short att_event   = 0x0020 + att_reg; // net is a named event (and a reg)
const short att_pull0   = 0x0040;       // net is pulled to 0
const short att_pull1   = 0x0080;       // net is pulled to 1
const short att_wor     = 0x0100;       // net is open collector wire-or
const short att_copy    = 0x0200;       // net is just a copy of real thing

const short att_inout   = att_input + att_output; // net is an inout

class Scalar;
struct Expr;


// a left or right index element of a range
class RangeIndex
{
public:
    bool        isConst;        // TRUE selects bit
    bool        isDisp;         // TRUE selects disp
    union
    {
        short       bit;        // if a constant: bit number or memory address
        short       disp;       // if an integer or param: local var displacement
    };
    Expr*       expr;           // general expression
    void        compile();
    int         codeIntExpr();
    void        codeLoadConst(int reg);
};

// a vector or memory range: [left] or [left:right]
class Range
{
public:
    bool        isFull;             // TRUE if full range. (left,right not used)
    bool        isScalar;           // TRUE if just left used. (bit select)
    unsigned int size;              // if constant: size of selection
    int         incr;               // bit-to-bit disp: +/- sizeof(Signal*)
    RangeIndex  left;
    RangeIndex  right;
                Range();
    void        compile();
                Range(Range* x)     // copy a Range
                            { *this = *x; }
                Range(int nBits)    // construct an n-bit Range
                            { this->isFull = TRUE;
                              this->isScalar = FALSE;
                              this->size = nBits;
                              this->incr = -1;
                              this->left.isConst = TRUE;
                              this->left.bit = nBits - 1;
                              this->right.isConst = TRUE;
                              this->right.bit = 0; }
};

// A module signal scalar or vector, or named event
class Net : public Variable
{
public:
    NetAttr     attr;           // attributes
    Scalar*     enable;         // enable assoc. with this internal, if any
    Net*        tri;            // tri-state signal this drives, if any
    Range*      triRange;       // range of bits driven in tri
    short       triSrcNo;       // next driver number if a tri-state bus
    EvHand*     evHand;         // event handler if not reg
    Model*      model;          // instantiated handler model if not reg
    bool        assigned;       // flag used to detect unassigned vars
    bool        isVisible;      // is to be displayed

                Net(const char* name, VExTyCode type, NetAttr attr, short disp,
                    Net* tri = 0, Range* triRange = 0);
                ~Net()          { if (triRange) delete triRange; }
    char*       repr();
};

// A module trigger signal, either a Scalar or a Memory.
// Local vars displacement is for a pointer to the actual Signal structure.
class TrigNet : public Net
{
    //Level       initLevel;      // initial level
public:
    Signal*     signal;         // instantiated signal

                TrigNet(const char* name, VExTyCode type, NetAttr attr, short disp,
                        TrigNet* tri = 0, Range* triRange = 0) :
                    Net(name, type, attr, disp, tri, triRange), signal(0)
                                        { }
};

// A module scalar signal. Local vars displacement is for a pointer
// to the actual Signal structure.
class Scalar : public TrigNet
{
public:
                Scalar(const char* name, NetAttr attr, short disp,
                        Scalar* tri = 0, Range* triRange = 0) :
                    TrigNet(name, ty_scalar, attr, disp, tri, triRange)
                                        { }
};

// A module vector (bus). Local vars displacement is to an array
// of pointers to Signals.
class Vector : public Net
{
public:
    Range*      range;          // declared bit range (may be variable)
    Model**     modelVec;       // associated models array (per bit)
    SignalVec*  signalVec;      // instantiated signals array (per bit)
    Level*      initLevelVec;   // initial level vector

                Vector(const char* name, NetAttr attr, short disp,
                       short maxSize, Range* range, Level* initLevelVec = 0,
                       Vector* tri = 0, Range* triRange = 0) :
                    Net(name, ty_vector, attr, disp, tri, triRange)
                            { this->exType.size = maxSize;
                              this->initLevelVec = initLevelVec;
                              this->range = range;
                              this->modelVec = new Model*[range->size];
                              memset(this->modelVec, 0, range->size * sizeof(Model*)); }
    char*       repr(SignalVec* sigVecPos, int nBits);
};

// A memory array.  !!! 32-bit integer width only for now
// Embedded Scalar is for triggering readers when memory is changed
class Memory : public TrigNet
{
public:
    Range*      memRange;       // declared address range (may be variable)
    Range*      elemRange;      // declared element bit range (may be variable)

                Memory(const char* name, NetAttr attr, short disp, short maxSize,
                       Range* memRange, Range* elemRange) :
                    TrigNet(name, ty_memory, attr, disp)
                            { this->exType.size = maxSize;
                              this->memRange = memRange;
                              this->elemRange = elemRange; }
};

// A node in a linked-list of nets
class NetList : SimObject
{
public:
    Net*        net;
    NetList*    next;

                NetList(Net* net, NetList* next)
                                    { this->net = net; this->next = next; }
};

//-----------------------------------------------------------------------------
// Expression trees

// operations, in order from highest to lowest precedence
// WARNING: must match Expr::kPrecedence table!
enum ExOpCode
{
    op_lit = 0, op_stacked, op_load, op_lea, op_conv, op_func,
    op_not, op_pos, op_neg, op_com,                         // ! + - ~
    op_uand, op_unand, op_uor, op_unor, op_uxor, op_uxnor,  // & ~& | ~| ^ ~^
    op_mul, op_div, op_mod,                                 // * / %
    op_add, op_sub,                                         // + -
    op_sla, op_sra,                                         // << >>
    op_lt, op_le, op_gt, op_ge,                             // < <= > >=
    op_eq, op_ne, op_ceq, op_cne,                           // == != === !==
    op_band, op_bnand, op_bxor, op_bxnor,                   // & ~& ^ ~^
    op_bor, op_bnor,                                        // | ~|
    op_and,                                                 // &&
    op_or,                                                  // ||
    op_sel, op_else,                                        // ?:
    op_paren, op_none,                                      // () end
    num_ExOpCodes
};


const ExOpCode ops_uLogic   = op_uand;  // first 'u'-logic opcode
const ExOpCode ops_dual     = op_mul;   // first dual-operand opcode
const ExOpCode ops_dualCmp  = op_lt;    // first dual-operand comparison-result opcode
const ExOpCode ops_dualLogic = op_band; // first dual-operand logic-result opcode
const ExOpCode ops_lastDualLogic = op_cne; // last dual-logic opcode
const ExOpCode ops_triple   = op_sel;   // first triple-operand opcode
const int max_exprs = 500;

enum SysFnCode
{
    sf_userFn = 0, sf_time, sf_random, sf_concat, sf_flagError, sf_display,
    sf_fopen, sf_fdisplay, sf_annotate, sf_dist_uniform, sf_readmemmif, sf_formatTime
};

// An Expression node, used to build expression trees
struct Expr
{
    static Expr*    pool;       // pool of expression nodes
    static Expr*    poolEnd;
    static Expr*    next;       // next available expr node in pool
    static bool     gatherTriggers; // enables compileExpr to make triggers list
    static bool     conditionedTriggers; // TRUE if triggers are conditioned by any '@'s
    static NetList* curTriggers;    // list of trigger nets to current expr
    static bool     parmOnly;   // restricts compileExpr to a parameter expr
    static const char kPrecedence[];
    static const char* kOpSym[];
    static const char* kExTySym[];

    ExOpCode    opcode;         // opcode
    VExTyCode   tyCode;         // return-type code
    unsigned short nBits;       // number of bits
    Token*      srcLoc;         // source location for error messages
    NetList*    triggers;       // first trigger input in linked-list
    NetList*    triggersEnd;    // trigger after last in list
//  union                       // operands:
//  {
        Expr*   arg[3];         // operations on data
        struct
        {
            union
            {
                Variable*   var;        // load/lea variable
                size_t      value;      // literal-integer value
                float       fValue;     // floating-point value parameter
                Level       sValue;     // literal-scalar value
                Level*      vValue;     // literal-vector value
            };
            int         scale;          // scaled-integer scale
            Variable*   extScopeRef;    // var's opt external scope reference
//          union
//          {
                Range       range;      // vector's bit range
                Expr*       index;      // memory's index
//          };
        } data;
        struct
        {
            SysFnCode   code;           // system function code, or if sf_userFn:
            char*       name;           // name of user function
            Variable*   extScopeRef;    // opt external scope reference
            Expr*       arg;            // first argument expression
            Expr*       nextArgNode;    // pointer to next arg's op_func expr
            size_t*     catStoInstr;    // concat'ed vector store instr ptr
        } func;
//  };
};

//-----------------------------------------------------------------------------
// a port name in list of module's ports
class Port : SimObject
{
public:
    const char* name;
    Variable*   var;            // actual variable
    Port*       next;           // next port in current scope

                Port(const char* name)  { this->name = name;
                                      this->var = 0;
                                      this->next = 0; }
    friend class Scope;
};

//-----------------------------------------------------------------------------
// Name scope: scope is usually the current and all enclosing scopes
// May be a module, function, task, or named begin-end.

class Scope : public NamedObj
{
    NamedObj*   names;          // list of all named objects in this scope
    NamedObj*   namesE;         // names list last
    Scope*      scopes;         // list of just scope-type named objects
    Scope*      scopesE;        // scopes list last
    Scope*      scopesNext;     // next in enclosing scope's list of scopes
protected:
    Port*       portsE;         // I/O ports list last
    VLModule*   enclModule;     // enclosing module
public:
    Scope*      enclScope;      // enclosing scope, or 0 if global scope
    Port*       ports;          // I/O ports list
    const char* lastBlockName;  // last outer begin-end block name, if named
    size_t      localSize;      // amount of local integer variable space (bytes)

                Scope();
                Scope(const char* name, VType type, VLModule* enclModule);
    Scope*      findScope(const char* name, bool noErrors = FALSE);
                                        // find a scope name in this scope
                                        // or enclosing scopes
    void        addName(NamedObj* obj)  // append a named object to names
                                    { if (this->namesE) this->namesE->namesNext = obj;
                                      else this->names = obj;
                                      this->namesE = obj; }
    void        addScope(Scope* obj)    // append a scope to scopes
                                    { if (this->scopesE) this->scopesE->scopesNext = obj;
                                      else this->scopes = obj;
                                      this->scopesE = obj; }
    void        addPort(Port* port)     // append a port to ports
                                    { if (this->portsE) this->portsE->next = port;
                                      else this->ports = port;
                                      this->portsE = port; }
                // find a name in this scope
    NamedObj*   find(const char* name, NamedObj* nextOfKin = 0,
                     bool noErrors = FALSE);
    Variable*   findOrCreateScopeRef(Scope* externScope);
    Net*        findNet(Signal* sig);
    Vector*     findVector(SignalVec* sigVec);
    void        checkVars();
    int         fullNameLen();
    size_t      newLocal(int nElems, size_t elemSize)
                    { this->localSize = (this->localSize + elemSize - 1) &
                                        ~(elemSize - 1);
                      size_t disp = this->localSize;
                      this->localSize += nElems * elemSize;
                      if (this->localSize > 32767)
                      {
                        printf("foo\n");
                        throw new VError(verr_memOverflow, "too many variables and constants in module");
                      }
                      return disp; }

    void        initVariables(char* instModule, char* fullDesig);
    void        linkVariables(char* instModule, char* fullDesig);
    virtual void addRetJmp(size_t* jmpAdr) { }

    static Scope* global;       // top-level scope
    static Scope* local;        // current compiling scope

    friend NamedObj* findFullName(Variable** exScopeRef, bool noErrors);
    friend class Instance;
    friend class ParmVal;
};

NamedObj*   findFullName(Variable** exScopeRef, bool noErrors = FALSE);

// Verilog reserved keywords.
// Must be kept in sync with kVKeyStr table in VLModule.cc.
enum VKeyword
{
    k_notFound = 0,
    k_always = 1, k_and, k_assign, k_begin, k_buf,
    k_bufif0, k_bufif1, k_case, k_casex, k_casez,
    k_cmos, k_deassign, k_default, k_defparam, k_disable,
    k_edge, k_else, k_end, k_endcase, k_endfunction, k_endgenerate,
    k_endmodule, k_endprimitive, k_endspecify, k_endtable, k_endtask,
    k_event, k_for, k_force, k_forever, k_fork, k_function,
    k_generate, k_genvar, k_highz0, k_highz1, k_if, k_initial,
    k_inout, k_input, k_integer, k_join, k_large,
    k_macromodule, k_medium, k_module, k_nand, k_negedge,
    k_nmos, k_nor, k_not, k_notif0, k_notif1,
    k_or, k_output, k_parameter, k_pmos, k_posedge,
    k_primitive, k_pull, k_pull1, k_pulldown, k_pullup,
    k_rcmos, k_reg, k_release, k_repeat, k_rnmos,
    k_rpmos, k_rtran, k_rtranif0, k_rtranif1, k_scalared,
    k_small, k_specify, k_specparam, k_strong0, k_strong1,
    k_supply0, k_supply1, k_table, k_task, k_time,
    k_tran, k_tranif0, k_tranif1, k_tri, k_tri0,
    k_tri1, k_triand, k_trior, k_vectored, k_wait,
    k_wand, k_weak0, k_weak1, k_while, k_wire,
    k_wor, k_xnor, k_xor,
    n_keys = 128                // pad out to next power of 2: 128
};

// Event handler: assign and tasks
class EvHand : SimObject
{
    VKeyword    type;           // k_assign, k_initial, k_always, or k_task
    EvHandCodePtr code;         // pointer to handler subroutine
    Model*      model;          // last instantiation of this handler
public:
    static EvHand* gAssignsReset;   // time zero reset handler: forces all others to init
    Net*        net;            // output signal (if k_assign)
    short       vecBaseBit;     // output bit range (if k_assign to a Vector)
    short       vecSize;
    Variable*   parm;           // output parameter (if k_parameter)
    NetList*    triggers;       // list of trigger signals
    EvHand*     modNext;        // next handler in module's list

                EvHand(VKeyword type, EvHandCodePtr code);
    void        instantiate(Instance* parent, const char* parentName, int taskNum);
    void        instantiateAssignsReset(Instance* parent);
    void        setDependencies(Instance* instance);
    friend class Instance;
};

// A module: a definition of a single part-- a set of event handlers that share
//           a common state and set of signals.
class VLModule: public Scope
{
    EvHand*     evHands;        // event handlers list
    EvHand*     evHandsE;       // event handlers list end
    Instance*   instTmpls;      // instance templates list (not instantiations)
public:
    Variable*   parms;          // parameters list
    Variable*   parmsE;         // parameters list end
    
                VLModule(char* name);    // compile current source text as a Verilog module
    void        getPorts();
    void        addParm(Variable* parm) // append a parmameter to parms list
                            { if (this->parmsE) this->parmsE->next = parm;
                              else this->parms = parm;
                              this->parmsE = parm; }
    void        codeParmDeclaration(bool isDefParm, char termChar);
    void        startHandler(VKeyword type, const char* desig, NetList* curTriggers = 0);
    void        addEvHand(EvHand* eh)   // append an event handler to list
                            { if (this->evHandsE) this->evHandsE->modNext = eh;
                              else this->evHands = eh;
                              this->evHandsE = eh; }
    void        endHandler();
    void        clearExprTriggers();
    void        endHandler(Net* net) // append an event handler's net to evHands list
                            { endHandler();
                              net->evHand = this->evHandsE;
                              this->evHandsE->net = net; }
    void        endHandler(Net* net, Range* vecRange) // append handler's Vector
                            { endHandler();
                              EvHand* eh = this->evHandsE;
                              net->evHand = eh;
                              eh->net = net;
                              Vector* vec = (Vector*)net;
                              eh->vecBaseBit = vecRange->isFull ? 0 :
                                    vec->range->incr * (vecRange->left.bit -
                                            vec->range->left.bit);
                              eh->vecSize = vecRange->size; }
    Net*        newInternNet(char idLet, VExType exType, Net* output,
                              const char* gateName, Range** range);
    void        codeNetDeclaration(NetAttr attr);
    void        codeBufIf(bool outPol, bool enPol);
    void        codeContAssignStmt(bool oneAssign = FALSE);
    void        codeModuleInstance();
    void        codeModuleItem();
    virtual void addRetJmp(size_t* jmpAdr) { }

    friend class Instance;
};

//-----------------------------------------------------------------------------

// A parameter value in list of instance's parameters
class ParmVal : SimObject
{
    const char* name;           // name of module's parameter to apply to, or 0
    size_t      value;          // constant value
    int         scale;          // scaled-integer scale
    bool        isDefParm;      // TRUE if from defparam
    Variable*   defparm;        // defparam value displacement in instModule
public:
    ParmVal*    next;           // next in instance's list

                ParmVal(const char* name, size_t value, int scale)
                                    { this->name = name;
                                      this->value = value;
                                      this->scale = scale;
                                      this->isDefParm = FALSE;
                                      this->next = 0; }
                ParmVal(const char* name, Variable* defparm)
                                    { this->name = name;
                                      this->defparm = defparm;
                                      this->isDefParm = TRUE;
                                      this->next = 0; }
    Variable*   bind(Variable* parm, VLModule* module, Instance* inst);
    Variable*   unbind(Variable* parm, VLModule* module, Instance* inst);
};

// A connection of a module instance's port signal to its parent's net
class Conn : SimObject
{
    Variable*   var;            // parent module's variable or port
    Range*      range;          // (sub)range of bits in a var, if a Vector
    const char* portName;       // name of module's port to connect to, or 0
    Port*       port;           // sub module's port it is connected to
    Instance*   instance;       // sub module instance
public:
    Conn*       next;           // next in instance's list

                Conn(Variable* var, Range* range, const char* portName)
                                    { this->var = var;
                                      this->range = range;
                                      this->portName = portName;
                                      this->instance = 0;
                                      this->port = 0;
                                      this->next = 0; }
    Port*       bind(Port* port, VLModule* module, char* instModule);
    Port*       unbind(Port* port, VLModule* module, char* parentName);

    friend class EvHand;
    friend class Instance;
};

// An instantiation template of a module
class Instance : Scope
{
    Instance*   nextTmpl;       // next instance template in parent module's list
public:
    const char* moduleName;     // name of module to be instantiated
    char*       instModule;     // instantiated module object (local vars)
    ParmVal*    parmVals;       // list of given parameter values, in order;
    VLModule*   module;         // module when being instantiated
    Instance*   parent;         // parent instance
    Conn*       conns;          // list of parent nets connected to module's port

                Instance(const char* moduleName, const char* inName, ParmVal* parmVals,
                           Instance* parent, Instance* next);
                ~Instance()             { delete [] instModule; }
    void        instantiate(const char* parentName = 0, Instance* parent = 0);
    void        labelAndThrow(VError* error, char* desig);
    virtual void addRetJmp(size_t* jmpAdr) { }
};

//-----------------------------------------------------------------------------
// A list of forward-jump instructions to a single location.

const int max_jmps = 128;

class JmpList
{
    size_t*     jmpAddr[max_jmps];
    size_t**    next;
public:
                JmpList()                   { this->next = this->jmpAddr; }
    void        add(size_t* jmpAddr);
    void        setJmpsToHere();
};

//-----------------------------------------------------------------------------

class Function : public Scope
{
    void*       code;       // subroutine code
    VExType     exType;     // return type

public:
                Function(const char* name);  // compile text as a Verilog function
};

class NamedTask : public Scope
{
    JmpList     disableJmps;    // list of jumps to end of body

public:
    NetList*    triggers;       // list of trigger signals to be added to callers

    void*       code;           // subroutine code
                NamedTask(const char* name, VLModule* enclModule, size_t localsStart);
                                // compile text as a Verilog named task
                                            // pointer to "UCP struct"
    virtual void addRetJmp(size_t* jmpAdr) { this->disableJmps.add(jmpAdr); }
};

// -------- Global Variables --------

extern const char* kVKeyStr[128];

// -------- Global Function Prototypes --------

Expr*   newConstInt(size_t n);
void    addTrigger(Net* net);
void    setTriggers(Expr* ex);
Expr*   compileExpr();
void    initExprPool();
void    resetExprPool();
Expr*   newExprNode(ExOpCode opcode);
Expr*   newOp2(ExOpCode opcode, Expr* arg0, Expr* arg1);
Expr*   newOp1(ExOpCode opcode, Expr* arg0);
Expr*   newLoadInt(Variable* var, Variable* extScopeRef);
Expr*   newLoadTrigNet(TrigNet* trigNet, Variable* extScopeRef);
Expr*   newLoadVector(Vector* vector, Range* range, Variable* extScopeRef);
Expr*   newLoadMem(Memory* mem, Expr* index, Variable* extScopeRef);
Expr*   compileConstant(int width = -1);
size_t  getConstIntExpr(int* scale);
bool    isConstZ(Expr* ex);
void    codeNewLine();

