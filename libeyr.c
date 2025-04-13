//{{{ Includes

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <setjmp.h>
#include "include/libeyr.h"

jmp_buf excBuf;

//}}}
//{{{ Language definition
//{{{ Lexical structure

// ASCII codes
#define aALower       97
#define aCLower       99
#define aFLower      102
#define aILower      105
#define aLLower      108
#define aWLower      119
#define aXLower      120
#define aYLower      121
#define aZLower      122
#define aAUpper       65
#define aFUpper       70
#define aZUpper       90
#define aDigit0       48
#define aDigit9       57

#define aPlus         43
#define aMinus        45
#define aTimes        42
#define aDivBy        47
#define aDot          46
#define aPercent      37

#define aParenLeft    40
#define aParenRight   41
#define aBracketLeft  91
#define aBracketRight 93
#define aCurlyLeft   123
#define aCurlyRight  125
#define aPipe        124
#define aAmp          38
#define aTilde       126
#define aBackslash    92

#define aSpace        32
#define aNewline      10

#define aApostrophe   39
#define aBacktick     96
#define aSharp        35
#define aDollar       36
#define aUnderscore   95
#define aCaret        94
#define aAt           64
#define aColon        58
#define aSemicolon    59
#define aExclamation  33
#define aQuote        34
#define aQuestion     63
#define aComma        44
#define aEqual        61
#define aLT           60
#define aGT           62

//{{{ Tokens

typedef struct { // :Token
   Unt tp : 6;
   Unt lenBts: 26;
   Unt startBt;
   Unt pl1;
   Unt pl2;
} Token;


// :Token types
// The following group of variants are transferred to the AST byte for byte, with no analysis
// Their values must exactly correspond with the initial group of variants in "Node"
// The largest value must be stored in "topVerbatimTokenVariant" constant
#define tokInt          0
#define tokLong         1
#define tokDouble       2
#define tokBool         3  // pl2 = value (1 or 0)
#define tokString       4

#define tokMisc         5  // pl1 = see the misc* constants. pl2 = underscore count iff miscUscore
                           // Also stands for "Void" among the primitive types
#define tokWord         6  // pl1 = nameId (index in the string table). pl2 = 1 iff followed by $
#define tokTypeName     7  // pl1 same as tokWord
#define tokTypeVar      8  // pl1 same as tokWord. The `$A`
#define tokKwArg        9  // pl2 = same as tokWord. The ":argName"
#define tokOperator    10  // pl1 = nameId = operId, pl2 = precedence. `+`
#define tokFieldAcc    11  // pl2 = nameId

// Statement or subexpr span types. pl2 = count of inner tokens
#define tokStmt        12  // firstSpanTokenType
#define tokClause      13  // Element of a comma-separated list
#define tokDef         14  // Compile-time known constant's definition. pl1 == 2 iff type def
#define tokParens      15  // subexpressions and struct/sum type instances
#define tokTypeCall    16  // `(Tu Int Str)`
#define tokData        17  // []
#define tokAccessor    18  // The umbrella around an accessor subexpression like `x[i][j][k]`
#define tokAccessIn    19  // The internal `[]` block inside an accessor
#define tokAssignment  20
#define tokAssignRight 21  // Right-hand side of assignment
#define tokAlias       22
#define tokAssert      23
#define tokBreakCont   24  // pl1 = 1 iff it's a continue
#define tokTrait       25
#define tokImport      26  // For test files and package decls
#define tokReturn      27

// Bracketed (multi-statement) token types. pl1 = spanLevel, see the "sl" constants
#define tokScope       28  // `(do ...)` firstScopeTokenType
#define tokIf          29  // `if ... { `. The If, ElseIf and Else tokens must be in that order
#define tokElseIf      30  // `eif ... {`
#define tokElse        31  // `else { `
#define tokMatch       32  // `(match ... ` pattern matching on sum type tag
#define tokFn          33  // `{{ a Int -> Str } body)`. pl1 = entityId
#define tokFnParams    34  //  `{ a Int -> Str }`. pl1 = entityId
#define tokTry         35  // `(try`
#define tokCatch       36  // `(catch e MyExc:`
#define tokImpl        37
#define tokFor         38
#define tokEach        39

#define topVerbatimTokenVariant tokString
#define topVerbatimType     tokMisc
#define voidType            tokMisc
// Not used in types, only in overloads to mark functions with first param = type param
constexpr Int outerTypeForTypeParam = topVerbatimType + 1;
#define firstSpanTokenType  tokStmt
#define firstScopeTokenType tokScope
#define countSyntaxForms    (tokEach + 1)

//}}}
//{{{ Lexer constants

// Span levels, must all be more than 0
#define slScope        1 // scopes (denoted by brackets): newlines and commas have no effect there
#define slStmt         2 // single-line statements: newlines and semicolons break 'em
#define slSubexpr      3 // parenthesized forms: newlines have no effect, semi-colons error out
#define slClauseList   4 // a comma-separated list
#define slUnbraced     5 // A scope that hasn't met its first brace, like an "if" before its "{"
#define slSingleBraced 6 // A "for" scope that has met exactly 1 curly brace


// List of keywords that don't correspond directly to a token.
// All these numbers must be below firstKeywordToken to avoid any clashes
#define keywTrue       1
#define keywFalse      2
#define keywBreak      3
#define keywContinue   4

#define miscPub        0    // pub. It must be 0 because it's the only one denoted by a keyword
#define miscUnderscore 1    // _
#define miscArrow      2    // ->

#define maxWordLength 128

//}}}

typedef void (*LexerFn)(const Arr(char), Compiler* restrict); // LexerFunc = &((A Char) *Lexer -> void)
private LexerFn LEX_TABLE[256]; // filled in by "tabulateLexer"

Byte const maxInt[19] = {
   9, 2, 2, 3, 3, 7, 2, 0, 3, 6,
   8, 5, 4, 7, 7, 5, 8, 0, 7
};
// The ASCII notation for the highest signed 64-bit integer abs value, 9_223_372_036_854_775_807

Byte const maximumPreciselyRepresentedFloatingInt[16] = {
   9, 0, 0, 7, 1, 9, 9, 2, 5, 4, 7, 4, 0, 9, 9, 2 };
// 2**53


constexpr char
standardText[] = "!.!0!=##$%&&.'*:++:--:/:/\\<<.<=><0===0>=<>>.>0?:@^.||."

                // reserved words: must be sorted alphabetically!
                "aliasassertbreakcatchcontinuedefeacheifelsefalsefor"
                "ifimplimportmatchpubreturntraittruetry"

                // reserved words end here; what follows may have arbitrary order
                "IntLongDoubleBoolStrVoidFLArrayDRecEnumTuPromiselencapf1f2print"
                "printErrmath:pimath:eTUlengthaddmain"
#ifdef TEST
                "foobarinner"
#endif
             ;


#define standardOperatorsLength 54 // length of the operator part above

// The :standardText prepended to all source code inputs and the hash table to provide a built-in
// string set. Eyr's reserved words must be at the start and sorted lexicographically.
// Also they must agree with the "standardStr" in eyr.internal.h

private constexpr Byte
standardStringLens[] = {
    5, 6, 5, 5, 8,
    3, 4, 3, 4, 5,
    3, 2, 4, 6, 5,
    3, 6, 5, 4, 3,
    // reserved words end here
    3, 4, 6, 4, 3, // Str(ing)
    4, 1, 1, 5, 1, // D(ict)
    3, 4, 2, 7, 3, // len
    3, 2, 2, 5, 8, // printErr
    7, 6, 1, 1, 6, // length
    3, 4,          // main
#ifdef TEST
    3, 3, 5        // foo, bar, inner
#endif
};

private Int
standardOffsets[sizeof(standardStringLens)]; // filled in by "populateStringOffsets"

private constexpr Int
standardKeywords[] = {
   tokAlias,    tokAssert,  keywBreak,  tokCatch,   keywContinue,
   tokDef,      tokEach,    tokElseIf,  tokElse,    keywFalse,
   tokFor,      tokIf,      tokImpl,    tokImport,  tokMatch,
   tokMisc,     tokReturn,  tokTrait,   keywTrue,   tokTry
};

//}}}
//{{{ Operators definitions

//constexpr Int countRealOperators = countOperators - 2; // The "unreal" ones are `a[..]`

#define nameLoc(start, len) ((len << 24) + start)
#define precUnary 100   // The highest precedence for operators (implies arity = 1)
#define precFn 10       // The precedence for function calls. Must be highest except "precUnary"

// There is a closed set of operators in the language.
//
// For added flexibility, some operators may be extended into another variant,
// e.g. (+) may be extended into (+.), while (/) may be extended into (/.).
// These extended operators are declared but not defined by the language, and may be defined
// for any type by the user, with the return type being arbitrary.
// For example, the type of 3D vectors may have two different multiplication
// operators: *. for vector product and * for scalar product.
//
// Plus, many have automatic assignment counterparts.
// For example, "a &&.= b" means "a = a &&. b" for whatever "&&." means.
typedef struct { // :OpDef
   char firstSymbol;
   Int prec;
   // Whether this operator permits defining overloads as well as extended operators (e.g. +.= )
   Bool overloadable;
   Bool assignable;
   Bool isTypelevel;
   NameLoc name;
} OpDef;

private constexpr OpDef //:OPERATORS
OPERATORS[countOperators] = {
   { .prec = precUnary, .name = nameLoc(0, 2), .firstSymbol = '!' },  // !.
   { .prec = 5,         .name = nameLoc(4, 2), .firstSymbol = '!' },  // !=
   { .prec = precUnary, .name = nameLoc(0, 1), .firstSymbol = '!' },  // !
   { .prec = precUnary, .name = nameLoc(6, 1), .firstSymbol = '#',    // #
        .overloadable=true },
   { .prec = precUnary, .name = nameLoc(8, 1),  .firstSymbol = '$'  }, // $
   { .prec = 9,         .name = nameLoc(9, 1),  .firstSymbol = '%'  }, // %
   { .prec = 4,         .name = nameLoc(10, 3), .firstSymbol = '&',   // &&.
        .assignable = true },
   { .prec = 1,         .name = nameLoc(10, 2), .firstSymbol = '&',   // &&
        .assignable=true },
   { .prec = precUnary, .name = nameLoc(13, 1), .firstSymbol = '\'',  // '
        .isTypelevel=true },
   { .prec = 9,         .name = nameLoc(14, 2), .firstSymbol = '*',   // *:
        .assignable = true, .overloadable = true},
   { .prec = 9,         .name = nameLoc(14, 1), .firstSymbol = '*',   // *
        .assignable = true, .overloadable = true},
   { .prec = 9,         .name = nameLoc(16, 2), .firstSymbol = '+',   // ++
        .assignable = false, .overloadable = false},
   { .prec = 8,         .name = nameLoc(17, 2), .firstSymbol = '+',   // +:
        .assignable = true, .overloadable = true},
   { .prec = 8,         .name = nameLoc(16, 1), .firstSymbol = '+',   // +
        .assignable = true, .overloadable = true},
   { .prec = 9,         .name = nameLoc(19, 2), .firstSymbol = '-',   // --
        .assignable = false, .overloadable = false},
   { .prec = 8,         .name = nameLoc(20, 2), .firstSymbol = '-',   // -:
        .assignable = true, .overloadable = true},
   { .prec = 8,         .name = nameLoc(19, 1), .firstSymbol = '-',   // -
        .assignable = true, .overloadable = true },
   { .prec = precUnary, .name = nameLoc(19, 1), .firstSymbol = '-' }, // -
   { .prec = 9,         .name = nameLoc(22, 2), .firstSymbol = '/',   // /:
        .assignable = true, .overloadable = true},
   { .prec = 0,         .name = nameLoc(24, 2), .firstSymbol = '/',   // /|
        .isTypelevel = true},
   { .prec = 9,         .name = nameLoc(24, 1), .firstSymbol = '/',   // /
        .assignable = true, .overloadable = true},
   { .prec = 7,         .name = nameLoc(26, 3), .firstSymbol = '<' }, // <<.
   { .prec = 6,         .name = nameLoc(29, 3), .firstSymbol = '<' }, // <=>
   { .prec = precUnary, .name = nameLoc(32, 2), .firstSymbol = '<' }, // <0
   { .prec = 6,         .name = nameLoc(29, 2), .firstSymbol = '<' }, // <=
   { .prec = 6,         .name = nameLoc(26, 1), .firstSymbol = '<' }, // <
   { .prec = 5,         .name = nameLoc(34, 3), .firstSymbol = '=' }, // ===
   { .prec = 5,         .name = nameLoc(35, 2), .firstSymbol = '=' }, // ==
   { .prec = 7,         .name = nameLoc(41, 3), .firstSymbol = '>',   // >>.
        .assignable=true, .overloadable = true},
   { .prec = precUnary, .name = nameLoc(44, 2), .firstSymbol = '>' }, // >0
   { .prec = 6,         .name = nameLoc(38, 2), .firstSymbol = '>' }, // >=
   { .prec = 6,         .name = nameLoc(31, 1), .firstSymbol = '>' }, // >
   { .prec = 0,         .name = nameLoc(46, 2), .firstSymbol = '?' }, // ?:
   { .prec = 0,         .name = nameLoc(46, 1), .firstSymbol = '?',   // ?
        .isTypelevel=true },
   { .prec = precUnary, .name = nameLoc(48, 1), .firstSymbol = '@' }, // @
   { .prec = 3,         .name = nameLoc(49, 2), .firstSymbol = '^',   // ^.
        .assignable = true },
   { .prec = 2,         .name = nameLoc(51, 3), .firstSymbol = '|',   // ||.
        .assignable = true },
   { .prec = 0,         .name = nameLoc(51, 2), .firstSymbol = '|',   // ||
        .assignable=true }
}; // real operator overloads filled in by "buildOperators"


constexpr Int
operatorStartSymbols[] = {
   // Symbols an operator may start with. "-" is absent because it's handled by lexMinus,
   // "=" - by lexEqual, "/" by "lexDivBy"
   aExclamation, aSharp, aDollar, aPercent, aAmp, aApostrophe, aTimes, aPlus,
   aDivBy, aLT, aGT, aQuestion, aAt, aCaret, aPipe
};

//}}}
//{{{ Syntactical structure

defstruct(BtToken);
defstruct(ParseFrame);
defstruct(TypeFrame);
defstruct(ExprFrame);
defstruct(Monomorphization);
defstruct(TypeLoc);

//{{{ Parse table

#define TOKS Arr(Token) restrict toks  // tokens that are used as input to the parser
#define AST Arr(Node const) restrict ast  // tokens that are used as input to the parser
typedef void (*ParserFn)(Token, Arr(Token), Compiler* restrict);

#define PARSER_FN(name) private void name(Token tok, TOKS, CM);
PARSER_FN(parseErrorBareAtom) PARSER_FN(pScope) PARSER_FN(pExpr) PARSER_FN(pAssignment) PARSER_FN(pDef)
PARSER_FN(pMisc) PARSER_FN(pAlias) PARSER_FN(parseAssert) PARSER_FN(pBreakCont) PARSER_FN(pReturn)
PARSER_FN(pIf) PARSER_FN(pElseIf) PARSER_FN(pElse) PARSER_FN(pFor)

private ParserFn const PARSE_TABLE[countSyntaxForms] = {
   [tokInt]        = parseErrorBareAtom,
   [tokLong]       = &parseErrorBareAtom,
   [tokDouble]     = &parseErrorBareAtom,
   [tokBool]       = &parseErrorBareAtom,
   [tokString]     = &parseErrorBareAtom,
   [tokMisc]       = &parseErrorBareAtom,
   [tokWord]       = &parseErrorBareAtom,
   [tokTypeName]   = &parseErrorBareAtom,
   [tokTypeVar]    = &parseErrorBareAtom,
   [tokKwArg]      = &parseErrorBareAtom,
   [tokOperator]   = &parseErrorBareAtom,
   [tokFieldAcc]   = &parseErrorBareAtom,

   [tokScope]      = &pScope,
   [tokStmt]       = &pExpr,
   [tokParens]     = &parseErrorBareAtom,
   [tokAssignment] = &pAssignment,

   [tokAlias]      = &pAlias,
   [tokAssert]     = &parseAssert,
   [tokBreakCont]  = &pBreakCont,
   [tokCatch]      = &pAlias,
   [tokFn]         = &pAlias,
   [tokTrait]      = &pAlias,
   [tokImport]     = &pAlias,
   [tokReturn]     = &pReturn,
   [tokTry]        = &pAlias,

   [tokIf]         = &pIf,
   [tokElseIf]     = &pElseIf,
   [tokElse]       = &pElse,
   [tokFor]        = &pFor
};

//}}}
//}}}
//{{{ Standard strings :standardStr

#define strAlias     0
#define strAssert    1
#define strBreak     2
#define strCatch     3
#define strcinue  4
#define strDo        5
#define strEach      6
#define strElseIf    7
#define strElse      8
#define strFalse     9
#define strFor      10
#define strIf       11
#define strImpl     12
#define strImport   13
#define strMatch    14
#define strPub      15
#define strReturn   16
#define strTrait    17
#define strTrue     18
#define strTry      19
#define strFirstNonReserved 20
#define strInt      strFirstNonReserved // types must come first here?, see "buildPreludeTypes"
#define strLong     21
#define strDouble   22
#define strBool     23
#define strString   24
#define strVoid     25
#define strF        26 // F(unction type)
#define strL        27 // L(ist)
#define strArray    28
#define strD        29 // D(ictionary)
#define strRec      30 // Record
#define strEnum     31 // Enum
#define strTu       32 // Tu(ple)
#define strPromise  33 // Promise
#define strLen      34
#define strCap      35
#define strF1       36
#define strF2       37
#define strPrint    38
#define strPrintErr 39
#define strMathPi   40
#define strMathE    41
#define strTypeVarT 42
#define strTypeVarU 43
#define strLength   44
#define strAdd      45
#define strMain     46
#ifndef TEST
#define strSentinel 47
#else
#define strSentinel 50
#endif

//}}}
//}}}
//{{{ Forward decls & generics

#define SRC Arr(char const) restrict source // Source text
#define LX Compiler* restrict lx // Compiler for lexer functions
#define CM Compiler* restrict cm // Compiler for parser functions
private void closeStatement(LX);
private NameId nameOfStandard(Int a);

defstruct(Expr);
defstruct(TExpr);

defstruct(Scopes);
void printLexer(LX);

private void exprCopyFromScratch(Int startNodeInd, CM);
private Int tIsFunction(TypeId typeId, CM);
private void addRawOverload(NameId nameId, TypeId typeId, FunctionId fnId, CM);
private TypeId exprUpToWithFrame(ParseFrame fr, SourceLoc loc, TOKS, CM);
private void typeAddHeader(TypeHeader hdr, CM);
private TypeHeader typeReadHeader(TypeId typeId, CM);
private void typeAddTypeParam(Int paramInd, Int arity, CM);
private Int typeEncodeTag(Unt sort, Int depth, Int arity, CM);
private TypeId getFirstParamType(TypeId funcTypeId, CM);
private TypeId tFunctionReturnType(TypeId funcTypeId, CM);
private bool isFunctionWithParams(TypeId typeId, CM);
private TypeId typeGetOuter(TypeId firstArgTypeId, CM);
private Int typeGetTyrity(TypeId typeId, CM);
private TypeId typeCheckBigExpr(Int indExpr, Int sentinel, CM);
private TypeId typecheckList(Int startInd, CM);
private TypeId tGetIndexOfFnFirstParam(TypeId fnType, CM);
private TypeId tCreateSingleParamTypeCall(TypeId outer, TypeId param, CM);
private Int tGetFnArity(TypeId fnType, CM);
private NameLoc nameOfHost(Int strId);

private void eWriteCallToScratch(ExprFrame frame, Expr* stEx);
private void tFreshState(TExpr* st);
private TypeId teClause(TExpr* st, Int sentinel, TOKS, CM);
private FunctionId findOverload(NameId name, TypeId tpFstArg, CM);

private TypeId typeGetGenericParam(TypeId t, Int ind, CM);
TypeId tGenericResolveConcrete(Function fn, Arr(Int) cont, Int start, Int end, CM);
TypeId typeTryGetFieldType(NameId name, TypeId t, OUT NameId* mbAltName, CM);
private void fillInCompilationResult(CM, OUT CompResult* cr);

DEFINE_LIST_HEADER(Token)
DEFINE_LIST_HEADER(BtToken)
DEFINE_LIST_HEADER(ParseFrame)
DEFINE_LIST_HEADER(ExprFrame)
DEFINE_LIST_HEADER(TypeFrame)
DEFINE_LIST_HEADER(Monomorphization)
DEFINE_LIST_HEADER(TypeLoc)

#define add(A, X) _Generic((X),\
   LInt*: addInt,\
   LUnt*: addUnt,\
   LUlong*: addUlong,\
   LBtToken*: addBtToken,\
   LToken*: addToken,\
   LParseFrame*: addParseFrame,\
   LExprFrame*: addExprFrame,\
   LTypeFrame*: addTypeFrame,\
   LMonomorphization*: addMonomorphization,\
   LTypeLoc*: addTypeLoc,\
   LNode*: addNode,\
   LSourceLoc*: addSourceLoc\
)(A, X)

#define removeLast(X) _Generic((X),\
   LInt*: removeLastInt,\
   LUnt*: removeLastUnt,\
   LUlong*: removeLastUlong,\
   LToken*: removeLastToken,\
   LBtToken*: removeLastBtToken,\
   LParseFrame*: removeLastParseFrame,\
   LExprFrame*: removeLastExprFrame,\
   LTypeFrame*: removeLastTypeFrame,\
   LMonomorphization*: removeLastMonomorphization,\
   LTypeLoc*: removeLastTypeLoc,\
   LNode*: removeLastNode,\
   LSourceLoc*: removeLastSourceLoc\
)(X)


#if defined(DEBUG) || defined(TEST)

void printName(NameId nameId, CM);
void printIntArray(Int count, Arr(Int) arr);
void printParser(Compiler* cm);
void dbgType0(TypeId type, CM);
#define dbgType(t) dbgType0(t, cm)
private void dbgExprFrames(Expr* st);
private void printLInt(LInt* st);
void dbgTypeFrames(TExpr* st);
void dbgOverloads(Int nameId, CM);
void dbgScopes(CM);
void dbgScopes0(Scopes* scopes);

#endif

//}}}
//{{{ Utils
//{{{ Arena

#define CHUNK_QUANT 32768

typedef struct ArenaChunk ArenaChunk;

struct ArenaChunk { // :ArenaChunk
   size_t size;
   ArenaChunk* next;
   char memory[]; // flexible array member
};

struct Arena { // :Arena
   ArenaChunk* firstChunk;
   ArenaChunk* currChunk;
   int currInd;
};


private size_t
minChunkSize(void) {
   return (size_t)(CHUNK_QUANT - 32);
}

Arena*
createArena() { //:createArena
   Arena* result = malloc(sizeof(Arena));

   size_t firstChunkSize = minChunkSize();
   ArenaChunk* firstChunk = malloc(firstChunkSize);
   if (!result || !firstChunk)
      { longjmp(excBuf, 1); }

   firstChunk->size = firstChunkSize - sizeof(ArenaChunk);
   firstChunk->next = null;
   result->firstChunk = firstChunk;
   result->currChunk = firstChunk;
   result->currInd = 0;
   return result;
}

private size_t
calculateChunkSize(size_t allocSize) { //:calculateChunkSize
// Calculates memory for a new chunk. Memory is quantized and is always 32 bytes less
// 32 for any possible padding malloc might use internally,
// so that the total allocation size is a good even number of OS memory pages
   size_t fullMemory = sizeof(ArenaChunk) + allocSize + 32;
   // struct header + main memory chunk + space for malloc bookkeep

   int mallocMemory = fullMemory < CHUNK_QUANT
                  ? CHUNK_QUANT
                  : (fullMemory % CHUNK_QUANT > 0
                     ? (fullMemory/CHUNK_QUANT + 1)*CHUNK_QUANT
                     : fullMemory);

   return mallocMemory - 32;
}

void*
allocateOnArena(size_t allocSize, Arena* a) { //:allocateOnArena
// Allocate memory in the arena, malloc'ing a new chunk if needed
   if ((size_t)a->currInd + allocSize >= a->currChunk->size) {
      if (a->currChunk->next != null && a->currChunk->next->size < allocSize) {
         // the next chunk is big enough, so we skip the rest of this chunk and move on
         print("reusing cleared memory from the arena!")
         a->currChunk = a->currChunk->next;
         a->currInd = 0;
      } else { // we need to allocate new chunk

         size_t newSize = calculateChunkSize(allocSize);
         ArenaChunk* newChunk = malloc(newSize);
         if (!newChunk) {
            perror("malloc error when allocating arena chunk");
            exit(EXIT_FAILURE);
         };
         // sizeof counts everything but the flexible array member, that's why we subtract it
         newChunk->size = newSize - sizeof(ArenaChunk);
         newChunk->next = a->currChunk->next; // if the arena has a (small) tail, don't lose it

         a->currChunk->next = newChunk;
         a->currChunk = newChunk;
         a->currInd = 0;
      }

   }
   void* result = (void*)(a->currChunk->memory + (a->currInd));
   a->currInd += allocSize;
   if (allocSize % 4 != 0)  {
      a->currInd += (4 - (allocSize % 4));
   }
   return result;
}

void
deleteArena(Arena* ar) { //:deleteArena
// Returns memory of the arena to the OS
   ArenaChunk* curr = ar->firstChunk;
   while (curr != null) {
      ArenaChunk* nextToFree = curr->next;
      free(curr);
      curr = nextToFree;
   }
   free(ar);
}

private void
clearArena(Arena* a) { //:clearArena
// Clears the memory of the arena for reuse. Does not free memory.
   a->currChunk = a->firstChunk;
   a->currInd = 0;
}

//}}}
//{{{ Internal lists

#define DEFINE_INTERNAL_LIST_TYPE(T)\
typedef struct {\
   Arr(T) c;\
   Int len;\
   Int cap;\
} InList##T;

#define DEFINE_INTERNAL_LIST_CONSTRUCTOR(T)             \
private InList##T createInList##T(Int initCap, Arena* a) { \
   return (InList##T){                            \
      .c = allocateArray(initCap, T, a),   \
      .len = 0, .cap = initCap };             \
}

#define DEFINE_INTERNAL_LIST(fieldName, T, aName)         \
   private void pushIn##fieldName(T newItem, Compiler* cm) {\
      if (cm->fieldName.len < cm->fieldName.cap) {\
         memcpy((T*)(cm->fieldName.c) + (cm->fieldName.len), &newItem, sizeof(T));\
      } else {\
         T* newContent = allocateArray(2*(cm->fieldName.cap), T, cm->aName);\
         memcpy(newContent, cm->fieldName.c, cm->fieldName.len*sizeof(T));\
         memcpy((T*)(newContent) + (cm->fieldName.len), &newItem, sizeof(T));\
         cm->fieldName.cap *= 2;\
         cm->fieldName.c = newContent;\
      }\
      cm->fieldName.len++;\
   }

//}}}
//{{{ MultiAssocList

// A growable list of growable lists of pairs of non-negative Ints. Is smart enough to reuse
// old allocations via an intrusive free list.
// Internal lists have the structure [len cap ...data...] or [nextFree cap ...] for the free sectors
// Units of measurement of len and cap are 1's. I.e. len can never be = 1, it starts with 2
typedef struct { // :MultiAssocList
   Int len;
   Int cap;
   Int freeList;
   Arr(Int) c;
   Arena* a;
} MultiAssocList;


MultiAssocList* //:createMultiAssocList
createMultiAssocList(Arena* a) {
   MultiAssocList* ml = allocate(MultiAssocList, a);
   Arr(Int) content = allocateArray(12, Int, a);
   (*ml) = (MultiAssocList) {
      .len = 0,
      .cap = 12,
      .c = content,
      .freeList = -1,
      .a = a,
   };
   return ml;
}

private Int //:multiListFindFree
multiListFindFree(Int neededCap, MultiAssocList* ml) {
   Int freeInd = ml->freeList;
   Int prevFreeInd = -1;
   Int freeStep = 0;
   while (freeInd > -1 && freeStep < 10) {
      Int freeCap = ml->c[freeInd + 1];
      if (freeCap == neededCap) {
         if (prevFreeInd > -1) {
            ml->c[prevFreeInd] = ml->c[freeInd]; // remove this node from the free list
         } else {
            ml->freeList = -1;
         }
         return freeInd;
      }
      prevFreeInd = freeInd;
      freeInd = ml->c[freeInd];
      freeStep++;
   }
   return -1;
}

private void //:multiListReallocToEnd
multiListReallocToEnd(Int listInd, Int listLen, Int neededCap, MultiAssocList* ml) {
   ml->c[ml->len] = listLen;
   ml->c[ml->len + 1] = neededCap;
   memcpy(ml->c + ml->len + 2, ml->c + listInd + 2, listLen*4);
   ml->len += neededCap + 2;
}

private void //:multiListDoubleCap
multiListDoubleCap(MultiAssocList* ml) {
   Int newMultiCap = ml->cap*2;
   Arr(Int) newAlloc = allocateArray(newMultiCap, Int, ml->a);
   memcpy(newAlloc, ml->c, ml->len*4);
   ml->cap = newMultiCap;
   ml->c = newAlloc;
}

private Int //:addMultiAssocList
addMultiAssocList(Int newKey, Int newVal, Int listInd, MultiAssocList* ml) {
// Add a new key-value pair to a particular list within the MultiAssocList.
// Returns the new index for this list in case it had to be reallocated, -1 if not. Throws exception
// if key already exists
   Int listLen = ml->c[listInd];
   Int listCap = ml->c[listInd + 1];
   ml->c[listInd + listLen + 2] = newKey;
   ml->c[listInd + listLen + 3] = newVal;
   listLen += 2;
   Int newListInd = -1;
   if (listLen == listCap) { // look in the freelist, but not more than 10 steps
                       // to not waste much time
      Int neededCap = listCap*2;
      Int freeInd = multiListFindFree(neededCap, ml);
      if (freeInd > -1) {
         ml->c[freeInd] = listLen;
         memcpy(ml->c + freeInd + 2, ml->c + listInd + 2, listLen);
         newListInd = freeInd;
      } ei (ml->len + neededCap + 2 < ml->cap) {
         newListInd = ml->len;
         multiListReallocToEnd(listInd, listLen, neededCap, ml);
      } else {
         newListInd = ml->len;
         multiListDoubleCap(ml);
         multiListReallocToEnd(listInd, listLen, neededCap, ml);
      }

      // add this newly freed sector to the freelist
      ml->c[listInd] = ml->freeList;
      ml->freeList = listInd;
   } else {
      ml->c[listInd] = listLen;
   }
   return newListInd;
}

private Int
multiListCreateList(Int initCap, MultiAssocList* ml) {
   Int newInd = multiListFindFree(initCap, ml);
   if (newInd == -1) {
      if (ml->len + initCap + 2 >= ml->cap) {
          multiListDoubleCap(ml);
      }
      newInd = ml->len;
      ml->len += (initCap + 2);
   }
   return newInd;
}

private Int //:listAddMultiAssocList
listAddMultiAssocList(Int newKey, Int newVal, MultiAssocList* ml) {
// Adds a new list to the MultiAssocList and populates it with one key-value
// pair. Returns its index
   Int initCap = 8;
   Int const newInd = multiListCreateList(initCap, ml);
   ml->c[newInd] = 2;
   ml->c[newInd + 1] = initCap;
   ml->c[newInd + 2] = newKey;
   ml->c[newInd + 3] = newVal;
   return newInd;
}

private Int //:listCreateMultiAssocList
listCreateMultiAssocList(MultiAssocList* ml) {
// Creates a new list in the MultiAssocList and returns its index
   Int initCap = 8;
   Int const newInd = multiListCreateList(initCap, ml);
   ml->c[newInd] = 0;
   ml->c[newInd + 1] = initCap;
   return newInd;
}

private Int //:searchMultiAssocList
searchMultiAssocList(Int searchKey, Int listInd, MultiAssocList* ml) {
// Search for a key in a particular list within the MultiAssocList. Returns
// the value if found, -1 otherwise
   Int len = ml->c[listInd]/2;
   Int const endInd = listInd + 2 + len;
   for (Int j = listInd + 2; j < endInd; j++) {
      if (ml->c[j] == searchKey) {
         return ml->c[j + len];
      }
   }
   return -1;
}

private MultiAssocList* //:copyMultiAssocList
copyMultiAssocList(MultiAssocList* ml, Arena* a) {
   MultiAssocList* result = allocate(MultiAssocList, a);
   Arr(Int) cont = allocateArray(ml->cap, Int, a);
   memcpy(cont, ml->c, 4*ml->cap);
   (*result) = (MultiAssocList){
      .len = ml->len, .cap = ml->cap, .freeList = ml->freeList, .c = cont, .a = a
   };
   return result;
}

//}}}
//{{{ Datatypes a la carte

DEFINE_INTERNAL_LIST_TYPE(Int)
DEFINE_INTERNAL_LIST_CONSTRUCTOR(Int) //:createInListInt

DEFINE_INTERNAL_LIST_TYPE(Ulong)
DEFINE_INTERNAL_LIST_CONSTRUCTOR(Ulong) //:createInListUlong

//}}}
//{{{ Strings

typedef struct { // :StringBuilder
   Arr(char) c;
   Int len;
   Int cap;
} StringBuilder;


String //:str
str(char const* content) {
   if (content == null) return (String){.c = null, .len = 0};
   Int len = 0;
   for (char const* p = content; *p != '\0'; p++) {
      len++;
   }

   return (String){.c = content, .len = len };
}

private Bool //:endsWith
endsWith(String a, String b) {
// Does string "a" end with string "b"?
   if (a.len < b.len) {
      return false;
   } ei (b.len == 0) {
      return true;
   }

   int shift = a.len - b.len;
   int cmpResult = memcmp(a.c + shift, b.c, b.len);
   return cmpResult == 0;
}

private Bool //:equal
equal(String a, String b) {
   if (a.len != b.len) {
      return false;
   }

   int cmpResult = memcmp(a.c, b.c, b.len);
   return cmpResult == 0;
}

private Int //:stringLenOfInt
stringLenOfInt(Int n) {
   if (n < 0) n = (n == INT_MIN) ? INT_MAX : -n;
   if (n < 10) return 1;
   if (n < 100) return 2;
   if (n < 1000) return 3;
   if (n < 10000) return 4;
   if (n < 100000) return 5;
   if (n < 1000000) return 6;
   if (n < 10000000) return 7;
   if (n < 100000000) return 8;
   if (n < 1000000000) return 9;
   return 10;
}

private String //:stringOfInt
stringOfInt(Int i, Arena* a) {
   Int stringLen = stringLenOfInt(i);
   char* cont = allocateOnArena(stringLen + 1, a);
   sprintf(cont, "%d", i);
   return (String){.c = cont, .len = stringLen};
}

void //:printString
printString(String s) {
   if (s.len == 0) 
      { return; }
   fwrite(s.c, 1, s.len, stdout);
   printf("\n");
}

void //:printStringNoLn
printStringNoLn(String s) {
   if (s.len == 0) 
      { return; }
   fwrite(s.c, 1, s.len, stdout);
}

private void
printStringBuilder(StringBuilder s) { //:printStringBuilder
   if (s.len == 0) 
      { return; }
   fwrite(s.c, 1, s.len, stdout);
   printf("\n");
}

private Bool //:isLetter
isLetter(Byte a) {
   return ((a >= aALower && a <= aZLower) || (a >= aAUpper && a <= aZUpper));
}

private Bool //:isCapitalLetter
isCapitalLetter(Byte a) {
   return a >= aAUpper && a <= aZUpper;
}

private Bool //:isLowercaseLetter
isLowercaseLetter(Byte a) {
   return a >= aALower && a <= aZLower;
}

private bool isDigit(Byte a) {
   return a >= aDigit0 && a <= aDigit9;
}

private bool isAlphanumeric(Byte a) { //:isAlphanumeric
   return isLetter(a) || isDigit(a);
}

