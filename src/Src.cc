// ****************************************************************************
//
//          PVSim Verilog Simulator Compiler Source Parser
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
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "Src.h"
#include "Utils.h"
#include "PSignal.h"
#include "SimPalSrc.h"

// ------------ Global Global Variables -------------


Token*  gScToken;           // token just scanned
Symbol** gHashTable;        // hash table
Symbol* gLastSymbol;        // last symbol parsed
Symbol* gLastNewSymbol;     // last symbol declared

size_t  gMaxStringSpace;    // storage limits, from initApplication
char*   gStrings;           // general string storage space
char*   gNextString;        // next available space in string storage
char*   gStringsLimit;
Space   gStringsSpace =     // gStrings space
{
    "gStrings",
    0,
    &gStrings,
    sizeof(char),
    &gNextString,
    &gStringsLimit
};

Macro* gMacros;         // list of defined text macros

Src*    VL::baseSrc;    // base Verilog file source
int VL::debugLevel;     // debugging display detail level

//-----------------------------------------------------------------------------
// Create a Token from last tokenized source.

Token::Token(Src* src, Token* prev)
{
    this->src = src;
    this->pos = src->tokPos;
    this->tokCode = src->tokCode;
    this->line = src->line;
    TokCode code = src->tokCode;
    if (code == NAME_TOKEN)
        this->name = newString(src->tokName);
    else if (code == STRING_TOKEN)
        this->name = src->tokName;
    else if (code == NUMBER_TOKEN)
        this->number = src->tokNumber;
    else
        this->fNumber = src->tokFNumber;
    this->prev = prev;
    this->next = 0;
}

//-----------------------------------------------------------------------------
// Delete a Token-- only to be done at GC time.

Token::~Token()
{
#if 0   // !!! needed when newString() converted to use new
    if (this->tokCode == NAME_TOKEN)
        delete this->name;
#endif
    Token* prev = this->prev;
    if (prev)
        prev->next = this->next;
    if (this->next)
        this->next->prev = prev;
}

//-----------------------------------------------------------------------------
// Construct a source by reading a source file into the source array.

Src::Src(const char* fileName, SrcMode mode, Src* parent)
{
    this->parent = parent;
    this->fileName = fileName;
    this->mode = mode;
    if (!gQuietMode)
        display("    loading '%s'...\n", fileName);
    FILE* ifp = openFile(fileName, "r");
    if (ifp == 0)
        throw new VError(verr_io, "can't read source file '%s'", fileName);

    if (!(fseek(ifp, (long)0, 2) == 0 && (this->size = ftell(ifp)) != EOF
      && fseek(ifp, (long)0, 0) == 0))
        throw new VError(verr_io, "can't position source file '%s'", fileName);

    this->base = new char[this->size + 2];
    char* p = this->base;
    size_t freadSize = 0;
    for (size_t i = this->size; i > 0; i -= freadSize)
    {
        if (i > 32000)
            freadSize = 32000;
        else
            freadSize = i;
        fread(p, 1, freadSize, ifp);
        p += freadSize;
    }
    closeFile(ifp);

    if (*(p-1) == '\377')
        p--;
    *p = 0;
    // convert UNIX line endings to Mac in buffer, if needed
    for (p = this->base; *p; p++)
        if (*p == '\r')
            *p = '\n';
}

//-----------------------------------------------------------------------------
// Construct a source from an existing text buffer.

Src::Src(const char* text, size_t size, SrcMode mode, Src* parent)
{
    this->parent = parent;
    this->mode = mode;
    this->fileName = 0;
    this->base = (char*)text;
    this->size = size;
}

//-----------------------------------------------------------------------------
// Display a warning, like printf.

void warnErr(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    char msg[4*max_nameLen];
    vsnprintf(msg, 4*max_nameLen-1, format, ap);
    va_end(ap);

    display("// *** WARNING: %s\n", msg);
    if (gWarningCount++ > 100)
        throw new VError(verr_stop, "Too many warnings");
}

