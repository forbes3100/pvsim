// ****************************************************************************
//
//              PVSim Verilog Simulator Verilog Expression Compiler
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

#include "Src.h"
#include "VL.h"
#include "VLSysLib.h"


// ------------ Global Global Variables -------------

Scope*  Scope::global;          // top-level scope
Scope*  Scope::local;           // current compiling scope

Expr*   Expr::pool;             // pool of expression nodes
Expr*   Expr::poolEnd;
Expr*   Expr::next;             // next available expr node in pool
bool    Expr::gatherTriggers;   // enables compileExpr to make triggers list
bool    Expr::conditionedTriggers; // TRUE if triggers are cond'ed by any '@'s
NetList* Expr::curTriggers;     // list of trigger nets to current expr
bool    Expr::parmOnly;         // restricts compileExpr to a parameter expr


// ------------ Local Global Variables -------------

// Table to convert characters to Levels
const Level X = (Level)(-1);
static Level kLevelChar[96] =
{
    X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,                    //  !"#$%&'()*+,-./
    LV_L,LV_H,X,X,X,X,X,X,X,X,X,X,X,X,X,LV_Z,           // 0123456789:;<=>?
    X,X,X,LV_C,X,X,LV_F,X,X,X,X,X,X,X,X,X,              // @ABCDEFGHIJKLMNO
    X,X,LV_R,LV_S,X,LV_U,X,LV_W,LV_X,X,LV_Z,X,X,X,X,X,  // PQRSTUVWXYZ[\]^_
    X,X,X,LV_C,X,X,LV_F,X,X,X,X,X,X,X,X,X,              // `abcdefghijklmno
    X,X,LV_R,LV_S,X,LV_U,X,LV_W,LV_X,X,LV_Z,X,X,X,X,X   // pqrstuvwxyz{|}~
};

// Precedence values for opcodes
// WARNING: must match enum ExOpCode table!

const char Expr::kPrecedence[num_ExOpCodes] =
{
    0, 0, 0, 0, 0, 0,           // lit, stacked, load, lea, conv, func
    55, 55, 55, 55,             // ! + - ~
    55, 55, 55, 55, 55, 55,     // & ~& | ~| ^ ~^
    50, 50, 50,                 // * / %
    45, 45,                     // + -
    40, 40,                     // << >>
    35, 35, 35, 35,             // < <= > >=
    30, 30, 30, 30,             // == != === !==
    25, 25, 25, 25,             // & ~& ^ ~^
    20, 20,                     // | ~|
    15,                         // &&
    10,                         // ||
    5, 5,                       // ?:
    2, 0                        // () end
};

// opcode display names
// WARNING: must match enum ExOpCode table!

const char* Expr::kOpSym[num_ExOpCodes] =
{
    "lit", "stacked", "load", "lea", "conv", "func",
    "!", "+", "-", "~",
    "&", "~&", "|", "~|", "^", "~^",
    "*", "/", "%",
    "+", "-",
    "<<", ">>",
    "<", "<=", ">", ">=",
    "==", "!=", "===", "!==",
    "&", "~&", "^", "~^",
    "|", "~|",
    "&&",
    "||",
    "?", ":",
    "()", "end"
};

// Type names -- must match enum VExTyCode table
const char* Expr::kExTySym[num_types] =
{
    "",     // ty_int
    "F",    // ty_float
    "CV",   // ty_vecConst
    "S",    // ty_scalar
    "V",    // ty_vector
    "M",    // ty_memory
    "R",    // ty_scopeRef
    "X"     // ty_none
};

//-----------------------------------------------------------------------------
// Construct a named object and add it to the current local scope.

NamedObj::NamedObj(const char* name, VType type)
{
    this->name = name;
    this->type = type;
    this->namesNext = 0;
    this->srcLoc = gScToken;
    this->srcLocObjName = name;
    if (Scope::local)
        Scope::local->addName(this);
}

//-----------------------------------------------------------------------------
// Construct a signal or event variable.

Net::Net(const char* name, VExTyCode type, NetAttr attr, short disp, Net* tri,
         Range* triRange) :
                                    Variable(name, type, disp)
{
    this->attr = attr;
    this->evHand = 0;
    this->model = 0;
    this->tri = tri;
    this->triRange = triRange;
    this->enable = 0;
    this->triSrcNo = 0;
    this->assigned = 0;
    this->isVisible = gSignalDisplayOn;
}

//-----------------------------------------------------------------------------
// Destruct a Variable: remove storage if a vector constant

Variable::~Variable()
{
    if (this->exType.code == ty_vector)
    {
        Level* vecConst = (Level*)value;
        delete [] vecConst;
    }
}

//-----------------------------------------------------------------------------
// Return the length of the full name of a scope ending in a '.'.

int Scope::fullNameLen()
{
    int len = (int)strlen(this->name) + 1;
    if (this->enclScope)
        len += this->enclScope->fullNameLen();
    return len;
}

//-----------------------------------------------------------------------------
// Construct a symbol scope and set current scope to it.

Scope::Scope(const char* name, VType type, VLModule* enclModule) :
                                                    NamedObj(name, type)
{
    this->enclScope = local;
    this->enclModule = enclModule;
    local = this;
    this->names = 0;
    this->namesE = 0;
    this->scopes = 0;
    this->scopesE = 0;
    this->scopesNext = 0;
    this->lastBlockName = 0;
    if (this->enclScope)
        this->enclScope->addScope(this);
}

//-----------------------------------------------------------------------------
// Find an instance, function, task, or begin-end name in this scope or
// its enclosing scopes.

Scope* Scope::findScope(const char* name, bool noErrors)
{
    for (Scope* parent = this; parent; parent = parent->enclScope)
        for (Scope* ct = parent->scopes; ct; ct = ct->scopesNext)
            if (strcmp(ct->name, name) == 0)
                return ct;

    if (!noErrors)
        throw new VError(verr_notFound, "'%s' not found", name);
    return 0;
}