private bool isHexDigit(Byte a) { //:isHexDigit
   return isDigit(a) || (a >= aALower && a <= aFLower) || (a >= aAUpper && a <= aFUpper);
}

private bool isSpace(Byte a) { //:isSpace
   return a == aSpace || a == aNewline;
}

private String
stringOf(char const* cString) {
   Int len = strlen(cString);
   return (String){.c = cString, .len = len};
}

//}}}
//{{{ Int Hashmap

typedef struct {
   Arr(int*) dict;
   int dictSize;
   int len;
   Arena* a;
} IntMap;


private IntMap*
createIntMap(int initSize, Arena* a) { //:createIntMap
   IntMap* result = allocate(IntMap, a);
   int realInitSize = (initSize >= 4 && initSize < 1024) ? initSize : (initSize >= 4 ? 1024 : 4);
   Arr(Int*) dict = allocateArray(realInitSize, Int*, a);

   result->a = a;

   int** d = dict;

   for (int i = 0; i < realInitSize; i++) {
      d[i] = null;
   }
   result->dictSize = realInitSize;
   print("in create int map %d", realInitSize);
   result->dict = dict;

   return result;
}

private void //:addIntMap
addIntMap(int key, int value, IntMap* hm) {
   if (key < 0) return;

   int hash = key % (hm->dictSize);
   if (*(hm->dict + hash) == null) {
      Arr(Int) newBucket = allocateArray(9, Int, hm->a);
      newBucket[0] = (8 << 16) + 1; // left u16 = capacity, right u16 = length
      newBucket[1] = key;
      newBucket[2] = value;
      *(hm->dict + hash) = newBucket;
   } else {
      Int* p = *(hm->dict + hash);
      Int maxInd = 2*((*p) & 0xFFFF) + 1;
      for (Int i = 1; i < maxInd; i += 2) {
         if (p[i] == key) { // key already present
            return;
         }
      }
      Int capacity = (Unt)(*p) >> 16;
      if (maxInd - 1 < capacity) {
         p[maxInd] = key;
         p[maxInd + 1] = value;
         p[0] = (capacity << 16) + (maxInd + 1)/2;
      } else {
         // TODO handle the case where we've overflowing the 16 bits of capacity
         Arr(Int) newBucket = allocateArray((4*capacity + 1), Int, hm->a);
         memcpy(newBucket + 1, p + 1, capacity*2*sizeof(int));
         newBucket[0] = ((2*capacity) << 16) + capacity;
         newBucket[2*capacity + 1] = key;
         newBucket[2*capacity + 2] = value;
         *(hm->dict + hash) = newBucket;
      }
   }
}

private bool
hasKeyIntMap(int key, IntMap* hm) { //:hasKeyIntMap
   if (key < 0) return false;

   int hash = key % hm->dictSize;
   printf("when searching, hash = %d\n", hash);
   if (hm->dict[hash] == null) { return false; }
   int* p = *(hm->dict + hash);
   int maxInd = 2*((*p) & 0xFFFF) + 1;
   for (int i = 1; i < maxInd; i += 2) {
      if (p[i] == key) {
         return true; // key already present
      }
   }
   return false;
}

private int
getIntMap(int key, int* value, IntMap* hm) { //:getIntMap
   int hash = key % (hm->dictSize);
   if (*(hm->dict + hash) == null) {
      return 1;
   }

   int* p = *(hm->dict + hash);
   int maxInd = 2*((*p) & 0xFFFF) + 1;
   for (int i = 1; i < maxInd; i += 2) {
      if (p[i] == key) { // key already present
         *value = p[i + 1];
         return 0;
      }
   }
   return 1;
}

private int
getUnsafeIntMap(int key, IntMap* hm) { //:getUnsafeIntMap
// Throws an exception when key is absent
   int hash = key % (hm->dictSize);
   if (*(hm->dict + hash) == null) {
      longjmp(excBuf, 1);
   }

   int* p = *(hm->dict + hash);
   int maxInd = 2*((*p) & 0xFFFF) + 1;
   for (int i = 1; i < maxInd; i += 2) {
      if (p[i] == key) { // key already present
         return p[i + 1];
      }
   }
   longjmp(excBuf, 1);
}

private bool
hasKeyValueIntMap(int key, int value, IntMap* hm) { //:hasKeyValueIntMap
   return false;
}

//}}}
//{{{ String Hashmap

#define initBucketSize 8

// Reference to first occurrence of a string identifier within input text
typedef struct { //:StringValue
   Unt hash;
   Int indString;
} StringValue;

typedef struct { //:Bucket
   Unt capAndLen;
   StringValue c[];
} Bucket;

// Hash map of all words/identifiers encountered in a source module
typedef struct { //:StringDict
   Arr(Bucket*) dict;
   int dictSize;
   int len;
   Arena* a;
} StringDict;


private StringDict* //:createStringDict
createStringDict(int initSize, Arena* a) {
   StringDict* result = allocate(StringDict, a);
   int realInitSize = (initSize >= initBucketSize && initSize < 2048)
      ? initSize
      : (initSize >= initBucketSize ? 2048 : initBucketSize);
   Arr(Bucket*) dict = allocateArray(realInitSize, Bucket*, a);

   result->a = a;

   Arr(Bucket*) d = dict;

   for (int i = 0; i < realInitSize; i++) {
      d[i] = null;
   }
   result->dictSize = realInitSize;
   result->dict = dict;

   return result;
}

private Unt //:hashCode
hashCode(char const* start, Int len) {
   Unt result = 5381;
   char const* p = start;
   for (Int i = 0; i < len; i++) {
      result = ((result << 5) + result) + p[i]; // hash*33 + c
   }

   return result;
}

private void //:addValueToBucket
addValueToBucket(Bucket** ptrToBucket, Int newIndString, Unt hash, Arena* a) {
   Bucket* p = *ptrToBucket;
   Int capacity = (p->capAndLen) >> 16;
   Int lenBucket = (p->capAndLen & 0xFFFF);
   if (lenBucket + 1 < capacity) {
      *(p->c + lenBucket) = (StringValue){.hash = hash, .indString = newIndString};
      (p->capAndLen)++;
   } else {
      // TODO handle the case when we're overflowing the 16 bits of capacity
      Bucket* newBucket = allocateOnArena(sizeof(Bucket) + 2*capacity*sizeof(StringValue), a);
      memcpy(newBucket->c, p->c, capacity*sizeof(StringValue));

      Arr(StringValue) newValues = (StringValue*)newBucket->c;
      newValues[capacity] = (StringValue){.indString = newIndString, .hash = hash};
      *ptrToBucket = newBucket;
      newBucket->capAndLen = ((2*capacity) << 16) + capacity + 1;
   }
}


private Int //:addStringDict
addStringDict(char const* text, Int startBt, Int lenBts, LUnt* names, StringDict* hm) {
// Unique'ing of symbols within source code
   Unt hash = hashCode(text + startBt, lenBts);
   Int hashOffset = hash % (hm->dictSize);
   Int newIndString;
   Bucket* bu = *(hm->dict + hashOffset);

   if (bu == null) {
      Bucket* newBucket = allocateOnArena(sizeof(Bucket) + initBucketSize*sizeof(StringValue), hm->a);
      newBucket->capAndLen = (initBucketSize << 16) + 1; // left u16 = cap, right u16 = len
      StringValue* firstElem = (StringValue*)newBucket->c;

      newIndString = names->len;
      NameLoc newName = ((Unt)(lenBts) << 24) + (Unt)startBt;
      add(newName, names);

      *firstElem = (StringValue){.hash = hash, .indString = newIndString };
      *(hm->dict + hashOffset) = newBucket;
   } else {
      int lenBucket = (bu->capAndLen & 0xFFFF);
      for (int i = 0; i < lenBucket; i++) {
         StringValue strVal = bu->c[i];
         if (strVal.hash == hash &&
              memcmp(text + (names->c[strVal.indString] & LOWER24BITS),
                   text + startBt,
                   lenBts) == 0) {
            // key already present
            return strVal.indString;
         }
      }

      newIndString = names->len;
      NameLoc newName = ((Unt)(lenBts) << 24) + (Unt)startBt;
      add(newName, names);
      addValueToBucket(hm->dict + hashOffset, newIndString, hash, hm->a);
   }
   return newIndString;
}

private Int //:getStringDict
getStringDict(Arr(char) text, String strToSearch, LUnt* names, StringDict* hm) {
// Returns the index of a string within the string table, or -1 if it's not present
   Int lenBts = strToSearch.len;
   Unt hash = hashCode(strToSearch.c, lenBts);
   Int hashOffset = hash % (hm->dictSize);
   if (*(hm->dict + hashOffset) == null) {
      return -1;
   } else {
      Bucket* p = *(hm->dict + hashOffset);
      int lenBucket = (p->capAndLen & 0xFFFF);
      Arr(StringValue) stringValues = (StringValue*)p->c;
      for (int i = 0; i < lenBucket; i++) {
         if (stringValues[i].hash == hash
            && memcmp(strToSearch.c,
                    text + (names->c[stringValues[i].indString] & LOWER24BITS),
                    lenBts) == 0) {
            return stringValues[i].indString;
         }
      }
      return -1;
   }
}

//}}}
//{{{ Algorithms

private void //:sortPairsDisjoint
sortPairsDisjoint(Int startInd, Int endInd, Arr(Int) arr) {
// Performs a "twin" ASC sort for faraway (Struct-of-arrays) pairs: for every swap of keys, the
// same swap on values is also performed.
// Example : [type1 type2 type3 entity1 entity2 entity3] ->
//           [type3 type2 type1 entity3 entity2 entity1]
// Params: startInd = inclusive
//       endInd = exclusive
   Int countPairs = (endInd - startInd)/2;
   if (countPairs == 2) return;
   Int keyEnd = startInd + countPairs;

   for (Int i = startInd; i < keyEnd; i++) {
      Int minValue = arr[i];
      Int minInd = i;
      for (Int j = i + 1; j < keyEnd; j++) {
         if (arr[j] < minValue) {
            minValue = arr[j];
            minInd = j;
         }
      }
      if (minInd == i) {
         continue;
      }

      // swap the keys
      Int tmp = arr[i];
      arr[i] = arr[minInd];
      arr[minInd] = tmp;
      // swap the corresponding values
      tmp = arr[i + countPairs];
      arr[i + countPairs] = arr[minInd + countPairs];
      arr[minInd + countPairs] = tmp;
   }
}

private void //:sortPairsDistant
sortPairsDistant(Int startInd, Int endInd, Int distance, Arr(Int) arr) {
// Performs a "twin" ASC sort for faraway (Struct-of-arrays) pairs: for every
// swap of keys, the same swap on values is also performed.
// Example: [type1 type2 type3 ... entity1 entity2 entity3 ...] ->
//        [type3 type1 type2 ... entity3 entity1 entity2 ...]
// The ... is the same number of elements in both halves that don't participate in sorting
// Params: startInd = inclusive
//       endInd = exclusive
   Int countPairs = (endInd - startInd)/2;
   if (countPairs == 2) return;
   Int keyEnd = startInd + countPairs;

   for (Int i = startInd; i < keyEnd; i++) {
      Int minValue = arr[i];
      Int minInd = i;
      for (Int j = i + 1; j < keyEnd; j++) {
         if (arr[j] < minValue) {
            minValue = arr[j];
            minInd = j;
         }
      }
      if (minInd == i) {
         continue;
      }

      // swap the keys
      Int tmp = arr[i];
      arr[i] = arr[minInd];
      arr[minInd] = tmp;
      // swap the corresponding values
      tmp = arr[i + distance];
      arr[i + distance] = arr[minInd + distance];
      arr[minInd + distance] = tmp;
   }
}

private void //:sortPairs
sortPairs(Int startInd, Int endInd, Arr(Int) arr) {
// Performs an ASC sort for compact pairs (array-of-structs) by key:
// Params: startInd = the first index of the overload (the one with the count of
// concrete overloads);
//       endInd = the last index belonging to the overload (the one with the last varId)
   Int countPairs = (endInd - startInd)/2;
   if (countPairs == 2) return;

   for (Int i = startInd; i < endInd; i += 2) {
      Int minValue = arr[i];
      Int minInd = i;
      for (Int j = i + 2; j < endInd; j += 2) {
         if (arr[j] < minValue) {
            minValue = arr[j];
            minInd = j;
         }
      }
      if (minInd == i) {
         continue;
      }

      // swap the keys
      Int tmp = arr[i];
      arr[i] = arr[minInd];
      arr[minInd] = tmp;
      // swap the values
      tmp = arr[i + 1];
      arr[i + 1] = arr[minInd + 1];
      arr[minInd + 1] = tmp;
   }
}

private void
sortLInts(LInt* st) { //:sortLInts
// Performs an ASC sort
   Int const len = st->len;
   if (len == 2) return;
   Arr(Int) arr = st->c;
   for (Int i = 0; i < len; i++) {
      Int minValue = arr[i];
      Int minInd = i;
      for (Int j = i + 1; j < len; j++) {
         if (arr[j] < minValue) {
            minValue = arr[j];
            minInd = j;
         }
      }
      if (minInd != i)  {
         Int tmp = arr[i];
         arr[i] = arr[minInd];
         arr[minInd] = tmp;
      }
   }
}

private Bool //:verifyUniquenessPairsDisjoint
verifyUniquenessPairsDisjoint(Int startInd, Int endInd, Arr(Int) arr) {
// For disjoint (struct-of-arrays) pairs, makes sure the keys are unique and sorted ascending
// Params: startInd = inclusive
//       endInd = exclusive
// Returns: true iff all's OK
   Int currKey = arr[startInd];
   Int i = startInd + 1;
   while (i < endInd) {
      if (arr[i] <= currKey) {
         return false;
      }
      currKey = arr[i];
      i++;
   }
   return true;
}

private Int //:binarySearch
binarySearch(Int key, Int start, Int end, Arr(Int) arr) {
   if (end <= start) {
      return -1;
   }
   Int i = start;
   Int j = end - 1;
   if (arr[start] == key) {
      return i;
   } ei (arr[j] == key) {
      return j;
   }

   while (i < j) {
      if (j - i == 1) {
         return -1;
      }
      Int midInd = (i + j)/2;
      Int mid = arr[midInd];
      if (mid > key) {
         j = midInd;
      } ei (mid < key) {
         i = midInd;
      } else {
         return midInd;
      }
   }
   return -1;
}

private void //:removeDuplicatesInList
removeDuplicatesInList(LInt* list) {
// [55 55 55 56] => [55 56]
// Precondition: the list must be sorted ASC
   Int initLen = list->len;
   if (initLen < 2)
      { return; }
   Int prevInd = 0;
   Int prevVal = list->c[0];

   for (Int i = 1; i < initLen; i++) {
      Int currVal = list->c[i];
      if (currVal != prevVal) {
         prevInd++;
         list->c[prevInd] = currVal;
         prevVal = currVal;
      }
   }
   list->len = prevInd + 1;
}

private void //:removeDuplicatesInInternalList
removeDuplicatesInInternalList(InListInt* list) {
// [55 55 55 56] => [55 56]
// Precondition: the list must be sorted
   Int initLen = list->len;
   if (initLen < 2)
      { return; }
   Int prevInd = 0;
   Int prevVal = list->c[0];

   for (Int i = 1; i < initLen; i++) {
      Int currVal = list->c[i];
      if (currVal != prevVal) {
         prevInd++;
         list->c[prevInd] = currVal;
      }
      prevVal = currVal;
   }
   list->len = prevInd + 1;
}

private Int //:minPositiveOf
minPositiveOf(Int count, ...) {
// Returns the minimum positive integer of a list, or 0 if none of them are positive
   if (count == 0)
      { return 0; }
   Int result = 0;
   va_list args;
   va_start(args, count);
   for (Int j = count; j > 0; j--)  {
      Int n = va_arg(args, Int);
      if (n > 0 && (n < result || result == 0))  {
         result = n;
      }
   }
   va_end(args);
   return result;
}

//}}}
//{{{ Generics

DEFINE_LIST(Int)
DEFINE_LIST(Ulong)
DEFINE_LIST(Unt)

// Backtrack token, used during lexing to keep track of all the nested stuff
struct BtToken { // :BtToken
   Unt tp : 6;
   Int tokenInd;
   Unt spanLevel : 3;
};

DEFINE_LIST(BtToken) //:createLBtToken
DEFINE_LIST(Token) //:createLToken


constexpr TypeId boolTy = { .v = tokBool };
constexpr TypeId intTy = { .v = tokInt };
constexpr TypeId ZERO_ARITY_TYPE = { .v = -1 };
constexpr TypeId VOID_TYPE = { .v = voidType };

Bool eq_TypeId(TypeId a, TypeId b) {
    return a.v == b.v;
}

#define pfrScope 1 // this frame is a scope (i.e. allows creation of var bindings)
#define pfrLoop  2 // this frame is a scope and a loop (allows break and continue)
#define pfrFn    3 // this frame is a function definition

struct ParseFrame { // :ParseFrame
   Int startNodeInd;
   Int sentinel;   // sentinel token
   Byte level;     // the "pfr" constants above
   TypeId typeId;  // valid only for fnDef (then it's the function's type) and loops
                   // (then it's the loop counter)
};

DEFINE_LIST(ParseFrame) //:createLParseFrame


struct TypeFrame { // :TypeFrame
   Byte tp;        // "tfr" constants
   Int sentinel;   // token id sentinel
   Int countArgs;  // accumulated number of type arguments
   TypeId id;      // For types, TypeId. For type params, their id within the params list
};

DEFINE_LIST(TypeFrame) //:createLTypeFrame

typedef struct { //:BtCodegen Backtrack for generating code
   Byte tp;        // instructions, i.e. the "i*" constants
   Int startInstr; // index of starting instruction
   Int sentinel;   // sentinel node of current function
} BtCodegen;

struct ExprFrame {   // :ExprFrame
   Byte tp;        // "exfr" constants below
   NameId name;
   Int sentinel;   // token sentinel
   Int precedence;
   Int argCount;   // accumulated number of arguments. Used for exfrCall & exfrDataAlloc only
   Int startNode;  // The id of first written node in @scr. Used for data allocators
   SourceLoc loc;  // The original token this frame is based on
   Bool isVarCall; // Iff it's a local variable being called rather than an overloaded fn name
};

DEFINE_LIST(ExprFrame) //:createLExprFrame

DEFINE_LIST(SourceLoc) //:createLSourceLoc

DEFINE_LIST(Node)

struct TypeLoc { //:TypeLoc
   Int currPos;
   Int sentinel;
};

DEFINE_LIST(TypeLoc)

#ifdef TEST
private void dbgLNode(LNode*, Arena*);
#endif

#define eq(X, Y) _Generic((X),\
   TypeId: eq_TypeId\
   )(X, Y)


//}}}
//}}}
//{{{ Internal types

typedef struct ScopeChunk ScopeChunk;
#define SCOPE_CHUNK_SZ 6 // 1024 - 4 for the pointers
struct ScopeChunk { //:ScopeChunk
   ScopeChunk *prev;
   ScopeChunk *next;
   Int c[SCOPE_CHUNK_SZ];
};

// A scope contains: an int index (searchable in the Scopes data structure) followed by a list of
// names of bindings introduced in this scope. All of that is packed in a linked list of integer
// arrays in the [aTmp] arena
struct Scopes { // :Scopes
   ScopeChunk* currChunk; // next points to here, start - not necessarily
   Int currLen; // length of current scope
   Int* start;  // address for the start of current scope.
                // Value @ that address is size of prev scope. Example:
                // (...)[1 2 3] (3)[4 5 6 7] (4)[1] (1)[...] (sizes are in (), scope contents in [])
   Int* curr;   // address for addition of next binding, points into @currChunk
};


#define exfrParen      1 // Parens
#define exfrExWrapper  2 // Expression wrapper created inside data allocators
#define exfrCall       3
#define exfrUnaryCall  4
#define exfrDataAlloc  5
#define exfrAccessor   6 // an umbrella for an accessor chain like `a[i][j][k]`
#define exfrAccessIn   7 // internal accessor like `..[i]`

// Eyr is a simple language, and in it public = immutable, private = mutable
#define classImm       1
#define classMutable   2
#define classPubMut    3

DEFINE_LIST(Var)

DEFINE_LIST(Function)

struct Expr { //:Expr State for parsing expressions
   LInt* exp;           // For assignments with complex left sides
   LExprFrame* frames;
   LNode* scr;          // "Scratch". Draft nodes written to during expression parsing
   LSourceLoc* locsScr; // SourceLocs for @scr
   Bool metAnAllocation;    // if we've met an allocation, we need to emit sub-expression nodes
   LToken* reorderBuf;  // Buffer for reordering tokens for mutation assignments
};

struct TExpr { // :TExpr State for parsing type expressions. Lives in [aTmp]
   LInt* exp;         //  TypeId
   LTypeFrame* frames;
   LInt* names;       // Function param names, record field names
   LInt* tParams;      // Type params of a fn type expression. (nameId typeId).
                          // Also used in generic call resolution
   LInt* tmp;         // Used in name uniqueness validation, and generic param substitution
   LInt* fnTypes;     // Used in function signature creation
   Bool isGeneric;        // Does this type expression contain at least a single type parameter
   LTypeLoc* genericSt;
   LTypeLoc* concreteSt;
};

typedef struct { //:Assignment
// Info about an assignment or a definition (functions, variables, types)
   Int nameTokenInd;     // index of the tokWord after tokDef
   Int rightTokenInd; // index of the tokAssignRight
   Int sentinel;
   Bool isDef;      // Is it a compile-time definition? Or a runtime var assignment?
   Bool isFunction;
   NameId name;
   Int entityId; // n < 0 => -n - 1 is an index into @functions, otherwise n => @vars
} Assignment;

typedef struct { //:GenericCall
   Int nodeInd;
   TypeId concrete;
   Int tokenInd;
} GenericCall;

DEFINE_INTERNAL_LIST_TYPE(Assignment)
DEFINE_INTERNAL_LIST_CONSTRUCTOR(Assignment)  //:createInListToplevel

DEFINE_INTERNAL_LIST_TYPE(Var)
DEFINE_INTERNAL_LIST_CONSTRUCTOR(Var) //:createInListVar
DEFINE_INTERNAL_LIST_TYPE(Function)
DEFINE_INTERNAL_LIST_CONSTRUCTOR(Function) //:createInListFunction

DEFINE_INTERNAL_LIST_TYPE(Token) //:InListToken
DEFINE_INTERNAL_LIST_TYPE(uint32_t)
DEFINE_INTERNAL_LIST_TYPE(Node)
DEFINE_INTERNAL_LIST_CONSTRUCTOR(Node) //:createInListNode


struct Monomorphization { //:Monomorphization
   TypeId concrete;  // full concrete type
   NameId name;      // function name, for codegen
   Int tokenInd;     // points into @tokens - the (generic) tokens. -1 => no codegen
   Int nodeInd;      // points into @ast - the monomorphized AST. -1 for built-ins
   FunctionId fnId;
};

DEFINE_LIST(Monomorphization)

struct Compiler { // :Compiler
   // LEXING
   String sourceCode;
   InListToken tokens;
   InListToken metas; // TODO - metas with links back into parent span tokens
   InListInt newlines;
   LSourceLoc* sourceLocs;
   InListInt numeric;          // [aTmp]
   LBtToken* lexBtrack;    // [aTmp]
   LUnt* names; // Operators, then standard strings, then imported ones, then
                               // parsed. Contains NameLoc pointing into @sourceCode
   StringDict* stringDict;

   // PARSING
   InListInt toplevels;        // indices into @functions
   Int entrypoint;             // index into @functions
   InListInt importNames;
   LParseFrame* backtrack; // [aTmp]
   Scopes scopes;             // lists of local variables for keeping track of scopes
   Expr* expr;                 // [aTmp]
   TExpr* tExpr;               // [aTmp]
   // For vars, index pointing into @vars.
   // For functions, (-ind - 2), ind points into @overloads. For types, index into @types
   Arr(Int) activeBindings;    // [aTmp]
   InListNode ast;            // Abstract syntax tree
   InListVar vars;                // local variables
   InListFunction functions;
   MultiAssocList* functionMonos; // (MultiAssocL (TypeId @monos), pointed into by Function.genericInd)

   InListInt publicFns;          // indices into @functions, subset of @toplevels
   InListInt publicConsts;       // indices into @vars
   MultiAssocList* rawOverloads; // [aTmp] (NameId => TypeId FunctionId)
   InListInt overloads;
   InListInt types;
   StringDict* typesDict;
   LMonomorphization* monos; // Addresses of monomorphizations of generic functions

   // GENERAL STATE
   Int i; // index into the table that is being read
   Int j; // index into the table that is being written to (AST during typecheck)
   Arena* a;
   Arena* aTmp;
   Bool wasError;
   String errMsg;
   CompStats stats;
};

DEFINE_INTERNAL_LIST(newlines, Int, a) //:pushInnewlines
DEFINE_INTERNAL_LIST(numeric, Int, a) //:pushInnumeric
DEFINE_INTERNAL_LIST(importNames, Int, a) //:pushInimportNames
DEFINE_INTERNAL_LIST(overloads, Int, a) //:pushInoverloads
DEFINE_INTERNAL_LIST(types, Int, a) //:pushIntypes
DEFINE_INTERNAL_LIST_CONSTRUCTOR(Token) //:createInListToken
DEFINE_INTERNAL_LIST(tokens, Token, a) //:pushIntokens
DEFINE_INTERNAL_LIST(toplevels, Int, a) //:pushIntoplevels
DEFINE_INTERNAL_LIST(vars, Var, a) //:pushInentities
DEFINE_INTERNAL_LIST(functions, Function, a) //:pushInfunctions
DEFINE_INTERNAL_LIST(ast, Node, a) //:pushInast

// see the Type layout chapter in the docs
#define sorDeclare         1 // Used for definitions of records and sum types, both generic and not
#define sorTypeCall        2 // A reference to a generic type. May be generic itself (when
                             // not all generic params are filled in)
#define sorGenericParam    3 // A generic variant. outer = de Bruijn index
#define sorMaxType         sorGenericParam

// the following constants must not clash with the "sor" constants
// Type expression data format: First element is the tag (one of the following
// constants), second is payload. Used in @expStack
#define tfrFunction       11
#define tfrParam          12 // payload: paramId
#define tfrFnTypeCall     13
#define tfrRecord         14
#define tfrTypeCall       15

private Compiler PROTO = {
      .sourceCode = null,
      .names = null, .stringDict = null,
      .typesDict = null,
      .activeBindings = null,
      .rawOverloads = null,
      .a = null,
      .i = -1
   };

private Bool _wasInit = false;

private void initCompiler();

//}}}
//{{{ Errors
//{{{ Internal errors

#define iErrorInconsistentSpans          1 // Inconsistent span length / structure of token
                                           // scopes!
#define iErrorImportedFunctionNotInScope 2 // There is a -1 or something else in the
                                           // @activeBindings for an imported function
#define iErrorParsedFunctionNotInScope   3 // There is a -1 or something else in the
                                           // @activeBindings for a parsed function
#define iErrorOverloadsOverflow          4 // There were more overloads for a function than
                                           // what was allocated
#define iErrorOverloadsNotFull           5 // There were fewer overloads for a function than
                                           // what was allocated
#define iErrorOverloadsIncoherent        6 // The overloads table is incoherent
#define iErrorExpressionIsNotAnExpr      7 // What is supposed to be an expression in the AST is
                                           // not a nodExpr
#define iErrorComplexExpression          8 // Error in a complex expression's internal definitions
#define iErrorZeroArityFuncWrongEmit     9 // A 0-arity function has a wrong "emit" (should be
                                           // one of the prefix ones)
#define iErrorGenericTypesInconsistent  10 // Two generic types have inconsistent layout
                                           // (premature end of type)
#define iErrorGenericTypesParamOutOfBou 11 // A type contains a paramId that is out-of-bounds
                                           // of the generic's tyrity
#define iErrorOuterTypeOfParam          12 // Tried to get an outer type of param
#define iErrorInconsistentTypeExpr      13 // Reduced type expression has != 1 elements
#define iErrorNotAFunction              14 // Expected to find a function type here
#define iErrorArrayElemButShouldBePtr   15 // An assignment with list accessor on left should be ptr
#define iErrorIllegalEmit               16 // This entity cannot have this emit type in codegen

//}}}
//{{{ Syntax errors

char const
errNonAscii[]               = "Non-ASCII symbols are not allowed in code - only inside comments & string literals!";
char const
errPrematureEndOfInput[]      = "Premature end of input";
char const
errUnrecognizedByte[]         = "Unrecognized Byte in source code!";
char const
errWordChunkStart[]          = "In an identifier, each word piece must start with a letter. Tilde may come only after an identifier";
char const
errWordCapitalizationOrder[]   = "An identifier may not contain a capitalized piece after an uncapitalized one!";
char const
errWordLengthExceeded[]       = "I don't know why you want an identifier of more than 128 chars, but they aren't supported";
char const
errWordMutability[]                     = "Mutable variables should look like `asdf$` with no spaces in between";
char const
errWordFreeFloatingFieldAcc[]   = "Free-floating field accessor";
char const
errWordInMeta[]             = "Only ordinary words are allowed inside meta blocks!";
char const
errNumericEndUnderscore[]      = "Numeric literal cannot end with underscore!";
char const
errNumericWidthExceeded[]      = "Numeric literal width is exceeded!";
char const
errNumericBinWidthExceeded[]   = "Integer literals cannot exceed 64 bit!";
char const
errNumericFloatWidthExceeded[]  = "Floating-point literals cannot exceed 2**53 in the significant bits, and 22 in the decimal power!";
char const
errNumericEmpty[]            = "Could not lex a numeric literal, empty sequence!";
char const
errNumericMultipleDots[]      = "Multiple dots in numeric literals are not allowed!";
char const
errNumericIntWidthExceeded[]   = "Integer literals must be within the range [-9,223,372,036,854,775,808; 9,223,372,036,854,775,807]!";
char const errPunctuationExtraOpening[]   = "Extra opening punctuation";
char const errPunctuationExtraClosing[]   = "Extra closing punctuation";
char const errPunctuationCommaNotClause[]  = "The comma is only allowed inside clauses!";
char const errPunctuationOnlyInMultiline[] = "The statement ender `;` is not allowed inside subexpressions!";
char const errPunctuationFnNotInStmt[]    = "Function definitions must be directly in a statement";
char const errPunctuationUnmatched[]      = "Unmatched closing punctuation";
char const errPunctuationScope[]         = "Scopes may only be opened in multi-line syntax forms or in `for`, `if` forms";
char const errOperatorUnknown[]         = "Unknown operator";
char const errOperatorAssignmentPunct[]   = "Incorrect assignment operator: must be directly inside an ordinary statement, after the binding name(s) or l-value!";
char const errAssignmentEmptyRight[]      = "Assignment or definition with empty right side";
char const errOperatorTypeDeclPunct[]     = "Incorrect type declaration operator placement: must be the first in a statement!";
char const
errOperatorMutationInDef[]     = "Mutation (e.g. `+=`) is not allowed for defs which signify compile-time known constants";
char const
errCoreNotInsideStmt[]        = "Core form must be directly inside statement";
char const
errCoreMisplacedElse[]        = "The else statement must be inside an if, ifEq, ifPr or match form";
char const
errCoreMissingParen[]         = "Core form requires opening parenthesis/curly brace immediately after keyword!";
char const
errBareAtom[]               = "Malformed token stream (atoms and parentheses must not be bare)";
char const
errImportsNonUnique[]         = "Import names must be unique!";
char const
errCannotMutateImmutable[]     = "Immutable variables cannot be reassigned to!";
char const
errPrematureEndOfTokens[]      = "Premature end of tokens";
char const
errUnexpectedToken[]         = "Unexpected token";
char const
errCoreFormTooShort[]         = "Core syntax form too short";
char const
errCoreFormUnexpected[]       = "Unexpected core form";
char const
errCoreFormAssignment[]       = "A core form may not contain any assignments!";
char const
errCoreFormInappropriate[]     = "Inappropriate reserved word!";
char const errIfLeft[]                     = "A left-hand clause in an if can only contain variables, boolean literals and expressions!";
char const errIfRight[]                    = "A right-hand clause in an if can only contain atoms, expressions, scopes and some core forms!";
char const errIfEmpty[]                    = "Empty `if` expression";
char const errIfMalformed[]                = "Malformed `if` expression, should look like (if pred: `true case` else `default`)";
char const errIfElseMustBeLast[]           = "An `else` subexpression must be the last thing in an `if`";
char const errFnNameAndParams[]            = "Function signature must look like this: `{x Type1 y Type 2 ->  ReturnType => body...}`";
char const errFnDuplicateParams[]          = "Duplicate parameter names in a function are not allowed";
char const errFnMissingBody[]              = "Function definition must contain a body which must be a Scope immediately following its parameter list!";
char const errLoopSyntaxError[]            = "A loop should look like `for {x = 0; x < 101; x++}{ loopBody } `";
char const errLoopNoCondition[]            = "A loop header should contain a condition";
char const errLoopEmptyStepBody[]          = "Empty loop step code & body, but at least one must be present!";
char const errLoopWrongFormInStepper[]     = "A for loop's stepper can only contain assignments, expressions and asserts";
char const errLoopBreakOutside[]           = "The break keyword can only be used inside a loop scope!";
char const errBreakContinueTooComplex[]    = "This statement is too complex! Continues and breaks may contain one thing only: the postitive number of enclosing loops to continue/break!";
char const errBreakContinueInvalidDepth[]  = "Invalid depth of break/continue! It must be a positive 32-bit integer!";
char const errDuplicateFunction[]          = "Duplicate function declaration: a function with same name and arity already exists in this scope!";
char const errExpressionError[]            = "Cannot parse expression!";
char const errExpressionWrongArgCount[]    = "Wrong argument count for a function";
char const errExpressionCannotContain[]    = "Expressions cannot contain scopes or statements!";
char const errExpressionFunctionless[]     = "Functionless expression!";
char const errTypeDefCountNames[]          = "Wrong count of names in a type definition!";
char const errTypeDefCannotContain[]       = "Type declarations may only contain types (like Int), type params (like A), type constructors (like List) and parentheses!";
char const errTypeDefError[]               = "Cannot parse type declaration!";
char const errTypeDefParamsError[]         = "Error parsing type params. Should look like this: [T U/2]";
char const errOperatorWrongArity[]         = "Wrong number of arguments for operator!";
char const errUnknownBinding[]             = "Unknown binding!";
char const errUnknownFunction[]            = "Unknown function!";
char const errOperatorUsedInappropriately[] = "Operator used in an inappropriate location!";
char const errAssignment[]                 = "Cannot parse assignment, it must look like `freshIdentifier` = `expression`";
char const errListDifferentEltTypes[]      = "A list's elements must all be of the same type";
char const errMutation[]                   = "Cannot parse mutation, it must look like `freshIdentifier` += `expression`";
char const errAssignmentShadowing[]        = "Assignment error: existing identifier is being shadowed";
char const errAssignmentToplevelFn[]       = "Assignment of top-level functions must be immutable";
char const errAssignmentLeftSide[]         = "Assignment error: left side must be a var name, a type name, or an existing var with one or more accessors";
char const errAssignmentAccessOnToplevel[] = "Accessor on the left side of an assignment at toplevel";
char const errAssignmentToFunctionVar[]    = "Assignment to a function variable should look like `fn F Int Long = overloadedName;`";
char const errReturn[]                     = "Cannot parse return statement, it must look like `return ` {expression}";
char const errScope[]                      = "A scope may consist only of expressions, assignments, function definitions and other scopes!";
char const errTemp[]                       = "Not implemented yet";

//}}}
//{{{ Type errors