//-----------------------------------------------------------------------------
// Display, like printf, a fatal error message, but without stopping.

void displayErrNoStop(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char msg[max_messageLen];
    vsnprintf(msg, max_messageLen-1, fmt, ap);
    va_end(ap);

    display("// *** ERROR: (%s) %s\n", msg);
    gFatalLoadErrors = TRUE;
}

//-----------------------------------------------------------------------------
// Convert a hex string to an integer.

int hextol(const char* s)
{
    int n = 0;
    const char* p = s;
    char ch = *p++;
    ch = toupper(ch);
    while (isdigit(ch) || (ch >= 'A' && ch <= 'F'))
    {
        ch -= '0';
        if (ch > 9)
            ch -= 7;
        n = (n << 4) + ch;
        ch = *p++;
        ch = toupper(ch);
    }
    return (n);
}

//-----------------------------------------------------------------------------
// Check a tzScanned token's string in name and set tokCode to NAME_TOKEN,
// or to NUMBER_TOKEN or FLOAT_TOKEN if its a number (and fill in number).

void Src::tzCheckForNumber()
{
    const char* p = this->tokName;
    if (*p == '-')
        p++;

    bool hasDecimal = FALSE;
    for ( ; *p && (isdigit(*p) || *p == '.'); p++)
        if (*p == '.')
        {
            if (hasDecimal)
                break;          // if 2 decimal points, not a number.
            hasDecimal = TRUE;
        }

    if (*p || ((this->tokName[1] == 0) &&
                (this->tokName[0] == '-' || this->tokName[0] == '.')))
        this->tokCode = NAME_TOKEN; // return NAME_TOKEN and name
    else
    {
        this->tokCode = NUMBER_TOKEN; // if number, return NUMBER_TOKEN
        if (hasDecimal)
        {
            sscanf(this->tokName, "%f", &this->tokFNumber);
            this->tokCode = FLOAT_TOKEN;
        }
        else
            this->tokNumber = atoi(this->tokName);
    }
}

//-----------------------------------------------------------------------------
// Tokenizing: scan the input source buffer for the next token, filling in
//  this->tokCode. If the token is an unrecognized name,
//  return NAME_TOKEN and the string in this->tokName.  If the token
//  is a number, return NUMBER_TOKEN or FLOAT_TOKEN and the numeric value
//  in this->tokNumber or tokFNumber.
//  If the source is sm_forth, this will only return
//  space-delimited words as NAME_TOKEN, NUMBER_TOKEN, or FLOAT_TOKEN.
//  If the source is sm_sigName, this will take names
//  like "1-5" and "CS+" as whole words.