//-----------------------------------------------------------------------------
// Find or create a New scope reference for given external scope for
// use with accesses in this scope.

Variable* Scope::findOrCreateScopeRef(Scope* externScope)
{
    TmpName refName("%s.", externScope->name);
    Variable* ctxtRef;
    ctxtRef = (Variable*)find(refName, externScope, TRUE);
    if (!ctxtRef)
    {           // not found: create an external-scope reference
        ctxtRef = new Variable(newString(refName), ty_scopeRef,
                                Scope::local->newLocal(1, sizeof(size_t)));
        ctxtRef->externScope = externScope;
    }
    return ctxtRef;
}

//-----------------------------------------------------------------------------
// Find a variable name in the local scope using a simple linear
// search.  Could make this a hashed search, but scopes are fairly small.
// Any VError points to location of nextOfKin, if given.

NamedObj* Scope::find(const char* name, NamedObj* nextOfKin, bool noErrors)
{
    for (NamedObj* sym = this->names; sym; sym = sym->namesNext)
        if (strcmp(sym->name, name) == 0)
            return sym;

    if (!noErrors)
    {
        if (nextOfKin)
            throw new VError(nextOfKin->srcLoc,
                             verr_notFound, "'%s' not found", name);
        else
            throw new VError(verr_notFound, "'%s' not found", name);
    }
    return 0;
}

//-----------------------------------------------------------------------------
// Locate a vector, given a pointer into its instantiated Signals vector.

Vector* Scope::findVector(SignalVec* sigVec)
{
    Vector* bestVec = 0;
    size_t bestDist = 999999;
    for (NamedObj* sym = this->names; sym; sym = sym->namesNext)
        if (sym->isType(ty_var))
        {
            Variable* var = (Variable*)sym;
            if (var->isExType(ty_vector))
            {
                Vector* vec = (Vector*)var;
                size_t dist = sigVec - vec->signalVec;
                // printf("fvec: vec=%s vec->sv=0x%08x dist=%d\n",
                //  vec->name, (int)vec->signalVec, dist);
                if (dist < bestDist)
                {
                    bestVec = vec;
                    bestDist = dist;
                }
            }
        }

    for (Scope* ct = this->scopes; ct; ct = ct->scopesNext)
    {
        Vector* vec = ct->findVector(sigVec);
        if (vec)
        {
            size_t dist = sigVec - vec->signalVec;
            // printf("fvecct: vec=%s vec->sv=0x%08x dist=%d\n",
            //  vec->name, (int)vec->signalVec, dist);
            if (dist < bestDist)
            {
                bestVec = vec;
                bestDist = dist;
            }
        }
    }

    return bestVec;
}

//-----------------------------------------------------------------------------
// Report any undefined or unconnected variables.

void Scope::checkVars()
{
    for (NamedObj* sym = this->names; sym; sym = sym->namesNext)
        if (sym->isType(ty_var))
        {
            Variable* var = (Variable*)sym;
            switch (var->exType.code)
            {
                case ty_scalar:
                {
                    Scalar* scalar = (Scalar*)var;
                    if (!scalar->signal)
                        throw new VError(var->srcLoc, verr_illegal,
                            "Scalar %s undefined or not connected",
                            scalar->name);
                }
                break;
                case ty_vector:
                {
                    Vector* vec = (Vector*)var;
                    if (!vec->signalVec)
                        throw new VError(var->srcLoc, verr_illegal,
                            "Vector %s undefined or not connected",
                            vec->name);
                }
                break;
                default:
                    ;
            }
        }

    for (Scope* ct = this->scopes; ct; ct = ct->scopesNext)
        ct->checkVars();
}

//-----------------------------------------------------------------------------
// Look up the current source token string in the symbol table.
//
// If the name is path-prefixed, it first searches for this external
// scope reference in the current scope, and creates it if needed.
// Uses a simple linear search. Could make this a hashed search, but scopes
// are fairly small.

NamedObj* findFullName(Variable** exScopeRef, bool noErrors)
{
    *exScopeRef = 0;
    // name has a scope prefix
    if (isNextToken('.'))
    {
        // first search local scope, then go up enclosing scopes
        Scope* symScope = Scope::local->findScope(gScToken->name);
        scan();
        scan();
        
        // found root scope: now locate exact scope
        while (isNextToken('.'))
        {
            if (symScope->isType(ty_instance))
            {
                Instance* inst = (Instance*)symScope;
                if (!inst->module)
                    throw new VError(verr_notFound,
                                     "module '%s' must be defined beforehand",
                                     inst->moduleName);
                symScope = inst->module;
            }
            symScope = (Scope*)symScope->find(gScToken->name);
            scan();
            scan();
        }
        // find or create an scope reference for given scope
        *exScopeRef = Scope::local->findOrCreateScopeRef(symScope);
        // locate object in given scope
        if (symScope->isType(ty_instance))
        {
            Instance* inst = (Instance*)symScope;
            if (!inst->module)
                throw new VError(verr_notFound,
                                 "module '%s' must be defined beforehand",
                                 inst->moduleName);
            symScope = inst->module;
        }
        return symScope->find(gScToken->name);
    }
    else    // no scope prefix: first see if name is in local scope
    {
        Scope* symScope = Scope::local;
        NamedObj* obj;
        for (;;)
        {
            if (!isToken(NAME_TOKEN))
                return 0;
            obj = symScope->find(gScToken->name, 0, TRUE);  // nonfatal search
            if (obj)
                break;

            // not found:
            // if not in any scope, it's really not found:
            //   let it fail if desired
            if (!symScope->enclScope)
            {
                if (noErrors)
                    return 0;
                symScope->find(gScToken->name);
            }
            // else try enclosing scope
            symScope = symScope->enclScope;
        }
        // if non-local and not in our module, find or create a scope
        // reference for given scope
        if (symScope != Scope::local && symScope != Scope::local->enclModule)
            *exScopeRef = Scope::local->findOrCreateScopeRef(symScope);
        return obj;
    }
}