char const errUnknownType[]                = "Unknown type";
char const errUnexpectedType[]             = "Unexpected to find a type here";
char const errExpectedType[]               = "Expected to find a type here";
char const errUnknownTypeConstructor[]     = "Unknown type constructor";
char const errTypeUnknownFirstArg[]        = "The type of first argument to a call must be known, otherwise I can't resolve the function overload!";
char const errTypeOverloadsIntersect[]     = "Two or more overloads of a single function intersect (impossible to choose one over the other)";
char const errTypeOverloadsOnlyOneZero[]   = "Only one 0-arity function version is possible, otherwise I can't disambiguate the overloads!";
char const errTypeNoMatchingOverload[]     = "No matching function overload was found";
char const errTypeWrongArgumentType[]      = "Wrong argument type";
char const errTypeWrongReturnType[]        = "Wrong return type";
char const errTypeMismatch[]               = "Declared type doesn't match actual type";
char const errTypeMustBeBool[]             = "Expression must have the Bool type";
char const errTypeConstructorWrongArity[]  = "Wrong arity for the type constructor";
char const errTypeTooManyParameters[]      = "Only up to 254 type parameters are supported";
char const errTypeOfNotList[]              = "Trying to get the element of a type which is not a list";
char const errTypeOfListIndex[]            = "The type of a list/array index must be Int";
char const errTypePolymorphicAssignment[]  = "Assignments and constants must be monomorphic (no type params)";
char const errTypeGenericCallDoesntUnify[] = "Generic function's type cannot be unified with its argument types";
char const errTypeFieldNotFound[]          = "Field access error in a type";

//}}}
//}}}
//{{{ Lexer
//{{{ LexerUtils

#define CURR_BT source[lx->i]
#define NEXT_BT source[lx->i + 1]
#define IND_BT (lx->i - lx->stats.standardTextLen)
#define VALIDATEI(cond, errInd) if (!(cond)) { throwExcInternal0(errInd, __LINE__, cm); }
#define VALIDATEL(cond, errMsg) if (!(cond)) { throwExcLexer0(errMsg, __LINE__, lx); }


#if defined(TEST) || defined (DEBUG)

Int pos(Compiler* lx);
void dbgLexBtrack(Compiler* lx);

#endif

typedef union {
   uint64_t i;
   double   d;
} FloatingBits;

String //:readSourceFile
readSourceFile(String fName, Arena* a) {
   FILE *file = fopen(fName.c, "r");
   if (!file)
      { return empty; }

   // Go to the end of the file
   if (fseek(file, 0L, SEEK_END) != 0)
      { goto cleanup; }
   long fileSize = ftell(file);
   if (fileSize == -1)
      { goto cleanup; }
   Int lenStandard = sizeof(standardText) - 1;
   // Allocate our buffer to that size, with space for the standard text in front of it
   Arr(char) result = allocateOnArena(lenStandard + fileSize + 1, a);

   // Go back to the start of the file
   if (fseek(file, 0L, SEEK_SET) != 0)
      { goto cleanup; }

   memcpy(result, standardText, lenStandard);
   // Read the entire file into memory
   size_t lenSource = fread(result + lenStandard, 1, fileSize, file);

   Int const len = lenStandard + lenSource; // extra 1 for the '\0'
   if (ferror(file) != 0 ) {
      fputs("Error reading file", stderr);
   } else {
      result[len] = '\0'; // Just to be safe
   }
   cleanup:
   fclose(file);
   return (String){.c = result, .len = len};
}

private String //:prepareInput
prepareInput(char const* content, Arena* a) {
// Allocates source code into an arena after prepending it with the standardText
   if (content == null) return empty;
   char const* ind = content;
   Int lenSource = 0;
   for (; *ind != '\0'; ind++) {
      lenSource++;
   }
   Int lenStandard = sizeof(standardText) - 1; // -1 for the invisible \0 char at end

   Arr(char) result = allocateOnArena(lenStandard + lenSource + 1, a); // +1 for the \0
   memcpy(result, standardText, lenStandard);
   memcpy(result + lenStandard, content, lenSource + 1); // + 1 to copy the \0
   return (String){.c = result, .len = lenStandard + lenSource};
}

NameId //:nameOfStandard
nameOfStandard(Int strId) {
// Converts a standard string to its nameId. Doesn't work for reserved words, obviously. So the
// argument must be >= "strFirstNonreserved"
   return (NameId)((Unt)(strId + countOperators));
}

private void //:skipSpaces
skipSpaces(Arr(char const) source, LX) {
   while (lx->i < lx->stats.inpLength) {
      Byte currBt = CURR_BT;
      if (!isSpace(currBt)) {
         return;
      }
      lx->i++;
   }
}

void //:ensureCapacityTokenBuf
ensureCapacityTokenBuf(Int neededSpace, LToken* st, CM) {
// Reserve space in the temp buffer used to shuffle tokens
   st->len = 0;
   if (neededSpace >= st->cap) {
      Arr(Token) newContent = allocateArray(2*(st->cap), Token, cm->a);
      st->cap *= 2;
      st->c = newContent;
   }
}

private void //:ensureCapacityTokens
ensureCapacityTokens(Int neededSpace, CM) {
// Reserve space in the main tokens list
   if (cm->tokens.len + neededSpace - 1 >= cm->tokens.cap) {
      Int const newCap = (2*(cm->tokens.cap) > cm->tokens.cap + neededSpace)
         ? 2*cm->tokens.cap
         : cm->tokens.cap + neededSpace;
      Arr(Token) newContent = allocateArray(newCap, Token, cm->a);
      memcpy(newContent, cm->tokens.c, cm->tokens.len*sizeof(Token));
      cm->tokens.cap = newCap;
      cm->tokens.c = newContent;
   }
}

private void //:ensureCapacityTypes
ensureCapacityTypes(Int neededSpace, CM) {
// Reserve space in the types table
   if (cm->types.len + neededSpace - 1 >= cm->types.cap) {
      Int const newCap = (2*(cm->types.cap) > cm->types.cap + neededSpace)
         ? 2*cm->types.cap
         : cm->types.cap + neededSpace;
      Arr(Int) newContent = allocateArray(newCap, Int, cm->a);
      memcpy(newContent, cm->types.c, cm->types.len*sizeof(Int));
      cm->types.cap = newCap;
      cm->types.c = newContent;
   }
}

_Noreturn private void
throwExcInternal0(Int errInd, Int lineNumber, CM) {
   cm->wasError = true;
#ifdef DEBUG
   printf("Internal error %d at line %d\n", errInd, lineNumber);
#endif
   cm->errMsg = stringOfInt(errInd, cm->a);
   printString(cm->errMsg);
   longjmp(excBuf, 1);
}

#define throwExcInternal(errInd, cm) throwExcInternal0(errInd, __LINE__, cm) //:throwExcInternal

_Noreturn private void
throwExcLexer0(char const errMsg[], Int lineNumber, LX) {
// Sets i to beyond input's length to communicate to callers that lexing is over
   lx->wasError = true;
#ifdef DEBUG
   printf("Error on code line %d, i = %d: %s\n", lineNumber, IND_BT, errMsg);
#endif
   lx->errMsg = str(errMsg);
   longjmp(excBuf, 1);
}

#define throwExcLexer(msg) throwExcLexer0(msg, __LINE__, lx)

//}}}
//{{{ Lexer proper

private void
checkPrematureEnd(Int requiredSymbols, LX) { //:checkPrematureEnd
// Checks that there are at least 'requiredSymbols' symbols left in the input
   VALIDATEL(lx->i + requiredSymbols <= lx->stats.inpLength, errPrematureEndOfInput)
}

private void
setSpanLengthLexer(Int tokenInd, LX) { //:setSpanLengthLexer
// Finds the top-level punctuation opener by its index, and sets its lengths.
// Called when the matching closer is lexed. Does not pop anything from the "lexBtrack"
   lx->tokens.c[tokenInd].lenBts = lx->i - lx->tokens.c[tokenInd].startBt + 1;
   lx->tokens.c[tokenInd].pl2 = lx->tokens.len - tokenInd - 1;
}

private void
setStmtSpanLength(Int spanInd, LX) { //:setStmtSpanLength
// Correctly calculates the lenBts for a single-line, statement-type span.
   lx->tokens.c[spanInd].lenBts = lx->i - lx->tokens.c[spanInd].startBt;
   lx->tokens.c[spanInd].pl2 = lx->tokens.len - spanInd - 1;
}

private void
addStatementSpan(Unt stmtType, Int startBt, LX) {
   add(((BtToken){ .tp = stmtType, .tokenInd = lx->tokens.len, .spanLevel = slStmt }),
               lx->lexBtrack);
   pushIntokens((Token){ .tp = stmtType, .startBt = startBt, .lenBts = 0 }, lx);
}

private void //:wrapInAStatement
wrapInAStatement(Int startBt, Arr(char const) source, LX) {
// Wraps a new token in a statement or, if we're in a tokFnParams, a clause
// Sets the startBt to a specific value
   if (lx->lexBtrack->len > 0) {
      BtToken const top = last(lx->lexBtrack);
      if (top.spanLevel == slScope || top.spanLevel == slUnbraced) {
         // the second case is for the conditions of "if" statements
         addStatementSpan(tokStmt, startBt, lx);
      } ei (top.spanLevel == slClauseList) {
         addStatementSpan(tokClause, startBt, lx);
      }
   } else {
      addStatementSpan(tokStmt, startBt, lx);
   }
}

private int64_t
calcIntegerWithinLimits(LX) { //:calcIntegerWithinLimits
   int64_t powerOfTen = (int64_t)1;
   int64_t result = 0;
   Int j = lx->numeric.len - 1;

   Int loopLimit = -1;
   while (j > loopLimit) {
      result += powerOfTen*lx->numeric.c[j];
      powerOfTen *= 10;
      j--;
   }
   return result;
}

private bool
integerWithinDigits(const Byte* b, Int bLength, LX) { //:integerWithinDigits
// Is the current numeric <= b if they are regarded as arrays of decimal digits (0 to 9)?
   if (lx->numeric.len != bLength) return (lx->numeric.len < bLength);
   for (Int j = 0; j < lx->numeric.len; j++) {
      if (lx->numeric.c[j] < b[j]) return true;
      if (lx->numeric.c[j] > b[j]) return false;
   }
   return true;
}

private Int
calcInteger(int64_t* result, LX) { //:calcInteger
   if (lx->numeric.len > 19 || !integerWithinDigits(maxInt, sizeof(maxInt), lx)) return -1;
   *result = calcIntegerWithinLimits(lx);
   return 0;
}

private Long
calcHexNumber(LX) { //:calcHexNumber
   int64_t result = 0;
   int64_t powerOfSixteen = 1;
   Int j = lx->numeric.len - 1;

   // If the literal is full 16 bits long, then its upper sign contains the sign bit
   Int loopLimit = -1;
   while (j > loopLimit) {
      result += powerOfSixteen*lx->numeric.c[j];
      powerOfSixteen = powerOfSixteen << 4;
      j--;
   }
   return result;
}

private void
hexNumber(Arr(char const) source, LX) { //:hexNumber
// Lexes a hexadecimal numeric literal (integer or floating-point)
// Examples of accepted expressions: 0xCAFE'BABE, 0xdeadbeef, 0x123'45A
// Examples of NOT accepted expressions: 0xCAFE'babe, 0x'deadbeef, 0x123'
// Checks that the input fits into a signed 64-bit fixnum.
// TODO add floating-point literals like 0x12FA
   checkPrematureEnd(2, lx);
   lx->numeric.len = 0;
   Int j = lx->i + 2;
   while (j < lx->stats.inpLength) {
      Byte cByte = source[j];
      if (isDigit(cByte)) {
         pushInnumeric(cByte - aDigit0, lx);
      } ei ((cByte >= aALower && cByte <= aFLower)) {
         pushInnumeric(cByte - aALower + 10, lx);
      } ei ((cByte >= aAUpper && cByte <= aFUpper)) {
         pushInnumeric(cByte - aAUpper + 10, lx);
      } ei (cByte == aUnderscore
               && (j == lx->stats.inpLength - 1 || isHexDigit(source[j + 1]))) {
         throwExcLexer(errNumericEndUnderscore);
      } else {
         break;
      }
      VALIDATEL(lx->numeric.len <= 16, errNumericBinWidthExceeded)
      j++;
   }
   int64_t resultValue = calcHexNumber(lx);
   pushIntokens((Token){ .tp = tokInt, .pl1 = resultValue >> 32, .pl2 = resultValue & LOWER32BITS,
            .startBt = lx->i, .lenBts = j - lx->i }, lx);
   lx->numeric.c = 0;
   lx->i = j; // CONSUME the hex number
}

private Int
calcFloating(double* result, Int powerOfTen, SRC, LX) {
//:calcFloating Parses the floating-point numbers using just the "fast path" of David Gay's
// "strtod" function, extended to 16 digits.
// I.e. it handles only numbers with 15 digits or 16 digits with the first digit not 9,
// and decimal powers within [-22; 22]. Parsing the rest of numbers exactly is a huge and pretty
// useless effort. Nobody needs these floating literals in text form: they are better input in
// binary, or at least text-hex or text-binary.
// Input: array of bytes that are digits (without leading zeroes), and the negative power of ten.
// So for '0.5' the input would be (5 -1), and for '1.23000' (123000 -5).
// Example, for input text '1.23' this function would get the args: ([1 2 3] 1)
// Output: a 64-bit floating-pointt number, encoded as a long (same bits)
   Int indTrailingZeroes = lx->numeric.len - 1;
   Int ind = lx->numeric.len;
   while (indTrailingZeroes > -1 && lx->numeric.c[indTrailingZeroes] == 0) {
      indTrailingZeroes--;
   }

   // how many powers of 10 need to be knocked off the significand to make it fit
   Int significandNeeds = ind - 16 >= 0 ? ind - 16 : 0;
   // how many power of 10 significand can knock off (these are just trailing zeroes)
   Int significantCan = ind - indTrailingZeroes - 1;
   // how many powers of 10 need to be added to the exponent to make it fit
   Int exponentNeeds = -22 - powerOfTen;
   // how many power of 10 at maximum can be added to the exponent
   Int exponentCanAccept = 22 - powerOfTen;

   if (significantCan < significandNeeds
      || significantCan < exponentNeeds
      || significandNeeds > exponentCanAccept) {
      return -1;
   }

   // Transfer of decimal powers from significand to exponent to make them both fit within their
   // respective limits
   // (10000 -6) -> (1 -2); (10000 -3) -> (10 0)
   Int transfer = (significandNeeds >= exponentNeeds) ? (
          (ind - significandNeeds == 16 && significandNeeds < significantCan
           && significandNeeds + 1 <= exponentCanAccept) ?
               (significandNeeds + 1) : significandNeeds
      ) : exponentNeeds;
   lx->numeric.len -= transfer;
   Int finalPowerTen = powerOfTen + transfer;

   if (!integerWithinDigits(maximumPreciselyRepresentedFloatingInt,
                     sizeof(maximumPreciselyRepresentedFloatingInt), lx)) {
      return -1;
   }

   int64_t significandInt = calcIntegerWithinLimits(lx);
   double significand = (double)significandInt; // precise
   double exponent = pow(10.0, (double)(abs(finalPowerTen)));

   *result = (finalPowerTen > 0) ? (significand*exponent) : (significand/exponent);
   return 0;
}

int64_t
longOfDoubleBits(double d) { //:longOfDoubleBits
   FloatingBits un = {.d = d};
   return un.i;
}

private double
doubleOfLongBits(int64_t i) { //:doubleOfLongBits
   FloatingBits un = {.i = i};
   return un.d;
}

private void
decNumber(bool isNegative, SRC, LX) { //:decNumber
// Lexes a decimal numeric literal (integer or floating-point). Adds a token.
// TODO: add support for the '1.23E4' format
   Int j = (isNegative) ? (lx->i + 1) : lx->i;
   Int digitsAfterDot = 0; // this is relative to first digit, so it includes the leading zeroes
   Bool metDot = false;
   Bool metNonzero = false;
   Int maximumInd = (lx->i + 40 > lx->stats.inpLength) ? (lx->i + 40) : lx->stats.inpLength;
   while (j < maximumInd) {
      Byte cByte = source[j];

      if (isDigit(cByte)) {
         if (metNonzero) {
            pushInnumeric(cByte - aDigit0, lx);
         } ei (cByte != aDigit0) {
            metNonzero = true;
            pushInnumeric(cByte - aDigit0, lx);
         }
         if (metDot) {
            digitsAfterDot++;
         }
      } ei (cByte == aUnderscore) {
         VALIDATEL(j != (lx->stats.inpLength - 1) && isDigit(source[j + 1]),
                 errNumericEndUnderscore)
      } ei (cByte == aDot) {
         if (j == lx->stats.inpLength - 1 || !isDigit(source[j + 1])) {
            // this dot is not a part of the number
            break;
         }
         VALIDATEL(!metDot, errNumericMultipleDots)
         metDot = true;
      } else {
         break;
      }
      j++;
   }

   VALIDATEL(j >= lx->stats.inpLength || !isDigit(source[j]), errNumericWidthExceeded)

   if (metDot) {
      double resultValue = 0;
      Int errorCode = calcFloating(&resultValue, -digitsAfterDot, source, lx);
      VALIDATEL(errorCode == 0, errNumericFloatWidthExceeded)

      Long bitsOfFloat = longOfDoubleBits((isNegative) ? (-resultValue) : resultValue);
      pushIntokens((Token){ .tp = tokDouble, .pl1 = (bitsOfFloat >> 32),
               .pl2 = (bitsOfFloat & LOWER32BITS), .startBt = lx->i, .lenBts = j - lx->i}, lx);
   } else {
      int64_t resultValue = 0;
      Int errorCode = calcInteger(&resultValue, lx);
      VALIDATEL(errorCode == 0, errNumericIntWidthExceeded)

      if (isNegative) resultValue = -resultValue;
      pushIntokens(
         (Token){ .tp = tokInt, .pl1 = resultValue >> 32, .pl2 = resultValue & LOWER32BITS,
         .startBt = lx->i, .lenBts = j - lx->i }, lx);
   }
   lx->i = j; // CONSUME the decimal number
}

private void
lexNumber(SRC, LX) { //:lexNumber
   wrapInAStatement(lx->i, source, lx);
   Byte cByte = CURR_BT;
   if (lx->i == lx->stats.inpLength - 1 && isDigit(cByte)) {
      pushIntokens((Token){ .tp = tokInt, .pl2 = cByte - aDigit0,
            .startBt = lx->i, .lenBts = 1 }, lx);
      lx->i++; // CONSUME the single-digit number
      return;
   }

   Byte nByte = NEXT_BT;
   if (nByte == aXLower) {
      hexNumber(source, lx);
   } else {
      decNumber(false, source, lx);
   }
   lx->numeric.len = 0;
}

private void
openPunctuation(Unt tType, Unt spanLevel, Int startBt, LX) {
//:openPunctuation Adds a token which serves punctuation purposes, i.e. either a ( or  a [
// These tokens are used to define the structure, that is, nesting within the AST.
// Upon addition, they are saved to the backtracking stack to be updated with their length
// once it is known. Consumes no bytes
   add(((BtToken){ .tp = tType, .tokenInd = lx->tokens.len, .spanLevel = spanLevel}),
         lx->lexBtrack);
   pushIntokens((Token) {.tp = tType, .pl1 = (tType < firstScopeTokenType) ? 0 : spanLevel,
                    .startBt = startBt }, lx);
}

private void
lexIf(Unt reservedWordType, Int startBt, SRC, LX) { //:lexIf
   if (reservedWordType == tokElse) {
      openPunctuation(tokElse, slScope, startBt, lx);
   } else {
      openPunctuation(reservedWordType, slUnbraced, startBt, lx);
   }
}

private void
lexDef(Int startBt, SRC, LX) { //:lexDef
   openPunctuation(tokDef, slStmt, startBt, lx);
}

private void
lexFor(Int startBt, SRC, LX) { //:lexFor
   openPunctuation(tokFor, slUnbraced, startBt, lx);
}

private void
lexProcessSyntaxForm(Unt reservedWordType, Int startBt, SRC, LX) { //:lexProcessSyntaxForm
// Lexer action for a paren-type or statement-type syntax form.
// Precondition: we are looking at the character immediately after the keyword
// We must NOT consume any characters here - that's been done in {{wordInternal}}
   LBtToken* bt = lx->lexBtrack;
   if (reservedWordType >= tokIf && reservedWordType <= tokElse) {
      lexIf(reservedWordType, startBt, source, lx);
   } ei (reservedWordType == tokDef) {
      lexDef(startBt, source, lx);
   } ei (reservedWordType == tokFor)  {
      lexFor(startBt, source, lx);
   } ei (reservedWordType >= firstScopeTokenType) {
      // A reserved word must be the first inside parentheses, but parentheses are always
      // wrapped in statements, so we need to check the TWO last tokens and two top BtTokens
      VALIDATEL(bt->len >= 2 && last(bt).tp == tokParens
        && bt->c[bt->len - 2].tp == tokStmt, errCoreFormInappropriate)
      Int const indLastToken = lx->tokens.len - 1;
      VALIDATEL(lx->tokens.c[indLastToken].tp == tokParens
        && lx->tokens.c[indLastToken - 1].tp == tokStmt, errCoreFormInappropriate)
      lx->tokens.c[indLastToken - 1].tp = reservedWordType;
      lx->tokens.c[indLastToken - 1].pl1 = slScope;
      lx->tokens.len--;
      bt->c[bt->len - 2].tp = reservedWordType;
      bt->c[bt->len - 2].spanLevel = slScope;
      bt->len--;
      skipSpaces(source, lx);
   } ei (reservedWordType >= firstSpanTokenType) {
      VALIDATEL(bt->len == 0 || last(bt).spanLevel == slScope, errCoreNotInsideStmt)
      addStatementSpan(reservedWordType, startBt, lx);
   }
}

private Bool
wordChunk(SRC, LX) { //:wordChunk
// Lexes a single chunk of a word, i.e. the characters between two minuses (or the whole word
// if there are no minuses). Returns True if the lexed chunk was capitalized
   Bool result = false;
   checkPrematureEnd(1, lx);

   Byte currBt = CURR_BT;
   if (isCapitalLetter(currBt)) {
      result = true;
   } else VALIDATEL(isLowercaseLetter(currBt), errWordChunkStart)

   lx->i++; // CONSUME the first letter of the word
   while (lx->i < lx->stats.inpLength && isAlphanumeric(CURR_BT)) {
      lx->i++; // CONSUME alphanumeric characters
   }
   return result;
}

private void
mbCloseAssignRight(BtToken* top, CM) { //:mbCloseAssignRight
// Handles the case we are closing a tokAssignRight: we need to close its parent tokAssignment!
   if (top->tp != tokAssignRight)
      { return; }
   setStmtSpanLength(top->tokenInd, cm);
#ifdef SAFETY
   VALIDATEI(cm->lexBtrack->len > 0 &&
             (last(cm->lexBtrack).tp == tokAssignment || last(cm->lexBtrack).tp == tokDef),
           iErrorInconsistentSpans
   )
#endif
   *top = removeLast(cm->lexBtrack);
   setStmtSpanLength(top->tokenInd, cm);
}

private void
lxCloseFnDef(BtToken* top, CM) { //:lxCloseFnDef
// Handles the case we are closing a function definition: we need to close its parent tokAssignment!
   LBtToken* bt = cm->lexBtrack;
   setStmtSpanLength(top->tokenInd, cm);
   if (bt->len == 0 || last(bt).tp != tokAssignRight)
      { return; }
   *top = removeLast(bt); // the tokAssignRight
   setStmtSpanLength(top->tokenInd, cm);

#ifdef SAFETY
   VALIDATEI(bt->len > 0 && last(bt).tp == tokAssignment, iErrorInconsistentSpans)
#endif
   *top = removeLast(bt); // the tokAssignment
   setStmtSpanLength(top->tokenInd, cm);
}

private void //:closeStatement
closeStatement(LX) {
// Closes the current statement. Consumes no tokens
   BtToken top = last(lx->lexBtrack);
   VALIDATEL(top.spanLevel == slStmt, errPunctuationExtraOpening)
   setStmtSpanLength(top.tokenInd, lx);
   removeLast(lx->lexBtrack);
   mbCloseAssignRight(&top, lx);
}

private void //:wordNormal
wordNormal(Unt wordType, Int uniqueStringId, Int startBt, Int realStartBt,
         Bool wasCapitalized, SRC, LX) {
// RealStartBt is the word-initial "$", "." etc, startBt is the first letter of word
   Int lenBts = lx->i - realStartBt;
   Token newToken = (Token){ .tp = wordType, .pl1 = uniqueStringId,
         .startBt = realStartBt, .lenBts = lenBts };
   if (wordType == tokWord && wasCapitalized)
      { newToken.tp = tokTypeName; }
   if (lx->i < lx->stats.inpLength) {
      if (CURR_BT == aBracketLeft && wordType == tokWord) {
         openPunctuation(tokAccessor, slSubexpr, realStartBt, lx);
         pushIntokens(newToken, lx);
         openPunctuation(tokAccessIn, slSubexpr, lx->i, lx);
         lx->i++; // CONSUME the `[`
         return;
      } ei (CURR_BT == aApostrophe) {
         newToken.pl2 = 1;
         lx->i++; // CONSUME the `'`
      }
   }
   pushIntokens(newToken, lx);
}

private void //:wordReserved
wordReserved(Unt wordType, Int wordId, Int startBt, Int realStartBt, SRC, LX) {
   Int const keywordTp = standardKeywords[wordId];
   if (keywordTp < firstSpanTokenType) {
      if (keywordTp == keywTrue) {
         wrapInAStatement(startBt, source, lx);
         pushIntokens((Token){.tp=tokBool, .pl2=1, .startBt=realStartBt, .lenBts=4}, lx);
      } ei (keywordTp == keywFalse) {
         wrapInAStatement(startBt, source, lx);
         pushIntokens((Token){.tp = tokBool, .pl2=0, .startBt=realStartBt, .lenBts=5}, lx);
      } ei (keywordTp == keywBreak) {

         add(((BtToken){ .tp = tokBreakCont, .tokenInd = lx->tokens.len, .spanLevel = slStmt}),
               lx->lexBtrack);
         pushIntokens((Token) {.tp = tokBreakCont, .pl1 = 0, .startBt = realStartBt }, lx);
      } ei (keywordTp == keywContinue) {
         add(((BtToken){ .tp = tokBreakCont, .tokenInd = lx->tokens.len, .spanLevel = slStmt}),
               lx->lexBtrack);
         pushIntokens((Token) {.tp = tokBreakCont, .pl1 = 1, .startBt = realStartBt }, lx);
      }
   } else {
      lexProcessSyntaxForm(keywordTp, realStartBt, source, lx);
   }
}

private void
wordInternal(Unt wordType, SRC, LX) { //:wordInternal
// Lexes a word (both reserved and identifier) according to Eyr's rules.
// Precondition: we are pointing at the first letter character of the word (i.e. past the possible
// "." or ":")
// Examples of acceptable words: A:B:c:d, asdf123, ab:cd45
// Examples of unacceptable words: 1asdf23, ab:cd_45
   Int const startBt = lx->i;
   Bool wasCapitalized = wordChunk(source, lx);
   while (lx->i < (lx->stats.inpLength - 1)) {
      Byte currBt = CURR_BT;
      if (currBt == aColon) {
         Byte nextBt = NEXT_BT;
         if (isLetter(nextBt)) {
            lx->i++; // CONSUME the colon
            Bool isCurrCapitalized = wordChunk(source, lx);
            VALIDATEL(!wasCapitalized, errWordCapitalizationOrder)
            wasCapitalized = isCurrCapitalized;
         } else {
            break;
         }
      } else {
         break;
      }
   }

   Int const realStartBt = (wordType == tokWord) ? startBt : (startBt - 1);
   // accounting for the initial ".", ":" or other symbol
   Int lenString = lx->i - startBt;
   VALIDATEL(lenString <= maxWordLength, errWordLengthExceeded)
   Int stringId = addStringDict(source, startBt, lenString, lx->names, lx->stringDict);
   if (stringId - countOperators < strFirstNonReserved)  {
      wordReserved(wordType, stringId - countOperators, startBt, realStartBt, source, lx);
   } else {
      wrapInAStatement(realStartBt, source, lx);
      wordNormal(wordType, stringId, startBt, realStartBt, wasCapitalized, source, lx);
   }
}

private void
lexWord(SRC, LX) { //:lexWord
   wordInternal(tokWord, source, lx);
}

private void //:lexComma
lexComma(SRC, LX) {
   lx->i++;  // CONSUME the ",". Doing it at the start so that span will calc len right
   VALIDATEL(lx->lexBtrack->len > 1 && last(lx->lexBtrack).tp == tokClause,
           errPunctuationCommaNotClause);

   BtToken top = removeLast(lx->lexBtrack);
   setStmtSpanLength(top.tokenInd, lx);
}

private void //:lexDot
lexDot(SRC, LX) {
// The dot is a start of a field accessor (if glued to prev token) or a function call.
   VALIDATEL(lx->tokens.len > 0, errUnexpectedToken);
   lx->i++; // CONSUME the dot
   VALIDATEL(lx->i < lx->stats.inpLength && isLetter(CURR_BT), errPrematureEndOfInput)
   wordInternal(tokFieldAcc, source, lx);
}

private void //:lexSemicolon
lexSemicolon(SRC, LX) {
// The semicolon is the statement ender.
   lx->i++;  // CONSUME the ";". Doing it at the start so that span will calc len right
   if (lx->lexBtrack->len == 0)
      { return; }
   BtToken top = last(lx->lexBtrack);
   VALIDATEL(top.spanLevel != slSubexpr, errPunctuationOnlyInMultiline);
   if (top.spanLevel == slStmt) {
      closeStatement(lx);
   }
}

private void
lexAssignment(Int const opType, LX) { //:lexAssignment
// Params: opType is the operator for mutations (like `*=`), -1 for normal assignments.
// Handles the "=", and "+=" tokens (for the latter, inserts the operator and duplicates the
// tokens from the left side). Changes existing stmt token into tokAssignment and opens up a new
// tokAssignRight span. Doesn't consume anything
   BtToken currSpan = last(lx->lexBtrack);
   VALIDATEL(currSpan.tp == tokStmt || currSpan.tp == tokDef, errOperatorAssignmentPunct);

   Int assignmentStartInd = currSpan.tokenInd;
   Token* tok = (lx->tokens.c + assignmentStartInd);
   if (currSpan.tp == tokStmt) {
      tok->tp = tokAssignment;
      lx->lexBtrack->c[lx->lexBtrack->len - 1].tp = tokAssignment;
   } else {
      VALIDATEL(opType == -1, errOperatorMutationInDef)
      if (lx->tokens.c[assignmentStartInd + 1].tp == tokTypeName){
         // type definition
         tok->pl1 = assiTypeDefinition;
      }
   }

   openPunctuation(tokAssignRight, slStmt, lx->i, lx);
   if (opType > -1) { // mutation
      // -2 because we've already opened the right side span
      Int const countLeftSide = lx->tokens.len - assignmentStartInd - 2;
      ensureCapacityTokens(countLeftSide + 1, lx); // + 1 for the operator

      memcpy(lx->tokens.c + lx->tokens.len,
            lx->tokens.c + assignmentStartInd + 1, countLeftSide*sizeof(Token));
      lx->tokens.len += countLeftSide;
      pushIntokens((Token){ .tp = tokOperator, .pl1 = opType,
               .pl2 = 0, .startBt = lx->i, .lenBts = (OPERATORS[opType].name >> 24)}, lx);
   }
}

private void
lexOperator(SRC, LX) { //:lexOperator
   wrapInAStatement(lx->i, source, lx);

   Byte firstSymbol = CURR_BT;
   Byte secondSymbol = (lx->stats.inpLength > lx->i + 1) ? source[lx->i + 1] : 0;
   Byte thirdSymbol = (lx->stats.inpLength > lx->i + 2) ? source[lx->i + 2] : 0;
   Int k = 0;
   Int opType = -1; // corresponds to the op... operator types
   while (k < countOperators && OPERATORS[k].firstSymbol < firstSymbol) {
      k++;
   }
   while (k < countOperators && OPERATORS[k].firstSymbol == firstSymbol) {
      NameLoc opName = OPERATORS[k].name;
      char const* opByte = source + (opName & LOWER24BITS) + 1;
      char const* sentinel = opByte + (opName >> 24) - 1;
      if (opByte == sentinel)  {
         opType = k;
         break;
      } ei (*opByte != secondSymbol) {
         k++;
         continue;
      }
      opByte++;
      if (opByte == sentinel)  {
         opType = k;
         break;
      } ei (*opByte != thirdSymbol) {
         k++;
         continue;
      }
      opType = k;
      break;
   }
   VALIDATEL(opType > -1, errOperatorUnknown)

   OpDef opDef = OPERATORS[opType];
   bool isAssignment = false;

   Int lengthOfOper = opDef.name >> 24;
   Int j = lx->i + lengthOfOper;
   if (opDef.assignable && j < lx->stats.inpLength && source[j] == aEqual) {
      isAssignment = true;
      j++;
   }
   if (isAssignment) { // mutation operators like "*=" or "*.="
      lexAssignment(opType, lx);
   } else {
      pushIntokens((Token){ .tp = tokOperator, .pl1 = opType, .pl2 = opDef.prec,
         .startBt = lx->i, .lenBts = j - lx->i}, lx);
   }
   lx->i = j; // CONSUME the operator
}

private void //:lexDollar
lexDollar(SRC, LX) {
// Handles type variables and ordinary mutable variables
   if (lx->i < lx->stats.inpLength - 1 && isCapitalLetter(NEXT_BT)) {
      lx->i++; // CONSUME the "$"
      wordInternal(tokTypeVar, source, lx);
   } else {
      lexOperator(source, lx);
   }
}

private void //:lexEqual
lexEqual(SRC, LX) {
// The humble "=" can be the definition statement or a comparison "=="
   checkPrematureEnd(2, lx);
   Byte nextBt = NEXT_BT;
   if (nextBt == aEqual || nextBt == aDigit0) {
      lexOperator(source, lx); // == or =0
   } else {
      lexAssignment(-1, lx);
      lx->i++; // CONSUME the =
   }
}

private void //:lexUnderscore
lexUnderscore(SRC, LX) {
   if ((lx->i < lx->stats.inpLength - 1) && NEXT_BT == aUnderscore) {
      pushIntokens((Token){ .tp = tokMisc, .pl1 = miscUnderscore, .pl2 = 2,
                .startBt = lx->i - 1, .lenBts = 2 }, lx);
      lx->i += 2; // CONSUME the "__"
   } else {
      pushIntokens((Token){ .tp = tokMisc, .pl1 = miscUnderscore, .pl2 = 1,
                .startBt = lx->i - 1, .lenBts = 2 }, lx);
      lx->i++; // CONSUME the "_"
   }
}

private void //:lexNewline
lexNewline(SRC, LX) {
   pushInnewlines(lx->i, lx);

   lx->i++;    // CONSUME the LF
   while (lx->i < lx->stats.inpLength) {
      if (!isSpace(CURR_BT))
         { break; }
      lx->i++; // CONSUME a space or tab
   }
}

private void //:lexComment
lexComment(SRC, LX) {
// Eyr separates between documentation comments (which live in meta info and are
// spelt as "meta(`comment`)") and comments for, well, eliding text from code;
// Elision comments are of the "//" form.
   lx->i += 2; // CONSUME the "//"

   for (;lx->i < lx->stats.inpLength - 1 && CURR_BT != aNewline; lx->i++) {
      // CONSUME the comment
   }
}

private void //:lexMinus
lexMinus(SRC, LX) {
// Handles the binary operator as well as the unary negation operator
   VALIDATEL(lx->i < lx->stats.inpLength - 1, errPrematureEndOfInput)
   Byte nextBt = NEXT_BT;
   if (isDigit(nextBt)) {
      wrapInAStatement(lx->i, source, lx);
      decNumber(true, source, lx);
      lx->numeric.len = 0;
   } ei (nextBt == aSpace) {
      pushIntokens((Token){ .tp = tokOperator, .pl1 = opMinus, .pl2 = OPERATORS[opMinus].prec,
                            .startBt = lx->i, .lenBts = 1 }, lx);
      lx->i += 2; // CONSUME the "-" and the space
   } ei (nextBt == aColon || nextBt == aEqual || nextBt == aMinus) {
      lexOperator(source, lx);
   } else {
      pushIntokens((Token){ .tp = tokOperator, .pl1 = opNegate, .pl2 = OPERATORS[opNegate].prec,
                            .startBt = lx->i, .lenBts = 1 }, lx);
      lx->i++; // CONSUME the "-"
   }
}