void Src::tzScan()
{
    const char* rip = this->ip;
    char ch;
    bool isVhdl = (this->mode & sm_vhdl);
    char commentChar = isVhdl ? '-' : '/';

    if (debugLevel(4))
        display("tzScan{%d-", rip - this->base);
    // skip over leading white space
    do
    {
        ch = *rip++;
        if (debugLevel(4))
            display("'%c", ch);
        if (ch == '\n')
            this->line++;

        if ((this->mode & sm_stripComments) && ch == commentChar)
        {
            if (*rip == commentChar)
            {
                // skip over to-EOL-style comments
                while (ch && ch != '\n')
                {
                    ch = *rip++;
                    if (debugLevel(4))
                        display("*%c", ch);
                }
                this->line++;
            }
            else if (!isVhdl && *rip == '*')
            {
                // or skip inline comments
                this->tokPos = rip;
                rip++;
                ch = *rip++;
                if (ch == '\n')
                    this->line++;
                while (ch && !(ch == '*' && *rip == '/'))
                {
                    ch = *rip++;
                    if (ch == '\n')
                        this->line++;
                }
                if (!ch)
                {
                    gScToken = new Token(this);
                    throw new VError(verr_illegal,
                                     "unterminated embedded comment");
                }
                rip++;
                ch = ' ';
                continue;
            }
        }
    } while (ch > 0 && ch <= ' ');
    const char* wordPos = rip - 1;
    this->tokPos = wordPos;

    // return zero if end-of-file
    if (ch == 0)
    {
        rip--;
        this->tokCode = EOF_TOKEN;
        if (debugLevel(4))
            display("EOF%d-", rip - this->base);
    }
    else if (ch == '"')
    {
        // a quoted string: look for end
        if (debugLevel(4))
            display("\"%c", ch);
        ch = *rip++;
        while (ch != '"')
        {
            if (ch < ' ' && ch != '\t')
            {
                new Token(this);
                throw new VError(verr_illegal, "unterminated quote");
            }
            ch = *rip++;
        }
        size_t len = rip - wordPos - 2;
        char name[len+1];
        // copy string and convert special characters
        rip = wordPos + 1;
        char *p = name;
        ch = *rip++;
        while (ch != '"')
        {
            if (ch == '\\')
            {
                switch (*rip)
                {
                    case 'e':
                        ch = '\e';
                        break;
                    case 'n':
                        ch = '\n';
                        break;
                    case 'r':
                        ch = '\r';
                        break;
                    case 't':
                        ch = '\t';
                        break;
                    default:
                        ch = *rip;
                }
                rip++;
            }
            *p++ = ch;
            ch = *rip++;
        }
        *p = 0;
        this->tokCode = STRING_TOKEN;
        this->tokName = newString(name);
    }
    else if (this->mode & sm_forth)
    {
        // Forth mode: scan space-delimited word
        char baseCh = toupper(*rip);
        if (ch == '0' && baseCh == 'X')
        {
            // a hex number
            rip++;
            ch = *rip++;
            while (isalnum(ch))
                ch = *rip++;
            rip--;
            size_t len = rip - wordPos - 2;
            char name[len+1];
            strncpy(name, wordPos+2, len+1);
            this->tokCode = NUMBER_TOKEN;
            this->tokNumber = hextol(name);
        }
        else if (ch == '0' && baseCh == 'B')
        {
            // a binary number
            rip++;
            ch = *rip++;
            int number = 0;
            while (ch == '0' || ch == '1')
            {
                number = (number << 1) + (ch - '0');
                ch = *rip++;
            }
            rip--;
            this->tokCode = NUMBER_TOKEN;
            this->tokNumber = number;
        }
        else
        {
            // a name
            do
            {
                ch = *rip++;
            } while (ch > ' ');
            rip--;
            size_t len = rip - wordPos;
            char name[len+1];
            strncpy(name, wordPos, len);
            name[len] = 0;
            this->tokCode = NAME_TOKEN;
            this->tokName = newString(name);
            tzCheckForNumber();
        }
    }
    else
    {
        // Non-Forth mode: punctuation counts as a token
        bool isDigits = isdigit(ch) || (ch == '.' && isdigit(*rip));
        if (isalnum(ch) || ch == '_' || (isDigits && (ch == '.')) ||
            (((this->mode & sm_sigName) &&
                    !isspace(ch) && ch != '{' && ch != '}') && ch != '='))
        {
            // if an alphanumeric name:
            do
            {
                ch = *rip++;
            } while (isalnum(ch) || ch == '_' || (isDigits && (ch == '.')) ||
                    ((this->mode & sm_verilog) &&
                             ch == '\\') ||
                    ((this->mode & sm_sigName) &&
                            !isspace(ch) && ch != '{' && ch != '}' &&
                            ch != '='));
            rip--;
            size_t len = rip - wordPos;
            char name[len+1];
            strncpy(name, wordPos, len);
            name[len] = 0;
            this->tokCode = NAME_TOKEN;
            this->tokName = newString(name);
            tzCheckForNumber();
        }
        else if (ch == '^' && *rip == 'H')
        {
            // hex number
            rip++;
            ch = *rip++;
            while (isalnum(ch))
                ch = *rip++;
            rip--;
            size_t len = rip - wordPos - 2;
            char name[len+1];
            strncpy(name, wordPos+2, len);
            name[len] = 0;
            this->tokCode = NUMBER_TOKEN;
            this->tokNumber = hextol(name);
        }
        else
            // just return the character as a token
            this->tokCode = (TokCode)ch;
    }

    if (debugLevel(4))
    {
        if (this->tokCode == NUMBER_TOKEN)
            display("%d=#%d}\n", rip - this->base, this->tokNumber);
        else if (this->tokCode == STRING_TOKEN)
            display("%d=\"%s\"}\n", rip - this->base, this->tokName);
        else if (this->tokCode == NAME_TOKEN)
            display("%d=%s}\n", rip - this->base, this->tokName);
        else
            display("%d=%d}\n", rip - this->base, this->tokCode);
    }
    this->ip = rip;
}