//-----------------------------------------------------------------------------
// Return a representation of a Net as a new string.

char* Net::repr()
{
    return newString(this->name);
}

//-----------------------------------------------------------------------------
// Return a representation of a Vector as a new string.
// sigVecPos is the starting bit position within the Vector's SignalVector.

char* Vector::repr(SignalVec* sigVecPos, int nBits)
{
    long i = sigVecPos - this->signalVec;
    TmpName rep("%s[%d:%d]", this->name, i, i+nBits);
    return newString(rep);
}

//-----------------------------------------------------------------------------
// Create the expression pool.

void initExprPool()
{
    Expr::pool = new Expr[max_exprs];
    Expr::poolEnd = Expr::pool + max_exprs;
}

//-----------------------------------------------------------------------------
// remove all nodes from expression pool.

void resetExprPool()
{
    Expr::next = Expr::pool;
}

//-----------------------------------------------------------------------------
// Return a new expression node with given opcode.

Expr* newExprNode(ExOpCode opcode)
{
    if (Expr::next >= Expr::poolEnd)
        throw new VError(verr_notYet, "expression too complex (Expr pool)");
    Expr* ex = Expr::next;
    Expr::next++;
    ex->opcode = opcode;
    ex->srcLoc = gScToken;
    ex->triggers = Expr::curTriggers;
    ex->triggersEnd = Expr::curTriggers;
    ex->nBits = 1;              // default size (bits)
    return ex;
}

//-----------------------------------------------------------------------------
// Create a constant integer expression.

Expr* newConstInt(size_t n)
{
    Expr* ex = newExprNode(op_lit);
    ex->tyCode = ty_int;
    ex->data.value = n;
    ex->data.scale = 1;
    if (debugLevel(4))
        display("%d ", n);
    return ex;
}

//-----------------------------------------------------------------------------
// Create a constant float expression.

Expr* newConstFloat(float n)
{
    Expr* ex = newExprNode(op_lit);
    ex->tyCode = ty_float;
    ex->data.fValue = n;
    if (debugLevel(4))
        display("%g ", n);
    return ex;
}

//-----------------------------------------------------------------------------
// Compile a constant scalar or vector and return its expression.
// If width is -1, it is taken to be not specified.

Expr* compileConstant(int width)
{
    scan();             // skip over tick
    if (!isToken(NAME_TOKEN))
        throwExpected("constant");

    const char* ip = gScToken->name;
    char baseCh = *ip++;
    int base, bitsDig;
    int value;
    Expr* ex;
    const unsigned int max_bits = 64;
    Level vec[max_bits+4];
    Level* vp;

    switch (baseCh)
    {
        case 'b':           // binary constant
        case 'B':
            base = 2;
            bitsDig = 1;
            goto scanMultilevel;
        case 'd':           // decimal constant
        case 'D':
            base = 10;
            bitsDig = 0;
            value = atoi(ip);
            if (width == -1)
                // if no width specified, can't create a vector
                return newConstInt(value);
            goto scanMultilevel;
        case 'o':           // octal constant
        case 'O':
            base = 8;
            bitsDig = 3;
            goto scanMultilevel;
        case 'h':           // hex constant
        case 'H':
            base = 16;
            bitsDig = 4;
            goto scanMultilevel;
        scanMultilevel:
        {
            if (width > (int)max_bits)
                throw new VError(verr_illegal,
                                 "width exceeds maximum of %d bits", max_bits);
            unsigned int nBits;
            char c;
            if (bitsDig > 0)
            {
                vp = vec;
                for (nBits = 0; nBits <= max_bits; nBits += bitsDig)
                {           // if constant digit is a number, store digit's bits
                    do
                    {
                        c = *ip++;
                    } while (c == '_');
                    int digit = c - '0';
                    if (digit >= ('a' - '0'))
                        digit -= ('a' - 'A');
                    if (digit >= 'A' - '0' && digit <= 'F' - '0')
                        digit -= ('A' - ('9' + 1));
                    else if (digit > 9)
                        digit = -1;
                    if (digit >= 0 && digit < base)
                    {
                        digit <<= 1;
                        for (int i = bitsDig; i > 0; i--)
                        {
                            *vp++ = (digit & base ? LV_H : LV_L);
                            digit <<= 1;
                        }
                    }
                    else            // else convert to a Level
                    {
                        if (c < 32)
                            break;
                        Level lev = kLevelChar[c - 32];
                        //if (lev == LV_X)
                        //  break;
                        for (int i = bitsDig; i > 0; i--)
                            *vp++ = lev;  // and set all digit's bits to this Level
                    }
                }
                if (c)
                    throw new VError(verr_illegal, "illegal digit");
            }
            else // non-binary: convert already-scanned value to Vector
            {
                nBits = width;
                vp = vec + width;
                for (int i = width; i > 0; i--)
                {
                    *(--vp) = (value & 1) ? LV_H : LV_L;
                    value >>= 1;
                }
            }

            if (width != -1)
            {                       // if a width specified
                int delta = width - nBits;
                if (delta < 0)
                {
                    if (delta <= -bitsDig)
                        throw new VError(verr_illegal, "too many digits given");
                    // shrink vector for any excess bits due to digit width
                    Level* svp = vec - delta;
                    Level* dvp = vec;
                    for (int i = width; i > 0; i--)
                        *dvp++ = *svp++;
                }
                if (delta > 0)
                {                   // extend vector, expanding 'z's and 'x's
                    Level* svp = vec + nBits;
                    Level* dvp = vec + width;
                    int i;
                    for (i = nBits; i > 0; i--)
                        *(--dvp) = *(--svp);
                    Level pad = vec[0];
                    if (pad == LV_L || pad == LV_H)
                        pad = LV_L;
                    for (i = delta; i > 0; i--)
                        *(--dvp) = pad;
                }
                nBits = width;      // now have a proper-width vector
            }
            else if (nBits <= sizeof(size_t)*8) // see if vector can be an int
            {
                size_t value = 0;
                for (vp = vec; vp < vec + nBits; vp++)
                {
                    Level lev = *vp;
                    if (!(lev == LV_L || lev == LV_H))
                        goto nonInteger;
                    value = (value << 1) + (lev == LV_H);
                }
                // can be an integer: save it
                return newConstInt(value);
            }

            nonInteger:
            ex = newExprNode(op_lit);
            if (nBits == 1)     // if just one bit: code a scalar constant
            {
                ex->tyCode = ty_scalar;
                ex->data.sValue = vec[0];
                if (debugLevel(4))
                    display("LV_%c ", c);
            }
            else            // else put a vector constant in memory
            {
                Level* vecConst = new Level[nBits];
                vp = vec;
                Level* dvp = vecConst;
                for (int i = nBits; i > 0; i--)
                    *dvp++ = *vp++;
                size_t levelVecDisp = Scope::local->newLocal(nBits,
                                                             sizeof(Level));
                Variable* vecConstVar = new Variable("_", ty_vecConst,
                                        levelVecDisp, (size_t)vecConst, nBits);
                            // return a 'load vector' expression
                ex->tyCode = ty_vector;
                ex->nBits = nBits;
                ex->data.var = vecConstVar;
                ex->data.extScopeRef = 0;
                ex->data.range.isFull = TRUE;
                ex->data.range.isScalar = FALSE;
                ex->data.range.size = nBits;
                ex->data.range.incr = -1;
                ex->data.range.left.isConst = TRUE;
                ex->data.range.left.bit = nBits-1;
                ex->data.range.right.isConst = TRUE;
                ex->data.range.right.bit = 0;
                if (debugLevel(4))
                    display("V@ ");
            }
            break;
        }

        default:
            throw new VError(verr_illegal, "illegal base character");
    }
    return ex;
}