private void
lexDivBy(SRC, LX) { //:lexDivBy
// Handles the binary operator as well as the comments
   if (lx->i + 1 < lx->stats.inpLength && NEXT_BT == aDivBy) {
      lexComment(source, lx);
   } else {
      lexOperator(source, lx);
   }
}

private void
lexParenLeft(SRC, LX) { //:lexParenLeft
   Int j = lx->i + 1;
   VALIDATEL(j < lx->stats.inpLength, errPunctuationExtraOpening)
   wrapInAStatement(lx->i, source, lx);
   openPunctuation(tokParens, slSubexpr, lx->i, lx);
   lx->i++; // CONSUME the left parenthesis
}

private void //:lexParenRight
lexParenRight(SRC, LX) {
// A closing parenthesis may close the following configurations of lexer backtrack:
// 1. [scope stmt] - if it's just a scope nested within another scope or a function
// 2. [coreForm stmt] - eg. if it's closing the function body
// 3. [if else/elseIf stmt]
// 4. [if else/elseIf ]
   LBtToken* bt = lx->lexBtrack;
   VALIDATEL(bt->len > 0, errPunctuationExtraClosing)
   BtToken top = removeLast(bt);

   VALIDATEL(top.spanLevel == slSubexpr, errPunctuationUnmatched)
   mbCloseAssignRight(&top, lx);

   setSpanLengthLexer(top.tokenInd, lx);
   lx->i++; // CONSUME the closing ")"
}


private void //:lexFn
lexFn(SRC, LX) {
   if (lx->lexBtrack->len > 0) {
      BtToken top = last(lx->lexBtrack);
      VALIDATEL(top.spanLevel == slStmt, errPunctuationFnNotInStmt)
   }

   openPunctuation(tokFn, slScope, lx->i, lx);
   openPunctuation(tokFnParams, slClauseList, lx->i + 1, lx);
   lx->i += 2; // CONSUME the "{{"
}

private void
lexCurlyLeft(SRC, LX) { //:lexCurlyLeft
// Handles scope openings and decorative braces in "if" and "for" forms
   if (NEXT_BT == aCurlyLeft) {
      lexFn(source, lx);
      return;
   }
   if (lx->lexBtrack->len > 0) {
      BtToken const top = last(lx->lexBtrack);
      if (top.spanLevel == slStmt) {
         // process the first curly brace in an "if ... {" form. If all is right,
         // updates its span level to slScope, so further curly braces work as usual
         Int const len = lx->lexBtrack->len;
         VALIDATEL(len > 1 && lx->lexBtrack->c[len - 2].spanLevel == slUnbraced,
                 errPunctuationScope)
         removeLast(lx->lexBtrack); // pop the top statement (if cond) because it's over
         setStmtSpanLength(top.tokenInd, lx);
         BtToken const second = last(lx->lexBtrack);
         lx->lexBtrack->c[len - 2].spanLevel = slScope;
         lx->tokens.c[second.tokenInd].pl1 = slScope;
         goto consumption;
      } ei (top.tp == tokElse) {
         goto consumption;
      } ei (top.tp == tokFor) {
         if (top.spanLevel == slUnbraced) {
            // the first curly brace inside "for" (for init, cond, step)
            lx->lexBtrack->c[lx->lexBtrack->len - 1].spanLevel = slSingleBraced;
            lx->tokens.c[top.tokenInd].pl1 = slSingleBraced;
         } ei (top.spanLevel == slSingleBraced) {
            // the second curly brace inside "for" (for body)
            lx->lexBtrack->c[lx->lexBtrack->len - 1].spanLevel = slScope;
            lx->tokens.c[top.tokenInd].pl1 = slScope;
            goto consumption;
         }
      }
   }
   openPunctuation(tokScope, slScope, lx->i, lx);
   consumption:
   lx->i++; // CONSUME the "{"
}

private void //:lexCurlyRight
lexCurlyRight(SRC, LX) {
   LBtToken* bt = lx->lexBtrack;
   VALIDATEL(bt->len > 0, errPunctuationExtraClosing)
   BtToken top = removeLast(bt);

   VALIDATEL(top.spanLevel == slScope || top.tp == tokFnParams, errPunctuationUnmatched)
   setSpanLengthLexer(top.tokenInd, lx);
   lx->i++; // CONSUME the "}"
}

private void //:lexBracketLeft
lexBracketLeft(SRC, LX) {
   wrapInAStatement(lx->i, source, lx);
   openPunctuation(tokData, slSubexpr, lx->i, lx);
   lx->i++; // CONSUME the `[`
}

private void //:lexBracketRight
lexBracketRight(SRC, LX) {
   LBtToken* bt = lx->lexBtrack;
   VALIDATEL(bt->len > 0, errPunctuationExtraClosing)
   BtToken top = removeLast(bt);
   VALIDATEL(top.tp == tokData || top.tp == tokAccessIn, errPunctuationUnmatched)

   setSpanLengthLexer(top.tokenInd, lx);

   if (lx->i + 1 < lx->stats.inpLength && NEXT_BT == aBracketLeft) { // `a[i][j]`
      openPunctuation(tokAccessIn, slSubexpr, lx->i + 1, lx);
      lx->i++; // CONSUME the `]` so the `[` will be consumed in this fn
   } else if (bt->len > 0 && last(bt).tp == tokAccessor) {
      top = removeLast(bt);
      setSpanLengthLexer(top.tokenInd, lx);
   }
   lx->i++; // CONSUME the closing `]` or opening `[`
}

private void
lexSpace(SRC, LX) { //:lexSpace
   lx->i++; // CONSUME the space
   while (lx->i < lx->stats.inpLength && isSpace(CURR_BT)) {
      lx->i++; // CONSUME a space
   }
}

private void
lexStringLiteral(SRC, LX) { //:lexStringLiteral
   wrapInAStatement(lx->i, source, lx);
   Int j = lx->i + 1;
   for (; j < lx->stats.inpLength && source[j] != aBacktick; j++);
   VALIDATEL(j != lx->stats.inpLength, errPrematureEndOfInput)
   pushIntokens((Token){.tp=tokString, .startBt=(lx->i), .lenBts=(j - lx->i + 1)}, lx);
   lx->i = j + 1; // CONSUME the string literal, including the closing quote character
}

private void
lexUnexpectedSymbol(SRC, LX) { //:lexUnexpectedSymbol
   throwExcLexer(errUnrecognizedByte);
}

private void
lexNonAsciiError(SRC, LX) { //:lexNonAsciiError
   throwExcLexer(errNonAscii);
}

private void
tabulateLexer() { //:tabulateLexer
   LexerFn* p = LEX_TABLE;
   for (Int i = 0; i < 128; i++) {
      p[i] = &lexUnexpectedSymbol;
   }
   for (Int i = 128; i < 256; i++) {
      p[i] = &lexNonAsciiError;
   }
   for (Int i = aDigit0; i <= aDigit9; i++) {
      p[i] = &lexNumber;
   }

   for (Int i = aALower; i <= aZLower; i++) {
      p[i] = &lexWord;
   }
   for (Int i = aAUpper; i <= aZUpper; i++) {
      p[i] = &lexWord;
   }
   p[aComma] = &lexComma;
   p[aDot] = &lexDot;
   p[aSemicolon] = &lexSemicolon;
   p[aEqual] = &lexEqual;
   p[aUnderscore] = &lexUnderscore;

   for (Int i = sizeof(operatorStartSymbols)/4 - 1; i > -1; i--) {
      p[operatorStartSymbols[i]] = &lexOperator;
   }
   p[aMinus] = &lexMinus; // to handle literal negation
   p[aParenLeft] = &lexParenLeft;
   p[aParenRight] = &lexParenRight;
   p[aCurlyLeft] = &lexCurlyLeft;
   p[aCurlyRight] = &lexCurlyRight;
   p[aBracketLeft] = &lexBracketLeft;
   p[aBracketRight] = &lexBracketRight;
   p[aDivBy] = &lexDivBy; // to handle the comments "//"
   p[aDollar] = &lexDollar; // to handle type variables "$a"

   p[aSpace] = &lexSpace;
   p[aNewline] = &lexNewline; // to make the newline a statement terminator sometimes
   p[aBacktick] = &lexStringLiteral;
   //return result;
}

void //:populateStringOffsets
populateStringOffsets(Arr(Byte const) stringLens, Int start, Int len, OUT Arr(Int) offsets) {
   Int curr = start;
   for (Int j = 0; j < len; j++) {
      offsets[j] = curr;
      curr += stringLens[j];
   }
}

//}}}
//}}}
//{{{ Parser
//{{{ Parser consts

// how to emit various names during codegen
#define emitPrefix         1  // normal native names
#define emitHostPrefix     2  // prefix names that are emitted differently than in source code
#define emitInfix          3  // infix operators that match between source code and target (e.g.
                              // arithmetic operators)
#define emitHostInfix      4  // infix operators that have a separate external name
#define emitField          5  // emitted as field accesses, like ".length"
#define emitNone           6

// host string constants
#define hostFunction  0
#define hostElse      1
#define hostConst     2
#define hostLet       3
#define hostLo        4
#define hostNew       5
#define hostArray     6
#define hostPrint     7
#define hostAdd       8
#define hostLength    9
#define hostAbs      10

//}}}
//{{{ Parser utils


#define VALIDATEP(cond, errMsg) if (!(cond)) { throwExcParser0(errMsg, __LINE__, cm); }

private TypeId exprUpTo(Int sentinelToken, SourceLoc loc, TOKS, CM);
private void eClose(Expr* s, CM);
private void addBinding(NameId nameId, Int bindingId, Compiler* cm);
private void mbCloseSpans(CM);
//private FunctionId importActivateEntity(Function ent, CM);
private void createBuiltins(Compiler* cm);
private Compiler* createLexer(String sourceCode, Bool prependStandard, Arena* a);
private void eParse(Int sentinel, TOKS, CM);
private TypeId exprHeadless(Int sentinel, SourceLoc loc, TOKS, CM);
private TypeId pExprWorker(Token tk, TOKS, CM);

#define TYPE_CREATE_START(typeHeader) TypeId const tentativeType = typeOf(cm->types.len);\
       pushIntypes(0, cm);\
       typeAddHeader(typeHeader, cm)

#define TYPE_CREATE_END cm->types.c[tentativeType.v] = cm->types.len - tentativeType.v - 1

_Noreturn private void
throwExcParser0(char const errMsg[], Int lineNumber, CM) {
   cm->wasError = true;
#ifdef DEBUG
   printf("Error on i = %d line %d\n", cm->i, lineNumber);
#endif
   cm->errMsg = str(errMsg);
   longjmp(excBuf, 1);
}

#define throwExcParser(errMsg) throwExcParser0(errMsg, __LINE__, cm)

private SourceLoc
locOf(Token tk) { return (SourceLoc){.startBt = tk.startBt, .lenBts = tk.lenBts}; }

private Node //:getNodVarForName
getNodVarForName(NameId name, CM) {
// Resolves an active binding, throws if it's not active
   Int rawValue = cm->activeBindings[name];
   VALIDATEP(rawValue > -1 && rawValue < BIG, errUnknownBinding)
   Var v = cm->vars.c[rawValue];
   if (v.fnId == -1) {
      return (Node){ .tp = nodVar, .pl1 = rawValue, .pl2 = 0, .pl3 = 0 };
   } else {
      return (Node){ .tp = nodVar, .pl1 = rawValue, .pl2 = v.fnId, .pl3 = assiFnVarUse };
   }
}

private VarId //:createVar
createVar(NameId name, Byte class, FunctionId fnId, CM) {
// Validates a new binding (that it is unique), creates a Var for it & adds it to the current scope
// "fnId" should be -1 for ordinary (non-function) local vars
// Consumes no nodes
   Int mbBinding = cm->activeBindings[name];
   // if it's a binding, it should be -1, and if overload, < -1
   if (mbBinding > -1) {
      print("ERR name %d mbBind %d i %d", name, mbBinding, cm->i)
      dbgScopes(cm);
   }
   VALIDATEP(mbBinding < 0, errAssignmentShadowing)

   VarId newVarId = cm->vars.len;
   pushInvars(((Var){ .name = name, .class = class, .fnId = fnId }), cm);
   if (name > -1) // nameId == -1 only for the built-in operators
      { addBinding(name, newVarId, cm); }
   return newVarId;
}

private VarId //:createVarWithType
createVarWithType(NameId name, TypeId typeId, Byte class, FunctionId fnId, CM) {
   VarId newVarId = createVar(name, class, fnId, cm);
   cm->vars.c[newVarId].typeId = typeId;
   return newVarId;
}

private Int //:calcSentinel
calcSentinel(Token tok, Int tokInd) {
// Calculates the sentinel token for a token at a specific index
   return (tok.tp >= firstSpanTokenType ? (tokInd + tok.pl2 + 1) : (tokInd + 1));
}

Int //:calcNodeSentinel
calcNodeSentinel(Node nd, Int nodeInd) {
// Calculates the sentinel token for a token at a specific index
   return (nd.tp >= nodScope ? (nodeInd + nd.pl2 + 1) : (nodeInd + 1));
}

void //:newNode
newNode(Node node, SourceLoc loc, CM) {
   pushInast(node, cm);
   add(loc, cm->sourceLocs);
}

private void //:eOperatorCall
eOperatorCall(Token tok, Int precedence, Bool isVarCall, CM) {
// Pushes a call to the temporary lists during expression parsing
   Expr* e = cm->expr;
   VALIDATEP(e->frames->len > 0, errExpressionError)
   ExprFrame frame = last(e->frames);

   if (frame.tp == exfrParen) { // for infix operators
      VALIDATEP(frame.argCount == 1, errExpressionWrongArgCount)
   } else {
      if (frame.tp == exfrCall) {
         // Pop all calls with same or higher precedence. This is where precedence is useful
         for (;
              e->frames->len > 0 && frame.tp == exfrCall && frame.precedence >= precedence;
              frame = last(e->frames)
         ) {
            frame = removeLast(e->frames);
            eWriteCallToScratch(frame, e);
         }
      }
   }

   add(((ExprFrame) {
         // argCount = 1 because if this call were the first, we would be in the branch with the
         // exfrParen. This call isn't the first, so what came before constitutes its first arg
         .tp = exfrCall, .name = tok.pl1, .sentinel = frame.sentinel, .precedence = precedence,
         .argCount = 1, .loc = locOf(tok), .isVarCall = isVarCall
      }),
      e->frames
   );
}

private void //:eSaveNodes
eSaveNodes(Int startInd, LNode* scr, LSourceLoc* locsScr, CM) {
// Pushes the tail of scratch space (from a specified index onward) into the main AST
   Int const pushCount = scr->len - startInd;
   if (pushCount == 0)
      { return; }
   if (cm->ast.len + pushCount + 1 < cm->ast.cap) {
      memcpy((Node*)(cm->ast.c) + (cm->ast.len), scr->c + startInd,
             pushCount*sizeof(Node));
      memcpy((SourceLoc*)(cm->sourceLocs->c) + (cm->sourceLocs->len),
             locsScr->c + startInd,
             pushCount*sizeof(SourceLoc));
   } else {
      Int const newCap = 2*(cm->ast.cap) + pushCount;
      Arr(Node) newContent = allocateArray(newCap, Node, cm->a);
      memcpy(newContent, cm->ast.c + startInd, cm->ast.len*sizeof(Node));
      memcpy((Node*)(newContent) + (cm->ast.len),
            scr->c + startInd,
            pushCount*sizeof(Node));
      cm->ast.cap = newCap;
      cm->ast.c = newContent;

      Arr(SourceLoc) newLocs = allocateArray(newCap, SourceLoc, cm->a);
      memcpy(newLocs, cm->sourceLocs->c + startInd, pushCount*sizeof(SourceLoc));
      memcpy((SourceLoc*)(newLocs) + (cm->sourceLocs->len), locsScr->c + startInd,
            pushCount*sizeof(SourceLoc));
      cm->sourceLocs->cap = newCap;
      cm->sourceLocs->c = newLocs;
   }
   cm->ast.len += pushCount;
   cm->sourceLocs->len += pushCount;
}

void //:scopesMoveForward
scopesMoveForward(Scopes* restrict s, CM) {
   s->curr++;
   if (s->curr - s->currChunk->c == SCOPE_CHUNK_SZ) {
      if (!(s->currChunk->next)) {
         s->currChunk->next = allocateOnArena(sizeof(ScopeChunk), cm->aTmp);
         s->currChunk->next->prev = s->currChunk;
      }
      s->currChunk = s->currChunk->next;
      s->curr = s->currChunk->c;
   }
}

void //:scopesMoveBackward
scopesMoveBackward(Scopes* restrict s, CM) {
   if (s->curr == s->currChunk->c) {
      if (!s->currChunk->prev)
         { print("Setting to NULL") }

      s->currChunk = s->currChunk->prev;
      s->curr = s->currChunk->c + SCOPE_CHUNK_SZ - 1;
   } else {
      s->curr--;
   }
}

void //:rewindLexicalScope
rewindLexicalScope(CM) {
   Scopes* const s = &(cm->scopes);
   // rewind curr
   for (; s->curr != s->start; scopesMoveBackward(s, cm)) {
      cm->activeBindings[*(s->curr)] = -1;
   }

   // rewind start
   Int const lenPrev = *(s->start);
   s->currLen = lenPrev;
   ScopeChunk* backChunk = s->currChunk;
   for (Int j = -1; j < lenPrev; j++, s->start--) {
      if (s->start == backChunk->c) {
         if (backChunk->prev) {
            backChunk = backChunk->prev;
            s->start = backChunk->c + SCOPE_CHUNK_SZ;
         }
      }
   }
   scopesMoveBackward(s, cm);
}

void //:scopesNewLexicalScope
scopesNewLexicalScope(CM) {
   Scopes* const s = &(cm->scopes);
   scopesMoveForward(s, cm);

   *(s->curr) = s->currLen; // length of the old scope
   s->currLen = 0;
   s->start = s->curr;
}

private void //:updateStats
updateStats(Compiler* restrict cm) {
   cm->stats.toksLen = cm->tokens.len;
   cm->stats.astLen = cm->ast.len;
   cm->stats.typesLen = cm->types.len;
}

//}}}
//{{{ Forward decls

private TypeId pTypeDef(TOKS, CM);

#ifdef DEBUG
void printIntArrayOff(Int startInd, Int count, Arr(Int) arr);
#endif

//}}}

private void //:openParsedScope
openParsedScope(Int sentinelToken, Node nd, SourceLoc loc, CM) {
// Performs coordinated insertions to start a scope within the parser
   add(((ParseFrame){
      .level = nd.tp == nodFor ? pfrLoop : pfrScope,
      .startNodeInd = cm->ast.len,
      .sentinel = sentinelToken,
      .typeId = nd.tp == nodFor ? nd.pl1 : 0
      }), cm->backtrack
   );
   scopesNewLexicalScope(cm);
   newNode(nd, loc, cm);
}

private void //:openFnScope
openFnScope(Int funcOrMonoId, TypeId fnType, Byte callSort, SourceLoc loc, Int sentinel, CM) {
// Performs coordinated insertions to start a function definition
   add(((ParseFrame){
      .level = pfrFn, .startNodeInd = cm->ast.len, .sentinel = sentinel,
      .typeId = fnType }), cm->backtrack);
   scopesNewLexicalScope(cm); // a function body is also a lexical scope
   newNode((Node){ .tp = nodFnDef, .pl1 = funcOrMonoId, .pl3 = callSort}, loc, cm);
}

private void //:pMisc
pMisc(Token tok, TOKS, CM) {
}

private void //:pScope
pScope(Token tok, TOKS, CM) {
   openParsedScope(cm->i + tok.pl2, (Node){.tp = nodScope}, locOf(tok), cm);
}

private void //:parseTry
parseTry(Token tok, TOKS, CM) {
   throwExcParser(errTemp);
}

private void //:ifOpenSpan
ifOpenSpan(Unt tp, Int sentinel, Int ifcl, SourceLoc loc, CM) {
   add(((ParseFrame){
      .level = pfrScope, .startNodeInd = cm->ast.len, .sentinel = sentinel }), cm->backtrack
   );
   scopesNewLexicalScope(cm);
   newNode((Node){ .tp = tp, .pl3 = ifcl }, loc, cm);
}

private Int //:pIfDetermineSentinel
pIfDetermineSentinel(Int ifSentinel, TOKS, CM) {
    Int j = ifSentinel;
    Int const toksLen = cm->stats.toksLen;
    for (; j < toksLen && toks[j].tp == tokElseIf; j += (toks[j].pl2 + 1)) {}
    if (j < toksLen && toks[j].tp == tokElse)    { j += (toks[j].pl2 + 1); }
    return j;
}

private void //:pElse
pElse(Token tok, TOKS, CM) {
// "Else" is a special case of "ElseIf" marked with .pl3 = 0
   mbCloseSpans(cm);
   Int const ifSentinel = cm->i + tok.pl2;
   ifOpenSpan(nodIfClause, ifSentinel, ifclElse, locOf(tok), cm);
}

private void //:pIfClause
pIfClause(Token tok, Int ifcl, TOKS, CM) {
   Int const clauseSentinel = cm->i + tok.pl2;
   ifOpenSpan(nodIfClause, clauseSentinel, ifcl, locOf(tok), cm);

   // The condition
   Token stmtTok = toks[cm->i];
   cm->i++; // CONSUME the stmt token
   TypeId typeLeft = pExprWorker(stmtTok, toks, cm);
   VALIDATEP(eq(typeLeft, boolTy), errTypeMustBeBool)
   mbCloseSpans(cm);
}

private void //:pElseIf
pElseIf(Token tok, TOKS, CM) {
   pIfClause(tok, ifclElseIf, toks, cm);
}

private void //:pIf
pIf(Token tok, TOKS, CM) {
// Parses and "if" expression with any following else-if clauses and an ending else clause.
   Int const firstClauseSentinel = calcSentinel(tok, cm->i - 1);
   Int const ifSentinel = pIfDetermineSentinel(firstClauseSentinel, toks, cm);

   ifOpenSpan(nodIf, ifSentinel, 0, locOf(tok), cm);
   pIfClause(tok, ifclIf, toks, cm);
}

private void //:pAssignmentFnVar
pAssignmentFnVar(Assignment assignment, Token leftNameTk, TypeId leftType, CM) {
// Resolution of an overloaded function into a local var.
// Validates that the right side consists of one word
   VALIDATEP(assignment.rightTokenInd + 2 == assignment.sentinel, errAssignmentToFunctionVar)
   Token rightTk = cm->tokens.c[assignment.rightTokenInd + 1];
   NameId fnName = rightTk.pl1;

   FunctionId fnId = findOverload(fnName, typeGetGenericParam(leftType, 0, cm), cm);
   NameId varName = leftNameTk.pl1;
   VarId varId = createVarWithType(
      varName, cm->functions.c[fnId].typeId,
      (leftNameTk.pl2 == 1 ? classMutable : classImm), fnId, cm
   );
   newNode((Node){ .tp = nodVar, .pl1 = varId, .pl2 = fnId, .pl3 = assiFnVarDef },
      locOf(leftNameTk), cm);
}

private TypeId //:pAssignmentLeftAccessors
pAssignmentLeftAccessors(Token firstTok, Int sentinel, TOKS, CM) {
// Complex left side in an assignment like `a[i][j] = ...`.
// It gets transformed like this:
// arr[i][j*2][k + 3] ==> arr i .getElem j 2 *(2) .getElem k 3 +(2) .getElemPtr
   LInt* sc = cm->expr->exp;
   sc->len = 0;
   Int const startBt = firstTok.startBt;
   Int const lastBt = toks[cm->i - 1].startBt + toks[cm->i - 1].lenBts;
   SourceLoc loc = (SourceLoc){.startBt = startBt, .lenBts = lastBt - startBt};

   VALIDATEP(toks[cm->i + 1].tp == tokWord, errAssignmentLeftSide)
   for (Int j = cm->i + 2; j < sentinel; ){
      Token accessorTk = toks[j];
      VALIDATEP(accessorTk.tp == tokAccessIn, errAssignmentLeftSide)
      j = calcSentinel(accessorTk, j);
      add(j, sc);
   }

   TypeId leftType = exprUpToWithFrame((ParseFrame){
      .level = 0, .startNodeInd = cm->ast.len, .sentinel = sentinel }, loc, toks, cm
   );

   Int lastNodeInd = cm->ast.len - 1;
   Node lastNode = cm->ast.c[lastNodeInd];
   if (lastNode.tp == nodCall)  {
#ifdef SAFETY
      VALIDATEI(lastNode.pl1 == opGetElem, iErrorArrayElemButShouldBePtr)
#endif
      cm->ast.c[lastNodeInd].pl1 = opGetElemPtr;
   }
   return leftType;
}

private TypeId //:pAssignmentLeftWithType
pAssignmentLeftWithType(Token firstTok, Assignment assignment, Int sentinel, OUT Bool* isAFnVar,
      TOKS, CM) {
// Typechecks a complex left side like `x Foo Int = ...` in an assignment, consumes tokens,
// inserts nodes. Returns the type of the left side.
// Precondition: we are looking right past tokDef or tokAssignment.
   LInt* sc = cm->expr->exp;
   sc->len = 0;
   Token nextTk = toks[cm->i + 1]; // +1 is safe because we know left side is long
   // when the left side is a var definition with its type declared
   cm->tExpr->isGeneric = false;
   TypeId leftType = teClause(cm->tExpr, sentinel, toks, cm);
   VALIDATEP(!cm->tExpr->isGeneric, errTypePolymorphicAssignment)
   if (nextTk.pl1 == nameOfStandard(strF)) {
      pAssignmentFnVar(assignment, firstTok, leftType, cm);
      *isAFnVar = true;
      cm->i = assignment.sentinel; // CONSUME the whole assignment
   } else {
      VarId varId = createVarWithType(
         assignment.name, leftType, (firstTok.pl2 == 1 ? classMutable : classImm), -1, cm
      );
      newNode((Node){ .tp = nodVar, .pl1 = varId, .pl2 = 0, .pl3 = assiVarAssignment },
         locOf(firstTok), cm);
   }
   return leftType;
}

private TypeId //:pAssignmentRight
pAssignmentRight(TypeId leftType, Token rightTk, Int sentinel, TOKS, CM) {
// The right side of an assignment
   if (rightTk.tp == tokFn) {
      return ZERO_ARITY_TYPE;
   } else {
      TypeId rightType = exprUpToWithFrame((ParseFrame){
        .level = 0, .startNodeInd = cm->ast.len, .sentinel = sentinel }, locOf(rightTk), toks, cm
      );
      VALIDATEP(rightType.v != -2, errAssignment)
      return rightType;
   }
}

private void //:pAssignmentWorker
pAssignmentWorker(Token tok, Assignment assignment, TOKS, CM) {
   Unt const tp = (tok.tp == tokDef) ? nodDef : nodAssignment;
   TypeId leftType = ZERO_ARITY_TYPE;
   Int const countLeftSide = assignment.rightTokenInd - assignment.nameTokenInd;
   Token rightTk = toks[assignment.rightTokenInd];
   VALIDATEP(assignment.rightTokenInd < assignment.sentinel && rightTk.pl2 > 0,
           errAssignmentEmptyRight)

   VarId varId = -1;
   Int const assignmentNodeInd = cm->ast.len;
   add(((ParseFrame){
      .level = 0, .startNodeInd = assignmentNodeInd, .sentinel = assignment.sentinel}),
      cm->backtrack
   );
   newNode((Node){ .tp = tp}, locOf(tok), cm);

   Token firstTok = toks[cm->i];
   if (countLeftSide == 1)  {
      varId = cm->activeBindings[assignment.name];
      Byte assiSort = assiVarAssignment;
      if (varId > -1) {
         VALIDATEP(cm->vars.c[varId].class == classMutable, errCannotMutateImmutable)
         leftType = cm->vars.c[varId].typeId;
         if (tIsFunction(leftType, cm) > -1) { // reassignment of a function var
            NameId fnName = cm->tokens.c[assignment.rightTokenInd + 1].pl1;
            FunctionId newFnId = findOverload(fnName, typeGetGenericParam(leftType, 0, cm), cm);
            cm->vars.c[varId].fnId = newFnId;
            cm->i = assignment.sentinel;
            goto closeSpans;
         }
         assiSort = assiReassignment;
      } else {
         varId = createVar(assignment.name, firstTok.pl2 == 1 ? classMutable : classImm, -1, cm);
      }
      newNode((Node){ .tp = nodVar, .pl1 = varId, .pl2 = 0, .pl3 = assiSort },
              locOf(firstTok), cm);
   } else if (firstTok.tp == tokAccessor) {
      leftType = pAssignmentLeftAccessors(firstTok, assignment.rightTokenInd, toks, cm);
   } else {
      Bool isAFnVar = false;
      leftType = pAssignmentLeftWithType(firstTok, assignment, cm->i + countLeftSide,
            OUT &isAFnVar, toks, cm);
      if (isAFnVar) {
         { goto closeSpans; }
      }
   }

   cm->i = assignment.rightTokenInd + 1; // CONSUME everything up to body of right side
   cm->ast.c[assignmentNodeInd].pl3 = cm->ast.len - assignmentNodeInd;

   TypeId const rightType = pAssignmentRight(leftType, rightTk, assignment.sentinel, toks, cm);
   if (varId > -1 && rightType.v > -1 && eq(leftType, ZERO_ARITY_TYPE)) {
      cm->vars.c[varId].typeId = rightType; // inferring the type of left binding
   } ei (leftType.v > -1 && rightType.v > -1) {
      VALIDATEP(eq(leftType, rightType), errTypeMismatch)
   }
closeSpans:
   mbCloseSpans(cm);
}

private Assignment //:pPreparseAssignment
pPreparseAssignment(Token tok, Int tokInd, TOKS, CM) {
// Looks at a tokDef or tokAssignment to determine its key points: where is the right side,
// is it a func definition, is the right side empty etc. Consumes no tokens.
// Precondition: tokInd is 1 past the "tok".
   Int const sentinel = calcSentinel(tok, tokInd - 1);
   Int indRight = tokInd;
   NameId firstTokenName = toks[tokInd].pl1;
   for (;
       indRight < sentinel && toks[indRight].tp != tokAssignRight;
       indRight++) {}

   VALIDATEP((indRight < sentinel && toks[indRight].pl2 > 0), errAssignmentEmptyRight);

   return (Assignment){
      .nameTokenInd = tokInd, .rightTokenInd = indRight, .sentinel = sentinel, .name = firstTokenName,
      .isDef = (tok.tp == tokDef), .isFunction = toks[indRight + 1].tp == tokFn
   };
}

private void //:pAssignment
pAssignment(Token tok, TOKS, CM) {
// Parses both assignments and compile-time defs
   if (tok.pl1 == assiTypeDefinition) {
      pTypeDef(toks, cm);
   } else {
      Assignment assi = pPreparseAssignment(tok, cm->i, toks, cm);
      pAssignmentWorker(tok, assi, toks, cm);
   }
}

private void //:preambleFor
preambleFor(Int sentinel, TOKS, CM, OUT Int* condInd, OUT Int* stepInd, OUT Int* bodyInd) {
// Pre-processes a "for" loop and finds its key tokens: the loop condition, the stepper and body.
// Every out index is set to either positive or 0 for "not found".
// A "for" syntax form is quadripartite:
// 1) var inits (they must all be assignments),
// 2) the condition (must be an expression),
// 3) statements for stepping to the next iteration (must be expressions, assignments or asserts),
// 4) loop body (arbitrary syntax forms).
// Precondition: looking at the tokScope right after tokFor.
// Postcondition: "condInd" & one of "stepInd" and "bodyInd" are guaranteed to be found
// (=> positive).

   Int const scopeSentinel = calcSentinel(toks[cm->i], cm->i);

   cm->i++; // CONSUME the tokScope
   Int j = cm->i;
   for (Token currTok = toks[j];
        (currTok.tp == tokAssignment || currTok.tp == tokAssignRight);
        currTok = toks[j]) {
      j = calcSentinel(currTok, j);
      VALIDATEP(j < sentinel, errLoopEmptyStepBody)
   }
   VALIDATEP(j < scopeSentinel, errLoopNoCondition)

   Token condTok = toks[j];
   VALIDATEP((condTok.tp == tokStmt && condTok.pl2 > 0) || condTok.tp == tokBool,
             errLoopNoCondition)
   *condInd = j;

   j = calcSentinel(condTok, j); // skipping the cond
   VALIDATEP(j < sentinel, errLoopEmptyStepBody);
   *stepInd = j;
   for (Token currTok = toks[j]; j < scopeSentinel; currTok = toks[j]) {
      VALIDATEP(currTok.tp == tokStmt || currTok.tp == tokAssignment || currTok.tp == tokAssert,
                errLoopWrongFormInStepper);
      j = calcSentinel(currTok, j);
   }

   *bodyInd = (j < sentinel) ? j : 0;
   VALIDATEP((*stepInd) + (*bodyInd) > 0, errLoopEmptyStepBody)
}

private void //:pFor
pFor(Token forTk, TOKS, CM) {
// For loops. Look like "(for x~ = 0;  x < 100; x++:  ... )"
//                            ^initInd ^condInd ^stepInd ^bodyInd
// At least a step or a body is syntactically required.
// End result of a parse looks like:
// nodFor
//    scope (pl3 = length of nodes to inner scope)
//       initializations
//       expr evaluating to a bool (the cond - if present)
//       step(s)
//       scope (if body not empty)
//          body
   Int const initInd = cm->i; // index of the tokScope inside tokFor

   cm->stats.loopCounter++;
   Int const sentinel = cm->i + forTk.pl2;

   Int condInd; // index of condition
   Int stepInd; // index of iteration stepping code
   Int bodyInd; // index of loop body
   Int const forNodeInd = cm->ast.len;

   VALIDATEP(toks[cm->i].tp == tokScope, errLoopSyntaxError)

   // sets inds to 0 if not found. At least one of stepInd, bodyInd is guaranteed to be positive
   preambleFor(sentinel, toks, cm, OUT &condInd, OUT &stepInd, OUT &bodyInd);
   openParsedScope(sentinel, (Node){.tp = nodFor, .pl1 = cm->stats.loopCounter}, locOf(forTk), cm);

   // variable initializations
   Int sndInd = minPositiveOf(3, condInd, stepInd, bodyInd);
   if (sndInd > initInd) {
      for (cm->i = initInd + 1; cm->i < sndInd;) {
         Token tok = toks[cm->i];
         cm->i++; // CONSUME the assignment span marker
         pAssignment(tok, toks, cm);
      }
   }

   // loop condition
   if (condInd > 0)  {
      Token condTok = toks[condInd];

      cm->i = condInd + 1; // +1 cause the expression parser needs to be 1 past the exprToken
      TypeId condType = exprUpToWithFrame((ParseFrame){
            .level = 0, .startNodeInd = cm->ast.len,
            .sentinel = minPositiveOf(3, stepInd, bodyInd, sentinel),
            .typeId = cm->stats.loopCounter
         },
         locOf(condTok), toks, cm
      );
      VALIDATEP(eq(condType, boolTy), errTypeMustBeBool)
   }

   // loop steps
   if (stepInd > 0) {
      Int const bodySentinel = minPositiveOf(2, bodyInd, sentinel);
      for (cm->i = stepInd; cm->i < bodySentinel; ) {
         Token stepTk = toks[cm->i];
         Int nextStep = calcSentinel(stepTk, cm->i);
         cm->i++; // CONSUME span token
         (PARSE_TABLE[stepTk.tp])(stepTk, toks, cm);
         cm->i = nextStep;
      }
   }

   // readying to parse the body + step statements
   Int bodyStartBt = toks[sndInd].startBt;
   Int const bodyNodeInd = cm->ast.len;

   cm->ast.c[forNodeInd].pl3 = bodyNodeInd - forNodeInd; // distance to inner scope
   if (bodyInd > 0) {
      openParsedScope(
         sentinel, (Node){.tp = nodScope },
         (SourceLoc){.startBt = bodyStartBt, .lenBts = forTk.lenBts - bodyStartBt + forTk.startBt },
         cm
      );
      cm->i = bodyInd; // CONSUME the "for" until the loop body
   } else {
      cm->i = sentinel; // CONSUME the loop with empty body
   }
}

