// ****************************************************************************
//
//          PVSim Verilog Simulator Compiler Source Parser Interface
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

#include "Utils.h"
#include "string.h"

const int MAX_SOURCE_STACK = 10;

enum TokCode            // token codes returned by scan()
{
    EOF_TOKEN = 0,
    NAME_TOKEN,
    STRING_TOKEN,
    NUMBER_TOKEN,
    FLOAT_TOKEN,
    FIRST_CHAR_TOKEN = 32,
    AT_TOKEN = '@',
    BANG_TOKEN = '!',
    POUND_TOKEN = '#',
    PERCT_TOKEN = '%',
    AMPR_TOKEN = '&',
    CARROT_TOKEN = '^',
    DOLLAR_TOKEN = '$',
    GT_TOKEN = '>',
    LT_TOKEN = '<',
    PLUS_TOKEN = '+',
    MINUS_TOKEN = '-',
    ASTR_TOKEN = '*',
    SLASH_TOKEN = '/',
    EQUAL_TOKEN = '=',
    LPAREN_TOKEN = '(',
    RPAREN_TOKEN = ')',
    QUEST_TOKEN = '?',
    COLON_TOKEN = ':',
    SEMI_TOKEN = ';',
    LBRACE_TOKEN = '{',
    RBRACE_TOKEN = '}',
    BAR_TOKEN = '|',
    TICK_TOKEN = '\'',
    TILDE_TOKEN = '~',
    LAST_CHAR_TOKEN = 127   // token codes 32-127 are the characters themselves
};

typedef short SrcMode;  // not enumerated so that they may be combined
const short sm_stripComments =  0x0001;
const short sm_sigName =        0x0002;
const short sm_forth =          0x0004;
const short sm_verilog =        0x0008;
const short sm_vhdl =           0x0010;

// A scanned token

class Token: SimObject
{
public:
    class Src*  src;        // source file
    const char* pos;        // pointer to source text
    TokCode     tokCode;    // token code
    short       line;       // line number
    Token*      prev;       // previous token in source, if any
    Token*      next;       // next token in source, if any
    union
    {
        const char* name;   // NAME_TOKEN string
        int     number;     // NUMBER_TOKEN value
        float   fNumber;    // FLOAT_TOKEN value
    };

            Token(class Src* src, Token* prev = 0);
            ~Token();
};

// A source file or macro body

class Src: SimObject
{
private:
    // for tokenizing
    SrcMode mode;           // tokenizing mode
    const char*   ip;       // input source pointer after current token
    short   line;           // line number after current token
    const char* tokPos;     // token location in source text
    TokCode tokCode;        // tzScanned token code
    union
    {
        const char* tokName; // tzScanned NAME_TOKEN string
        int     tokNumber;  // tzScanned NUMBER_TOKEN value
        float   tokFNumber; // tzScanned FLOAT_TOKEN value
    };
    size_t  exVal;          // preprocessor expression parsing current value

    // tokenizer
    void    tzScan();
    void    tzCheckForNumber();
    void    tzSkipTo(char c);
    void    tzScanToNextLine();
    bool    tzIsName(const char* s);
    void    tzExpect(TokCode t);
    void    tzExpectNameOf(const char* item);
    inline bool tzIsToken(TokCode t)    { return this->tokCode == t; }
    inline bool tzIsToken(char c)       { return this->tokCode == c; }
    inline void tzExpect(char c)        { tzExpect((TokCode)(c)); }
    // Verilog preprocessor
    void    simpleExpr();
    void    term();
    void    expr();
    void    skipSection();
    int     scanTimescale();

public:
    Src*    parent;             // pointer to including source
    const char*   fileName;     // input file name
    char*   base;               // start of source
    long    size;               // input file size
    Token*  tokens;             // linked-list of tokens representing source
    Token*  lastTok;            // last in tokens list, for gluing include files
    char*   dispp;              // debugging: last displayed text

            Src(const char* fileName, SrcMode mode, Src* parent);
            Src(const char* text, size_t size, SrcMode mode, Src* parent);

    void    tokenize();
    void    expect(TokCode t);
    bool    isName(const char* s);
    inline void expect(char c)                  { expect((TokCode)c); }

    friend class Token;
    friend class Macro;
};

// Text macros, always prefixed with a backquote (`). Stored as token lists.

class Macro : SimObject
{
    Macro*      next;
public:
    const char* name;
    Token*      tokens;
                Macro(const char* name, const char* text, Src* parent);

static Macro*   find(Src* src, bool noErrors = FALSE);
};

// -------- global variables --------

extern Token*   gScToken;           // token just scanned
extern size_t   gMaxStringSpace;    // storage limits, from initApplication
extern Space    gStringsSpace;      // strings space
extern char*    gStrings;           // general string storage space
extern char*    gNextString;        // next available space in string storage
extern Macro*   gMacros;            // list of defined text macros

// -------- global function prototypes --------

extern inline float scFloat()
                { return gScToken ? gScToken->fNumber : 0; }
extern inline bool isToken(TokCode t)
                { return gScToken && gScToken->tokCode == t; }
extern inline bool isToken(char c)
                { return gScToken && gScToken->tokCode == c; }
extern inline bool isName(const char* s)
                { return gScToken && gScToken->tokCode == NAME_TOKEN &&
                  strcmp(gScToken->name, s) == 0; }

void warnErr(const char* format, ...);
void displayErrNoStop(const char* format, ...);
void throwExpected(const char* name);
void throwExpected(Token* srcLoc, const char* name);
void throwExpectedConst();

int hextol(const char* s);
char *newString (const char *s);

TokCode scan();
void expect(TokCode t);
inline void expect(char c)  { expect((TokCode)(c)); }
void expectSkip(char c);
void expectName(const char* name);
void expectNameOf(const char* item);
void scanFullName(TmpName* symName);
void nextLine();
bool isNextToken(TokCode t);

extern inline bool isNextToken(char c)
                { return isNextToken((TokCode)c); }