//-----------------------------------------------------------------------------
// Compile a left or right index for a range.

void RangeIndex::compile()
{
    expr = compileExpr();
    if (expr->opcode == op_lit && expr->tyCode == ty_int)
    {
        // a constant integer expression
        isConst = TRUE;
        bit = expr->data.value;
    }
    else if (expr->opcode == op_load && expr->tyCode == ty_int)
    {
        // a variable integer
        isConst = FALSE;
        isDisp = TRUE;
        disp = expr->data.var->disp;
    }
    else
    {
        // a general signal expression
        isConst = FALSE;
        isDisp = FALSE;
    }
}

//-----------------------------------------------------------------------------
// Construct an empty range subscript.

Range::Range()
{
    size = 0;
}

//-----------------------------------------------------------------------------
// Compile a range subscript.
//
// For vectors: a bit-range selector, which may be one or two expressions,
//              or be ommitted altogether to specify the full range.
// For memory: an address.

void Range::compile()
{
    if (isToken('['))
    {
        isFull = FALSE;
        size = 1;
        incr = 1;
        scan();
        bool saveGather = Expr::gatherTriggers;
        Expr::gatherTriggers = FALSE;
        Expr::parmOnly = FALSE;

        left.compile();

        if (isToken(':'))
        {
            scan();
            right.compile();

            int diff;
            if (right.isConst && left.isConst)
                diff = right.bit - left.bit;
            else
            {
                // if non-constant range, only allow the forms
                // [a:a+n], [a:a-n], [a+n:a], [a-n:a], where n is a constant
                Expr* l = left.expr;
                Expr* r = right.expr;
                if (l->opcode == op_add || l->opcode == op_sub)
                {
                    r = l;
                    l = right.expr;
                    incr = -incr;
                }

                Expr* ra0 = r->arg[0];
                Expr* ra1 = r->arg[1];
                if (!(l->opcode == op_load &&
                      (r->opcode == op_add || r->opcode == op_sub) &&
                      ra0->opcode == op_load &&
                      (ra1->opcode == op_lit || ra1->opcode == op_conv) &&
                      l->data.var == ra0->data.var &&
                      l->data.extScopeRef == ra0->data.extScopeRef))
                    throwExpected("constant-width bit range");

                if (r->opcode == op_sub)
                    incr = -incr;

                if (ra1->opcode == op_conv)
                    ra1 = ra1->arg[0];
                if (!(ra1->opcode == op_lit && ra1->tyCode == ty_int))
                    throwExpected("constant-width bit range");

                diff = (int)ra1->data.value;
            }
            if (diff < 0)
            {   diff = -diff;
                incr = -incr;
            }
            size = diff + 1;
            if (size > 4095)
                throw new VError(verr_illegal, "vector size too large");

            isScalar = (size == 1);
        }
        else
            isScalar = TRUE;
        expectSkip(']');
        Expr::gatherTriggers = saveGather;
    }
    else
    {
        isFull = TRUE;
        isScalar = FALSE;
        size = 0;
        incr = 0;
        left.isConst = TRUE;
        left.bit = 0;
        right.isConst = TRUE;
        right.bit = 0;
    }
}

//-----------------------------------------------------------------------------
// Create a new load-integer-variable expression.

Expr* newLoadInt(Variable* var, Variable* extScopeRef)
{
    Expr* ex = newExprNode(op_load);
    ex->data.extScopeRef = extScopeRef;
    ex->data.var = var;
    ex->data.scale = var->scale;
    ex->tyCode = ty_int;
    if (debugLevel(4))
        display("%s @ (x%d)", var->name, var->scale);
    return ex;
}

//-----------------------------------------------------------------------------
// Create a new load-trigger-variable as a scalar-type expression.

Expr* newLoadTrigNet(TrigNet* trigNet, Variable* extScopeRef)
{
    Expr* ex = newExprNode(op_load);
    ex->data.extScopeRef = extScopeRef;
    ex->data.var = trigNet;
    if (Expr::gatherTriggers)
        addTrigger(trigNet);
    ex->triggers = Expr::curTriggers;
    ex->tyCode = ty_scalar;
    if (debugLevel(4))
        display("%s S@ ", trigNet->name);
    return ex;
}