private void //:parseErrorBareAtom
parseErrorBareAtom(Token tok, TOKS, CM) {
   throwExcParser(errTemp);
}

private ParseFrame //:popAParseFrame
popAParseFrame(CM) {
// Pops a frame from the scopes. For a scope type of frame, also deactivates its bindings.
// Returns pointer to previous frame (which will be top after this call) or null if there isn't any
   ParseFrame frame = removeLast(cm->backtrack); // matched by scopes->len-- below
   if (frame.level < pfrScope)
      { goto finishUp; }

   rewindLexicalScope(cm);
finishUp:
   cm->ast.c[frame.startNodeInd].pl2 = cm->ast.len - frame.startNodeInd - 1;
   return frame;
}

private TypeId //:exprSingleItem
exprSingleItem(Token tk, CM) {
// A single-item expression, like "foo". Consumes no tokens.
// Pre-condition: we are 1 token past the token we're parsing.
// Returns the type of the single item
   TypeId typeId = ZERO_ARITY_TYPE;
   if (tk.tp == tokWord) {
      Node node = getNodVarForName(tk.pl1, cm);
      typeId = cm->vars.c[node.pl1].typeId;
      newNode(node, locOf(tk), cm);
   } ei (tk.tp == tokOperator) {
      Int operBindingId = tk.pl1;
      OpDef operDefinition = OPERATORS[operBindingId];
      VALIDATEP(operDefinition.prec == precUnary, errOperatorWrongArity)
      newNode((Node){ .tp = nodVar, .pl1 = operBindingId }, locOf(tk), cm);
      // TODO add the type when we support first-class functions
   } ei (tk.tp <= topVerbatimType) {
      newNode((Node){.tp = tk.tp, .pl1 = tk.pl1, .pl2 = tk.pl2}, locOf(tk), cm);
      typeId = typeOf(tk.tp);
   } ei (tk.tp == tokData)  {
      newNode((Node){.tp = nodDataAlloc, .pl2 = 0}, locOf(tk), cm);
   } else {
      throwExcParser(errUnexpectedToken);
   }
   return typeId;
}

private void //:subexDataAllocation
subexDataAllocation(ExprFrame frame, Expr* e, CM) {
// Creates an assignment in main. Then walks over the data allocator
// nodes and counts elements that are subexpressions. Then copies the nodes from scratch to main,
// careful to wrap subexpressions in a nodExpr. Finally, replaces the copied nodes in scr with
// an id linked to the new entity
   LNode* scr = e->scr;  // ((ind in scr) (count of nodes in subexpr))

   const VarId newVarId = cm->vars.len;
   pushInvars(((Var) { .class = classImm, .fnId = -1 }), cm);

   Int countElements = 0;
   Int countNodes = scr->len - frame.startNode;
   for (Int j = frame.startNode; j < scr->len; ++j)  {
      Node nd = scr->c[j];
      countElements++;

      if (nd.tp == nodExpr)
         { j += nd.pl2; }
   }
   SourceLoc const rawLoc = frame.loc;
   newNode((Node){.tp = nodAssignment, .pl1 = 0, .pl2 = countNodes + 2, .pl3 = 2}, rawLoc, cm);
   newNode((Node){.tp = nodVar, .pl1 = newVarId, .pl2 = 0, .pl3 = assiVarAssignment}, rawLoc, cm);
   newNode((Node){.tp = nodDataAlloc, .pl1 = frame.name, .pl2 = countNodes, .pl3 = countElements },
           rawLoc, cm);

   Int const mainNodeInd = cm->ast.len;
   eSaveNodes(frame.startNode, scr, e->locsScr, cm);

   if (countNodes > 0)  {
      TypeId eltType = typecheckList(mainNodeInd, cm);
      TypeId collType = tCreateSingleParamTypeCall(
         typeOf(cm->activeBindings[nameOfStandard(strL)]), eltType, cm
      );
      cm->vars.c[newVarId].typeId = collType;
   }

   e->scr->c[frame.startNode] = (Node){ .tp = nodVar, .pl1 = newVarId, .pl2 = 0,
      .pl3 = 0 };
   scr->len = frame.startNode + 1;
   e->locsScr->len = frame.startNode + 1;
}

private void //:eBumpArgCount
eBumpArgCount(LExprFrame* frames) {
   Int const ind = frames->len - 1;
   Int const tp = frames->c[ind].tp;
   if (tp == exfrCall || tp == exfrDataAlloc || tp == exfrParen || tp == exfrExWrapper)
      { frames->c[ind].argCount++; }
}

private void //:eWriteUnaryCalls
eWriteUnaryCalls(Expr* e) {
   ExprFrame* zero = e->frames->c;
   ExprFrame* const initFrame = zero + (e->frames->len - 1);
   ExprFrame* frame = initFrame;
   for (; frame >= zero && frame->tp == exfrUnaryCall; frame--) {
      add(((Node){.tp = nodCall, .pl1 = frame->name, .pl2 = 1, .pl3 = 0}), e->scr);
      add(frame->loc, e->locsScr);
   }
   if (frame < initFrame)
      { e->frames->len = frame - zero + 1; }
}

private void //:eWriteCallToScratch
eWriteCallToScratch(ExprFrame frame, Expr* e) {
// Writes a call to the nodes scratch space.
// Precondition: the ExprFrame has already been popped
   LNode* scr = e->scr;
   Node call = {
      .tp = nodCall,
      .pl1 = frame.name,
      .pl2 = frame.argCount,
      .pl3 = (frame.isVarCall ? callVar : callNormal)
   };

   add(call, scr);
   add(frame.loc, e->locsScr);
}

private void //:eClose
eClose(Expr* restrict e, CM) {
// Flushes the finished subexpr frames from the top of the funcall stack.
// Handles data allocations
   while (e->frames->len > 0 && cm->i == last(e->frames).sentinel) {
      ExprFrame frame = removeLast(e->frames);
      switch (frame.tp) {
      case exfrCall:
         eWriteCallToScratch(frame, e); break;
      case exfrDataAlloc:
         subexDataAllocation(frame, e, cm); break;
      case exfrParen:
         eWriteUnaryCalls(e);
         eBumpArgCount(e->frames);
         break;
      case exfrAccessIn:
         add(((Node){.tp = nodCall, .pl1 = frame.name, .pl2 = 2, .pl3 = callGetElem}), e->scr);
         add(frame.loc, e->locsScr);
         break;
      case exfrAccessor:
         eWriteUnaryCalls(e);
         eBumpArgCount(e->frames);
         break;
      case exfrExWrapper:
         eWriteUnaryCalls(e);
         e->scr->c[frame.startNode - 1].pl2 = e->scr->len - frame.startNode;
         break;
      }
   }
}

private void //:exprCopyFromScratch
exprCopyFromScratch(Int startNodeInd, CM) {
// Copy nodes from scratch into main AST
   Expr* restrict e = cm->expr;
   LNode* restrict scr = e->scr;
   LSourceLoc* restrict locs = e->locsScr;
   if (e->metAnAllocation)
      { cm->ast.c[startNodeInd].pl1 = 1; }
   if (cm->ast.len + scr->len + 1 < cm->ast.cap) {
      memcpy((Node*)(cm->ast.c) + (cm->ast.len), scr->c, scr->len*sizeof(Node));
      memcpy((SourceLoc*)(cm->sourceLocs->c) + (cm->sourceLocs->len), locs->c,
            locs->len*sizeof(SourceLoc));

   } else {
      Int newCap = 2*(cm->ast.cap) + scr->len;
      Arr(Node) newContent = allocateArray(newCap, Node, cm->a);
      memcpy(newContent, cm->ast.c, cm->ast.len*sizeof(Node));
      memcpy((Node*)(newContent) + (cm->ast.len), scr->c, scr->len*sizeof(Node));
      cm->ast.cap = newCap;
      cm->ast.c = newContent;

      Arr(SourceLoc) newLocs = allocateArray(newCap, SourceLoc, cm->a);
      memcpy(newLocs, cm->sourceLocs->c, cm->sourceLocs->len*sizeof(SourceLoc));
      memcpy((SourceLoc*)(newLocs) + (cm->sourceLocs->len), locs->c,
            locs->len*sizeof(SourceLoc));
      cm->sourceLocs->cap = newCap;
      cm->sourceLocs->c= newLocs;
   }
   cm->ast.len += scr->len;
   cm->sourceLocs->len += scr->len;
}

Int //:subexSkipFirstThing
subexSkipFirstThing(Int subSentinel, TOKS, CM) {
// Skips a lump of tokens consisting of
// - possibly unary operator calls
// - definitely, an atom or a span
// - possibly, field accessors
   Int j = cm->i;
   for (; j < subSentinel && toks[j].tp == tokOperator && OPERATORS[toks[j].pl1].prec == precUnary;
         j++
   ) {
   }
   VALIDATEP(j < subSentinel, errExpressionError);

   j = calcSentinel(toks[j], j);
   for (; j < subSentinel && toks[j].tp == tokFieldAcc; j++) {
   }
   return j;
}

void //:subexCallFirstToken
subexCallFirstToken(Int subSentinel, Bool isInParens, TOKS, CM) {
// Pre-parses the start of a complex subexpression. At start there should either be
// - a word (and then it's a call unless the next token is a non-prefix operator or .fld),
// - something else (and then the next token must be a non-prefix operator or .fld).
   Int j = subexSkipFirstThing(subSentinel, toks, cm);
   if (j == subSentinel)
      { goto finishCloser; }
   Token tok = toks[cm->i];
   if (tok.tp == tokWord && j == cm->i + 1) { // initial word that is a call
      NameId name = tok.pl1;
      if (toks[j].tp == tokOperator && OPERATORS[toks[j].pl1].prec != precUnary
         || toks[j].tp == tokFieldAcc)
         { goto finishCloser; }

      add(((ExprFrame) {
            .tp = exfrCall, .name = name, .sentinel = subSentinel, .precedence = precFn,
            .argCount = 0, .loc = locOf(tok), .isVarCall = cm->activeBindings[name] > -1
         }),
         cm->expr->frames
      );
      if (!isInParens)
         { cm->i++; } // CONSUME the call at start of expression
   } else {
      VALIDATEP(toks[j].tp == tokOperator && OPERATORS[toks[j].pl1].prec != precUnary
         || toks[j].tp == tokFieldAcc,
         errExpressionFunctionless);
   finishCloser:
      if (isInParens)
         { cm->i--; } // ROLL BACK to the tokParens
   }
}

private void //:eParens
eParens(Token cTk, ExprFrame parent, Expr* e, TOKS, CM) {
// Precondition: we are pointing at tokParens
// Consumes 0 or 1 tokens.
   Int parensSentinel = calcSentinel(cTk, cm->i);
   SourceLoc loc = locOf(cTk);
   if (parensSentinel == cm->i + 2) { // A nullary call like `(call)`
      Token callTk = toks[cm->i + 1];
      SourceLoc callLoc = locOf(callTk);
      if (parent.tp == exfrDataAlloc) {
         // inside a data allocator, subexprs need to be wrapped in nodExpr for t-checking & codegen
         add(((Node){ .tp = nodExpr, .pl1 = 1 }), e->scr);
         add(loc, e->locsScr);
      }
      add(((Node){ .tp = nodCall, .pl1 = callTk.pl1, .pl2 = 0 }), e->scr);
      add(callLoc, e->locsScr);

      eWriteUnaryCalls(e);
      eBumpArgCount(e->frames);
      cm->i++; // CONSUME the tokParens (and the loop in eParse will consume the call)
   } else {
      Unt tp = exfrParen;
      if (parent.tp == exfrDataAlloc) {
         // inside a data allocator, subexprs need to be wrapped in nodExpr for t-checking & codegen
         tp = exfrExWrapper;
         add(((Node){ .tp = nodExpr, .pl1 = 0 }), e->scr);
         add(loc, e->locsScr);
      }
      add(((ExprFrame){
            .tp = tp, .startNode = e->scr->len, .sentinel = parensSentinel,
            .argCount = 0, .loc = loc }), e->frames);
      cm->i++; // CONSUME the tokParens. Will roll back if necessary
      subexCallFirstToken(parensSentinel, true, toks, cm);
   }
}

private void //:eProcessToken
eProcessToken(Token cTk, Int sentinel, Expr* restrict e, TOKS, CM) {
   ExprFrame parent = last(e->frames);
   SourceLoc loc = locOf(cTk);
   NameId name = cTk.pl1;
   Byte tokType = cTk.tp;
   switch (tokType) {
   case tokOperator:
      Int precedence = OPERATORS[name].prec;
      if (precedence == precUnary) {
         add(((ExprFrame) {
               .tp = exfrUnaryCall, .name = name, .sentinel = parent.sentinel,
               .precedence = precUnary, .argCount = 1, .loc = loc, .startNode = -1,  }),
            e->frames);
      } else {
         eOperatorCall(cTk, precedence, false, cm);
      }
      break;
   case tokAccessor:
      add(((ExprFrame) {
            .tp = exfrAccessor, .name = opGetElem, .sentinel = calcSentinel(cTk, cm->i),
            .loc = loc
         }),
         e->frames
      );
      cm->i++; // CONSUME the tokAccessor
      Token varTk = toks[cm->i];
      VALIDATEP(varTk.tp == tokWord, errExpressionError);
      Node node = getNodVarForName(varTk.pl1, cm);
      add(node, e->scr);
      add(locOf(varTk), e->locsScr);
      break;
   case tokAccessIn:
      add(((ExprFrame) {
            .tp = exfrAccessIn, .name = opGetElem, .sentinel = calcSentinel(cTk, cm->i), .loc = loc
         }),
         e->frames); break;
   case tokFieldAcc:
      add(((Node){.tp = nodCall, .pl1 = name, .pl3 = callField}), e->scr); break;
   case tokInt:
   case tokLong:
   case tokDouble:
   case tokBool:
   case tokString:
      add(((Node){ .tp = cTk.tp, .pl1 = name, .pl2 = cTk.pl2 }), e->scr);
      //-fallthrough
   case tokWord:
      if (tokType == tokWord)
         { add(getNodVarForName(name, cm), e->scr); }
      add(loc, e->locsScr);
      eWriteUnaryCalls(e);
      eBumpArgCount(e->frames);
      break;
   case tokParens:
      eParens(cTk, parent, e, toks, cm); break;
   case tokData:
      e->metAnAllocation = true;
      eBumpArgCount(e->frames);
      add(((ExprFrame) {
            .tp = exfrDataAlloc, .name = nameOfStandard(strL),
            .sentinel = calcSentinel(cTk, cm->i), .startNode = e->scr->len,
            .loc = loc  }),
           e->frames
      );
      break;
   default:
      throwExcParser(errExpressionCannotContain);
   }
}

private void //:eParse
eParse(Int sentinel, TOKS, CM) {
// The core code of the general, long expression parse. Starts at cm->i and parses until
// "sentinel". Produces a linear sequence of operands and calls with arg counts in
// Reverse Polish Notation. Handles data allocations, too. But not single-item exprs.
// Consumes the whole expression
// Pre-condition: we are 1 past the nodExpr, if any (but NOT past nodData if it's the whole exp)
   Expr* e = cm->expr;
   e->metAnAllocation = false;
   LNode* scr = e->scr;
   LSourceLoc* locsScr = cm->expr->locsScr;
   LExprFrame* frames = cm->expr->frames;
   frames->len = 0;
   scr->len = 0;
   locsScr->len = 0;
   if (toks[cm->i].tp != tokParens || calcSentinel(toks[cm->i], cm->i) < sentinel)
      { add(((ExprFrame){ .tp = exfrParen, .sentinel = sentinel}), frames); }

   subexCallFirstToken(sentinel, false, toks, cm);
   for (; cm->i < sentinel; cm->i++) { // CONSUME any expression token
      eClose(e, cm);
      eProcessToken(toks[cm->i], sentinel, e, toks, cm);
   }
   eClose(e, cm);
}

private TypeId //:exprUpToWithFrame
exprUpToWithFrame(ParseFrame frame, SourceLoc loc, TOKS, CM) {
// The main "big" expression parser. Parses an expression whether there is a
// token or not. Starts from cm->i and goes up to the sentinel. Returns the expression's type
// Precondition: we are looking 1 past the tokExpr or tokParens
// CONSUMES the whole expression
   if (cm->i + 1 == frame.sentinel) { // the [stmt 1, tokInt] case
      Token singleToken = toks[cm->i];
      if (singleToken.tp <= topVerbatimTokenVariant || singleToken.tp == tokWord
            || singleToken.tp == tokData) {
         cm->i++;
         return exprSingleItem(singleToken, cm);
      }
   }
   Int const startNodeInd = cm->ast.len;
   add(frame, cm->backtrack);
   newNode((Node){ .tp = nodExpr}, loc, cm);

   eParse(frame.sentinel, toks, cm);
   exprCopyFromScratch(startNodeInd, cm);
   TypeId exprType = typeCheckBigExpr(startNodeInd, cm->ast.len, cm);
   mbCloseSpans(cm);
   return exprType;
}

private TypeId //:exprUpTo
exprUpTo(Int sentinelToken, SourceLoc loc, TOKS, CM) {
// The main "big" expression parser. Parses an expression whether there is a token or not.
// Precondition: we are looking 1 past the tokExpr or tokParens
// Starts from cm->i and goes up to the sentinel token.
// Emits a nodExpr and opens a corresponding parse frame
// Returns the expression's type
   Int startNodeInd = cm->ast.len;
   add(((ParseFrame){
      .startNodeInd = startNodeInd, .sentinel = sentinelToken }), cm->backtrack);
   newNode((Node){ .tp = nodExpr}, loc, cm);
   eParse(sentinelToken, toks, cm);
   exprCopyFromScratch(startNodeInd, cm);
   TypeId exprType = typeCheckBigExpr(startNodeInd, cm->ast.len, cm);
   mbCloseSpans(cm);
   return exprType;
}

private TypeId //:exprHeadless
exprHeadless(Int sentinel, SourceLoc loc, TOKS, CM) {
// Precondition: we are looking at the first token of expr which does not have a
// tokStmt/tokParens header. If "omitSpan" is set, this function will not emit a nodExpr nor
// create a ParseFrame.
// Consumes 1 or more tokens. Returns the type of parsed expression
   if (cm->i + 1 == sentinel) { // the [stmt 1, tokInt] case
      Token singleToken = toks[cm->i];
      if (singleToken.tp <= topVerbatimTokenVariant || singleToken.tp == tokWord) {
         cm->i++; // CONSUME the single literal
         return exprSingleItem(singleToken, cm);
      }
   }
   return exprUpTo(sentinel, loc, toks, cm);
}

private TypeId //:pExprWorker
pExprWorker(Token tok, TOKS, CM) {
// Precondition: we are looking 1 past the first token of expr, which is the first parameter.
// Consumes 1 or more tokens. Handles single items also Returns the type of parsed expression

   if (tok.tp == tokStmt || tok.tp == tokParens) {
      if (tok.pl2 == 1) {
         Token singleToken = toks[cm->i];
         if (singleToken.tp <= topVerbatimTokenVariant || singleToken.tp == tokWord) {
            // [stmt 1, tokInt]
            cm->i++; // CONSUME the single literal token
            return exprSingleItem(singleToken, cm);
         }
      }

      return exprUpTo(cm->i + tok.pl2, locOf(tok), toks, cm);
   } else {
      return exprSingleItem(tok, cm);
   }
}

private void //:pExpr
pExpr(Token tok, TOKS, CM) { pExprWorker(tok, toks, cm); }


private void //:mbCloseSpans
mbCloseSpans(CM) {
// When we are at the end of a function parsing a parse frame, we might be at the end of said frame
// (otherwise => we've encountered a nested frame, like in "1 + { x = 2; x + 1}"),
// in which case this function handles all the corresponding stack poppin'.
// It also always handles updating all inner frames with consumed tokens
// This is safe to call anywhere, pretty much
   while (cm->backtrack->len > 0) { // loop over subscopes and expressions inside FunctionDef
      ParseFrame frame = last(cm->backtrack);
      if (cm->i < frame.sentinel)
         { return; }
#ifdef SAFETY //{{{
      if (cm->i > frame.sentinel) {
         print("Span inconsistency i %d  frame.level %d frame.sentinelToken %d startInd %d",
            cm->i, frame.sentinel, frame.level, frame.startNodeInd);
      }
      VALIDATEI(cm->i == frame.sentinel, iErrorInconsistentSpans)
#endif //}}}
      popAParseFrame(cm);
   }
}

private void //:parseUpTo
parseUpTo(Int sentinelToken, TOKS, CM) {
// Parses anything from current cm->i to "sentinelToken"
   while (cm->i < sentinelToken) {
      Token currTok = toks[cm->i];
      cm->i++;
      (PARSE_TABLE[currTok.tp])(currTok, toks, cm);
      mbCloseSpans(cm);
   }
}

private void //:pAlias
pAlias(Token tok, TOKS, CM) {
   throwExcParser(errTemp);
}

private void
parseAssert(Token tok, TOKS, CM) {
   throwExcParser(errTemp);
}

private Int //:breakContinue
breakContinue(Token tok, Int* sentinel, TOKS, CM) {
// Returns the number of levels to break/continue to, or 1 if there weren't any specified
   VALIDATEP(tok.pl2 <= 1, errBreakContinueTooComplex);
   Int unwindLevel = 1;
   *sentinel = cm->i;
   if (tok.pl2 == 1) {
      Token nextTok = toks[cm->i];
      VALIDATEP(nextTok.tp == tokInt && nextTok.pl1 == 0 && nextTok.pl2 > 0,
                errBreakContinueInvalidDepth)

      unwindLevel = nextTok.pl2;
      (*sentinel)++; // CONSUME the Int after the `break`
   }
   if (unwindLevel == 1)
      { return 1; }

   for (Int j = cm->backtrack->len - 1; j > -1; j--) {
      if (cm->backtrack->c[j].level != pfrLoop)
         { continue; }
      unwindLevel--;
      if (unwindLevel != 0)
         { continue; }
      ParseFrame loopFrame = cm->backtrack->c[j];
      Int loopId = loopFrame.typeId.v;
      cm->ast.c[loopFrame.startNodeInd].pl1 = loopId;
      return unwindLevel == 1 ? -1 : loopId;
   }

   throwExcParser(errBreakContinueInvalidDepth);
}

private void //:pBreakCont
pBreakCont(Token tok, TOKS, CM) {
   Int sentinel = cm->i;
   Int loopId = breakContinue(tok, &sentinel, toks, cm);
   if (tok.pl1 > 0) // continue
      { loopId += BIG; }
   newNode((Node){.tp = nodBreakCont, .pl1 = loopId}, locOf(tok), cm);
   cm->i = sentinel; // CONSUME the whole break statement
}

private void
parseCatch(Token tok, TOKS, CM) {
   throwExcParser(errTemp);
}

private void parseDefer(Token tok, TOKS, CM) {
   throwExcParser(errTemp);
}


private void pTrait(Token tok, TOKS, CM) {
   throwExcParser(errTemp);
}


private void parseImpl(Token tok, TOKS, CM) {
   throwExcParser(errTemp);
}


private void parseLambda(Token tok, TOKS, CM) {
   throwExcParser(errTemp);
}


private void parseLambda1(Token tok, TOKS, CM) {
   throwExcParser(errTemp);
}


private void parseLambda2(Token tok, TOKS, CM) {
   throwExcParser(errTemp);
}


private void parsePackage(Token tok, TOKS, CM) {
   throwExcParser(errTemp);
}

private void //:pReturn
pReturn(Token tok, TOKS, CM) {
   Int lenTokens = tok.pl2;
   Int sentinelToken = cm->i + lenTokens;
   if (lenTokens == 0) {
      newNode((Node){.tp = nodReturn}, locOf(tok), cm);
      return;
   }

   Int j = cm->backtrack->len - 1;

   while (j > -1 && cm->backtrack->c[j].level != pfrFn)
      { j--; }
   TypeId fnTy = cm->backtrack->c[j].typeId;
   add(((ParseFrame){ .level = 0, .startNodeInd = cm->ast.len,
                  .sentinel = sentinelToken }), cm->backtrack);
   newNode((Node){.tp = nodReturn}, locOf(tok), cm);

   Token rTk = toks[cm->i];
   SourceLoc loc = {.startBt = rTk.startBt, .lenBts = tok.lenBts - rTk.startBt + tok.startBt};
   TypeId const exprTy = exprHeadless(sentinelToken, loc, toks, cm);
   VALIDATEP(exprTy.v > -1, errReturn)
   TypeId const returnType = tFunctionReturnType(fnTy, cm);
   VALIDATEP(eq(returnType, exprTy), errTypeWrongReturnType);
}

private void //:importVars
importVars(Arr(Var) impts, Int const countVars, CM) {
   for (int j = 0; j < countVars; j++) {
      Var const ent = impts[j];
      VALIDATEP(cm->activeBindings[ent.name] == -1, errAssignmentShadowing)
      Int newVarId = cm->vars.len;
      pushInvars(ent, cm);
      cm->activeBindings[ent.name] = newVarId;
   }
   cm->stats.countNonparsedVars = cm->vars.len;
}

private void //:importFns
importFns(Arr(Function) impts, Int const countFns, CM) {
   for (int j = 0; j < countFns; j++) {
      Function const fn = impts[j];
      NameId const name = fn.name;
      Int newFnId = cm->functions.len;
      pushInfunctions(fn, cm);
      addRawOverload(name, fn.typeId, newFnId, cm);
      pushInimportNames(name, cm);
   }
   cm->stats.countNonparsedFns = cm->functions.len;
}

private LUnt* //:copyNames
copyNames(LUnt* table, Arena* a) {
   LUnt* result = createLUnt(table->cap, a);
   result->len = table->len;
   result->cap = table->cap;
   memcpy(result->c, table->c, table->len*4);
   return result;
}

private StringDict* //:copyStringDict
copyStringDict(StringDict* from, Arena* a) {
   StringDict* result = allocate(StringDict, a);
   Int const dictSize = from->dictSize;
   Arr(Bucket*) dict = allocateArray(dictSize, Bucket*, a);

   result->a = a;
   for (int i = 0; i < dictSize; i++) {
      if (from->dict[i] == null) {
         dict[i] = null;
      } else {
         Bucket* old = from->dict[i];
         Int capacity = old->capAndLen >> 16;
         Int len = old->capAndLen & LOWER16BITS;
         Bucket* new = allocateOnArena(sizeof(Bucket) + capacity*sizeof(StringValue), a);
         new->capAndLen = old->capAndLen;
         memcpy(new->c, old->c, len*sizeof(StringValue));
         dict[i] = new;
      }
   }
   result->dictSize = dictSize;
   result->dict = dict;
   return result;
}

private void //:finalizeLexer
finalizeLexer(LX) {
// Finalizes the lexing of a single input: checks for unclosed scopes, and closes semicolons and
// an open statement, if any
   lx->stats.toksLen = lx->tokens.len;
   if (lx->lexBtrack->len == 0)
      { return; }
   BtToken top = removeLast(lx->lexBtrack);
   setStmtSpanLength(top.tokenInd, lx);
   mbCloseAssignRight(&top, lx);
   VALIDATEL(top.spanLevel != slScope && lx->lexBtrack->len == 0, errPunctuationExtraOpening)
}

private Compiler* //:lexicallyAnalyzeInner
lexicallyAnalyzeInner(Compiler* lx, Arena* a) {
// Main lexer function. Precondition: the input Byte array has been prepended
// with StandardText
   Int const inpLength = lx->stats.inpLength;
   Arr(char const) inp = lx->sourceCode.c;
   VALIDATEL(inpLength > 0, "Empty input")
   
   // Main loop over the input
   if (setjmp(excBuf) == 0) {
      while (lx->i < inpLength) {
         (LEX_TABLE[inp[lx->i]])(inp, lx);
      }
      finalizeLexer(lx);
   }
   return lx;
}

Compiler* //:lexicallyAnalyzeFromFile
lexicallyAnalyzeFromFile(String sourceCode, Arena* a) {
// Main lexer function. Precondition: the input Byte array has been prepended
// with StandardText
   Compiler* lx = createLexer(sourceCode, false, a);
   return lexicallyAnalyzeInner(lx, a);
}

private Compiler* //:lexicallyAnalyze
lexicallyAnalyze(String sourceCode, Arena* a) {
// Main lexer function. Precondition: the input Byte array has been prepended
// with StandardText
   Compiler* lx = createLexer(sourceCode, true, a);
   return lexicallyAnalyzeInner(lx, a);
}

private size_t
ceiling4(size_t sz) {
   size_t rem = sz % 4;
   return sz + (rem != 0 ? 4 - rem : 0);
}

private size_t
floor4(size_t sz) {
   size_t rem = sz % 4;
   return sz - rem;
}

#define CHUNK_SIZE 65536
#define FRESH_CHUNK_LEN floor4(CHUNK_SIZE - sizeof(ScopeChunk))/4

private Scopes //:createScopes
createScopes(Arena* a) {
   ScopeChunk* firstChunk = allocateOnArena(sizeof(ScopeChunk), a);
   firstChunk->prev = null;
   firstChunk->next = null;
   firstChunk->c[0] = 0;
   return (Scopes) {
      .currChunk = firstChunk, .currLen = 0, .start = firstChunk->c, .curr = firstChunk->c
   };
}

private void //:addBinding
addBinding(NameId name, Int bindingId, CM) {
   Scopes* const s = &(cm->scopes);
   scopesMoveForward(s, cm);
   *(s->curr) = name;
   s->currLen++;
   cm->activeBindings[name] = bindingId;
}

private void //:addRawOverload
addRawOverload(NameId const name, TypeId const typeId, FunctionId const fnId, CM) {
// Adds an overload of a function to the @rawOverloads and activates it, if needed
   Int mbListId = -cm->activeBindings[name] - 2;

   TypeId firstParamType = getFirstParamType(typeId, cm);
   if (mbListId == -1) {
      Int newListId = listAddMultiAssocList(firstParamType.v, fnId, cm->rawOverloads);
      cm->activeBindings[name] = -newListId - 2;
      cm->stats.countOverloadedNames++;
   } else {
      Int updatedListId = addMultiAssocList(firstParamType.v, fnId, mbListId, cm->rawOverloads);
      if (updatedListId != -1)
         { cm->activeBindings[name] = -updatedListId - 2; }
   }
   cm->stats.countOverloads++;
}

private TypeId //:mergeTypeWorker
mergeTypeWorker(TypeId startInd, Int lenInts, CM) {
   Arr(Int) types = cm->types.c;
   StringDict* hm = cm->typesDict;
   Int const lenBts = lenInts*4;
   Unt theHash = hashCode((char*)(types + startInd.v), lenBts);
   Int hashOffset = theHash % (hm->dictSize);
   if (*(hm->dict + hashOffset) == null) {
      Bucket* newBucket = allocateOnArena(sizeof(Bucket) + initBucketSize*sizeof(StringValue),
            hm->a);
      newBucket->capAndLen = (initBucketSize << 16) + 1; // left u16 = cap, right u16 = len
      StringValue* firstElem = (StringValue*)newBucket->c;
      *firstElem = (StringValue){.hash = theHash, .indString = startInd.v };
      *(hm->dict + hashOffset) = newBucket;
   } else {
      Bucket* p = *(hm->dict + hashOffset);
      int lenBucket = (p->capAndLen & 0xFFFF);
      Arr(StringValue) stringValues = (StringValue*)p->c;

      for (int i = 0; i < lenBucket; i++) {
         if (stringValues[i].hash == theHash
              && memcmp(types + stringValues[i].indString, types + startInd.v, lenBts) == 0) {
            // key already present
            cm->types.len = startInd.v;
            return typeOf(stringValues[i].indString);
         }
      }
      addValueToBucket((hm->dict + hashOffset), startInd.v, theHash, hm->a);
   }
   hm->len++;
   return startInd;
}

private TypeId //:mergeType
mergeType(TypeId startInd, CM) {
// Unique'ing of types. Precondition: the type is parked at the end of cm->types, forming its
// tail, and covered by @types.len. Returns the resulting index of this type and updates the
// length of cm->types if appropriate
   Int lenInts = cm->types.c[startInd.v] + 1; // +1 for the type length
   return mergeTypeWorker(startInd, lenInts, cm);
}

private TypeId //:addConcrFnType
addConcrFnType(Int arity, Arr(Int) paramsAndReturn, CM) {
// Function types are stored as: (paramType1, paramType2, ..., returnType)
   TypeId newInd = typeOf(cm->types.len);
   pushIntypes(TYPE_PREFIX_LEN + arity, cm);
   typeAddHeader(
      (TypeHeader){ .sort = sorDeclare, .tyrity = 0, .arity = arity + 1,
         .name = nameOfStandard(strF), .isGeneric = false },
      cm
   );
   for (Int k = 0; k <= arity; k++) { // <= because there are (arity + 1) elts - the return type!
      pushIntypes(paramsAndReturn[k], cm);
   }
   return mergeType(newInd, cm);
}

private void //:importGenericTypesList
importGenericTypesList(OUT TypeId* addType, OUT TypeId* lengthType, CM) {
// Types for the generic list's `add` function: `L $T, $T -> Void`
// and the `#` function: `L $T -> Int`
   // add $0 type
   TypeId tentativeType = typeOf(cm->types.len);
   pushIntypes(TYPE_PREFIX_LEN, cm);
   typeAddHeader(((TypeHeader){ .sort = sorGenericParam, .tyrity = 1, .arity = 0, .name = 0,
        .isGeneric = true }), cm);
   TypeId p0 = mergeType(tentativeType, cm);

   // add L $0
   tentativeType = typeOf(cm->types.len);
   pushIntypes(TYPE_PREFIX_LEN + 1, cm);
   typeAddHeader(
      ((TypeHeader){ .sort = sorTypeCall, .tyrity = 1, .arity = 2, .name = nameOfStandard(strL),
         .isGeneric = true }),
         cm
   );
   pushIntypes(cm->stats.listType, cm);
   pushIntypes(p0.v, cm);
   TypeId l0 = mergeType(tentativeType, cm);

   // add L $0, $0 -> Void
   tentativeType = typeOf(cm->types.len);
   pushIntypes(TYPE_PREFIX_LEN + 2, cm);
   typeAddHeader(
      ((TypeHeader){ .sort = sorTypeCall, .tyrity = 1, .arity = 3, .name = nameOfStandard(strF),
         .isGeneric = true }),
         cm
   );
   pushIntypes(l0.v, cm);
   pushIntypes(p0.v, cm);
   pushIntypes(voidType, cm);
   *addType = mergeType(tentativeType, cm);

   // # L $0 -> Int
   tentativeType = typeOf(cm->types.len);
   pushIntypes(TYPE_PREFIX_LEN + 1, cm);
   typeAddHeader(
      ((TypeHeader){ .sort = sorTypeCall, .tyrity = 1, .arity = 2, .name = nameOfStandard(strF),
         .isGeneric = true }),
         cm
   );
   pushIntypes(l0.v, cm);
   pushIntypes(tokInt, cm);
   *lengthType = mergeType(tentativeType, cm);
}