//-----------------------------------------------------------------------------
// Compare tokName to the given string, independent of case.
// Returns TRUE if they match.

bool Src::tzIsName(const char* s)
{
    if (this->tokCode != NAME_TOKEN)
        return (FALSE);

    char nameuc[max_nameLen];
    strncpy(nameuc, this->tokName, max_nameLen-1);
    char* p = nameuc;
    for ( ; *p; p++)
        *p = toupper(*p);

    char suc[max_nameLen];
    strncpy(suc, s, max_nameLen-1);
    for (p = suc; *p; p++)
        *p = toupper(*p);

    return (strcmp(nameuc, suc) == 0);
}

//-----------------------------------------------------------------------------
// Expect the given token code, and report an error if not there.

void Src::tzExpect(TokCode t)
{
    if (this->tokCode != t)
    {
        gScToken = new Token(this);
        if ((int)t > FLOAT_TOKEN)
            throw new VError(verr_illegal, "'%c' expected", t);
        else
            throw new VError(verr_illegal, t == NAME_TOKEN ? "name expected" :
                             "number expected");
    }
}

//-----------------------------------------------------------------------------
// If current token is not a name, throw a '<item> name expected' error message.

void Src::tzExpectNameOf(const char* item)
{
    if (!tzIsToken(NAME_TOKEN))
    {
        gScToken = new Token(this);
        throw new VError(verr_illegal, "%s name expected", item);
    }
}

//-----------------------------------------------------------------------------
// Skip tokenizer over characters in the input source until the given character
//  is encountered, and skip over it.

void Src::tzSkipTo(char c)
{
    char rc = c;
    const char* rip = this->ip;
    for ( ; *rip && *rip != rc; rip++)
        if (*rip == '\n')
            this->line++;
    if (*rip == '\n')
        this->line++;
    if (*rip)
        rip++;
    this->ip = rip;
}

//-----------------------------------------------------------------------------
// Move tokenizer scan point to the first word on the next line.

void Src::tzScanToNextLine()
{
    tzSkipTo('\n');
    tzScan();
}

//-----------------------------------------------------------------------------
// Throw a '<name> expected' error message.

void throwExpected(const char* name)
{
    throw new VError(verr_illegal, "%s expected", name);
}

//-----------------------------------------------------------------------------
// Throw a '<name> expected' error message, with given error source location.

void throwExpected(Token* srcLoc, const char* name)
{
    throw new VError(srcLoc, verr_illegal, "%s expected", name);
}

//-----------------------------------------------------------------------------
// Throw a 'constant value expected' error message.

void throwExpectedConst()
{
    throw new VError(verr_illegal, "constant value expected");
}

//-----------------------------------------------------------------------------
// Construct a macro -- compile it into tokens.

Macro::Macro(const char* name, const char* text, Src* parent)
{
    display("      define '%s'\n", name);
    this->name = name;
    if (parent)
    {
        Src* macSrc = new Src(text, strlen(text), parent->mode, parent);
        macSrc->tokenize();
        this->tokens = macSrc->tokens;
    }
    else
        this->tokens = 0;
    this->next = gMacros;
    gMacros = this;
}