//-----------------------------------------------------------------------------
// Create a new load-vector-variable expression.

Expr* newLoadVector(Vector* vector, Range* range, Variable* extScopeRef)
{
    Expr* ex = newExprNode(op_load);
    ex->tyCode = ty_vector;
    ex->data.var = vector;
    ex->data.extScopeRef = extScopeRef;
    if (Expr::gatherTriggers)
        addTrigger((Net*)vector);
    if (debugLevel(4))
        display("%s V@ ", vector->name);

    if (range->isFull)
        ex->data.range = *vector->range;
    else
        ex->data.range = *range;

    ex->nBits = ex->data.range.size;
    if (ex->nBits == 1)
    {               // if vector bit selected: convert it to a scalar type
        Expr* convEx = newExprNode(op_conv);
        convEx->tyCode = ty_scalar;
        convEx->arg[0] = ex;
        ex = convEx;
    }
    ex->triggers = Expr::curTriggers;
    return ex;
}

//-----------------------------------------------------------------------------
// Create a new load-memory-variable expression.

Expr* newLoadMem(Memory* mem, Expr* index, Variable* extScopeRef)
{
    Expr* ex = newExprNode(op_load);
    ex->tyCode = ty_memory;
    ex->data.var = mem;
    ex->data.extScopeRef = extScopeRef;
    ex->data.index = index;
    if (mem->elemRange)
        ex->nBits = mem->elemRange->size;
    else
        ex->nBits = sizeof(size_t);
    if (Expr::gatherTriggers)
        addTrigger(mem);
    if (debugLevel(4))
        display("%s M@ ", mem->name);
    ex->triggers = Expr::curTriggers;
    return ex;
}

//-----------------------------------------------------------------------------
// Create a new expression binary operation or fold into a load-literal if
// a constant expression.

Expr* newOp2(ExOpCode opcode, Expr* arg0, Expr* arg1)
{
    if (arg0->opcode == op_lit && arg1->opcode == op_lit &&
        arg0->tyCode == ty_int && arg1->tyCode == ty_int)
    {
        size_t value = arg0->data.value;
        int scale = arg0->data.scale;
        size_t secondValue = arg1->data.value;
        int secondScale = arg1->data.scale;
        switch (opcode)
        {
            case op_mul:
                value *= secondValue;
                scale *= secondScale;
                break;
            case op_div:
                value /= secondValue;
                scale /= secondScale;
                break;
            case op_mod:
                value %= secondValue;
                scale /= secondScale;
                break;
            default:
                if (scale > secondScale)
                {
                    secondValue *= scale/secondScale;
                    secondScale = scale;
                }
                else if (secondScale > scale)
                {
                    value *= secondScale/scale;
                    scale = secondScale;
                }
                switch (opcode)
                {
                    case op_add:
                        value += secondValue;
                        break;
                    case op_sub:
                        value -= secondValue;
                        break;

                    case op_band:
                        value &= secondValue;
                        break;
                    case op_bor:
                        value |= secondValue;
                        break;
                    case op_bxor:
                        value ^= secondValue;
                        break;
                    case op_sla:
                        value <<= secondValue;
                        break;
                    case op_sra:
                        value >>= secondValue;
                        break;

                    case op_eq:
                        value = value == secondValue;
                        break;
                    case op_ne:
                        value = value != secondValue;
                        break;
                    case op_gt:
                        value = value > secondValue;
                        break;
                    case op_le:
                        value = value <= secondValue;
                        break;
                    case op_lt:
                        value = value < secondValue;
                        break;
                    case op_ge:
                        value = value >= secondValue;
                        break;

                    default:
                        throw new VError(verr_illegal, 
                                         "no constant '%s' op yet",
                                         Expr::kOpSym[opcode]);
                }
        }
        arg0->data.value = value;
        arg0->data.scale = scale;
        return arg0;
    }
    // else: make arguments to same type by converting up lesser type
    if (arg0->tyCode != arg1->tyCode && opcode != op_and && opcode != op_or)
    {
        Expr* ex = newExprNode(op_conv);
        switch (arg0->tyCode)
        {
            case ty_int:
            upgradeArg0:
                if (debugLevel(4))
                    display("SWAP %s2%s SWAP ",
                            Expr::kExTySym[arg0->tyCode],
                            Expr::kExTySym[arg1->tyCode]);
                ex->tyCode = arg1->tyCode;
                ex->nBits = arg1->nBits;
                ex->data.range.size = arg1->data.range.size;
                ex->arg[0] = arg0;
                arg0 = ex;
                break;

            case ty_float:
                if (arg1->tyCode == ty_int)
                    goto upgradeArg1;
                else
                    goto upgradeArg0;

            case ty_scalar:
                if (arg1->tyCode == ty_int || arg1->tyCode == ty_float)
                    goto upgradeArg1;
                else
                    goto upgradeArg0;

            case ty_vector:
            case ty_memory:
            upgradeArg1:
                if (debugLevel(4))
                    display("%s2%s ",
                            Expr::kExTySym[arg1->tyCode],
                            Expr::kExTySym[arg0->tyCode]);
                ex->tyCode = arg0->tyCode;
                ex->nBits = arg0->nBits;
                ex->data.range.size = arg0->data.range.size;
                ex->arg[0] = arg1;
                arg1 = ex;
                break;

            default:
                throw new VError(verr_bug, "BUG: newOp2");
        }
    }
    // create a new expression node for operation
    Expr* ex = newExprNode(opcode);
    if (opcode >= ops_dualCmp && opcode < ops_dualLogic)
        ex->tyCode = ty_int;        // op_eq, etc. condense to integer
    else if (opcode >= ops_dualLogic && opcode <= ops_lastDualLogic)
        ex->tyCode = ty_scalar;     // op_uand, etc. condense to scalar
    else
    {
        ex->tyCode = arg0->tyCode;
        ex->nBits = arg0->nBits;
    }
    ex->arg[0] = arg0;
    ex->arg[1] = arg1;
    ex->triggers = Expr::curTriggers;
    if (debugLevel(4))
        display("%s%s ", Expr::kExTySym[arg0->tyCode], Expr::kOpSym[opcode]);
    return ex;
}