//~private TypeId //:importGenericTypeLength
//~importGenericTypeLength(CM) {
//~// Type for the generic list's `length` function: `L $T -> Int`
//~   // add $0 type
//~   TypeId tentativeType = typeOf(cm->types.len);
//~   pushIntypes(TYPE_PREFIX_LEN, cm);
//~   typeAddHeader(((TypeHeader){ .sort = sorGenericParam, .tyrity = 1, .arity = 0, .name = 0,
//~        .isGeneric = true }), cm);
//~   TypeId p0 = mergeType(tentativeType, cm);
//~
//~   // add L $0
//~   tentativeType = typeOf(cm->types.len);
//~   pushIntypes(TYPE_PREFIX_LEN + 1, cm);
//~   typeAddHeader(
//~      ((TypeHeader){ .sort = sorTypeCall, .tyrity = 1, .arity = 2, .name = nameOfStandard(strL),
//~         .isGeneric = true }),
//~         cm
//~   );
//~   pushIntypes(cm->stats.listType, cm);
//~   pushIntypes(p0.v, cm);
//~   TypeId l0 = mergeType(tentativeType, cm);
//~
//~   // add L $0, $0 -> Void
//~   tentativeType = typeOf(cm->types.len);
//~   pushIntypes(TYPE_PREFIX_LEN + 2, cm);
//~   typeAddHeader(
//~      ((TypeHeader){ .sort = sorTypeCall, .tyrity = 1, .arity = 3, .name = nameOfStandard(strF),
//~         .isGeneric = true }),
//~         cm
//~   );
//~   pushIntypes(l0.v, cm);
//~   pushIntypes(p0.v, cm);
//~   pushIntypes(voidType, cm);
//~   TypeId res = mergeType(tentativeType, cm);
//~   return res;
//~}

private void //:buildStandardStrings
buildStandardStrings(LX) {
// Inserts all strings from the standardText into the string table and the hash table
// But first inserts a reservation for every operator symbol (that's "countOperators" nameIds,
// the lx->names contains zeros in those places)
   for (Int j = 0; j < countOperators; j++) {
      add(0, lx->names);
   }
   for (Int i = 0; i < strSentinel; i++) {
      addStringDict(lx->sourceCode.c, standardOffsets[i], standardStringLens[i],
                 lx->names, lx->stringDict);
   }
}

private NameId //:stToFullName
stToFullName(Int sta, CM) {
// Converts a standard string to its nameId. Doesn't work for reserved words, obviously
   return cm->names->c[sta + countOperators];
}

private void //:buildPreludeTypes
buildPreludeTypes(CM) {
// Creates the built-in types in the proto compiler
   // primitive types up to topVerbatimType (inclusive)
   for (int i = strInt; i <= strVoid; i++) {
      cm->activeBindings[nameOfStandard(i)] = i - strInt;
      pushIntypes(0, cm);
   }
   pushIntypes(0, cm); //empty type for "outerTypeForTypeParam"

   // List
   Int typeIndL = cm->types.len;
   pushIntypes(TYPE_PREFIX_LEN + 2, cm); // 4 for the field names & types
   NameId name = nameOfStandard(strL);
   typeAddHeader((TypeHeader){.sort = sorDeclare, .arity = 1, .tyrity = 1,
      .isGeneric = true, .name = nameOfStandard(strL)},
      cm);
   pushIntypes(tokInt, cm);
   pushIntypes(nameOfStandard(strLen), cm);
   pushIntypes(nameOfStandard(strLength), cm);
   cm->activeBindings[name] = typeIndL;
   cm->stats.listType = cm->activeBindings[nameOfStandard(strL)];

   // Array
   Int typeIndA = cm->types.len;
   pushIntypes(TYPE_PREFIX_LEN + 2, cm);
   name = nameOfStandard(strArray);
   typeAddHeader((TypeHeader){.sort = sorDeclare, .isGeneric = true,
                         .arity = 1, .tyrity = 1, .name = nameOfStandard(strArray)}, cm);
   pushIntypes(tokInt, cm);
   pushIntypes(nameOfStandard(strLen), cm);
   cm->activeBindings[name] = typeIndA;
}

private void //:buildInfixOperator
buildInfixOperator(Int operId, TypeId typeId, CM) {
// Creates an entity, pushes it to [rawOverloads] and activates its name
   FunctionId newEntityId = cm->functions.len;
   pushInfunctions((Function){ .typeId = typeId }, cm);
   addRawOverload(operId, typeId, newEntityId, cm);
}

private void //:buildOperator
buildOperator(Int operId, TypeId typeId, Byte emit, Int hostName, CM) {
//Creates an entity, pushes it to [rawOverloads] and activates its name
   FunctionId newFnId = cm->functions.len;
   pushInfunctions(
      (Function){
          .typeId = typeId, .name = OPERATORS[operId].name,
          .emit = emit, .hostName = hostName
      },
      cm
   );
   addRawOverload(operId, typeId, newFnId, cm);
}

private void //:buildOperators
buildOperators(CM) {
// Operators are the first-ever functions to be defined. This function builds their @types,
// @functions and overload counts. The order must agree with the order of operator
// definitions in eyr.internal.h, and every operator must have at least one type defined
   TypeId boolOfIntInt   = addConcrFnType(2, (Int[]){ tokInt, tokInt, tokBool}, cm);
   TypeId boolOfDoubDoub = addConcrFnType(2, (Int[]){ tokDouble, tokDouble, tokBool}, cm);
   TypeId boolOfStrStr   = addConcrFnType(2, (Int[]){ tokString, tokString, tokBool}, cm);
   TypeId boolOfBool     = addConcrFnType(1, (Int[]){ tokBool, tokBool}, cm);
   TypeId boolOfStr      = addConcrFnType(1, (Int[]){ tokString, tokBool}, cm);
   TypeId boolOfInt      = addConcrFnType(1, (Int[]){ tokInt, tokBool}, cm);
   TypeId boolOfDoub     = addConcrFnType(1, (Int[]){ tokDouble, tokBool}, cm);
   TypeId boolOfBoolBool = addConcrFnType(2, (Int[]){ tokBool, tokBool, tokBool}, cm);
   TypeId intOfStr       = addConcrFnType(1, (Int[]){ tokString, tokInt}, cm);
   TypeId intOfInt       = addConcrFnType(1, (Int[]){ tokInt, tokInt}, cm);
   TypeId intOfIntInt    = addConcrFnType(2, (Int[]){ tokInt, tokInt, tokInt}, cm);
   TypeId intOfDoubDoub  = addConcrFnType(2, (Int[]){ tokDouble, tokDouble, tokInt}, cm);
   TypeId intOfStrStr    = addConcrFnType(2, (Int[]){ tokString, tokString, tokInt}, cm);
   TypeId strOfInt       = addConcrFnType(1, (Int[]){ tokInt, tokString}, cm);
   TypeId strOfFloat     = addConcrFnType(1, (Int[]){ tokDouble, tokString}, cm);
   TypeId strOfBool      = addConcrFnType(1, (Int[]){ tokBool, tokString}, cm);
   TypeId strOfStrStr    = addConcrFnType(2, (Int[]){ tokString, tokString, tokString}, cm);
   TypeId flOfFlFl       = addConcrFnType(2, (Int[]){ tokDouble, tokDouble, tokDouble}, cm);
   TypeId flOfFl         = addConcrFnType(1, (Int[]){ tokDouble, tokDouble}, cm);
   TypeId voidOfInt      = addConcrFnType(1, (Int[]){ tokInt, voidType}, cm);
   buildOperator(opBitwiseNeg,   intOfInt, emitHostPrefix, hostConst, cm); // !. // dummy host name
   buildOperator(opNotEqual,     boolOfIntInt, emitInfix, -1, cm);
   buildOperator(opNotEqual,     boolOfDoubDoub, emitInfix, -1, cm);
   buildOperator(opNotEqual,     boolOfStrStr, emitInfix, -1, cm);
   buildOperator(opBoolNeg,      boolOfBool, emitHostPrefix, hostConst, cm);
   buildOperator(opSize,         intOfStr, emitField, hostLength, cm); // #
   buildOperator(opSize,         intOfInt, emitHostPrefix, hostAbs, cm);
   buildOperator(opToString,     strOfInt, emitNone, hostConst, cm); // $
   buildOperator(opToString,     strOfBool, emitNone, hostConst, cm);
   buildOperator(opToString,     strOfFloat, emitNone, hostConst, cm);
   buildOperator(opRemainder,    intOfIntInt, emitInfix, -1, cm); // %
   buildOperator(opBitwiseAnd,   intOfIntInt, emitInfix, -1, cm); // &&.
   buildOperator(opBoolAnd,      boolOfBoolBool, emitInfix, -1, cm); // &&
   buildOperator(opRef,          intOfIntInt, emitInfix, -1, cm); // ' dummy type, this oper is type-level
   buildOperator(opTimesExt,     flOfFlFl, emitHostPrefix, hostConst, cm);
   buildOperator(opTimes,        intOfIntInt, emitInfix, -1, cm);
   buildOperator(opTimes,        flOfFlFl, emitInfix, -1, cm);
   buildOperator(opIncrement,    voidOfInt, emitInfix, -1, cm);
   buildOperator(opPlusExt,      strOfStrStr, emitInfix, -1, cm);
   buildOperator(opPlus,         intOfIntInt, emitInfix, -1, cm);
   buildOperator(opPlus,         flOfFlFl, emitInfix, -1, cm);
   buildOperator(opPlus,         strOfStrStr, emitInfix, -1, cm);
   buildOperator(opDecrement,    voidOfInt, emitInfix, -1, cm);
   buildOperator(opMinusExt,     intOfIntInt, emitInfix, -1, cm);
   buildOperator(opMinus,        intOfIntInt, emitInfix, -1, cm);
   buildOperator(opMinus,        flOfFlFl, emitInfix, -1, cm);
   buildOperator(opNegate,       intOfInt, emitInfix, -1, cm);
   buildOperator(opNegate,       flOfFl, emitInfix, -1, cm);
   buildOperator(opDivByExt,     intOfIntInt, emitInfix, -1, cm);
   buildOperator(opIntersect,    intOfIntInt, emitInfix, -1, cm); // dummy, oper is type-level
   buildOperator(opDivBy,        intOfIntInt, emitInfix, -1, cm);
   buildOperator(opDivBy,        flOfFlFl, emitInfix, -1, cm);
   buildOperator(opBitShiftL,    intOfDoubDoub, emitInfix, -1, cm);
   buildOperator(opComparator,   intOfIntInt, emitInfix, -1, cm);
   buildOperator(opComparator,   intOfDoubDoub, emitInfix, -1, cm);
   buildOperator(opComparator,   intOfStrStr, emitInfix, -1, cm);
   buildOperator(opLTZero,       boolOfInt, emitInfix, -1, cm);
   buildOperator(opLTZero,       boolOfDoub, emitInfix, -1, cm);
   buildOperator(opLTZero,       boolOfStr, emitInfix, -1, cm);
   buildOperator(opLTEQ,         boolOfIntInt, emitInfix, -1, cm); // <=
   buildOperator(opLTEQ,         boolOfDoubDoub, emitInfix, -1, cm);
   buildOperator(opLTEQ,         boolOfStrStr, emitInfix, -1, cm);
   buildOperator(opLessTh,       boolOfIntInt, emitInfix, -1, cm);
   buildOperator(opLessTh,       boolOfDoubDoub, emitInfix, -1, cm);
   buildOperator(opLessTh,       boolOfStrStr, emitInfix, -1, cm);
   buildOperator(opRefEquality,  boolOfIntInt, emitInfix, -1, cm);
   buildOperator(opEquality,     boolOfIntInt, emitInfix, -1, cm);
   buildOperator(opBitShiftR,    boolOfBoolBool, emitInfix, -1, cm);
   buildOperator(opGTZero,       boolOfInt, emitInfix, -1, cm);
   buildOperator(opGTZero,       boolOfDoub, emitInfix, -1, cm);
   buildOperator(opGTZero,       boolOfStr, emitInfix, -1, cm);
   buildOperator(opGTEQ,         boolOfIntInt, emitInfix, -1, cm);
   buildOperator(opGTEQ,         boolOfDoubDoub, emitInfix, -1, cm);
   buildOperator(opGTEQ,         boolOfStrStr, emitInfix, -1, cm);
   buildOperator(opGreaterTh,    boolOfIntInt, emitInfix, -1, cm);
   buildOperator(opGreaterTh,    boolOfDoubDoub, emitInfix, -1, cm);
   buildOperator(opGreaterTh,    boolOfStrStr, emitInfix, -1, cm);
   buildOperator(opNullCoalesce, intOfIntInt, emitInfix, -1, cm); // ?:
   buildOperator(opQuestionMark, intOfIntInt, emitInfix, -1, cm); // dummy, type
   buildOperator(opAwait,        flOfFlFl, emitInfix, -1, cm); // @ dummy, this will be async/await
   buildOperator(opBitwiseXor,   intOfIntInt, emitInfix, -1, cm);
   buildOperator(opBitwiseOr,    intOfIntInt, emitInfix, -1, cm);
   buildOperator(opBoolOr,       flOfFl, emitInfix, -1, cm);
   buildOperator(opGetElem,      flOfFl, emitInfix, -1, cm); // dummy
   buildOperator(opGetElemPtr,   flOfFl, emitInfix, -1, cm); // dummy
}

private void //:createBuiltins
createBuiltins(CM) {
// Entities and functions for the built-in operators, types and functions
   buildStandardStrings(cm);
   buildPreludeTypes(cm);
   buildOperators(cm);
   cm->stats.countOperatorFns = cm->functions.len;
}

private void //:importPrelude
importPrelude(CM) {
// Imports the standard, Prelude stuff into the compiler immediately after the lexing phase
   buildPreludeTypes(cm);
   TypeId const strToVoid = addConcrFnType(1, (Int[]){ tokString, voidType }, cm);
   TypeId const intToVoid = addConcrFnType(1, (Int[]){ tokInt, voidType }, cm);
   TypeId const dblToVoid = addConcrFnType(1, (Int[]){ tokDouble, voidType }, cm);

   // Generic functions
   TypeId listAdd, listLength;
   importGenericTypesList(OUT &listAdd, OUT &listLength, cm);
   Int const genericInd = listCreateMultiAssocList(cm->functionMonos);
   Int const genericInd2 = listCreateMultiAssocList(cm->functionMonos);

   // List length
   Int lengthFnId = cm->functions.len;
   pushInfunctions(
      (Function){ .name = opSize, .typeId = listLength, .hostName = hostLength,
                  .genericInd = genericInd2, .tokenInd = -1, .emit = emitField },
      cm
   );
   addRawOverload(opSize, listLength, lengthFnId, cm);

   //TypeId intToDoub = addConcrFnType(1, (Int[]){ tokInt, tokDouble}, cm);
   //TypeId doubToInt = addConcrFnType(1, (Int[]){ tokDouble, tokInt}, cm);
   Var constImports[2] = {
      (Var){
         .name = nameOfStandard(strMathPi), .typeId = tokDouble, .class = classImm, .fnId = -1
      },
      (Var){
         .name = nameOfStandard(strMathE), .typeId = tokDouble, .class = classImm, .fnId = -1
      },
   };
   Function fnImports[5] =  {
      (Function){ .name = nameOfStandard(strPrint), .typeId = strToVoid, .hostName = hostPrint,
                  .emit = emitHostPrefix },
      (Function){ .name = nameOfStandard(strPrint), .typeId = intToVoid, .hostName = hostPrint,
                  .emit = emitHostPrefix },
      (Function){ .name = nameOfStandard(strPrint), .typeId = dblToVoid, .hostName = hostPrint,
                  .emit = emitHostPrefix },
      (Function){ .name = nameOfStandard(strAdd), .typeId = listAdd, .hostName = hostAdd,
                  .genericInd = genericInd, .tokenInd = -1, .emit = emitHostInfix },
      (Function){ .name = nameOfStandard(strPrintErr), .typeId = strToVoid }
      // TODO functions for casting (int, double, unsigned)
   };

   // These primitive types occupy the first places in the names and in the types table.
   // So for them nameId == typeId, unlike type funcs like L(ist) and A(rray)
   for (Int j = strInt; j <= strVoid; j++) {
      cm->activeBindings[j - strInt + countOperators] = j - strInt;
   }
   importVars(AARG(constImports, Var), cm);
   importFns(AARG(fnImports, Function), cm);
}

private Compiler* //:createLexer
createLexer(String sourceCode, Bool prependStandardText, Arena* a) {
// The proto compiler contains just the built-in definitions and tables. This fn
// copies it and performs initialization. Post-condition: i has been incremented by the
// standardText size
   if (!_wasInit)
      { initCompiler(); }
   Compiler* lx = allocate(Compiler, a);
   Arena* aTmp = createArena();

   (*lx) = (Compiler){
      // this assumes that the source code is prefixed with the "standardText"
      .i = sizeof(standardText) - 1,
      .sourceCode = prependStandardText ? prepareInput(sourceCode.c, a) : sourceCode,
      .tokens = createInListToken(LEXER_INIT_SIZE, a),
      .metas = createInListToken(100, a),
      .newlines = createInListInt(500, a),
      .numeric = createInListInt(50, aTmp),
      .lexBtrack = createLBtToken(16, aTmp),
      .names = copyNames(PROTO.names, a),
      .stringDict = copyStringDict(PROTO.stringDict, a),
      .stats = PROTO.stats,
      .a = a, .aTmp = aTmp
   };
   lx->stats.inpLength = sourceCode.len + (prependStandardText ? (sizeof(standardText) - 1) : 0);
   return lx;
}

private void //:initializeParser
initializeParser(Compiler* lx, Arena* a) {
// Turns a lexer into a parser. Initializes all the parser & typer stuff after lexing is done
   if (lx->wasError)
      { return; }

   Compiler* cm = lx;
   Int initNodeCap = lx->tokens.len > 64 ? lx->tokens.len : 64;
   cm->scopes = createScopes(lx->aTmp);
   cm->backtrack = createLParseFrame(16, lx->aTmp);
   cm->i = 0;

   cm->ast = createInListNode(initNodeCap, a);
   cm->sourceLocs = createLSourceLoc(initNodeCap, a);
   cm->functionMonos = createMultiAssocList(a);

   Expr* stForExprs = allocate(Expr, a);
   (*stForExprs) = (Expr) {
      .exp = createLInt(16, cm->aTmp),
      .frames = createLExprFrame(16*sizeof(ExprFrame), a),
      .scr = createLNode(16*sizeof(Node), a),
      .locsScr = createLSourceLoc(16*sizeof(SourceLoc), a),
      .reorderBuf = createLToken(16*sizeof(Token), a)
   };
   cm->expr = stForExprs;

   cm->rawOverloads = copyMultiAssocList(PROTO.rawOverloads, cm->aTmp);
   cm->overloads = (InListInt){.len = 0, .c = null};

   cm->activeBindings = allocateArray(lx->names->len, Int, lx->aTmp);
   memcpy(cm->activeBindings, PROTO.activeBindings, 4*countOperators); // operators only

   Int extraActive = lx->names->len - countOperators;
   if (extraActive > 0)
      { memset(cm->activeBindings + countOperators, 0xFF, extraActive*4); }

   cm->vars = createInListVar(PROTO.vars.cap, a);
   memcpy(cm->vars.c, PROTO.vars.c, PROTO.vars.len*sizeof(Var));
   cm->vars.len = PROTO.vars.len;
   cm->vars.cap = PROTO.vars.cap;

   cm->functions = createInListFunction(PROTO.functions.cap, a);
   memcpy(cm->functions.c, PROTO.functions.c, PROTO.functions.len*sizeof(Function));
   cm->functions.len = PROTO.functions.len;
   cm->functions.cap = PROTO.functions.cap;

   cm->types.cap = PROTO.types.cap*2;
   cm->types.c = allocateArray(cm->types.cap, Int, a);
   memcpy(cm->types.c, PROTO.types.c, PROTO.types.len*4);
   cm->types.len = PROTO.types.len;

   cm->typesDict = copyStringDict(PROTO.typesDict, a);

   cm->importNames = createInListInt(8, lx->aTmp);
   cm->toplevels = createInListInt(8, lx->a);
   cm->monos = createLMonomorphization(16, lx->a);

   cm->tExpr = allocate(TExpr, a);
   (*cm->tExpr) = (TExpr) {
      .exp = createLInt(16, cm->aTmp),
      .frames = createLTypeFrame(16*sizeof(TypeFrame), cm->aTmp),
      .names = createLInt(16, cm->aTmp),
      .tParams = createLInt(16, cm->aTmp),
      .tmp = createLInt(16, cm->aTmp),
      .fnTypes = createLInt(16, cm->aTmp),
      .genericSt = createLTypeLoc(16, cm->aTmp),
      .concreteSt = createLTypeLoc(16, cm->aTmp),
   };

   importPrelude(cm);
}

private void //:validateNameOverloads
validateNameOverloads(Int listId, Int countOverloads, NameId name, CM) {
// Validates the overloads for a name don't intersect via their outer types
// 1. First parameter outer types must be unique
// 2. A zero-arity function, if any, must be unique
// 3. If a blanket overload (outerTypeForTypeParam) then the only other acceptable one is 0-arity
   Arr(Int) ov = cm->overloads.c;
   Int start = listId + 1;
   Int const outerSentinel = start + countOverloads;
   if (ov[start] == outerTypeForTypeParam)
      { VALIDATEP(outerSentinel == start + 1, errTypeOverloadsIntersect); }

   Int o = start + 1;
   for (Int prevOuter = ov[start]; o < outerSentinel; prevOuter = ov[o], o++) {
#ifdef DEBUG //{{{
      if (ov[o] == prevOuter) {
         print("Overload intersection for name %d ov[k] %d prevOuter %d @o = %d countOvers %d",
            name, ov[o], prevOuter, o, countOverloads);
         printName(name, cm);
         dbgOverloads(name, cm);
      }
#endif //}}}
      VALIDATEP(ov[o] != prevOuter, errTypeOverloadsIntersect)
   }
}

private Int //:createNameOverloads
createNameOverloads(NameId name, CM) {
// Creates a final subtable in @overloads for a name and returns the index of said subtable.
// Precondition: @rawOverloads contain twoples of (typeId ref)
// (typeId = the full type of a function)(ref = entityId or monoId)(yes, "twople" = tuple of two)
// Postcondition: @overloads will contain a subtable of length(outerTypeIds)(refs)
   Arr(Int) raw = cm->rawOverloads->c;
   Int const listId = -cm->activeBindings[name] - 2;
   Int const rawStart = listId + 2;

#if defined(SAFETY) || defined(TEST)
   VALIDATEI(rawStart != -1, iErrorImportedFunctionNotInScope)
#endif
   Int const countOverloads = raw[listId]/2;
   Int const rawSentinel = rawStart + raw[listId];

   Arr(Int) ov = cm->overloads.c;
   Int const newInd = cm->overloads.len;
   ov[newInd] = 2*countOverloads; // length of the subtable for this name
   cm->overloads.len += 2*countOverloads + 1;

   for (Int j = rawStart, k = newInd + 1; j < rawSentinel; j += 2, k++) {
      TypeId const firstParamType = typeOf(raw[j]);
      if (firstParamType.v > -1) {
         TypeId outerType = typeGetOuter(firstParamType, cm);
         ov[k] = outerType.v;
      } else {
         ov[k] = -1;
      }
      ov[k + countOverloads] = raw[j + 1]; // entityId
   }
   sortPairsDistant(newInd + 1, newInd + 1 + 2*countOverloads, countOverloads, ov);
   validateNameOverloads(newInd, countOverloads, name, cm);
   return newInd;
}

private void //:createOverloads
createOverloads(CM) {
// Fills @overloads from @rawOverloads. Replaces all indices in @activeBindings to point to the new
// @overloads table (they pointed to @rawOverloads previously)
   cm->overloads.c = allocateOnArena(
         cm->stats.countOverloads*8 + cm->stats.countOverloadedNames*4, cm->a
   );
   // Each overload requires 2x4 = 8 bytes for the pair of (outerType entityId).
   // Plus you need an int per overloaded name to hold the length of the overloads for that name

   cm->overloads.len = 0;
   for (Int j = 0; j < countOperators; j++) {
      Int newIndex = createNameOverloads(j, cm);
      cm->activeBindings[j] = -newIndex - 2;
   }

   LInt* uniqueFnNames = createLInt(
      cm->functions.len - cm->stats.countNonparsedFns + cm->importNames.len, cm->aTmp
   );

   // Imported functions
   for (Int j = 0; j < cm->importNames.len; j++) {
      add(cm->importNames.c[j], uniqueFnNames);
   }
   // Parsed functions
   for (Int j = cm->stats.countNonparsedFns; j < cm->functions.len; j++) {
      add(cm->functions.c[j].name, uniqueFnNames);
   }
   sortLInts(uniqueFnNames);
   removeDuplicatesInList(uniqueFnNames);
   for (Int j = 0; j < uniqueFnNames->len; j++) {
      NameId name = uniqueFnNames->c[j];
      Int newIndex = createNameOverloads(name, cm);
      cm->activeBindings[name] = -newIndex - 2;
   }
}

private Bool //:determineIfFnDef
determineIfFnDef(Int tokInd, Int const sentinel, TOKS, CM, OUT Int* indRight) {
// Determines if a toplevel definition is a function definition (true ret value) or value (false)
   for (*indRight = cm->i;
       *indRight < sentinel && toks[*indRight].tp != tokAssignRight;
       *indRight++) {}

#ifdef SAFETY
   print("ind Right %d sentinel %d", *indRight, sentinel);
   VALIDATEI((*indRight < sentinel && toks[*indRight].pl2 > 0), iErrorInconsistentSpans);
#endif
   return (toks[(*indRight) + 1].tp == tokFn);
}


private void //:pToplevelTypes
pToplevelTypes(CM) {
// Parses top-level types but not functions. Writes them to the types table and adds
// their bindings to the scope
   cm->i = 0;
   Arr(Token) toks = cm->tokens.c;
   Int const len = cm->tokens.len;
   while (cm->i < len) {
      Token tok = toks[cm->i];
      if (tok.tp == tokDef && tok.pl1 == assiTypeDefinition) {
         cm->i++; // CONSUME the def token
         pTypeDef(toks, cm);
      } else {
         cm->i += (tok.pl2 + 1);
      }
   }
}

private void //:pToplevelConstants
pToplevelConstants(CM) {
// Parses top-level constants but not functions, and adds their bindings to the scope
   cm->i = 0;
   Arr(Token) toks = cm->tokens.c;
   Int const len = cm->tokens.len;
   while (cm->i < len) {
      Token tok = toks[cm->i];
      if (tok.tp == tokDef) {
         Assignment assi = pPreparseAssignment(tok, cm->i + 1, toks, cm);
         if (assi.isFunction)
            { cm->i = assi.sentinel; } // CONSUME the top-level function definition
         else {
            cm->i++; // CONSUME the tokDef
            pAssignmentWorker(tok, assi, toks, cm);
         }
      } else {
         cm->i = calcSentinel(tok, cm->i);
      }
   }
}

#ifdef SAFETY

private void
validateOverloadsFull(CM) {
/*
   Int lenTypes = cm->types.len; Int lenEntities = cm->vars.len;
   for (Int i = 1; i < cm->overloadIds.len; i++) {
      Int currInd = cm->overloadIds.c[i - 1];
      Int nextInd = cm->overloadIds.c[i];

      VALIDATEI((nextInd > currInd + 2) && (nextInd - currInd) % 2 == 1, iErrorOverloadsIncoherent)

      Int countOverloads = (nextInd - currInd - 1)/2;
      Int countConcreteOverloads = cm->overloads.c[currInd];
      VALIDATEI(countConcreteOverloads <= countOverloads, iErrorOverloadsIncoherent)
      for (Int j = currInd + 1; j < currInd + countOverloads; j++) {
         if (cm->overloads.c[j] < 0) {
            throwExcInternal(iErrorOverloadsNotFull, cm);
         }
         if (cm->overloads.c[j] >= lenTypes) {
            throwExcInternal(iErrorOverloadsIncoherent, cm);
         }
      }
      for (Int j = currInd + countOverloads + 1; j < nextInd; j++) {
         if (cm->overloads.c[j] < 0) {
            print("ERR overload missing entity currInd %d nextInd %d j %d cm->overloads.c[j] %d", currInd, nextInd,
               j, cm->overloads.c[j])
            throwExcInternal(iErrorOverloadsNotFull, cm);
         }
         if (cm->overloads.c[j] >= lenEntities) {
            throwExcInternal(iErrorOverloadsIncoherent, cm);
         }
      }
   }
*/
}

#endif

TypeId //:pFnCreateType
pFnCreateType(TExpr* te, CM) {
   Int const depth = te->fnTypes->len;
   TYPE_CREATE_START(
      ((TypeHeader){ .sort = sorDeclare, .isGeneric = te->isGeneric,
                     .tyrity = te->tParams->len, .arity = depth, .name = nameOfStandard(strF) })
   );
   for (Int j = 0; j < depth; j++) {
      pushIntypes(te->fnTypes->c[j], cm);
   }
   TYPE_CREATE_END;
   return mergeType(tentativeType, cm);
}

private void //:pFnSignature
pFnSignature(Assignment fnAssign, TypeId voidToVoid, TOKS, CM) {
// Parses a function signature. Emits no nodes, adds data to @toplevels, @functions, @overloads.
// Pre-condition: we are at tokFnParams
   TExpr* te = cm->tExpr;
   Int const indParams = cm->i;

#ifdef SAFETY
   VALIDATEI(toks[cm->i].tp == tokFnParams, iErrorInconsistentSpans);
#endif

   Token paramListTk = toks[cm->i];
   Int paramsSentinel = calcSentinel(paramListTk, cm->i);
   TypeId newFnType = voidToVoid; // default for nullary functions
   Bool const hasReturnType = fnAssign.rightTokenInd - fnAssign.nameTokenInd > 1;

   te->isGeneric = false;
   if (!hasReturnType && paramListTk.pl2 == 0) // A void -> void function
      { goto entityAdding; }

   te->tParams->len = 0; // list of params pertains to the whole function
   TypeId returnType = typeOf(voidType);
   if (hasReturnType) {
      cm->i = fnAssign.nameTokenInd; // To function name token
      returnType = teClause(te, fnAssign.rightTokenInd, toks, cm);
   }
   if (paramListTk.pl2 == 0)
      { goto entityAdding; }

   tFreshState(te);
   Int arity = 0;
   te->fnTypes->len = 0;
   for (cm->i = indParams + 1; cm->i < paramsSentinel;) {
      Token clause = toks[cm->i];
      VALIDATEP(clause.tp == tokClause, errTypeDefCannotContain)
      Int const clauseSentinel = calcSentinel(clause, cm->i);
      cm->i++; // CONSUME the tokStmt
      TypeId paramType = teClause(te, clauseSentinel, toks, cm);

      add(paramType.v, te->fnTypes);
      cm->i = clauseSentinel; // CONSUME the statement
      arity++;
   }

   if (arity == 0)
      { add(voidType, te->fnTypes); }
   add(returnType.v, te->fnTypes);
   newFnType = pFnCreateType(te, cm);
   entityAdding:
   FunctionId newFnId = cm->functions.len;
   fnAssign.entityId = -(newFnId) - 1;

   Int genericInd = te->isGeneric ? listCreateMultiAssocList(cm->functionMonos) : -1;
   pushInfunctions(
      ((Function){ .name = fnAssign.name, .typeId = newFnType, .emit = emitPrefix,
                   .genericInd = genericInd, .tokenInd = fnAssign.rightTokenInd + 1 }),
      cm
   );
   addRawOverload(fnAssign.name, newFnType, newFnId, cm);
   pushIntoplevels(newFnId, cm);
}

private void //:pToplevelBodyWorker
pToplevelBodyWorker(Int tokenInd, Int funcOrMonoId, TypeId concreteType, Byte callSort, TOKS, CM) {
   cm->i = tokenInd; // tokFn
   Token fnTk = toks[tokenInd];
   Int const fnSentinel = calcSentinel(fnTk, tokenInd);

   openFnScope(funcOrMonoId, concreteType, callSort, locOf(fnTk), fnSentinel, cm);

   cm->i++; // CONSUME the tokFn token
   Token paramsTk = toks[cm->i]; // tokFnParams
   Int const paramsSentinel = calcSentinel(paramsTk, cm->i);
   if (paramsTk.pl2 == 0)
      { goto bodyParsing; }

   cm->i++; // CONSUME the tokFnParams
   for (
      Int j = tGetIndexOfFnFirstParam(concreteType, cm).v;
      cm->i < paramsSentinel;
      cm->i = calcSentinel(toks[cm->i], cm->i), // CONSUME the tokens of param clause
      j++
   ) {
      // must get params type from the concrete function type we got, not
      // from tokens (where they may be generic)

      Token paramNameTk = toks[cm->i + 1];
      TypeId paramType = typeOf(cm->types.c[j]);
      NameId name = paramNameTk.pl1;
      VarId newVarId = createVarWithType(
            name, paramType, paramNameTk.pl2 == 1 ? classMutable : classImm, -1, cm
      );
      newNode(
            ((Node){.tp = nodVar, .pl1 = newVarId, .pl2 = 0, .pl3 = assiFnParam}),
            locOf(paramNameTk), cm
      );
   }
   bodyParsing:
   cm->i = paramsSentinel;
   parseUpTo(fnSentinel, toks, cm);
}

private void //:pToplevelBody
pToplevelBody(FunctionId fnId, TOKS, CM) {
// Parses a top-level function. The result is the AST [ FnDef ParamList body... ]
// Uses the function's type to introduce local vars for the function params
   cm->stats.loopCounter = 0;
   cm->functions.c[fnId].nodeInd = cm->ast.len;
   Function fn = cm->functions.c[fnId];
   TypeId fnType = fn.typeId;
   TypeHeader hdr = typeReadHeader(fnType, cm);
   if (hdr.isGeneric) // generic functions arn't parsed, only their monomorphizations
      { return; }

   pToplevelBodyWorker(fn.tokenInd, fnId, fnType, callNormal, toks, cm);
}

void //:generateMonomorphizations
generateMonomorphizations(TOKS, CM) {
// Generate monomorphizations for any code in @genericCalls
   for (Monomorphization* m = cm->monos->c; m < cm->monos->c + cm->monos->len; m++) {
      Int const newFnId = cm->functions.len;
      if (m->tokenInd != -1) { // parsed functions
         m->nodeInd = cm->ast.len;
         pushInfunctions(
            ((Function){ .name = m->name, .typeId = m->concrete, .emit = emitPrefix,
                         .nodeInd = cm->ast.len, .genericInd = -1, .tokenInd = m->tokenInd }),
            cm
         );
         pushIntoplevels(newFnId, cm);
         m->fnId = newFnId;
         pToplevelBodyWorker(m->tokenInd, m - cm->monos->c, m->concrete, callMonomorph,
            toks, cm);
      } else { // imported host functions
         pushInfunctions(
            ((Function){ .name = m->name, .typeId = m->concrete,
                         .emit = cm->functions.c[m->fnId].emit,
                         .hostName = cm->functions.c[m->fnId].hostName,
                         .nodeInd = -1, .genericInd = -1, .tokenInd = -1 }),
            cm
         );
         pushIntoplevels(newFnId, cm);
         m->fnId = newFnId;
      }
   }
}

private void //:pFunctionBodies
pFunctionBodies(TOKS, CM) {
// Parses top-level function params and bodies
   for (int j = 0; j < cm->toplevels.len; j++) {
      pToplevelBody(cm->toplevels.c[j], toks, cm);
   }
}