//-----------------------------------------------------------------------------
// Look up src->tokName in the current macro list and return it if found.

Macro* Macro::find(Src* src, bool noErrors)
{
    for (Macro* m = gMacros; m; m = m->next)
        if (strcmp(m->name, src->tokName) == 0)
            return m;
    if (!noErrors)
    {
        gScToken = new Token(src);
        throw new VError(verr_notFound, "macro not defined");
    }
    return 0;
}

//-----------------------------------------------------------------------------
// Scan a simple expression. Leaves result in exVal.

void Src::simpleExpr()
{
    switch (this->tokCode)
    {
        case '(':
            tzScan();
            expr();
            tzExpect((TokCode)')');
            tzScan();
            break;

        case '!':
            tzScan();
            simpleExpr();
            exVal = !exVal;
            break;
            
        case NAME_TOKEN:
            {
                Symbol* sym = lookup(this->tokName);
                if (!sym)
                {
                    gScToken = new Token(this);
                    throw new VError(verr_illegal,
                                    "unknown name '%s'", this->tokName);
                }
                exVal = sym->arg;
                tzScan();
            }
            break;

        default:
            gScToken = new Token(this);
            throw new VError(verr_illegal, "illegal expression syntax");
            break;
    }
}

//-----------------------------------------------------------------------------
// Scan an and-term. Leaves result in this->exVal.

void Src::term()
{
    simpleExpr();
    if (tzIsToken('&'))
    {
        bool leftVal = exVal;
        tzScan();
        tzExpect((TokCode)'&');
        tzScan();
        simpleExpr();
        exVal = leftVal && exVal;
    }
}

//-----------------------------------------------------------------------------
// Scan a boolean expression for #if. Leaves result in this->exVal.

void Src::expr()
{
    term();
    if (tzIsToken('|'))
    {
        bool leftVal = exVal;
        tzScan();
        tzExpect((TokCode)'|');
        tzScan();
        term();
        exVal = leftVal || exVal;
    }
}

//-----------------------------------------------------------------------------
// Preprocessor: Skip ahead to the end of the current ifdef-endif statement.
// Leaves scan pointing to 'else' or 'endif' token.

void Src::skipSection()
{
    int depth = 0;
    int curLine = line;
    do
    {
        if (line == curLine)
            tzScanToNextLine();
        curLine = line;
        if (tzIsToken('`'))
        {
            tzScan();
            if (tzIsName("ifdef") || tzIsName("ifndef") || tzIsName("if"))
                depth++;
            else if ((tzIsName("else") && depth == 0) || tzIsName("endif"))
                depth--;
        }
        else if (!this->tokCode)
        {
            gScToken = new Token(this);
            throw new VError(verr_illegal, "missing '`else' or '`endif'");
        }
        else
            tzScan();
    } while (depth >= 0);
}

//-----------------------------------------------------------------------------
// Parse a timescale ("1 ns") and convert to exponent form (-9).

int Src::scanTimescale()
{
    tzScan();
    float scale;
    if (this->tokCode == FLOAT_TOKEN)
        scale = this->tokFNumber;
    else if (this->tokCode == NUMBER_TOKEN)
        scale = this->tokNumber;
    else
    {
        // no space between number and unit
        if (this->tokCode != NAME_TOKEN || strlen(this->tokName) < 3)
            throw new VError(verr_illegal, "time unit expected");
        scale = atoi(this->tokName);
        this->ip -= 2;
    }
    tzScan();
    static const char* unitName[] = {"s", "ms", "us", "ns", "ps", "fs"};
    int i;
    for (i = 0; i < 6; i++)
        if (tzIsName(unitName[i]))
            return (int)round(log10(scale) - 3 * i);

    throw new VError(verr_illegal, "unrecognized time unit");
}

//-----------------------------------------------------------------------------
// Convert source into a string of new tokens. Also processes macros.