//-----------------------------------------------------------------------------
// Create a new expression unary operation or fold into a load-literal if
// a constant expression.

Expr* newOp1(ExOpCode opcode, Expr* arg0)
{
    if (arg0->opcode == op_lit && arg0->tyCode == ty_int)
    {
        size_t value = arg0->data.value;
        switch (opcode)
        {
            case op_not:
                value = !value;
                break;

            case op_com:
                value = ~value;
                break;

            case op_neg:
                value = -value;
                break;

            case op_pos:
                break;

            default:
                throw new VError(verr_illegal, "no constant '%s' op yet",
                                 Expr::kOpSym[opcode]);
        }
        arg0->data.value = value;
        return arg0;
    }
    // else: create a new expression node for the operation
    Expr* ex = newExprNode(opcode);
    if (opcode >= ops_uLogic && opcode < ops_dual)
        ex->tyCode = ty_scalar;     // op_uand, etc. condense vector to scalar
    else
    {
        ex->tyCode = arg0->tyCode;
        ex->nBits = arg0->nBits;
    }
    ex->arg[0] = arg0;
    ex->triggers = Expr::curTriggers;
    if (debugLevel(4))
        display("%s%s ", Expr::kExTySym[arg0->tyCode], Expr::kOpSym[opcode]);
    return ex;
}

//-----------------------------------------------------------------------------
// Add a net to the list of triggers for an assign statement or task.

void addTrigger(Net* net)
{
    if (!net->isType(ty_var) ||
        !(net->isExType(ty_scalar) || net->isExType(ty_vector) ||
        net->isExType(ty_memory)))
        throw new VError(verr_bug, "BUG: addTrigger: adding '%s'", net->name);
    for (NetList* n = Expr::curTriggers; n; n = n->next)
        if (n->net == net)
            return;
    Expr::curTriggers = new NetList(net, Expr::curTriggers);
}

//-----------------------------------------------------------------------------
// Set Expr::curTriggers list to encompass only inputs to given expression.

void setTriggers(Expr* ex)
{
    NetList* t = ex->triggers;
    NetList* tEnd = ex->triggersEnd;
    Expr::curTriggers = t;
    // set last trigger in list before tEnd to be the end of list
    while (t && t->next != tEnd)
        t = t->next;
    if (t)
        t->next = 0;
}

//-----------------------------------------------------------------------------
// Compile an expression into an expression tree built of expression nodes
// in Expr::pool.
//
// The basic rule is to compile data pushes and stack operators, and only pop
// operators off the opStack to code them when forced to: when we want to
// push a lower or equal precedence operator on the stack, or we're at the
// end of the expression or a parenthetical sub-expression.
//
// must also keep track of identifiers relative to operators:
// A + B    ok
// A + ! B  ok
// A B      expression ends with 'A'
// A + + B  no!
//
// Returns a valid expression or throws an error.