private void //:pToplevelSignatures
pToplevelSignatures(TOKS, CM) {
// Walks the top-level functions' signatures (but not bodies). Increments counts of overloads
// Result: the overload counts and the list of toplevel functions to parse. No nodes emitted
   cm->i = 0;
   Int const len = cm->tokens.len;

   TypeId const voidToVoid = addConcrFnType(1, (Int[]){ voidType, voidType}, cm);

   Int nextI = 0;
   for (Token tok = toks[cm->i]; cm->i < len; cm->i = nextI, tok = toks[cm->i]) {
      nextI = calcSentinel(tok, cm->i);
      if (tok.tp != tokDef)
         { continue; }
      Assignment fnAssign = pPreparseAssignment(tok, cm->i + 1, toks, cm);
      if (!fnAssign.isFunction)
         { continue; }

      Token nameTk = toks[cm->i + 1];
      if (!(nameTk.tp == tokWord && nameTk.pl2 == 0))  {
         print("i %d name tp %d", cm->i, nameTk.tp)
      }
      VALIDATEP(nameTk.tp == tokWord && nameTk.pl2 == 0, errAssignmentToplevelFn)

      // since this is an immutable definition tokDef, its pl1 is the nameId
      NameId name = (Unt)nameTk.pl1;
      cm->i = fnAssign.rightTokenInd + 2; // CONSUME the left side, tokAssignmentRight and tokFn
      fnAssign.name = name;

      pFnSignature(fnAssign, voidToVoid, toks, cm);
   }
}

void //:parseMain
parseMain(CM, Arena* a) {
   if (setjmp(excBuf) == 0) {
      Arr(Token) toks = cm->tokens.c;

      pToplevelTypes(cm);
      // This gives the complete overloads & overloadIds tables + list of toplevel functions
      pToplevelSignatures(toks, cm);
      createOverloads(cm);
      pToplevelConstants(cm);

#ifdef SAFETY
      validateOverloadsFull(cm);
#endif
      // The main parse (all top-level function bodies)
      pFunctionBodies(toks, cm);
      // Parse & typecheck all the necessary monomorphized versions of generic functions
      generateMonomorphizations(toks, cm);
      updateStats(cm);
   } else {
#ifndef TEST
      print("Exception!");
#endif
   }
}

Compiler* //:parse
parse(CM, Arena* a) {
// Parses a single file in 4 passes, see docs/parser.txt
   initializeParser(cm, a);
   parseMain(cm, a);
   clearArena(cm->aTmp);
   return cm;
}

//}}}
//{{{ Types

//{{{ Type utils

#define TYPE_DEFINE_EXP const LInt* exp = te->exp

private Int //:typeEncodeTag
typeEncodeTag(Unt sort, Int depth, Int arity, CM) {
   return (Int)((Unt)(sort << 16) + (depth << 8) + arity);
}

private void //:typeAddHeader
typeAddHeader(TypeHeader hdr, CM) {
// Writes the bytes for the type header to the tail of the cm->types table.
// Adds one 4-byte element
   pushIntypes((Int)((Unt)((Unt)hdr.isGeneric << 24) + (Unt)((Unt)hdr.sort << 16) +
         ((Unt)hdr.arity << 8) + hdr.tyrity), cm);
   pushIntypes(hdr.name, cm);
}

private void //:typeExpAddHeader
typeExpAddHeader(TypeHeader hdr, TExpr* te) {
// Writes the bytes for the type header to the tail of the cm->types table.
// Adds one 4-byte element
   add((Int)((Unt)((Unt)hdr.sort << 16) + ((Unt)hdr.arity << 8) + hdr.tyrity), te->exp);
   add(hdr.name, te->exp);
}

private TypeHeader //:typeReadHeader
typeReadHeader(TypeId t, CM) {
// Reads a type header from the type array. Does not work for primitive types
   Int tag = cm->types.c[t.v + 1];
   return (TypeHeader){ .isGeneric = (tag >> 24) > 0, .sort = ((Unt)tag >> 16) & LOWER16BITS,
         .arity = (tag >> 8) & 0xFF, .tyrity = tag & 0xFF,
         .name = cm->types.c[t.v + 2]
   };
}

TypeHeader //:tech_sozonov_eyr_readTypeHeader
tech_sozonov_eyr_readTypeHeader(TypeId t, Arr(Int) types) {
// Reads a type header from the type array. Does not work for primitive types
   Int tag = types[t.v + 1];
   return (TypeHeader){ .isGeneric = (tag >> 24) > 0, .sort = ((Unt)tag >> 16) & LOWER16BITS,
         .arity = (tag >> 8) & 0xFF, .tyrity = tag & 0xFF, .name = types[t.v + 2]
   };
}

private Int //:typeGetTyrity
typeGetTyrity(TypeId typeId, CM) {
   return (cm->types.c[typeId.v] == 0) ? 0 : cm->types.c[typeId.v + 1] & 0xFF;
}

private TypeId //:typeGetOuter
typeGetOuter(TypeId t, CM) {
// A          => A  (concrete simple types)
// (A B)      => A  (concrete complex types)
// (F A -> B) => (F A -> B) (function types are their own outer types)
// Undefined for generic types
   if (t.v <= topVerbatimType)
      { return t; }
   TypeHeader hdr = typeReadHeader(t, cm);
   if (hdr.sort == sorGenericParam)
      { return typeOf(outerTypeForTypeParam); }
   return hdr.name == nameOfStandard(strF)
      ? t
      : typeOf(cm->types.c[t.v + TYPE_PREFIX_LEN]);
}

private TypeId //:typeGetGenericParam
typeGetGenericParam(TypeId t, Int ind, CM) {
// (S Foo) -> Foo
   return typeOf(cm->types.c[t.v + TYPE_PREFIX_LEN + ind]);
}

private TypeId //:tGetIndexOfFnFirstParam
tGetIndexOfFnFirstParam(TypeId fnType, CM) {
#ifdef SAFETY //{{{
   NameId name = typeReadHeader(fnType, cm).name;
#ifdef DEBUG
   if (name != nameOfStandard(strF))
      { print("A function is not a function! TypeId = %d", fnType); }
#endif
   VALIDATEI(name == nameOfStandard(strF), iErrorNotAFunction);
#endif //}}}
   return typeOf(fnType.v + TYPE_PREFIX_LEN);
}

private Int //:tGetFnArity
tGetFnArity(TypeId fnType, CM) {
   TypeHeader hdr = typeReadHeader(fnType, cm);
#ifdef SAFETY //{{{
   if(hdr.name != nameOfStandard(strF)) {
      print("name %d but should've been %d for type %d ", hdr.name, nameOfStandard(strF), fnType.v);
      dbgType(fnType);
   }
   VALIDATEI(hdr.name == nameOfStandard(strF), iErrorNotAFunction);
#endif //}}}
   return hdr.arity;
}

private Int //:tIsFunction
tIsFunction(TypeId t, CM) {
// Returns the function's arity if the type is a function type, -1 otherwise
   if (t.v < topVerbatimType)
      { return -1; }
   TypeHeader hdr = typeReadHeader(t, cm);
   return (hdr.name == nameOfStandard(strF)) ? (hdr.arity - 1) : -1;
}

private TypeLoc //:tGetBody
tGetBody(TypeId ty, CM) {
   return (TypeLoc){
      .currPos = ty.v + TYPE_PREFIX_LEN,
      .sentinel = ty.v + TYPE_PREFIX_LEN + typeReadHeader(ty, cm).arity
   };
}

//}}}
//{{{ Parsing type names

private void //:typeAddTypeParam
typeAddTypeParam(Int paramInd, Int tyrity, CM) {
// Adds a type param to a TypeCall-sort type. Tyrity > 0 means the param is a type call
   pushIntypes((0xFF << 24) + (paramInd << 8) + tyrity, cm);
}

private void //:typeAddTypeCall
typeAddTypeCall(Int typeInd, Int arity, CM) {
// Known type fn call
   pushIntypes((arity << 24) + typeInd, cm);
}

private TypeId //:typeGetTypeByName
typeGetTypeByName(Int t, CM) {
   Int mbTypeId = cm->activeBindings[t];
   VALIDATEP(mbTypeId > -1, errUnknownType);
   return typeOf(mbTypeId);
}

private Int //:typeParamBinarySearch
typeParamBinarySearch(Int nameIdToFind, CM) {
// Performs a binary search of the binary params in {typeParams}. Returns index of found type param,
// or -1 if nothing is found
   LInt* params = cm->tExpr->tParams;
   if (params->len == 0) {
      return -1;
   }
   Arr(Int) st = params->c;
   Int i = 0;
   Int j = params->len - 2;
   if (st[i] == nameIdToFind) {
      return i;
   } ei (st[j] == nameIdToFind) {
      return j;
   }

   while (i < j) {
      if (j - i == 2) {
         return -1;
      }
      Int midInd = (i + j)/2;
      if (midInd % 2 == 1) {
         midInd--;
      }
      Int mid = st[midInd];
      if (mid > nameIdToFind) {
         j = midInd;
      } ei (mid < nameIdToFind) {
         i = midInd;
      } else {
         return midInd;
      }
   }
   return -1;
}

//}}}
//{{{ Type expressions

private void //:tFreshState
tFreshState(TExpr* te) {
   te->frames->len = 0;
   te->exp->len = 0;
}

private Int //:tSubexValidateNamesUnique
tSubexValidateNamesUnique(TExpr* te, Int start, CM) {
// Validates that the names in a record or function sign are unique.
// Returns function/record's arity
   Int const end = te->names->len;
   if (end == 0)
      { return 0; }
   LInt* names = te->names;
   LInt* tmp = te->tmp;
   // copy from names to tmp
   if (tmp->cap < names->len) {
      Arr(Int) arr = allocateArray(names->len, Int, cm->aTmp);
      tmp->c = arr;
      tmp->cap = names->len;
   }
   memcpy(tmp->c, names->c, names->len);
   tmp->len = names->len;

   sortLInts(tmp);
   NameId prev = tmp->c[0];
   for (Int j = 1; j < tmp->len; j++) {
      if (tmp->c[j] == prev)
         { throwExcParser(errFnDuplicateParams); }
   }
   Int const countNames = names->len;
   names->len = 0;
   return countNames;
}

/*
private TypeId typeCreateRecord(TExpr* st, Int startInd, Unt nameAndLen,
                        CM) { //:typeCreateRecord
// Creates/merges a new record type from a sequence of pairs in @exp and a list of type params
// in @params. The sequence must be flat, i.e. not include any nested structs, and be in the
// final position of @exp. "nameAndLen" may be -1 if it's an anonymous record.
// Returns the typeId of the new/existing type
   TYPE_DEFINE_EXP;
   tSubexValidateNamesUnique(st, startInd, exp->len, cm);
   Int tentativeTypeId = cm->types.len;
   pushIntypes(0, cm);
   Int sentinel = exp->len;

#ifdef SAFETY
   VALIDATEP((sentinel - startInd) % 4 == 0, "typeCreateStruct err not divisible by 4")
#endif
   Int countFields = (sentinel - startInd)/4;
   typeAddHeader((TypeHeader){
      .sort = sorDeclare, .tyrity = st->params->len/2, .arity = countFields,
      .nameAndLen = nameAndLen }, cm);
   for (Int j = 1; j < st->params->len; j += 2) {
      pushIntypes(st->params->c[j], cm);
   }

   for (Int j = startInd + 1; j < sentinel; j += 4) {
      // names of fields
      pushIntypes(exp->c[j], cm);
   }

   for (Int j = startInd + 3; j < sentinel; j += 4) {
      // types of fields
#ifdef SAFETY
      VALIDATEP(exp->c[j - 1] == tyeType, "not a type")
#endif
      pushIntypes(exp->c[j], cm);
   }
   cm->types.c[tentativeTypeId] = cm->types.len - tentativeTypeId - 1;
   return mergeType(tentativeTypeId, cm);
}
*/

private TypeId //:tCreateTypeCall
tCreateTypeCall(TExpr* te, Byte sort, Int startInd, TypeFrame frame, CM) {
// Creates/merges a new type call from a sequence of pairs in @exp
// Handles ordinary type calls and function types. Returns the typeId of the new type
   TypeId genericId = frame.id;
   TypeHeader genericHdr = typeReadHeader(genericId, cm);
   VALIDATEP(genericHdr.tyrity == frame.countArgs, errTypeConstructorWrongArity)
   TYPE_DEFINE_EXP;

   Int const sentinel = exp->len;

   TYPE_CREATE_START(
      ((TypeHeader){ .sort = sort, .tyrity = 0,
         .arity = (sentinel - startInd + 1),
         .name = genericHdr.name, .isGeneric = false })
   );
   pushIntypes(frame.id.v, cm);
   for (Int j = startInd; j < sentinel; j++) {
      pushIntypes(exp->c[j], cm);
   }

   TYPE_CREATE_END;
   TypeId r =  mergeType(tentativeType, cm);
   return r;
}

private TypeId //:teMergeParam
teMergeParam(NameId name, TExpr* restrict te, CM) {
   LInt* params = te->tParams;
   Int deBruijnIndex = -1;
   for (Int j = 0; j < params->len; j += 2) {
      if (params->c[j] == name) {
         deBruijnIndex = j;
         break;
      }
   }
   if (deBruijnIndex == -1)  {
      deBruijnIndex = params->len;
      add(name, params);
   }

   TYPE_CREATE_START(
      ((TypeHeader){ .sort = sorGenericParam, .tyrity = 0, .arity = 0, .name = deBruijnIndex,
         .isGeneric = true })
   );
   TYPE_CREATE_END;
   TypeId paramType = mergeType(tentativeType, cm);

   add(paramType.v, te->exp);
   te->isGeneric = true;
   return paramType;
}

private TypeId //:tCreateFnTypeCall
tCreateFnTypeCall(TExpr* te, Int startInd, TypeFrame frame, CM) {
   TYPE_DEFINE_EXP;

   Int const depth = exp->len - startInd; // this isn't function arity, it's type arity
   Int const sentinel = exp->len;

   TYPE_CREATE_START(
      ((TypeHeader){ .sort = sorDeclare, .tyrity = 0, .arity = depth, .name = nameOfStandard(strF),
               .isGeneric = false})
   );
   //pushIntypes(nameOfStandard(strF), cm);
   for (Int j = startInd; j < sentinel; j++) {
      pushIntypes(exp->c[j], cm);
   }

   TYPE_CREATE_END;
   return mergeType(tentativeType, cm);
}

private TypeId //:tCreateSingleParamTypeCall
tCreateSingleParamTypeCall(TypeId outer, TypeId param, CM) {
// Creates a type like (L Int)
   TypeId listType = typeOf(cm->activeBindings[nameOfStandard(strL)]);
   TYPE_CREATE_START(
      ((TypeHeader){.sort = sorTypeCall, .tyrity = 0, .arity = 2,
         .name = nameOfStandard(strL), .isGeneric = false})
   );
   pushIntypes(listType.v, cm);
   pushIntypes(param.v, cm);

   TYPE_CREATE_END;
   TypeId res = mergeType(tentativeType, cm);
   return res;
}

private TypeId //:tCreateFnSignature
tCreateFnSignature(TExpr* te, Int startInd, CM) {
// Creates/merges a new function type from a sequence of types in "exp" & a list of type params
// in "params". The sequence must be flat, i.e. not include any nested structs.
// Returns the typeId of the new type
// Example input: exp () params ()
   TYPE_DEFINE_EXP;

   Int const sentinel = exp->len;

   Int const depth = exp->len - startInd;
   Int const arity = depth - 1;
   Int const countNames = tSubexValidateNamesUnique(te, startInd, cm);

   VALIDATEP(depth == countNames, errTypeDefCountNames);
   Int tyrity = te->tParams->len;

   TYPE_CREATE_START(
      ((TypeHeader){ .sort = sorDeclare, .tyrity = tyrity, .arity = arity, .isGeneric = false })
   );
   pushIntypes(nameOfStandard(strF), cm);
   for (Int j = startInd; j < sentinel; j++) {
      // types of params and return type
      pushIntypes(exp->c[j], cm);
   }
   TYPE_CREATE_END;
   return mergeType(tentativeType, cm);
}


#define maxTypeParams 254

private void //:teClose
teClose(TExpr* te, CM) {
// Flushes the finished subexpr frames from the top of the funcall stack.
// Handles data allocations
   LInt* exp = te->exp;
   LTypeFrame* frames = te->frames;
   while (frames->len > 0 && last(frames).sentinel == cm->i) {
      TypeFrame frame = removeLast(frames);
      Int startInd = exp->len - frame.countArgs;
      TypeId newType = ZERO_ARITY_TYPE;

      if (frame.tp == tfrFunction) {
         newType = tCreateFnSignature(te, startInd, cm);
      } ei (frame.tp == tfrFnTypeCall)  {
         newType = tCreateFnTypeCall(te, startInd, frame, cm);
      } ei (frame.tp == tfrRecord) {
         throwExcParser(errTemp);
         //typeCreateRecord(st, startInd, -1, cm);
      } ei (frame.tp == tfrTypeCall) {
         newType = tCreateTypeCall(te, sorTypeCall, startInd, frame, cm);
      } else { // tyeParamCall, a call of a type which is a parameter
         // TODO
         throwExcParser(errTemp);
      }
      exp->c[startInd] = newType.v;
      exp->len = startInd + 1; // +1 because we've put one type for the call we've reduced
   }
}

private void //:teOpenTypeCall
teOpenTypeCall(NameId typeName, Int sentinel, LTypeFrame* frames, CM) {
// Adds a new type call to @exp during type expression parsing
   if (typeName == nameOfStandard(strF)) { // F ...
      add(((TypeFrame){ .tp = tfrFnTypeCall, .sentinel = sentinel}), frames);
   } ei (typeName == nameOfStandard(strRec)) { // inline types  `(id Int name String)`
      add(((TypeFrame){ .tp = tfrRecord, .sentinel = sentinel}), frames);
   } else { // ordinary type call
      TypeId typeId = typeOf(cm->activeBindings[typeName]);
      VALIDATEP(typeId.v > -1, errUnknownTypeConstructor)
      add(((TypeFrame){.tp = tfrTypeCall, .id = typeId, .sentinel = sentinel}),
         frames);
   }
}

private TypeId //:teClauseComplexType
teClauseComplexType(TExpr* te, Int sentinel, TOKS, CM) {
// For a clause like `lst L Double`, parses the `L Double` part.
// Precondition: we are looking JUST PAST the first type token (`Double` in this example),
// while the first one has been added as a type call.
   LInt* exp = te->exp;
   LTypeFrame* frames = te->frames;
   while (cm->i < sentinel) {
      teClose(te, cm);
      Token cTk = toks[cm->i];
      cm->i++; // CONSUME the current token

      VALIDATEP(frames->len > 0, errTypeDefError)
      if (cTk.tp == tokWord) { // name of a field in a struct/variant
         VALIDATEP(cm->i < sentinel, errTypeDefError)
         Int ctxType = last(frames).tp;
         VALIDATEP(ctxType == sorDeclare, errTypeDefError)

         Token nextTk = cm->tokens.c[cm->i];
         VALIDATEP(nextTk.tp == tokTypeName || nextTk.tp == tokTypeCall, errTypeDefError)
         add(cTk.pl1, te->names);
         continue;
      }

      frames->c[frames->len - 1].countArgs++;

      if (cTk.tp == tokTypeName) {
         add(typeGetTypeByName(cTk.pl1, cm).v, exp);
      } ei (cTk.tp == tokTypeVar) {
         // create/reuse a type of sorGenericParam for a newly encountered type param
         NameId name = cTk.pl1;
         teMergeParam(name, te, cm);
      } ei (cTk.tp == tokParens) {
         VALIDATEP(cm->i < sentinel, errTypeDefError)
         Token typeFuncTk = cm->tokens.c[cm->i];
         VALIDATEP(typeFuncTk.tp == tokTypeName, errTypeDefError)

         Int const typeCallSent = calcSentinel(cTk, cm->i - 1);

         teOpenTypeCall(typeFuncTk.pl1, typeCallSent, frames, cm);
         cm->i++; // CONSUME the type function's name
      } else {
         print("erroneous type %d", cTk.tp)
         throwExcParser(errTypeDefError);
      }
   }

   teClose(te, cm);

   VALIDATEI(exp->len == 1, iErrorInconsistentTypeExpr);
   return typeOf(exp->c[0]);
}

private TypeId //:tExpr
tExpr(TExpr* te, Int sentinel, TOKS, CM) {
// Parses `L Double`.
// Precondition: we are looking at the first type token (e.g. `L`).
// Produces a linear, RPN sequence. Populates @te.exp
   Token firstTypeTk = toks[cm->i];
   VALIDATEP(firstTypeTk.tp == tokTypeName || firstTypeTk.tp == tokTypeVar, errTypeDefError)
   if (cm->i + 1 == sentinel) { // single-name type
      if (firstTypeTk.tp == tokTypeName)  {
         TypeId simpleType = typeGetTypeByName(firstTypeTk.pl1, cm);
         add(simpleType.v, te->exp);
         return simpleType;
      } else {
         return teMergeParam(firstTypeTk.pl1, te, cm);
      }
   } else  {

      teOpenTypeCall(firstTypeTk.pl1, sentinel, te->frames, cm);
      cm->i++; // CONSUME the first type name (which is actually a call)
      return teClauseComplexType(te, sentinel, toks, cm);
   }
}

private TypeId //:teClause
teClause(TExpr* te, Int sentinel, TOKS, CM) {
// Parses `lst S Double`.
// Precondition: we are looking at the name token (e.g. `lst`).
// @te.frames, @te.exp etc must be empty. Produces a linear, RPN sequence.
   tFreshState(te);
   Token nameTk = toks[cm->i];
   VALIDATEP(nameTk.tp == tokWord, errTypeDefError)
   add(nameTk.pl1, te->names);
   cm->i++; // CONSUME the name of the clause
   return tExpr(te, sentinel, toks, cm);
}

private TypeId //:pTypeDef
pTypeDef(TOKS, CM) {
// Builds a type expression from a type definition or a function signature.
// Example 1: `Foo = (Rec id Int; name String;)`
// Example 2: `(F a Double; b Bool; String;)`
//
// Accepts a name or -1 for nameless type exprs (like function signatures).
// Uses cm->exp to build a "type expression" and cm->params for the type parameters
// Produces no AST nodes, but potentially lots of new types
// Consumes the whole type assignment right side, or the whole function signature
// Data format: see "Type expression data format"
// Precondition: we are 1 past the tokAssignmentRight token, or tokFnParams token
   VALIDATEP(toks[cm->i + 1].tp == tokAssignRight, errAssignmentLeftSide)
   cm->tExpr->frames->len = 0;

   Int sentinel = cm->i + toks[cm->i - 1].pl2; // we get the length from the tokAssignmentRight
   Token nameTk = toks[cm->i];
   cm->i += 2; // CONSUME the type name and the tokAssignmentRight

   VALIDATEP(cm->i < sentinel, errTypeDefError)
   TypeId newType = tExpr(cm->tExpr, sentinel, toks, cm);
   NameId name = nameTk.pl1;
   cm->activeBindings[name] = newType.v;
   cm->types.c[newType.v + 1] = name;
   return newType;
}

//}}}
//{{{ Overloads, type check & resolve

private TypeId //:getFirstParamType
getFirstParamType(TypeId t, CM) {
// Gets the type of the first param of a type. Returns -1 iff it's zero arity
   TypeHeader hdr = typeReadHeader(t, cm);
   if (hdr.arity == 0)
      { return ZERO_ARITY_TYPE; }
   return typeOf(cm->types.c[t.v + TYPE_PREFIX_LEN]);
}

private TypeId //:getFirstParamInd
getFirstParamInd(TypeId funcTypeId, CM) {
// Gets the ind of the first param of a function. Precondition: function has a non-zero arity!
   TypeHeader hdr = typeReadHeader(funcTypeId, cm);
   return typeOf(funcTypeId.v + TYPE_PREFIX_LEN + hdr.tyrity);
}

private TypeId //:tFunctionReturnType
tFunctionReturnType(TypeId funcTypeId, CM) {
   TypeHeader hdr = typeReadHeader(funcTypeId, cm);
   return typeOf(cm->types.c[funcTypeId.v + TYPE_PREFIX_LEN + hdr.arity - 1]);
}

private Bool //:isFunctionWithParams
isFunctionWithParams(TypeId typeId, CM) {
   return cm->types.c[typeId.v] > 1;
}

private Bool //:tFindOverload
tFindOverload(TypeId typeId, Int ovInd, CM, OUT FunctionId* fn) {
// Params: typeId = type of the first function parameter, or -1 if it's 0-arity
//         ovInd = ind in @overloads, which is found via @activeBindings
//         entityId = address where to store the result, if successful
// We have 4 scenarios here, sorted from left to right in the outerType part of [overloads]:
// 1. outerType = -1 => 0-arity function
// 2. outerType = outerTypeForTypeParam => a blanket overload
// 3. contains (0 BIG) outerType => non-function types with outer concrete, e.g. "L U" => ind of L
// 4. outerType >= BIG: function types (generic or concrete), e.g. "(F Int -> String)" => BIG + 1
   Int const start = ovInd + 1;
   Arr(Int) overs = cm->overloads.c;
   Int const countOverloads = overs[ovInd]/2;
   Int const sentinel = ovInd + countOverloads + 1;
   if (eq(typeId, ZERO_ARITY_TYPE)) { // scenario 1
      Int j = ovInd + 1;
      if (j < sentinel && overs[j] == -1) {
         (*fn) = overs[j + countOverloads];
         return true;
      } else {
         return false;
      }
   }

   TypeId const outerType = typeGetOuter(typeId, cm);
   Int mbFuncArity = tIsFunction(typeId, cm);
   if (mbFuncArity > -1) { // scenario 4
      mbFuncArity += BIG;
      Int j = sentinel - 1;
      for (; j > start && overs[j] > BIG; j--) {
         if (overs[j] == mbFuncArity) {
            (*fn) = overs[j + countOverloads];
            return true;
         }
      }
   } else { // scenarios 2 or 3
      Int firstNonneg = start;
      for (; firstNonneg < sentinel && overs[firstNonneg] < 0; firstNonneg++);

      Int k = sentinel - 1;
      for (; k > firstNonneg && overs[k] >= BIG; k--) {}
      if (k < firstNonneg)
         { return false; }
      if (overs[firstNonneg] == outerTypeForTypeParam) {
         (*fn) = overs[firstNonneg + countOverloads];
         return true;
      }

      Int ind = binarySearch(outerType.v, firstNonneg, k + 1, overs);
      if (ind == -1)
         { return false; }
      (*fn) = overs[ind + countOverloads];
      return true;
   }
   return false;
}

private FunctionId //:findOverload
findOverload(NameId name, TypeId tpFstArg, CM) {
   Int indOverl = -cm->activeBindings[name] - 2;
   VALIDATEP(tpFstArg.v > -1, errTypeUnknownFirstArg)
   Int fnId;
   Bool ovFound = tFindOverload(tpFstArg, indOverl, cm, OUT &fnId);
#if defined(DEBUG) //{{{
   if (!ovFound) {
      print("Overload not found: indOverl %d name %d j %d", indOverl, name, cm->j)
      printLexer(cm);
      printLInt(cm->expr->exp);
   }
#endif //}}}
   if (name == 98) {
      printName(98, cm);
   }
   VALIDATEP(ovFound, errTypeNoMatchingOverload)
   return fnId;
}

private FunctionId //:eFindOverload
eFindOverload(NameId name, Int argCount, LInt* exp, CM) {
   TypeId tpFstArg;
   if (argCount == 0) {
      tpFstArg = VOID_TYPE;
   } else {
      tpFstArg = typeOf(exp->c[exp->len - argCount]);
      if (tpFstArg.v == -1) { //{{{
         Int a = exp->c[exp->len - argCount];
         print("can't get first type of type %d name %d cmj %d", a, name, cm->j);
      } //}}}
      VALIDATEP(tpFstArg.v > -1, errTypeUnknownFirstArg)
   }
   return findOverload(name, tpFstArg, cm);
}

private void //:typeCheckCall
typeCheckCall(Node nd, LInt* restrict exp, CM) {
   if (nd.pl3 == callGetElem) {
      VALIDATEP(exp->len >= 2, errExpressionError)

      TypeId type1 = typeOf(exp->c[exp->len - 2]);
      TypeId outer1 = typeGetOuter(type1, cm);
      VALIDATEP(outer1.v == cm->stats.listType, errTypeOfNotList)

      TypeId type2 = typeOf(exp->c[exp->len - 1]);
      VALIDATEP(eq(type2, intTy), errTypeOfListIndex) // list index == Int

      TypeId eltType = typeGetGenericParam(type1, 1, cm);
      exp->len -= 2; // replace collection and its index type (Int) with element type
      add(eltType.v, exp);
   } ei (nd.pl3 == callField) { // a field accessor
      VALIDATEP(exp->len >= 1, errExpressionError)
      NameId name = nd.pl1;
      NameId mbAltName = -1;
      Int prevType = exp->c[exp->len - 1];
      VALIDATEP(prevType > topVerbatimType, errTypeFieldNotFound);
      TypeId fieldType = typeTryGetFieldType(name, typeOf(prevType), OUT &mbAltName, cm);

      cm->ast.c[cm->j].pl1 = fieldType.v;
      if (mbAltName != -1)
         { cm->ast.c[cm->j].pl1 = mbAltName; }

      exp->c[exp->len - 1] = fieldType.v;
   } else {
      // A function call. cont[j] contains the argument count, cont[j + 1] index in @overloads
      Bool isVarCall = nd.pl3 == callVar;
      Int const argCount = nd.pl2;
      Int const name = nd.pl1; // name for function calls, varId for var calls

      Int fnId = -1;
      TypeId typeOfFunc;
      Bool isGeneric = false;
      VarId varId;
      if (!isVarCall) {
         fnId = eFindOverload(name, argCount, exp, cm);
         typeOfFunc = cm->functions.c[fnId].typeId;
         isGeneric = typeReadHeader(typeOfFunc, cm).isGeneric;
      } else {
         varId = cm->activeBindings[name];
         typeOfFunc = cm->vars.c[varId].typeId;
      }

#ifdef DEBUG //{{{
      if (typeReadHeader(typeOfFunc, cm).arity != (argCount == 0 ? 1 : argCount) + 1) {
         print("arity %d type %d argc %d", typeReadHeader(typeOfFunc, cm).arity, typeOfFunc.v,
            (argCount == 0 ? 1 : argCount) + 1);
      }
#endif //}}}
      // first param matches, but does arity?
      VALIDATEP(typeReadHeader(typeOfFunc, cm).arity == (argCount == 0 ? 1 : argCount) + 1,
         errTypeNoMatchingOverload)

      TypeId firstParamInd = getFirstParamInd(typeOfFunc, cm);
      if (!isGeneric) {
         // We know the type of the function, now to validate arg types against param types
         for (Int k = exp->len - argCount, l = firstParamInd.v; k < exp->len; k++, l++) {
            VALIDATEP(exp->c[k] > - 1, errUnknownType)
            if (exp->c[k] != cm->types.c[l])  { // TODO delete
               print("type diff: expected %d at j %d", cm->types.c[l], cm->j);
               printLInt(exp);
            }
            VALIDATEP(exp->c[k] == cm->types.c[l], errTypeWrongArgumentType)
         }
         cm->ast.c[cm->j].pl1 = isVarCall ? varId : fnId;
      } else {
         Function fn = cm->functions.c[fnId];
         TypeId concreteFnType = tGenericResolveConcrete(
            fn, exp->c, exp->len - argCount, exp->len, cm
         );
         Int monoInd = searchMultiAssocList(concreteFnType.v, fn.genericInd, cm->functionMonos);

         if (monoInd == -1) {
            monoInd = cm->monos->len;
            addMultiAssocList(concreteFnType.v, monoInd, fn.genericInd, cm->functionMonos);
            add(
               ((Monomorphization){ .name = fn.name, .concrete = concreteFnType, .fnId = fnId,
                  .tokenInd = cm->functions.c[fnId].tokenInd }),
               cm->monos
            );
         }
         cm->ast.c[cm->j].pl1 = monoInd;
         cm->ast.c[cm->j].pl3 = callMonomorph;
      }

      exp->len -= argCount;
      TypeId retType = tFunctionReturnType(typeOfFunc, cm);
      add(retType.v, exp);
   }
}

private void //:typeReduceExpr
typeReduceExpr(Int const indExpr, CM) {
// Runs the typechecking "reduction" on a pure expression, i.e. one that doesn't
// contain any nested subexpressions (data allocations or lambdas)
// We go from left to right: resolving the calls, typechecking & collapsing args, and replacing
// calls with their return types
// "indExpr" is the index of the nodExpr or nodAssignmentRight
   cm->j = indExpr + 1; // index in @ast
   Node exprNd = cm->ast.c[indExpr];
   // pl2 > 0 case is for subexpressions inside data allocs, the other one is for normal exprs
   Int const sentinelNode = exprNd.pl2 > 0 ? calcNodeSentinel(exprNd, indExpr) : cm->ast.len;
   LInt* exp = cm->expr->exp;
   exp->len = 0;

   // Skip internal assignments, if any
   for ( ; cm->j < sentinelNode; ) {
      Node nd = cm->ast.c[cm->j];
      if (nd.tp != nodAssignment)
         { break; }
      cm->j += (nd.pl2 + 1);
   }
   for (; cm->j < sentinelNode; cm->j++) {
      Node nd = cm->ast.c[cm->j];
      if (nd.tp == nodCall) {
         typeCheckCall(nd, exp, cm);
      } else {
         if (nd.tp <= topVerbatimTokenVariant) {
            add((Int)nd.tp, exp);
         } ei (nd.tp == nodVar) {
            add(cm->vars.c[nd.pl1].typeId.v, exp);
         } else { // overloadId
            add(nd.pl1, exp); // overloadId
         }
      }
   }
}

private TypeId //:typeCheckBigExpr
typeCheckBigExpr(Int indExpr, Int sentinelNode, CM) {
// Typechecks and resolves overloads in a single expression. "Big" refers to
// the fact that this expr may contain sub-assignments for data allocation.
// "indExpr" is the index of nodExpr or nodAssignmentRight
// CONSUMES the whole expression
   LInt* exp = cm->expr->exp;
   typeReduceExpr(indExpr, cm);
   if (exp->len == 1) {
      return typeOf(exp->c[0]); // the last remaining stack elt is the
                                   // type of the whole expression
   } else {
      return ZERO_ARITY_TYPE;
   }
}

private TypeId //:typecheckAndProcessListElt
typecheckAndProcessListElt(Int* j, CM) {
// Also updates the current index to skip the current element
   Node nd = cm->ast.c[*j];
   if (nd.tp <= topVerbatimTokenVariant) {
      *j++;
      return typeOf(nd.tp);
   } ei (nd.tp == nodVar) {
      *j++;
      return cm->vars.c[nd.pl1].typeId;
   } else {
      Int sentinel = (*j) + nd.pl2 + 1;
      TypeId exprType = typeCheckBigExpr(*j, sentinel, cm);
      *j = sentinel;
      return exprType;
   }
}

private TypeId //:typecheckList
typecheckList(Int startInd, CM) {
   Int j = startInd;
   TypeId fstType = typecheckAndProcessListElt(&j, cm);
   for (j++; j < cm->ast.len; j++) {
      TypeId eltType = typecheckAndProcessListElt(&j, cm);
      VALIDATEP(eq(eltType, fstType), errListDifferentEltTypes)
   }
   return fstType;
}

TypeId //:typeTryGetFieldType
typeTryGetFieldType(NameId name, TypeId t, OUT NameId* mbAltName, CM) {
   TypeHeader hdr = typeReadHeader(t, cm);
   TypeId rootType = t;
   if (hdr.sort == sorTypeCall) {
      rootType = typeOf(cm->types.c[t.v + TYPE_PREFIX_LEN]);
      hdr = typeReadHeader(rootType, cm);
   }
   Int payloadSize = cm->types.c[rootType.v] - TYPE_PREFIX_LEN + 1;
   Int ratio = payloadSize/hdr.arity;
   VALIDATEP(ratio >= 2, errTypeFieldNotFound);
   Int const namesStart = rootType.v + TYPE_PREFIX_LEN + hdr.arity;
   Int const namesEnd = rootType.v + TYPE_PREFIX_LEN + 2*hdr.arity;
   Int nameInd = namesStart;
   for (; nameInd < namesEnd; nameInd++) {
      if (name == cm->types.c[nameInd])
         { break; }
   }
   VALIDATEP(nameInd < namesEnd, errTypeFieldNotFound);
   if (ratio == 3)
      { *mbAltName = cm->types.c[nameInd + hdr.arity]; } // alternative name for codegen
   return typeOf(cm->types.c[nameInd - hdr.arity]);
}