void Src::tokenize()
{
    this->ip = this->base;
    this->line = 1;
    this->tokens = 0;
    Token* tok = 0;

    while (1)
    {
        while (1)
        {
            tzScan();
            if (!tzIsToken('`'))    // end of 'file': end of macro?
                break;
            tzScan();
            tzExpectNameOf("macro identifier");
            if (tzIsName("define"))
            {                           // define a macro
                tzScan();
                tzExpectNameOf("New macro identifier");
                char* macroName = newString(this->tokName);
                const char* text = this->ip;
                while (*text == ' ' || *text == '\t')
                    text++;
                tzSkipTo('\n');
                char* endp = (char*)this->ip -1;
                *endp = 0;
                new Macro(macroName, text, this);
                *endp = '\n';
            }
            else if (tzIsName("ifdef") || tzIsName("ifndef"))
            {
                bool isSkipIfDef = tzIsName("ifndef");
                tzScan();
                tzExpectNameOf("definition");
                if (isSkipIfDef ^ !Macro::find(this, TRUE))
                    skipSection();  // if not defined: skip section
            }
            else if (tzIsName("if"))            // `if <expression>
            {
                tzScan();
                expr();
                if (!this->exVal)
                    skipSection();
                else
                    tzScanToNextLine();
            }
            else if (tzIsName("else"))      // `else
            {
                int elseLineNo = this->line;
                tzScan();
                if (this->line == elseLineNo && tzIsName("if"))
                            // `else if <expression>
                {
                    tzScan();
                    expr();
                    if (!this->exVal)
                        skipSection();
                    else
                        tzScanToNextLine();
                }
                else
                    skipSection();
            }
            else if (tzIsName("endif"))     // `endif
                ;
            else if (tzIsName("include"))       // `include
            {
                tzScan();

                // get include filename
                tzExpect(STRING_TOKEN);
                Src* incSrc = new Src(this->tokName, this->mode, this);
                incSrc->tokenize();
                if (incSrc->tokens)
                {
                    if (tok)
                        tok->next = incSrc->tokens;
                    incSrc->tokens->prev = tok;
                    tok = incSrc->lastTok;
                }
            }
            else if (tzIsName("timescale"))
            {
                // set time scale and rounding
                gTimeScaleExp = scanTimescale();
                gTimeScale = gTicksNS * pow(10, gTimeScaleExp + 9);
                tzScan();
                tzExpect('/');
                gTimeRoundExp = scanTimescale();
            }
            else                        // else look up macro and substitute it
            {
                Macro* m = Macro::find(this);
                for (Token* mt = m->tokens; mt; mt = mt->next)
                {
                    Token* nxTok = new Token(*mt);
                    if (tok)
                        tok->next = nxTok;
                    nxTok->prev = tok;
                    if (!this->tokens)
                        this->tokens = nxTok;
                    tok = nxTok;
                }
            }
        }
        if (!this->tokCode)
            break;

        // now have a non-preprocessor token: append it to token list
        Token* newTok = new Token(this, tok);
        if (tok)
            tok->next = newTok;
        if (!this->tokens)
            this->tokens = newTok;
        tok = newTok;
    }

    // rewind to beginning of this file for token parsing
    this->lastTok = tok;
    gScToken = this->tokens;
    this->dispp = this->base;

#if 0
    Token* tPrev = this->tokens;
    for (Token* t = this->tokens; t; t = t->next)
    {
        if ((int)t < 0x1000 || (int)t > 0x40000000)
        {
            gScToken = t;
            throw new VError(verr_bug, "bad source token at 0x%x->0x%x",
                            (int)tPrev, (int)t);
        }
        tPrev = t;
    }
#endif
}

//-----------------------------------------------------------------------------
// Look at next source token, if any.