Expr* compileExpr()
{
    const int max_ops = 20;
    Expr* exStack[max_ops];
    ExOpCode opStack[max_ops];
    Expr** exSP = exStack + max_ops;
    ExOpCode* opSP = opStack + max_ops;
    *--opSP = op_none;              // push beginning-of-expr op
    bool haveValue = FALSE;
    ExOpCode opcode;
    const char* parmMsg = "parameter or constant";

    if (!gScToken)
        throwExpected("expression");
    TokCode tok = gScToken ? gScToken->tokCode : EOF_TOKEN;
    for (;;)
    {
        bool haveScanned = FALSE;
        char nxTok = (gScToken && gScToken->next) ?
                        gScToken->next->tokCode : EOF_TOKEN;
        switch (tok)
        {
            // Data pushes
            case NUMBER_TOKEN:
            {
                if (haveValue)
                    goto endOfExpr;
                int n = gScToken->number;
                if (nxTok == '\'')
                {
                    scan();                     // a constant like 3'b101
                    *--exSP = compileConstant(n);
                }
                else
                    *--exSP = newConstInt(n); // a number push
                haveValue = TRUE;
                break;
            }
            case FLOAT_TOKEN:
            {
                if (haveValue)
                    goto endOfExpr;
                *--exSP = newConstFloat(scFloat()); // a float push
                haveValue = TRUE;
                break;
            }
            case NAME_TOKEN:            // push data on parmameter stack
            {
                if (haveValue)
                    goto endOfExpr;
                Variable* extScopeRef;

                // nonfatal search for name
                NamedObj* sym = findFullName(&extScopeRef, TRUE);
                if (!sym)
                {
                    // not found: auto-create a wire (yuck!)
                    sym = new Scalar(gScToken->name, 0,
                            Scope::local->newLocal(1, sizeof(size_t)));
                }
                else if (!sym->isType(ty_var))
                    throwExpected("variable, function, or task name");
                if (Expr::parmOnly && !((Variable*)sym)->isParm)
                    throwExpected(parmMsg);
                if (isNextToken('('))   // function or task call
                    throw new VError(verr_illegal, "no functions yet");
                else
                {
                    switch (((Variable*)sym)->exType.code)
                    {
                        case ty_int:        // an integer fetch
                        {
                            scan();
                            Range range;
                            range.compile();
                            Expr* ex = newLoadInt((Variable*)sym, extScopeRef);
                            if (!range.isFull)
                            {
                                // optional bit or bit-slice of an integer
                                if (range.isScalar)
                                {
                                    Expr* ex2 = newExprNode(op_sra);
                                    ex2->tyCode = ty_int;
                                    ex2->arg[0] = ex;
                                    ex2->arg[1] = newConstInt(range.left.bit);
                                    ex = newExprNode(op_band);
                                    ex->tyCode = ty_int;
                                    ex->arg[0] = ex2;
                                    ex->arg[1] = newConstInt(1);
                                }
                                else
                                {
                                    Expr* ex2 = newExprNode(op_sra);
                                    ex2->tyCode = ty_int;
                                    ex2->arg[0] = ex;
                                    ex2->arg[1] = newConstInt(range.right.bit);
                                    Expr* ex3 = newExprNode(op_sla);
                                    ex3->tyCode = ty_int;
                                    ex3->arg[0] = newConstInt(1);
                                    ex3->arg[1] = newConstInt(range.left.bit -
                                                          range.right.bit + 1);
                                    Expr* ex4 = newExprNode(op_com);
                                    ex4->tyCode = ty_int;
                                    ex4->arg[0] = ex3;
                                    ex = newExprNode(op_band);
                                    ex->tyCode = ty_int;
                                    ex->arg[0] = ex2;
                                    ex->arg[1] = ex4;
                                }
                            }
                            *--exSP = ex;
                            haveScanned = TRUE;
                            break;
                        }
                        case ty_scalar:     // a signal fetch
                            *--exSP = newLoadTrigNet((Scalar*)sym, extScopeRef);
                            break;

                        case ty_vector:     // a vector signal fetch
                        {
                            scan();
                            Range range;
                            range.compile();
                            *--exSP = newLoadVector((Vector*)sym, &range,
                                                    extScopeRef);
                            haveScanned = TRUE;
                            break;
                        }
                        case ty_memory:     // a memory element fetch
                        {
                            scan();
                            expectSkip('[');
                            Expr* index = compileExpr();
                            expectSkip(']');
                            *--exSP = newLoadMem((Memory*)sym, index,
                                                 extScopeRef);
                            haveScanned = TRUE;
                            break;
                        }
                        default:
                            throw new VError(verr_illegal,
                                             "expr var type: not yet");
                    }
                }
                haveValue = TRUE;
                break;
            }
            case '$':                   // system function call
                if (haveValue)
                    goto endOfExpr;
                if (Expr::parmOnly)
                    throwExpected(parmMsg);
                *--exSP = compileSystemFn();
                haveScanned = TRUE;
                haveValue = TRUE;
                break;

            case '\'':                  // single quote preceeds binary, hex,
                                        // signal level constants
                if (haveValue)
                    goto endOfExpr;
                *--exSP = compileConstant();
                haveValue = TRUE;
                break;

            case STRING_TOKEN:          // double quote quotes a string
            {
                if (haveValue)
                    goto endOfExpr;
                *--exSP = newConstInt((size_t)newString(gScToken->name));
                haveValue = TRUE;
                break;
            }
            case '{':                   // bus concatenation:  { e1 , e2 , e3 }
            {
                if (haveValue)
                    goto endOfExpr;
                scan();
                Expr* ex = newExprNode(op_func);
                *--exSP = ex;
                ex->tyCode = ty_vector;
                ex->func.code = sf_concat;
                Expr* argEx = compileExpr();    // first arg goes in main node
                ex->func.nextArgNode = 0;
                int nBits = 1;
                    nBits = argEx->nBits;
                ex->func.arg = argEx;
                while (isToken(','))
                {
                    scan();
                    // add nodes for other args and link them in a list
                    Expr* nextEx = newExprNode(op_func);
                    ex->func.nextArgNode = nextEx;
                    ex = nextEx;
                    argEx = compileExpr();
                    ex->func.arg = argEx;
                        nBits += argEx->nBits;
                }
                expect('}');
                ex->func.nextArgNode = 0;
                // patch in total vector size, and span all sub-expr triggers
                (*exSP)->nBits = nBits;
                (*exSP)->triggers = Expr::curTriggers;
                haveValue = TRUE;
                break;
            }
            // Operations
            case '<':
                if (nxTok == '=')
                {
                    scan();
                    opcode = op_le;
                }
                else if (nxTok == '<')
                {
                    scan();
                    opcode = op_sla;
                }
                else
                    opcode = op_lt;
                goto testOp;

            case '>':
                if (nxTok == '=')
                {
                    scan();
                    opcode = op_ge;
                }
                else if (nxTok == '>')
                {
                    scan();
                    opcode = op_sra;
                }
                else
                    opcode = op_gt;
                goto testOp;

            case '=':
                if (nxTok == '=')
                {
                    scan();
                    if (isNextToken('='))
                    {
                        scan();
                        opcode = op_ceq;
                    }
                    else
                        opcode = op_eq;
                    goto testOp;
                }
                else
                    goto endOfExpr;

            case '!':
                if (nxTok == '=')
                {
                    scan();
                    if (isNextToken('='))
                    {
                        scan();
                        opcode = op_cne;
                    }
                    else
                        opcode = op_ne;
                }
                else if (haveValue)
                    goto endOfExpr;
                else
                    opcode = op_not;
                goto testOp;

            case '|':
                if (nxTok == '|')
                {
                    if (!haveValue)
                        goto endOfExpr;
                    scan();
                    opcode = op_or;
                }
                else if (haveValue)
                    opcode = op_bor;
                else
                    opcode = op_uor;
                goto testOp;

            case '&':
                if (nxTok == '&')
                {
                    if (!haveValue)
                        goto endOfExpr;
                    scan();
                    opcode = op_and;
                }
                else if (haveValue)
                    opcode = op_band;
                else
                    opcode = op_uand;
                goto testOp;

            case '^':
                if (haveValue)
                    opcode = op_bxor;
                else
                    opcode = op_uxor;
                goto testOp;

            case '+':
                if (haveValue)
                {
                    opcode = op_add;
                    goto testOp;
                }
                else
                {
                    scan();
                    break;
                }

            case '-':
                if (haveValue)
                    opcode = op_sub;
                else
                    opcode = op_neg;
                goto testOp;

            case '*':
                opcode = op_mul;
                goto testOp;

            case '/':
                opcode = op_div;
                goto testOp;

            case '%':
                opcode = op_mod;
                goto testOp;

            case '~':
                if (haveValue)
                    switch (nxTok)
                    {
                        case '&':
                            scan();
                            opcode = op_bnand;
                            break;
                        case '|':
                            scan();
                            opcode = op_bnor;
                            break;
                        case '^':
                            scan();
                            opcode = op_bxnor;
                            break;
                        default:
                            goto endOfExpr;
                    }
                else
                    switch (nxTok)
                    {
                        case '&':
                            scan();
                            opcode = op_unand;
                            break;
                        case '|':
                            scan();
                            opcode = op_unor;
                            break;
                        case '^':
                            scan();
                            opcode = op_uxnor;
                            break;
                        default:
                            opcode = op_com;
                    }
                goto testOp;

            case '?':             // conditional: '<expr> ? <Texpr> : <Fexpr>'
                opcode = op_sel;
                goto testOp;

            case ':':
                opcode = op_else;
                goto testOp;

            case '(':
                if (haveValue)
                    goto endOfExpr;
                opcode = op_paren;
                *--opSP = opcode;
                break;

            default:                    // finish coding this expression
            endOfExpr:
                if (!haveValue)
                    throw new VError(verr_illegal, "bad expression");
                opcode = op_none;       // code all remaining ops on stack
                goto testOp;

            testOp:
                int thisPrecedence = Expr::kPrecedence[opcode];
                ExOpCode lastOp = *opSP;
                while (Expr::kPrecedence[lastOp] >= thisPrecedence)
                {           // compile any stacked ops of higher precedence
                    Expr* arg = *exSP;
                    if (lastOp == op_none)
                        goto exprDone;
                    else if (lastOp == op_paren)
                    {
                        opSP++;         // if we have reached stacked paired '('
                        goto continueExpr; // remove it and continue expression
                    }
                    else if (lastOp == op_sel)
                    {
                        if (opcode == op_else)
                            break;
                        // just stack conditional arg
                    }
                    else if (lastOp == op_else)
                    {
                        if (opcode == op_sel)
                            // nested conditional: stack it
                            break;
                        // conditional: '<expr> ? <Texpr> : <Fexpr>'
                        opSP++;
                        Expr* falseEx = arg;
                        exSP++;
                        Expr* trueEx = *exSP++;
                        Expr* enableEx = *exSP;
                        Expr* ex = newExprNode(op_sel);
                        ex->tyCode = trueEx->tyCode;
                        ex->nBits = (trueEx->nBits > falseEx->nBits) ?
                                        trueEx->nBits : falseEx->nBits;
                        ex->arg[0] = enableEx;
                        ex->arg[1] = trueEx;
                        ex->arg[2] = falseEx;
                        // set the op_sel's trigger list to include all
                        //   3 expr's triggers
                        ex->triggersEnd = enableEx->triggersEnd;
                        *exSP = ex;
                        if (debugLevel(4))
                            display("?:%d ", ex->nBits);
                    }
                    else if (lastOp >= ops_dual)
                    {
                        exSP++;         // or compile a dual operand
                        *exSP = newOp2(lastOp, *exSP, arg);
                    }
                    else                // or compile a single operand
                        *exSP = newOp1(lastOp, arg);
                    lastOp = *++opSP;
                }

                // now we've finished any higher-precedence sub-expressions,
                // and can check if a ':' (op_else) is not part of a conditional
                if (opcode == op_else && lastOp != op_sel)
                    goto endOfExpr;

                if (opcode >= ops_dual && !haveValue)
                    goto endOfExpr; // binary: must have already parsed 1st val

                haveValue = FALSE;
                *--opSP = opcode;
                break;
        }

    continueExpr:
        if (haveScanned)
            tok = gScToken ? gScToken->tokCode: EOF_TOKEN;
        else
            tok = scan();
        if (exSP <= exStack || opSP <= opStack)
            throw new VError(verr_notYet, "expression too complex (exSP)");
    }

exprDone:
    return *exSP;
}