//}}}
//{{{ Generic types


TypeId //:tGenericSubstituteParams
tGenericSubstituteParams(TypeId t, CM) {
// Performs the substitutions of type params into generic types according to @tExpr.params
   if (t.v <= topVerbatimType)
      { return t; }
   TypeHeader hdr = typeReadHeader(t, cm);
   if (!hdr.isGeneric)
      { return t; }
   TExpr* te = cm->tExpr;
   te->tmp->len = 0;  // used to store start inds of type subexpressions

   Int arity = hdr.arity;
   Int genericSent = t.v + TYPE_PREFIX_LEN + arity + 1;

   // @temp is populated by types minus the lengths (so [header][content])
   add(((TypeLoc){.currPos = t.v + TYPE_PREFIX_LEN, .sentinel = genericSent}),
      te->genericSt);
   for (; te->genericSt->len > 0; ) {
      TypeLoc* genericLoc = &last(te->genericSt);
      TypeId currNode = { .v = cm->types.c[genericLoc->currPos] };

      genericLoc->currPos++;
      if (genericLoc->currPos == genericLoc->sentinel) {
         Int startOfSubExp = removeLast(te->tmp);
         te->genericSt->len--;

         Int countOfNewElts = te->exp->len - startOfSubExp;
         TypeId newType = typeOf(cm->types.len);
         pushIntypes(countOfNewElts + TYPE_PREFIX_LEN + 1, cm);
         memcpy(cm->types.c + cm->types.len, te->exp + startOfSubExp, 4*countOfNewElts);

         add(mergeType(newType, cm).v, te->tmp);
      }

      if (currNode.v <= topVerbatimType) {
         add(currNode.v, te->tmp);
      } else {
         TypeHeader currHdr = typeReadHeader(currNode, cm);
         if (currHdr.sort == sorGenericParam) {
            Int deBruijnInd = cm->types.c[currNode.v + TYPE_PREFIX_LEN + 1];
            add(te->tParams->c[deBruijnInd], te->tmp);
         } else if (currHdr.isGeneric) {
            add(te->exp->len, te->tmp);
            typeExpAddHeader(
               ((TypeHeader){.sort = currHdr.sort, .arity = currHdr.arity, .tyrity = currHdr.tyrity,
                  .isGeneric = false}), // it will stop being generic once we substitute all params
               te
            );

            add(tGetBody(currNode, cm), te->genericSt);
         } else {
            add(currNode.v, te->tmp);
         }
      }
   }
   VALIDATEI(te->exp->len == 1, iErrorInconsistentTypeExpr)
   return typeOf(te->exp->c[0]);
}

void //:tGenericTryUnifyTreeNodes
tGenericTryUnifyTreeNodes(TypeId gener, TypeId concr,
      LTypeLoc* genericSt, LTypeLoc* concreteSt, CM
) {
// Unification of a single node pair in the type trees. Possibly pushes TypeLocs to the stacks,
// or sets param values in @tExpr->params
   if (eq(gener, concr))
      { return; }
   TypeHeader generHdr = typeReadHeader(gener, cm);
   if (generHdr.sort == sorGenericParam) {
      Int deBruijnInd = generHdr.name;
      Int currParamVal = cm->tExpr->tParams->c[deBruijnInd];
      if (currParamVal == -1) {
         cm->tExpr->tParams->c[deBruijnInd] = concr.v;
      } else {
         VALIDATEP(currParamVal == concr.v, errTypeGenericCallDoesntUnify)
      }
   } else {
      TypeHeader concrHdr = typeReadHeader(concr, cm);
//~      if (generHdr.arity != concrHdr.arity || generHdr.name != concrHdr.name) {
//~         print("UNEQ @ %d", cm->j);
//~         dbgType(gener);
//~         dbgType(concr);
//~      }
      VALIDATEP(generHdr.arity == concrHdr.arity && generHdr.name == concrHdr.name,
         errTypeGenericCallDoesntUnify);
      add(tGetBody(gener, cm), genericSt);
      add(tGetBody(concr, cm), concreteSt);
   }
}

TypeId //:tGenericTryUnifyFunctionTypes
tGenericTryUnifyFunctionTypes(TypeId generic, TypeHeader genericHdr,
      TypeId concrete, TypeHeader concreteHdr, CM) {
// Type tree walkin' to determine and validate type parameters' values.
// Since we're unifying a generic function with its arguments, we have only the arg types,
// so don't have anything to unify for the return type.
// Pre-condition: @tExpr->params has been filled with a -1 for every type param
// Returns: the concrete return type of a resolved generic function call.
   VALIDATEP(genericHdr.arity == concreteHdr.arity + 1, errTypeGenericCallDoesntUnify);
   Int arity = genericHdr.arity;
   Int genericSent = generic.v + TYPE_PREFIX_LEN + arity - 1;
   Int concreteSent = concrete.v + TYPE_PREFIX_LEN + arity;
   TExpr* restrict te = cm->tExpr;
   te->genericSt->len = 0;
   te->concreteSt->len = 0;

   add(((TypeLoc){.currPos = generic.v + TYPE_PREFIX_LEN, .sentinel = genericSent}),
      te->genericSt);
   add(((TypeLoc){.currPos = concrete.v + TYPE_PREFIX_LEN, .sentinel = concreteSent}),
      te->concreteSt);
   for (; te->genericSt->len > 0 && te->concreteSt->len > 0; ) {
      TypeLoc* genericLoc = &last(te->genericSt);
      TypeLoc* concreteLoc = &last(te->concreteSt);
      TypeId g = { .v = cm->types.c[genericLoc->currPos] };
      TypeId c = { .v = cm->types.c[concreteLoc->currPos] };

      // next step in the tree-walk
      genericLoc->currPos++;
      concreteLoc->currPos++;
      if (genericLoc->currPos == genericLoc->sentinel) {
         te->genericSt->len--;
         te->concreteSt->len--;
      }

      tGenericTryUnifyTreeNodes(g, c, te->genericSt, te->concreteSt, cm);
   }
   for (Int j = 0; j < te->tParams->len; j++) {
      VALIDATEP(te->tParams->c[j] > -1, errTypeGenericCallDoesntUnify)
   }
   return tGenericSubstituteParams(tFunctionReturnType(generic, cm), cm);
}

TypeId //:tGenericTryUnifyTypes
tGenericTryUnifyTypes(Function fn, TypeId concrete, CM) {
// Resolves type params in a generic type
// Returns: for a generic function call, its concrete return type. Otherwise, -1.
   TypeHeader genericHdr = typeReadHeader(fn.typeId, cm);
   TypeHeader concreteHdr = typeReadHeader(concrete, cm);

   cm->tExpr->tParams->len = genericHdr.tyrity;
   for (Int j = 0; j < genericHdr.tyrity; j++) {
      cm->tExpr->tParams->c[j] = -1;
   }
   if (genericHdr.name == nameOfStandard(strF)) {
      return tGenericTryUnifyFunctionTypes(fn.typeId, genericHdr, concrete, concreteHdr, cm);
   } else  {
      return ZERO_ARITY_TYPE;
   }
}

TypeId //:tGlueReturnTypeOntoFn
tGlueReturnTypeOntoFn(TypeId args, TypeId returnType, CM) {
// `F Int Double -> String`, `Foo` -> `F Int Double String -> Foo`. Used for generic resolutions.
   Int const sizeArgs = cm->types.c[args.v];
   Int const tentativeType = cm->types.len;
   ensureCapacityTypes(sizeArgs + 2, cm); // +2 for the size (in front) and return type (in back)

   cm->types.c[tentativeType] = sizeArgs + 1;
   cm->types.len++;
   TypeHeader argsHdr = typeReadHeader(args, cm);
   TypeHeader fullHdr = argsHdr;
   fullHdr.arity++; // for the return type
   typeAddHeader(fullHdr, cm);

   memcpy(
      cm->types.c + tentativeType + TYPE_PREFIX_LEN,
      cm->types.c + args.v + TYPE_PREFIX_LEN,
      4*sizeArgs - sizeof(TypeHeader)
   );
   cm->types.c[tentativeType + sizeArgs + 1] = returnType.v;
   cm->types.len += (sizeArgs + 2);

   return mergeType(typeOf(tentativeType), cm);
}

TypeId //:tGenericResolveConcrete
tGenericResolveConcrete(Function fn, Arr(Int) argTypes, Int start, Int end, CM) {
// Finds or creates a concrete type for a generic function call.
// 1. Copies the param types into @types to build an actual type
// 2. Walks two trees in depth-first fashion, left-to-right
// 3. Two corresponding nodes must be either equal, or one of them is a type param
// The result is either the function's full concrete type or a type exception (iff this generic
// function type is not unifiable with the arg types).
   Int arity = end - start;
   ensureCapacityTypes(arity + TYPE_PREFIX_LEN + 1, cm);

   // For `F Int Str -> Double` this will be `F Int -> Str`, i.e. the return type is missing
   TYPE_CREATE_START(((TypeHeader){ .sort = sorDeclare, .tyrity = 0, .arity = arity,
      .name = nameOfStandard(strF), .isGeneric = false }));
   memcpy(cm->types.c + cm->types.len, argTypes + start, arity*4);
   cm->types.len += arity;
   TYPE_CREATE_END;
   TypeId args = mergeType(tentativeType, cm);

   TypeId returnType = tGenericTryUnifyTypes(fn, args, cm);

   return tGlueReturnTypeOntoFn(args, returnType, cm);
}

//}}}
//}}}
//{{{ Utils for tests & debugging

#if defined(DEBUG) || defined(TEST)
//{{{ General utils

void //:printIntArray
printIntArray(Int count, Arr(Int) arr) {
   printf("[");
   for (Int k = 0; k < count; k++) {
      printf("%d ", arr[k]);
   }
   printf("]\n");
}

void //:printIntArrayOff
printIntArrayOff(Int startInd, Int count, Arr(Int) arr) {
   printf("[...");
   for (Int k = 0; k < count; k++) {
      printf("%d ", arr[startInd + k]);
   }
   printf("...]\n");
}

void //:printLInt
printLInt(LInt* st) {
   printIntArray(st->len, st->c);
}


void //:printNameAndLen
printNameAndLen(Unt unsign, CM) {
   Int startBt = unsign & LOWER24BITS;
   Int len = (unsign >> 24) & 0xFF;
   fwrite(cm->sourceCode.c + startBt, 1, len, stdout);
}

void //:printName
printName(NameId nameId, CM) {
   Unt unsign = cm->names->c[nameId];
   printNameAndLen(unsign, cm);
   printf("\n");
}

void //:printNameNoLn
printNameNoLn(NameId nameId, CM) {
   Unt unsign = cm->names->c[nameId];
   printNameAndLen(unsign, cm);
}

//}}}
//{{{ Lexer testing

// Must agree in order with Token types in eyr.internal.h
char const* tokNames[] = {
   "Int", "Long", "Double", "Bool", "String", "misc",
   "word", "Type", "@TVar", ":kwarg", "oper", ".field",
   "stmt", "clause", "def", "()",
   "(T ...)", "data", "a[b][c]", "[]",
   "=", "=...", "alias", "assert", "breakCont",
   "trait", "import", "return",
   "{", "if...", "eif ...", "else {", "match", "{{fn", "{fn params}",
   "try{", "{catch", "impl", "for{", "{each"
};


Int
pos(LX) { return lx->i - sizeof(standardText) + 1; }

Int
posInd(Int ind) { return ind - sizeof(standardText) + 1; }

void
dbgLexBtrack(LX) { //:dbgLexBtrack
   LBtToken* bt = lx->lexBtrack;

   for (Int k = 0; k < bt->len; k++) {
      printf("%s ", tokNames[bt->c[k].tp]);
   }
   printf("  ]\n");

   printf("lexBtTrack StartInds = [");

   for (Int k = 0; k < bt->len; k++) {
      printf("%d ", bt->c[k].tokenInd);
   }
   printf("  ]\n");
}

Int
equalityLexer(Compiler* a, Compiler* b) { //:equalityLexer
// Returns -2 if lexers are equal, -1 if they differ in errorfulness, and the index of the first
// differing token otherwise
   if (a->wasError != b->wasError
         || !endsWith(a->errMsg, b->errMsg)) {
      return -1;
   }
   int commonLength = a->tokens.len < b->tokens.len ? a->tokens.len : b->tokens.len;
   int i = 0;
   for (; i < commonLength; i++) {
      Token tokA = a->tokens.c[i];
      Token tokB = b->tokens.c[i];
      if (tokA.tp != tokB.tp || tokA.lenBts != tokB.lenBts || tokA.startBt != tokB.startBt
         || tokA.pl1 != tokB.pl1 || tokA.pl2 != tokB.pl2) {
         printf("\n\nUNEQUAL RESULTS on token %d\n", i);
         if (tokA.tp != tokB.tp) {
            printf("Diff in tp, %s but was expected %s\n", tokNames[tokA.tp], tokNames[tokB.tp]);
         }
         if (tokA.lenBts != tokB.lenBts) {
            printf("Diff in lenBts, %d but was expected %d\n", tokA.lenBts, tokB.lenBts);
         }
         if (tokA.startBt != tokB.startBt) {
            printf("Diff in startBt, %d but was expected %d\n",
                  posInd(tokA.startBt), posInd(tokB.startBt));
         }
         if (tokA.pl1 != tokB.pl1) {
            printf("Diff in pl1, %d but was expected %d\n", tokA.pl1, tokB.pl1);
         }
         if (tokA.pl2 != tokB.pl2) {
            printf("Diff in pl2, %d but was expected %d\n", tokA.pl2, tokB.pl2);
         }
         return i;
      }
   }
   return (a->tokens.len == b->tokens.len) ? -2 : i;
}

void
printLexer(LX) { //:printLexer
   if (lx->wasError) {
      printf("Error: ");
      printString(lx->errMsg);
   }
   Int indent = 0;
   Arena* a = lx->a;
   LInt* sentinels = createLInt(16, a);
   for (int i = 0; i < lx->tokens.len; i++) {
      Token tok = lx->tokens.c[i];
      for (int m = sentinels->len - 1; m > -1 && sentinels->c[m] == i; m--) {
         sentinels->len--;
         indent--;
      }

      Int realStartBt = tok.startBt - sizeof(standardText) + 1;
      if (i < 10) {
         printf(" ");
      }
      printf("%d: ", i);
      for (int j = 0; j < indent; j++) {
         printf("  ");
      }
      if (tok.pl1 != 0 || tok.pl2 != 0) {
         printf("%s %d %d [%d; %d]\n",
               tokNames[tok.tp], tok.pl1, tok.pl2, realStartBt, tok.lenBts);
      } else {
         printf("%s [%d; %d]\n", tokNames[tok.tp], realStartBt, tok.lenBts);
      }
      if (tok.tp >= firstSpanTokenType && tok.pl2 > 0) {
         add(i + tok.pl2 + 1, sentinels);
         indent++;
      }
   }
}

//}}}
//{{{ Parser testing

// Must agree in order with node types in eyr.internal.h
char const* nodeNames[] = {
   "Int", "Long", "Double", "Bool", "String", "_", "misc",
   "var", "call",
   "{", "Expr", "=", "[]",
   "assert", "breakCont", "catch", "import",
   "{{ fn }}", "value def", "trait", "return", "try",
   "for", "if", "if clause", "impl", "match"
};


CompStats
getStats(CM) { return cm->stats; }

void
setLexerError(String errMsg, CM) {
   cm->wasError = true;
   cm->errMsg = errMsg;
}

void
setParserError(String errMsg, CM) {
   cm->wasError = true;
   cm->errMsg = errMsg;
}

void //:printParser
printParser(CM) {
   if (cm->wasError) {
      printf("Error: ");
      printString(cm->errMsg);
   }
   Arena* a = cm->a;
   Int indent = 0;
   LInt* sentinels = createLInt(16, a);
   CompStats stats = getStats(cm);
   for (int i = 0; i < cm->ast.len; i++) {
      Node nod = cm->ast.c[i];
      SourceLoc loc = cm->sourceLocs->c[i];
      for (int m = sentinels->len - 1; m > -1 && sentinels->c[m] == i; m--) {
         sentinels->len--;
         indent--;
      }

      if (i < 10) printf(" ");
      printf("%d: ", i);
      for (int j = 0; j < indent; j++) {
         printf("  ");
      }
      Int startBt = loc.startBt - stats.standardTextLen;
      if (nod.tp == nodCall) {
         printf("call %d argc = %d c %d [%d; %d] type = \n", nod.pl1, nod.pl2, nod.pl3,
            startBt, loc.lenBts);
         //printType(cm->vars.c[nod.pl1].typeId, cm);
      } ei (nod.pl1 != 0 || nod.pl2 != 0) {
         if (nod.pl3 != 0)  {
            printf("%s %d %d %d [%d; %d]\n", nodeNames[nod.tp], nod.pl1, nod.pl2, nod.pl3,
                  startBt, loc.lenBts);
         } else {
            printf("%s %d %d [%d; %d]\n", nodeNames[nod.tp], nod.pl1, nod.pl2,
                  startBt, loc.lenBts);
         }
      } else {
         printf("%s [%d; %d]\n", nodeNames[nod.tp], startBt, loc.lenBts);
      }
      if (nod.tp >= nodScope && nod.pl2 > 0) {
         add(i + nod.pl2 + 1, sentinels);
         indent++;
      }
   }
}

void
dbgRawOverload(Int listInd, Compiler* cm) { //:dbgRawOverload
   MultiAssocList* ml = cm->rawOverloads;
   Int len = ml->c[listInd]/2;
   printf("[");
   for (Int j = 0; j < len; j++) {
      printf("%d: %d ", ml->c[listInd + 2 + 2*j], ml->c[listInd + 2*j + 3]);
   }
   print("]");
   printf("types: ");
   for (Int j = 0; j < len; j++) {
      dbgType(typeOf(ml->c[listInd + 2 + 2*j]));
      printf("\n");
   }
}

void
dbgLNode(LNode* st, Arena* a) { //:dbgLNode
   Int indent = 0;
   LInt* sentinels = createLInt(16, a);
   for (int i = 0; i < st->len; i++) {
      Node nod = st->c[i];
      for (int m = sentinels->len - 1; m > -1 && sentinels->c[m] == i; m--) {
         sentinels->len--;
         indent--;
      }

      if (i < 10) printf(" ");
      printf("%d: ", i);
      for (int j = 0; j < indent; j++) {
         printf("  ");
      }
      if (nod.tp == nodCall) {
         printf("call %d type = \n", nod.pl1);
      } ei (nod.pl1 != 0 || nod.pl2 != 0) {
         if (nod.pl3 != 0)  {
            printf("%s %d %d %d\n", nodeNames[nod.tp], nod.pl1, nod.pl2, nod.pl3);
         } else {
            printf("%s %d %d [%d; %d]\n", nodeNames[nod.tp], nod.pl1, nod.pl2);
         }
      } else {
         printf("%s\n", nodeNames[nod.tp]);
      }
      if (nod.tp >= nodScope && nod.pl2 > 0) {
         add(i + nod.pl2 + 1, sentinels);
         indent++;
      }
   }
}

void //:dbgExprFrames
dbgExprFrames(Expr* st) {
   print("Expr frames<<<");
   for (Int j = 0; j < st->frames->len; j++) {
      ExprFrame fr = st->frames->c[j];
      if (fr.tp == exfrCall) {
         printf("Call %d", fr.name);
      } ei (fr.tp == exfrUnaryCall) {
         printf("Unary %d", fr.name);
      } ei (fr.tp == exfrDataAlloc) {
         printf("DataAlloc");
      } ei (fr.tp == exfrParen) {
         printf("(");
      } ei (fr.tp == exfrAccessor) {
         printf("a[ccessor]");
      } ei (fr.tp == exfrAccessIn) {
         printf("..[]");
      } else {
         printf("tp %d", fr.tp);
      }
      printf(" arg %d sent %d; ", fr.argCount, fr.sentinel);
      if (j % 6 == 0) {
          print("\n");
      }
   }
   printf("\n>>>\n\n");
}

Int
getBinding(Int id, CM) { return cm->activeBindings[id]; }

void
setLoc(SourceLoc loc, Int j, CM) { cm->sourceLocs->c[j] = loc; }

void //:dbgScopes0
dbgScopes0(Scopes* s) {
   print("Scope Stack<<<");
   if (!(s->currChunk->prev) && s->curr - s->currChunk->c <= 1)
      { goto closing; }
   ScopeChunk* ch = s->currChunk;

   Int currLen = s->currLen;
   printf("Scope with %d bindings: [", currLen);
   for (Int* p = s->curr; p > ch->c || ch->prev; p--) {
      if (currLen == 0) {
         print("]");
         currLen = *p;
         printf("Scope with %d bindings: [", currLen);
      } else {
         printf("%d ", *p);
         currLen--;
      }
      if (p == ch->c) {
         ch = ch->prev;
         p = ch->c + SCOPE_CHUNK_SZ;
      }
   }
   print("]");
closing:
   printf(">>>\n\n");
}


void //:dbgScopes
dbgScopes(CM) {
   dbgScopes0(&(cm->scopes));
}


//}}}
//{{{ Types testing

void //:dbgTypeOuter
dbgTypeOuter(TypeHeader currHdr, CM) {
// Print the name of the outer type. `(Tu Int Double)` -> `Tu`
   if (currHdr.name == nameOfStandard(strF)) {
      printf("(F ");
   } else {
      printf("(");
      printNameNoLn(currHdr.name, cm);
      printf(" ");
   }
}

void //:dbgType1
dbgType1(Int t, CM) {
   printIntArrayOff(t, 6, cm->types.c);

   LTypeLoc* st = createLTypeLoc(16, cm->aTmp);
   TypeLoc* top = null;

   TypeHeader hdr = typeReadHeader(typeOf(t), cm);
   Int sentinel = t + cm->types.c[t] + 1;
   dbgTypeOuter(hdr, cm);
   Int startingT = t + TYPE_PREFIX_LEN;
   if (hdr.sort == sorTypeCall && hdr.name != nameOfStandard(strF))
      { startingT++; }

   add(((TypeLoc){ .currPos = startingT, .sentinel = sentinel }), st);
   top = st->c;

   for (Int countIters = 0; top != null && countIters < 10; countIters++)  {
      Int currT = cm->types.c[top->currPos];
      if (currT <= topVerbatimType)  {
         printf("%s ", currT != voidType ? nodeNames[currT] : "Void");
         top->currPos++;
      } else {
         TypeHeader currHdr = typeReadHeader(typeOf(currT), cm);
         if (currHdr.sort == sorGenericParam) {
            printf("$%d ", currHdr.name);
            top->currPos++;
            goto nextIter;
         }
         dbgTypeOuter(currHdr, cm);

         Int nextT = currT + TYPE_PREFIX_LEN;
         if (currHdr.name != nameOfStandard(strF))
            { nextT++; }
         top->currPos++;
         if (currHdr.sort == sorTypeCall) {
            TypeLoc newTypeLoc = (TypeLoc){ .currPos = nextT,
                  .sentinel = currT + cm->types.c[currT] + 1};
            add(newTypeLoc, st);
            top = &last(st);
         }
      }
      nextIter:
      // closing open spans
      while (top != null && top->currPos == top->sentinel) {
         st->len--;
         top = st->len > 0 ? &last(st) : null;
         printf(") ");
      }
   }
   printf("\n");
}

void //:dbgType0
dbgType0(TypeId type, CM) {
// Print a single type fully for debugging purposes
   //printf("Printing the type [ind = %d, len = %d]\n", typeId, cm->types.c[typeId]);
   Int typeId = type.v;

   TypeHeader hdr = typeReadHeader(type, cm);
   if (typeId <= topVerbatimType) {
      printf("%s\n", nodeNames[typeId]);
      return;
   } else if (hdr.sort == sorGenericParam) {
      printf("$%d ", hdr.name);
   } else {
      dbgType1(type.v, cm);
   }
}

void
dbgTypeFrames(TExpr* te) { //:dbgTypeFrames
   LTypeFrame* frames = te->frames;
   print(">>> Type frames cnt %d", frames->len);
   for (Int j = 0; j < frames->len; j++) {
      TypeFrame fr = frames->c[j];
      if (fr.tp == tfrFunction) {
         printf("Func ");
      } ei (fr.tp == tfrTypeCall) {
         printf("TypeCall ");
      }
      printf("typeArgs: %d ", fr.countArgs);
      printf("sent: %d \n", fr.sentinel);
   }
   printf(">>>\n\n");
}

void
dbgOverloads(Int nameId, CM) { //:dbgOverloads
   Int listId = -cm->activeBindings[nameId] - 2;
   if (listId < 0) {
      print("Overloads for name %d not found", nameId)
      return;
   }
   Arr(Int) overs = cm->overloads.c;
   Int countOverloads = overs[listId]/2;
   printf("%d overloads @listId %d:\n", countOverloads, listId);
   printf("[outer types, %d]\n", countOverloads);
   Int sentinel = listId + countOverloads + 1;
   Int j = listId + 1;
   for (; j < sentinel; j++) {
      printf("%d ", overs[j]);
   }
   printf("\n[typeId: ");
   sentinel += countOverloads;
   for (; j < sentinel; j++) {
      printf("%d ", overs[j]);
   }
   printf("]\n[ref = ");
   sentinel += countOverloads;
   for (; j < sentinel; j++) {
      printf("%d ", overs[j]);
   }
   printf("]\n\n");
}

//}}}

#endif
//{{{ Tests only

#ifdef TEST

//{{{ Definitions

#define S   70000000 // A constant larger than the largest allowed file size.
                // Separates parsed entities from others
#define I  140000000 // The base index for imported entities/overloads
#define S2 210000000 // A constant larger than the largest allowed file size.
                //  Separates parsed entities from others
#define O  280000000 // The base index for operators

typedef struct { // :TestEntityImport
    Int nameInd; // 0, 1 or 2. Corresponds to the "foobarinner" in standardText
    Int typeInd; // index in the intermediary array of types that is imported alongside
} TestEntityImport;

//}}}

Int
tryGetOper0(Int opType, Int typeId, Compiler* protoOvs) {
// Try and convert test value to operator entityId
   Int entityId;
   Int ovInd = -getBinding(opType, protoOvs) - 2;
   bool foundOv = tFindOverload(typeOf(typeId), ovInd, protoOvs, OUT &entityId);
   if (foundOv)  {
      return entityId + O;
   } else {
      return -1;
   }
}

Arr(TypeId) //:importTestTypes
importTestTypes(Arr(Int) types, Int countTypes, CM, Arena* aTmp) {
// Importing simple function types for testing purposes
   Int countImportedTypes = 0;
   for (Int j = 0; j < countTypes; j += (types[j] + 1)) {
      if (types[j] == 0) {
         return NULL; // should never happen
      }
      countImportedTypes++;
   }
   Arr(TypeId) typeIds = allocateOnArena(countImportedTypes*4, aTmp);
   Int t = 0;
   for (Int j = 0; j < countTypes; t++) {
      const Int importLen = types[j];
      const Int typeSentinel = j + importLen + 1;
      TypeId initTypeId = typeOf(cm->types.len);

      pushIntypes(importLen + TYPE_PREFIX_LEN - 1, cm);

      typeAddHeader(
         (TypeHeader){.sort = sorDeclare, .tyrity = 0, .arity = importLen,
                  .name = nameOfStandard(strF) }, cm);
      for (Int k = j + 1; k < typeSentinel; k++) { // <= because there are (arity + 1) elts -
                                     // +1 for the return type!
         pushIntypes(types[k], cm);
      }

      TypeId mergedType = mergeType(initTypeId, cm);
      typeIds[t] = mergedType;
      j = typeSentinel;
   }
   return typeIds;
}

void
importTestFns(Arr(Int) types, Int countTypes,
              Arr(TestEntityImport) imports, Int const countImports, Arena* a, OUT CM
) {
   if (countImports == 0)
      { return; }

   Arr(TypeId) typeIds = importTestTypes(types, countTypes, cm, a);
   Arr(Function) importedFns = allocateArray(countImports, Function, a);

   for (Int j = 0; j < countImports; j++) {
      importedFns[j] = (Function) {
         .name = nameOfStandard(strSentinel - 3 + imports[j].nameInd),
         .typeId = typeIds[imports[j].typeInd]
      };
   }
   importFns(importedFns, countImports, cm);
}

Int
equalityParser(/* test specimen */Compiler* a, /* expected */Compiler* b, Bool compareLocsToo) {
// Returns -2 if lexers are equal, -1 if they differ in errorfulness, and the index of the first
// differing token otherwise
   CompResult* statsA = getCompResult(a);
   CompResult* statsB = getCompResult(b);
   if (statsA->wasParserError != statsB->wasParserError 
         || (!endsWith(statsA->errMsg, statsB->errMsg)))
      { return -1; }
   Int const commonLength = MIN(statsA->stats.astLen, statsB->stats.astLen);
   int i = 0;
   for (; i < commonLength; i++) {
      Node nodA = a->ast.c[i];
      Node nodB = b->ast.c[i];
      if (nodA.tp != nodB.tp
         || nodA.pl1 != nodB.pl1 || nodA.pl2 != nodB.pl2 || nodA.pl3 != nodB.pl3) {
         printf("\n\nUNEQUAL RESULTS on %d\n", i);
         if (nodA.tp != nodB.tp) {
            printf("Diff in tp, %d but was expected %d\n", nodA.tp, nodB.tp);
         }
         if (nodA.pl1 != nodB.pl1) {
            printf("Diff in pl1, %d but was expected %d\n", nodA.pl1, nodB.pl1);
         }
         if (nodA.pl2 != nodB.pl2) {
            printf("Diff in pl2, %d but was expected %d\n", nodA.pl2, nodB.pl2);
         }
         if (nodA.pl3 != nodB.pl3) {
            printf("Diff in pl3, %d but was expected %d\n", nodA.pl3, nodB.pl3);
         }
         return i;
      }
   }
   if (compareLocsToo) {
      for (i = 0; i < commonLength; ++i) {
         SourceLoc locA = a->sourceLocs->c[i];
         SourceLoc locB = b->sourceLocs->c[i];
         if (locA.startBt != locB.startBt || locA.lenBts != locB.lenBts) {
            printf("\n\nUNEQUAL SOURCE LOCS on %d\n", i);
            if (locA.lenBts != locB.lenBts) {
               printf("Diff in lenBts, %d but was expected %d\n", locA.lenBts, locB.lenBts);
            }
            if (locA.startBt != locB.startBt) {
               printf("Diff in startBt, %d but was expected %d\n", locA.startBt, locB.startBt);
            }
            return i;
         }
      }
   }
   return (a->ast.len == b->ast.len) ? -2 : i;
}

#endif

//}}}

//}}}
//{{{ Init

private void //:createProtoCompiler
createProtoCompiler(OUT Compiler* proto, Arena* a) {
// Creates a proto-compiler, which is used not for compilation but as a seed value to be cloned
// for every source code module. The proto-compiler contains the following data:
// - types that are sufficient for the built-in operators
// - entities with the built-in operator entities
// - overloadIds with counts
   LUnt* st = createLUnt(16, a);
   (*proto) = (Compiler){
      .vars = createInListVar(32, a),
      .functions = createInListFunction(8, a),
      .sourceCode = str(standardText),
      .names = st, .stringDict = createStringDict(128, a),
      .types = createInListInt(64, a), .typesDict = createStringDict(128, a),
      .activeBindings = allocateArray(countOperators, Int, a),
      .rawOverloads = createMultiAssocList(a),
      .stats = (CompStats) {
         .loopCounter = 0,
         .standardTextLen = sizeof(standardText) - 1,
         .firstParsedName = (strSentinel + countOperators),
         .firstBuiltin = countOperators,
         .countOverloads = PROTO.stats.countOverloads,
         .countOverloadedNames = PROTO.stats.countOverloadedNames
      },
      .wasError = false, .errMsg = empty,
      .a = a
   };

   // operators are always active, and take up the initial chunk of names
   memset(proto->activeBindings, 0xFF, 4*countOperators);
   createBuiltins(proto);
}

private void //:initCompiler
initCompiler() {
// Definition of the operators, lexer dispatch, parser dispatch etc tables for the compiler.
// This function should only be called once, at compiler init.
// Its results are global shared const.
   static_assert(TYPE_PREFIX_LEN == sizeof(TypeHeader)/4 + 1, "Sizeof TypeHeader check");
   static_assert(sizeof(TypeId) == 4, "C has added useless some padding to opaque id TypeId!");

   if (_wasInit)
      { return; }

   populateStringOffsets(
      standardStringLens, standardOperatorsLength, sizeof(standardStringLens), OUT standardOffsets
   );
   tabulateLexer();
   Arena* aGlobal = createArena(); // it's ok to leak it. Will be cleaned up on process exit
   createProtoCompiler(&PROTO, aGlobal);
   _wasInit = true;
}

CompResult* //:tech_sozonov_eyr_compile
tech_sozonov_eyr_compile(String sourceCode) {
   Arena* a = createArena();
   CompResult* cr = allocate(CompResult, a);
   if (sourceCode.len == 0) { 
      cr->wasLexerError = true;
      cr->errMsg = s("Empty input");
      return cr;
   }

   initCompiler();
   Compiler* cm = lexicallyAnalyze(sourceCode, a);
   if (cm->wasError) {
#if defined(DEBUG)
      printString(cm->errMsg);
#endif

      cr->wasLexerError = true;
      cr->errMsg = cm->errMsg;
      return cr;
   }

   cm = parse(cm, a);
   if (cm->wasError) {

#if defined(DEBUG)
   printString(cm->errMsg);
#endif
      cr->wasParserError = true;
      cr->errMsg = cm->errMsg;
      return cr;
   }
   fillInCompilationResult(cm, OUT cr);
   return cr;
}

CompResult* //:tech_sozonov_eyr_compileFile
tech_sozonov_eyr_compileFile(String filename) {
   Arena* a = createArena();
   CompResult* cr = allocate(CompResult, a);
   if (filename.len == 0) {
      cr->errMsg = s("Empty file name!");
      cr->wasLexerError = true;
      return cr;
   } 
   initCompiler();

   String sourceCode = readSourceFile(filename, a);
   
   Compiler* cm = lexicallyAnalyzeFromFile(sourceCode, a);
   if (cm->wasError) {
      printString(cm->errMsg);
      cr->errMsg = cm->errMsg;
      cr->wasLexerError = true;
      return cr;
   }
   cm = parse(cm, a);
   if (cm->wasError) {
      printString(cm->errMsg);
      cr->errMsg = cm->errMsg;
      cr->wasParserError = true;
      return cr;
   }
   
#ifdef TRACE
   printParser(cm);
#endif
   fillInCompilationResult(cm, OUT cr);
   return cr;
}

private void
fillInCompilationResult(CM, OUT CompResult* cr) {
   *cr = (CompResult) {
      .toplevels = sliceOfInternal(cm->toplevels),
      .entrypoint = cm->entrypoint,
      .ast = sliceOfInternal(cm->ast),
      .sourceLocs = cm->sourceLocs != null 
            ? ((SliSourceLoc){.len = cm->sourceLocs->len, .c = cm->sourceLocs->c})
            : ((SliSourceLoc){.len = 0, .c = null}),
      .vars = sliceOfInternal(cm->vars),
      .functions = sliceOfInternal(cm->functions),
      .publicFns = sliceOfInternal(cm->publicFns),
      .publicConsts = sliceOfInternal(cm->publicConsts),
      .types = sliceOfInternal(cm->types),
      .names = cm->names != null
            ? ((SliUnt){.len = cm->names->len, .c = cm->names->c})
            : ((SliUnt){.len = 0, .c = null}),
      .a = cm->a,
      .stats = cm->stats,
   }; 
}

CompResult*
getCompResult(CM) {
   CompResult* cr = allocate(CompResult, cm->a);
   fillInCompilationResult(cm, OUT cr);
   return cr;
}

//}}}