TokCode scan()
{
    if (gScToken)
        gScToken = gScToken->next;
    if (gScToken)
    {
        if (debugLevel(3))
        {
            // debugging: display up to and including token's source line
            Src* src = gScToken->src;
            char* ep = src->dispp;
            const char* tokPos = gScToken->pos;
            if (ep < tokPos)
            {
                while (*ep && (ep < tokPos || *ep != '\n'))
                    ep++;
                if (*ep)
                    ep++;
                if (ep > src->dispp)
                {
                    char esave = *ep;
                    *ep = 0;
                    // careful to not take src line as a format string
                    display(">>>%s", src->dispp);
                    *ep = esave;
                    src->dispp = ep;
                }
            }
            if (debugLevel(4))
            {
                display("{%d}", src->dispp - src->base);
                TokCode tc = gScToken->tokCode;
                if (tc == NAME_TOKEN)
                    display("tkn=%s\n", gScToken->name);
                else if (tc == STRING_TOKEN)
                    display("tks=\"%s\"\n", gScToken->name);
                else if (tc == NUMBER_TOKEN)
                    display("tk#=%d\n",gScToken->number);
                else if (tc == FLOAT_TOKEN)
                    display("tkf=%g\n",gScToken->number);
                else
                    display("tk=%c\n", (char)tc);
            }
        }
        return gScToken->tokCode;
    }
    else
    {
        if (debugLevel(4))
            display("tk=EOF\n");
        return EOF_TOKEN;
    }
}

//-----------------------------------------------------------------------------
// Expect the given token code, and report an error if not there.

void Src::expect(TokCode t)
{
    if (!gScToken || gScToken->tokCode != t)
    {
        if ((int)t > FLOAT_TOKEN)
        {
            char s[20];
            snprintf(s, 19, "'%c' expected", t);
            throw new VError(verr_illegal, s);
        }
        else
            throw new VError(verr_illegal, t == NAME_TOKEN ?
                                "name expected" : "number expected");
    }
}

//-----------------------------------------------------------------------------
// Compare the current name token to the given string, independent of
//  case. Returns TRUE if they match.

bool Src::isName(const char* s)
{
    if (!gScToken || gScToken->tokCode != NAME_TOKEN)
        return (FALSE);

    char nameuc[max_nameLen];
    strncpy(nameuc, gScToken->name, max_nameLen-1);
    char* p = nameuc;
    for ( ; *p; p++)
        *p = toupper(*p);

    char suc[max_nameLen];
    strncpy(suc, s, max_nameLen-1);
    for (p = suc; *p; p++)
        *p = toupper(*p);

    return (strcmp(nameuc, suc) == 0);
}

//-----------------------------------------------------------------------------
// Scanning checking.

void expect(TokCode t)
{
    if (!isToken(t))
    {
        if (t == NAME_TOKEN)
            throwExpected("identifier");
        else if (t == NUMBER_TOKEN)
            throwExpected("decimal number");
        else
            throw new VError(verr_illegal, "expected '%c'", (char)t);
    }
}

void expectSkip(char c)
{
    expect(c);
    scan();
}

void expectName(const char* name)
{
    if (!isName(name))
        throwExpected(name);
}

//-----------------------------------------------------------------------------
// If current token is not a name, throw a '<item> name expected' error message.

void expectNameOf(const char* item)
{
    if (!isToken(NAME_TOKEN))
        throw new VError(verr_illegal, "%s name expected", item);
}

//-----------------------------------------------------------------------------
// Return TRUE if token after current one matches t.

bool isNextToken(TokCode t)
{
    return gScToken->next && gScToken->next->tokCode == t;
}

//-----------------------------------------------------------------------------
// scan to start of next line.

void nextLine()
{
    if (!gScToken)
        return;
    int curLine = gScToken->line;
    while (gScToken && gScToken->line == curLine)
        gScToken = gScToken->next;
}

//-----------------------------------------------------------------------------
// Scan in a object name with optional scope path prefix.

void scanFullName(TmpName* symName)
{
    *symName = gScToken->name;
    while (isToken((TokCode)'.'))
    {
        scan();
        *symName += ".";
        scan();
        if (!isToken(NAME_TOKEN))
            throwExpected("name");
        *symName += gScToken->name;
    }
}