//-----------------------------------------------------------------------------
// Get a constant integer expression, returing the constant's value, and
// scale (if a scaled integer).

size_t getConstIntExpr(int* scale)
{
    Expr* ex = compileExpr();
    *scale = 1;
    if (ex->opcode == op_lit)
        switch (ex->tyCode)
        {
            case ty_int:
                return ex->data.value;
            case ty_float:
                // a float value is assumed to be a time in ns, so convert it
                // to integer ticks
                *scale = gTicksNS;
                return (int)(ex->data.fValue * gTicksNS);
            case ty_scalar:
            {
                Level lev = ex->data.sValue;
                if (lev != LV_L && lev != LV_H)
                    throwExpected("constant 0 or 1");
                return lev != LV_L;
            }
            case ty_vector:
            {
                Level* lvp = ex->data.var->levelVec;
                size_t value = 0;
                for (int i = 0; i < ex->nBits; i++)
                {
                    value <<= 1;
                    Level lev = *lvp++;
                    if (lev == LV_H)
                        value++;
                    else if (lev != LV_L)
                        throwExpected("binary constant");
                }
                return value;
            }
            default:
                break;
        }
    else if (ex->opcode == op_load && ex->data.var->isParm)
        return ex->data.var->value;

    throwExpected("integer or time constant, or parameter");
    return 0;
}

//-----------------------------------------------------------------------------
// Return TRUE if an expression is a constant tri-state level.

bool isConstZ(Expr* ex)
{
    if (ex->opcode != op_lit)
        return FALSE;
    switch (ex->tyCode)
    {
        case ty_scalar:
            return (ex->data.sValue == LV_Z);
            break;

        case ty_vector:
        {
            Vector* v = (Vector*)ex->data.var;
            if (!v->isExType(ty_vecConst))
                return FALSE;
            Level* lp = v->levelVec;
            for (int i = ex->nBits; i > 0; i--)
                if (*lp++ != LV_Z)
                    return FALSE;
            return TRUE;
            break;
        }
        default:
            return FALSE;
    }
}

