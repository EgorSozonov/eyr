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
typedef libeyr_String String;
typedef libeyr_StringBuilder StringBuilder;
#define UNUSED __attribute__((unused))

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

#define tokMisc         5  // pl1 = see the misc* constants. pl2 = underscore count iff
                           // miscUnderscore, step iff nonzero and miscLoopStep
                           // Also stands for "Void" among the primitive types
                           // Also works as a marker in "for" loops: initially it's placed after
                           // tokFor and pl2 = token ind of body start.
                           // After {reorderFor}, it's placed right between body and stepping code.
#define tokWord         6  // pl1 = nameId (index in @names). pl2 = 1 iff followed by '
#define tokTypeVar      7  // pl1 same as tokWord. `$A`
#define tokKey          8  // pl1 pl2 = same as tokWord.
                           // `:argName` or `:``structField` or `:dictKey`
#define tokOperator     9  // pl1 = nameId = operId, pl2 = precedence. `+`
#define tokFieldAcc    10  // pl1 = nameId. `.field`

// Statement or subexpr span types. pl2 = count of inner tokens
#define tokStmt        11  // firstSpanTokenType
#define tokClause      12  // Element of a comma-separated list
#define tokToplevelFn  13  // Toplevel function definition
#define tokParens      14  // subexpressions and struct/sum type instances
#define tokType        15  // `Int`, `[Tu Int Str]` or `F[A -> B]`. Atom if pl2 = 0, span otherwise.
                           // If span, then pl1 = nameId of the type
#define tokStruct      16  // Struct literal `Foo(:id 15 :name name)`
#define tokData        17  // Data literals `[]`. If pl1 == 1, it's a list. If pl1 += BIG, it's
                           // filled by meta `[@ Int 15]`
#define tokAccessor    18  // The umbrella around an accessor subexpression like `x[i][j][k]`
#define tokAccessorIn  19  // The internal `[]` block inside an accessor
#define tokAssignment  20  // iff type definition, pl1 = assiTypeDefinition
#define tokAssignRight 21  // Right-hand side of assignment
#define tokMeta        22  // @meta(...)
#define tokAlias       23
#define tokAssert      24
#define tokBreakCont   25  // pl1 = 1 iff it's a continue
#define tokTrait       26
#define tokImport      27  // For test files and package decls
#define tokReturn      28

// Bracketed (multi-statement) token types. pl1 = spanLevel, see the "sl" constants
#define tokScope       29  // `(do ...)` firstScopeTokenType
#define tokIf          30  // `if ... { `. The If, ElseIf and Else tokens must be in that order
#define tokElseIf      31  // `eif ... {`
#define tokElse        32  // `else { `
#define tokMatch       33  // `(match ... ` pattern matching on sum type tag
#define tokFn          34  // `f{a b -> body}`. pl1 = entityId
#define tokTry         35  // `try {`
#define tokCatch       36  // `catch e MyExc {`
#define tokImpl        37
#define tokFor         38
#define tokEach        39

#define topVerbatimTokenVariant tokString
#define firstSpanTokenType  tokStmt
#define firstScopeTokenType tokScope
#define countSyntaxForms    (tokEach + 1)

//}}}
//{{{ Lexer constants

// Span levels, must all be greater than 0
#define slScope          1 // scopes (denoted by brackets): newlines and commas have no effect there
#define slStmt           2 // single-line statements: newlines and semicolons break 'em
#define slSubexpr        3 // parenthesized forms: newlines have no effect, semi-colons error out
#define slClauseList     4 // a comma-separated list
#define slUnbraced       5 // A scope that hasn't met its first brace, like an "if" before its "{"
#define slFnTp           6 // `F[...]`
#define slFnReturn       7 // `F[A -> B]` the `B` part (after exactly 1 arrow symbol)
#define slToplevel       8 // `fn foo A -> B` A toplevel function declaration

// List of keywords that don't correspond directly to a token type.
// Must all be below "firstSpanTokenType"
#define keywTrue       1
#define keywFalse      2
#define keywBreak      3
#define keywContinue   4
#define keywNot        5

#define miscPub        0    // pub. It must be 0 because it's the only one denoted by a keyword
#define miscUnderscore 1    // _
#define miscArrow      2    // ->
#define miscLoopStep0  3    // token that provides space for a "for" loop reorganization
#define miscLoopStep   4    // token that marks stepping code in a "for" loop
#define miscEachElem   5    // `coll.@` in an "each" loop - element
#define miscEachInd    6    // `coll.#` in an "each" loop - index of element
#define miscField      7    // `:kwarg` in a function call or `:field` in a struct initializer

typedef struct { //:ChInterval
   Int startBt;
   Int lenBts;
} ChInterval;

//}}}

typedef void (*LexerFn)(const Arr(char), Compiler* restrict); // LexerFunc = &((A Char) *Lexer -> void)
private LexerFn LEX_TABLE[256]; // filled in by "tabulateLexer"

// The ASCII notation for the highest signed 64-bit integer abs value, 9_223_372_036_854_775_807
private Byte const
maxInt[19] = {
   9, 2, 2, 3, 3, 7, 2, 0, 3, 6,
   8, 5, 4, 7, 7, 5, 8, 0, 7
};

private Byte const
maximumPreciselyRepresentedFloatingInt[16] = {
   9, 0, 0, 7, 1, 9, 9, 2, 5, 4, 7, 4, 0, 9, 9, 2 };
// 2**53


constexpr char
standardText[] = "!.!0!=##$%&&.'*:+:-:/:/\\<<.<=><0===0>=<>>.>0?:@^.||."

                // reserved words: must be sorted alphabetically!
                "aliasassertbreakcatchcontinueeacheielsefalsefnfor"
                "ifimplimportmatchpubreturntraittruetrynot"

                // reserved words end here; what follows may have arbitrary order
                "skipstepbalkIntLongDoubleBoolStrVoidFLADstructEnumTulencapf1f2print"
                "printErrmath:pimath:eTUlengthaddmaincont"
#ifdef DEBUG
                "foobarinner"
#endif
                "\n"
             ;

#define standardOperatorsLength 52 // length of the operator part above

// The :standardText prepended to all source code inputs and the hash table to provide a built-in
// string set. Eyr's reserved words must be at the start and sorted lexicographically.
// Also they must agree with the "standardStr" in eyr.internal.h

private constexpr Byte
standardStringLens[] = {
    5, 6, 5, 5, 8,
    4, 2, 4, 5, 2,
    3, 2, 4, 6, 5,
    3, 6, 5, 4, 3,
    3,
    // reserved words end here
    4, 4, 4,       // balk
    3, 4, 6, 4, 3, // Str(ing)
    4, 1, 1, 1, 1, // D(ict)
    6, 4, 2, 3,    // len
    3, 2, 2, 5, 8, // printErr
    7, 6, 1, 1, 6, // length
    3, 4, 4,       // cont
#ifdef DEBUG
    3, 3, 5        // foo, bar, inner
#endif
};

private Int
standardOffsets[sizeof(standardStringLens)]; // filled in by "populateStringOffsets"

private constexpr Int
standardKeywords[] = {
   tokAlias,    tokAssert,  keywBreak,  tokCatch,   keywContinue,
   tokEach,     tokElseIf,  tokElse,    keywFalse,  tokToplevelFn,
   tokFor,      tokIf,      tokImpl,    tokImport,  tokMatch,
   tokMisc,     tokReturn,  tokTrait,   keywTrue,   tokTry,
   keywNot
};

//}}}
//{{{ Operators definitions

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
OPERATORS[countSignOperators + 1] = { // +1 for the "not" which is not a sign operator
   { .prec = precUnary, .name = nameLoc(0, 2), .firstSymbol = '!' },  // !.
   { .prec = 5,         .name = nameLoc(4, 2), .firstSymbol = '!' },  // !=
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
   { .prec = 8,         .name = nameLoc(16, 2), .firstSymbol = '+',   // +:
        .assignable = true, .overloadable = true},
   { .prec = 8,         .name = nameLoc(16, 1), .firstSymbol = '+',   // +
        .assignable = true, .overloadable = true},
   { .prec = 8,         .name = nameLoc(18, 2), .firstSymbol = '-',   // -:
        .assignable = true, .overloadable = true},
   { .prec = 8,         .name = nameLoc(18, 1), .firstSymbol = '-',   // -
        .assignable = true, .overloadable = true },
   { .prec = precUnary, .name = nameLoc(18, 1), .firstSymbol = '-' }, // -
   { .prec = 9,         .name = nameLoc(20, 2), .firstSymbol = '/',   // /:
        .assignable = true, .overloadable = true},
   { .prec = 0,         .name = nameLoc(22, 2), .firstSymbol = '/',   // /|
        .isTypelevel = true},
   { .prec = 9,         .name = nameLoc(22, 1), .firstSymbol = '/',   // /
        .assignable = true, .overloadable = true},
   { .prec = 7,         .name = nameLoc(24, 3), .firstSymbol = '<' }, // <<.
   { .prec = 6,         .name = nameLoc(27, 3), .firstSymbol = '<' }, // <=>
   { .prec = precUnary, .name = nameLoc(30, 2), .firstSymbol = '<' }, // <0
   { .prec = 6,         .name = nameLoc(27, 2), .firstSymbol = '<' }, // <=
   { .prec = 6,         .name = nameLoc(24, 1), .firstSymbol = '<' }, // <
   { .prec = 5,         .name = nameLoc(32, 3), .firstSymbol = '=' }, // ===
   { .prec = 5,         .name = nameLoc(33, 2), .firstSymbol = '=' }, // ==
   { .prec = 7,         .name = nameLoc(39, 3), .firstSymbol = '>',   // >>.
        .assignable=true, .overloadable = true},
   { .prec = precUnary, .name = nameLoc(42, 2), .firstSymbol = '>' }, // >0
   { .prec = 6,         .name = nameLoc(36, 2), .firstSymbol = '>' }, // >=
   { .prec = 6,         .name = nameLoc(29, 1), .firstSymbol = '>' }, // >
   { .prec = 0,         .name = nameLoc(44, 2), .firstSymbol = '?' }, // ?:
   { .prec = 0,         .name = nameLoc(44, 1), .firstSymbol = '?',   // ?
        .isTypelevel=true },
   { .prec = 3,         .name = nameLoc(47, 2), .firstSymbol = '^',   // ^.
        .assignable = true },
   { .prec = 2,         .name = nameLoc(49, 3), .firstSymbol = '|',   // ||.
        .assignable = true },
   { .prec = 0,         .name = nameLoc(49, 2), .firstSymbol = '|',   // ||
        .assignable=true },
   { .prec = precUnary,  .name = 0, .firstSymbol = 'n' }             // not
}; // real operator overloads filled in by "buildOperators"

constexpr Int
operatorStartSymbols[] = {
   // Symbols an operator may start with. "+" is absent because it's handled by lexPlus,
   // "-" because it's handled by lexMinus, "=" by lexEqual, "/" by "lexDivBy".
   aExclamation, aSharp, aDollar, aPercent, aAmp, aApostrophe, aTimes,
   aDivBy, aLT, aGT, aQuestion, aCaret, aPipe
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

#define TOKENS Arr(Token) restrict tokens // tokens that are used as input to the parser
#define AST Arr(Node const) restrict ast  // nodes that are used as input to the library consumers
typedef void (*ParseFn)(Token, Int, Arr(Token), Compiler* restrict);

#define PARSE_FN(name) private void name(Token tok, Int sentinel, TOKENS, CM);
PARSE_FN(parseErrorBareAtom) PARSE_FN(pScope) PARSE_FN(pExpr) PARSE_FN(pAssignment)
PARSE_FN(pLoopStepMarker) PARSE_FN(pAlias) PARSE_FN(pAssert)
PARSE_FN(pBreakCont) PARSE_FN(pMeta) PARSE_FN(pReturn) PARSE_FN(pIf) PARSE_FN(pElseIf)
PARSE_FN(pElse) PARSE_FN(pFor) PARSE_FN(pEach)

private ParseFn const //:PARSE_TABLE
PARSE_TABLE[countSyntaxForms] = {
   [tokInt]        = parseErrorBareAtom,
   [tokLong]       = &parseErrorBareAtom,
   [tokDouble]     = &parseErrorBareAtom,
   [tokBool]       = &parseErrorBareAtom,
   [tokString]     = &parseErrorBareAtom,
   [tokMisc]       = &pLoopStepMarker,
   [tokWord]       = &parseErrorBareAtom,
   [tokTypeVar]    = &parseErrorBareAtom,
   [tokKey]        = &parseErrorBareAtom,
   [tokOperator]   = &parseErrorBareAtom,
   [tokFieldAcc]   = &parseErrorBareAtom,

   [tokScope]      = &pScope,
   [tokStmt]       = &pExpr,
   [tokParens]     = &parseErrorBareAtom,
   [tokAssignment] = &pAssignment,

   [tokAlias]      = &pAlias,
   [tokAssert]     = &pAssert,
   [tokBreakCont]  = &pBreakCont,
   [tokCatch]      = &pAlias,
   [tokFn]         = &pAlias,
   [tokMeta]       = &pMeta,
   [tokTrait]      = &pAlias,
   [tokImport]     = &pAlias,
   [tokReturn]     = &pReturn,
   [tokTry]        = &pAlias,

   [tokIf]         = &pIf,
   [tokElseIf]     = &pElseIf,
   [tokElse]       = &pElse,
   [tokFor]        = &pFor,
   [tokEach]       = &pEach
};

//}}}
//}}}
//}}}
//{{{ Forward decls & generics

#define BIG 70000000
DECLARE_LIST(Token)
DECLARE_LIST(BtToken)
DECLARE_LIST(ParseFrame)
DECLARE_LIST(ExprFrame)
DECLARE_LIST(TypeFrame)
DECLARE_LIST(Monomorphization)
DECLARE_LIST(TypeLoc)
DECLARE_LIST(ChInterval)
defstruct(CompileError);
DECLARE_LIST(CompileError)

typedef libeyr_CompResult CompResult;
#define SRC Arr(char const) restrict source // Source text
#define LX Compiler* restrict lx // Compiler for lexer functions
#define CM Compiler* restrict cm // Compiler for parser functions
private void closeStatement(LX);

defstruct(Expr);
defstruct(TParse);

defstruct(Scopes);
void printLexer(LX);
defstruct(EachData);

private void eSaveNodes(Int startNodeInd, CM);
private void eachLoopAddStep(ParseFrame fr, Int step, TOKENS, CM);
private Int tIsFunction(TypeId typeId, CM);
private void addRawOverload(NameId nameId, TypeId typeId, FunctionId fnId, CM);
private TypeId exprUpToWithFrame(ParseFrame fr, ChInterval chi, TOKENS, CM);
private void typeAddHeader(TypeHeader hdr, CM);
private TypeHeader typeReadHeader(TypeId typeId, CM);
private Int typeEncodeTag(Unt sort, Int depth, Int arity, CM);
private TypeId getFirstParamType(TypeId funcTypeId, CM);
private TypeId typeGetOuter(TypeId firstArgTypeId, CM);
private TypeId typeCheckBigExpr(Int indExpr, Int sentinel, CM);
private TypeId typecheckList(Node nd, Int startInd, CM);
private TypeId tGetIndexOfFnFirstParam(TypeId fnType, CM);
private TypeId tCreateSingleParamTypeCall(NameId outerName, TypeId param, CM);
private TypeId tFunctionReturnType(TypeId t, CM);
private TypeId tCreateFnTypeCall(TParse* te, Int startInd, TypeFrame frame, CM);
private TypeId tCreateTypeCall(TParse* te, Byte sort, Int startInd, TypeFrame frame, CM);
private void teOpenTypeCall(NameId typeName, Int sentinel, TParse* te, CM);
private Int teMergeParam(NameId name, TParse* restrict te, CM);

private TypeId tParse(Int sentinel, OUT Bool* isGeneric, TOKENS, CM);
private NameLoc nameOfHost(Int strId);
void printNameNoLn(NameId nameId, CM);

defstruct(PrintableCompiler);
void printNameNoLnCr(NameId nameId, PrintableCompiler prc);

private void eWriteCallToScratch(ExprFrame frame, Expr* stEx);
private void tFreshState(TParse* st);
//~private TypeId teClause(TParse* st, Int sentinel, TOKENS, CM);
private FunctionId findOverload(NameId name, TypeId tpFstArg, CM);
private Int calcSentinel(Token tok, Int tokInd);
private void reorderFor(Int forStart, Int sentinel, TOKENS, LX);

private Int getBinding(Int id, CM);
TypeId tResolveGenericFnCall(Function fn, Arr(Int) args, Int argCount, CM);
private TypeId typeTryGetField(NameId name, TypeId t, OUT Int* mbFieldInd, CM);
void printType(TypeId type, PrintableCompiler prc);

private libeyr_CompilationErrors* getCompilationErrors(CM);
private void fillInCompilationResult(CM, OUT CompResult* cr);

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
   LChInterval*: addChInterval,\
   LCompileError*: addCompileError,\
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

defstruct(MultiAssocList);

#ifdef DEBUG

void printName(NameId nameId, CM);
void printIntArray(Int count, Arr(Int) arr);
void printAssocList(Int listInd, MultiAssocList* ml);
void printParser(Compiler* cm);
private void dbgExprFrames(CM);
private void printLInt(LInt* st);
void dbgTypeFrames(TParse* st);
void dbgOverloads(Int nameId, CM);
void dbgScopes(CM);
void dbgParseFrames(CM);
void dbgNodes(LNode*);

#define dbgType(t) printf("type %d len %d", t, cm->types.c[t.v] + 1);\
   printIntArrayOff(t.v, cm->types.c[t.v] + 1, cm->types.c);\
   printType(t, printableOfCompiler(cm));\
   printf("\n");

void dbgAllTypes(CM);

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
         d("reusing cleared memory from the arena!")
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

#define DECLARE_INTERNAL_LIST(T)\
typedef struct {\
   Arr(T) c;\
   Int len;\
   Int cap;\
} InList##T;\
private InList##T createInList##T(Int initCap, Arena* a) { \
   return (InList##T){                            \
      .c = allocateArray(initCap, T, a),   \
      .len = 0, .cap = initCap };\
}

#define DEFINE_INTERNAL_LIST(fieldName, T, aName)           \
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
struct MultiAssocList { // :MultiAssocList
   Int len;
   Int cap;
   Int freeList;
   Arr(Int) c;
   Arena* a;
};


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

private Int //:multiListCreateList
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
   Int len = ml->c[listInd];
   Int const endInd = listInd + 2 + len;
   for (Int j = listInd + 2; j < endInd; j += 2) {
      if (ml->c[j] == searchKey) {
         return ml->c[j + 1];
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

DECLARE_INTERNAL_LIST(Int)
DECLARE_INTERNAL_LIST(Ulong)

//}}}
//{{{ Strings

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

private String //:stringOf
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
   d("in create int map %d", realInitSize);
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

private bool //:hasKeyIntMap
hasKeyIntMap(int key, IntMap* hm) {
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

private int //:getIntMap
getIntMap(int key, int* value, IntMap* hm) {
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

private int //:getUnsafeIntMap
getUnsafeIntMap(int key, IntMap* hm) {
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

   NameLoc newName = ((Unt)(lenBts) << 24) + (Unt)startBt;
   if (bu == null) {
      Bucket* newBucket = allocateOnArena(sizeof(Bucket) + initBucketSize*sizeof(StringValue), hm->a);
      newBucket->capAndLen = (initBucketSize << 16) + 1; // left u16 = cap, right u16 = len
      StringValue* firstElem = (StringValue*)newBucket->c;

      newIndString = names->len;

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
//         endInd = exclusive
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

private Int //:binaryIntervalSearch
binaryIntervalSearch(Int key, Int end, Arr(Int) arr) {
// Finds the interval containing the key in a sorted (ascending) array
   Int i = 0;
   Int j = end - 1;
   if (end == 1)
      { return key >= arr[0] ? 0 : -1; }
   if (arr[0] <= key && key < arr[1]) {
      return 0;
   } ei (arr[j] <= key) {
      return j;
   }

   while (i < j) {
      if (j - i == 1)
         { return -1; }
      Int midInd = (i + j)/2;
      Int mid = arr[midInd];
      if (mid > key) {
         j = midInd;
      } ei (mid < key) {
         if (midInd == j - 1 || key < arr[midInd + 1])
            { return midInd; }
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
   Unt spanLevel : 4;
};

DEFINE_LIST(BtToken) //:createLBtToken
DEFINE_LIST(Token) //:createLToken

constexpr TypeId boolTy = { .v = tokBool };
constexpr TypeId intTy = { .v = tokInt };
constexpr TypeId ZERO_ARITY_TYPE = { .v = tokMisc };
constexpr TypeId VOID_TYPE = { .v = voidType };

constexpr Int arrLitRuntimeLength = BIG;  // array literal without elements but with runtime-known
                                          // length, [@Int (x + 2)]
constexpr Int arrLitKnownElements = BIG + 1; // array literal with all elements known
constexpr Int arrLitKnownLength = BIG + 2;  // array literal with comp-time known length, [@Str 15]

Bool eq_TypeId(TypeId a, TypeId b) {
    return a.v == b.v;
}

struct EachData{ //:EachData Data for "each" loops
   Int collVar;
   Int indexVar;
   Int elementVar;
};

#define pfrScope 1 // this frame is a scope (i.e. allows creation of var bindings)
#define pfrLoop  2 // this frame is a scope and a loop (allows break and continue)
#define pfrFn    3 // this frame is a function definition

struct ParseFrame { // :ParseFrame
   Int startTokenInd;
   Int startNodeInd;
   Int sentinel;      // sentinel token
   Byte level;        // the "pfr" constants above, or 0 if the frame is not a scope
   TypeId typeId;     // valid only for fnDef (then it's the function's type)
   EachData eachData; // For tracking hidden variables that make "each" loops run
};

DEFINE_LIST(ParseFrame) //:createLParseFrame


struct TypeFrame {   // :TypeFrame
   Byte tp;          // "tfr" constants
   Bool isGeneric;   // Have we encountered any type params here?
   Int sentinel;     // token id sentinel
   Int countArgs;    // accumulated number of type arguments
   TypeId id;        // For types, TypeId. For type params, their id within the params list
};

DEFINE_LIST(TypeFrame) //:createLTypeFrame

typedef struct {   //:BtCodegen Backtrack for generating code
   Byte tp;        // instructions, i.e. the "i*" constants
   Int startInstr; // index of starting instruction
   Int sentinel;   // sentinel node of current function
} BtCodegen;

struct ExprFrame { // :ExprFrame
   Byte tp;        // "exfr" constants below
   Bool isVarCall; // Iff it's a local variable being called rather than an overloaded fn name
   NameId name;
   Int sentinel;   // token sentinel
   Int precedence;
   Int argCount;   // accumulated number of arguments. Used for exfrCall & exfrDataLit only
   Int startNode;  // The id of first written node in @scr. Used for data literals
   ChInterval chi; // The original char interval this frame is based on
};

#define exfrParen       1 // Parens
#define exfrExWrapper   2 // Expression wrapper created inside data allocators
#define exfrCall        3
#define exfrUnaryCall   4
#define exfrDataLit     5
#define exfrAccessor    6 // an umbrella for an accessor chain like `a[i][j][k]`
#define exfrAccessIn    7 // internal accessor like `..[i]`
#define exfrStruct      8 // struct initializer like `Foo(:x 5 :y 15)`
#define exfrStructField 9 // struct field initializer like `:x a + b`


DEFINE_LIST(ExprFrame)  //:createLExprFrame
DEFINE_LIST(ChInterval) //:createLChInterval
DEFINE_LIST(SourceLoc)  //:createLSourceLoc

DEFINE_LIST(Node)

struct TypeLoc { //:TypeLoc
   Int currPos;
   Int sentinel;
};

DEFINE_LIST(TypeLoc)

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
   ScopeChunk* currChunk; // curr points to here, start may point to one of the previous ones
   Int* start;  // address for the start of current scope.
                // Value @ that address is size of prev scope. Example:
                //
                // (...)[1 2 3] (3)[4 5 6 7] (4)[1] (1)[...]
                //
                //   ^^ (sizes are in (), scope contents in [])
   Int* curr;   // address for addition of next binding, points into @currChunk
   Int currScopeLen; // length of current scope
   Int countScopes;
};


DEFINE_LIST(Var)

DEFINE_LIST(Function)

struct Expr { //:Expr State for parsing expressions
   LInt* exp;            // For assignments with complex left sides
   LExprFrame* frames;
   LNode scr;           // "Scratch". Draft nodes written to during expression parsing
   LChInterval locsScr; // SourceLocs for @scr
   LInt reorderKeys;    // (start end) which point into @scr. Used in struct reordering [aTmp]
   LNode reorderBuf;    // Reordering buffer for @scr used for struct reordering        [aTmp]
   Bool metAnAllocation; // if we've met an allocation, we need to emit sub-expression nodes
};

typedef struct { //:TypeHeader
   Unt start;     // index into @types
   Unt arity : 8; // count of immediate children (struct fields, or function params + return types)
   Unt len : 24;  // number of nodes in types (not integers, but nodes! type calls are >1 nodes)
   Unt tyrity : 8;   // count of type parameters
   Unt concrId : 24; // All 1111's unless tyrity = 0
} TypeHeader;

struct TParse { // :TParse State for parsing type expressions. Lives in [aTmp]
   LInt* exp;           //  TypeId
   LTypeFrame* frames;
   LInt names;         // Record field names
   LInt paramNames;    // Unique type param names
   LInt tParams;       // Unique type params of a type expression. [(nameId typeId)]
                       // Used in generic call resolution
   LInt* tmp;           // Used in name uniqueness validation, and generic param substitution
   LTypeLoc* genericWalk; // Type location stack, used for type tree traversal
   LTypeLoc* concreteWalk;
};

typedef struct { //:Assignment
// Info about an assignment or a definition (functions, variables, types)
   Int nameTokenInd;     // index of the tokWord after tokToplevelFn
   Int rightTokenInd; // index of the tokAssignRight
   Int sentinel;
   Bool isFunction;
   NameId name;
   Int entityId; // n < 0 => -n - 1 is an index into @functions, otherwise n => @vars
} Assignment;

typedef struct { //:GenericCall
   Int nodeInd;
   TypeId concrete;
   Int tokenInd;
} GenericCall;

DECLARE_INTERNAL_LIST(Assignment)
DECLARE_INTERNAL_LIST(Var)
DECLARE_INTERNAL_LIST(Function)
DECLARE_INTERNAL_LIST(Token) //:InListToken
DECLARE_INTERNAL_LIST(Node)
DECLARE_INTERNAL_LIST(FieldName)

struct Monomorphization { //:Monomorphization
   Int tokenInd;     // points into @tokens - the (generic) tokens. -1 => no codegen
   Int nodeInd;      // points into @ast - the monomorphized AST. -1 for built-ins
   FunctionId fnId;
};

DEFINE_LIST(Monomorphization)

struct Compiler { // :Compiler Private type holding all lexing and parsing state and results
   // LEXING
   String sourceCode;
   InListToken tokens;
   InListToken metas; // TODO - metas with links back into parent span tokens
   InListInt newlines;
   LSourceLoc* sourceLocs;
   InListInt numeric;      // [aTmp]
   LBtToken* lexBtrack;    // [aTmp]
   LUnt* names; // Operators, then standard strings, then imported ones, then
                           // parsed. Contains NameLoc pointing into @sourceCode

   LToken* reorderBuf;  // Buffer for reordering tokens for mutation assignments
   StringDict* stringDict;

   // PARSING
   InListInt toplevels;        // indices into @functions
   Int entrypoint;             // index into @functions
   InListInt importNames;
   LParseFrame* parseFrames;   // [aTmp]
   Scopes scopes;              // lists of local variables for keeping track of lexical scopes
   Expr expr;                  // all the contents are in [aTmp]
   TParse tParse;              // all the contents are in [aTmp]
   // For vars, index pointing into @vars.
   // For functions, (-ind - 2), ind points into @overloads. For types, index into @types
   Arr(Int) activeBindings;    // [aTmp]
   InListNode ast;             // Abstract syntax tree
   InListVar vars;                // local variables
   InListFunction functions;
   MultiAssocList* functionMonos; // (MultiAssocL (TypeId @monos), pointed into by Function.genericInd)

   InListInt publicFns;          // indices into @functions, subset of @toplevels
   InListInt publicConsts;       // indices into @vars
   MultiAssocList* rawOverloads; // [aTmp] (NameId => TypeId FunctionId)
   InListInt overloads;
   InListInt types;              // sea of nodes, see docs/types.txt
   InListInt fieldTypes;         // (len)[types of fields/fn params/return types]
   InListTypeHeader typeHeaders;
   StringDict* typesDict;    
   InListFieldName fieldNames; // fields of sorDeclare types (i.e. not type instantiations)
                                    // type instantiations have their own lists of types of fields
                                    // but not their names - the names are defined once per generic
                                    // type and kept here.
   SliUnt concreteFields; 
   LMonomorphization* monos; // Addresses of monomorphizations of generic functions

   // GENERAL STATE
   Int i; // index into the table that is being read
   Int j; // index into the table that is being written to (AST during typecheck)
   Arena* a;
   Arena* aTmp;
   LCompileError* errors;
   CompStats stats;
};

DEFINE_INTERNAL_LIST(newlines, Int, a) //:pushInnewlines
DEFINE_INTERNAL_LIST(numeric, Int, a) //:pushInnumeric
DEFINE_INTERNAL_LIST(importNames, Int, a) //:pushInimportNames
DEFINE_INTERNAL_LIST(overloads, Int, a) //:pushInoverloads
DEFINE_INTERNAL_LIST(types, Int, a) //:pushIntypes
DEFINE_INTERNAL_LIST(tokens, Token, a) //:pushIntokens
DEFINE_INTERNAL_LIST(toplevels, Int, a) //:pushIntoplevels
DEFINE_INTERNAL_LIST(vars, Var, a) //:pushInentities
DEFINE_INTERNAL_LIST(functions, Function, a) //:pushInfunctions
DEFINE_INTERNAL_LIST(ast, Node, a) //:pushInast
DEFINE_INTERNAL_LIST(fieldNames, FieldName, a) //:pushInfieldNames

// the following constants for TypeFrame must not clash with the "sor" constants
// Type expression data format: First element is the tag (one of the following
// constants), second is payload. Used in @expStack
#define tfrFunction       11
#define tfrParam          12 // payload: paramId
#define tfrRecord         13
#define tfrTypeCall       14

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

struct PrintableCompiler { //:PrintableCompiler Just the bits of the compiler needed to print type names
   Arr(Byte) sourceCode;
   Arr(Int) types;
   Arr(Int) names;
   Arena* a;
};

private PrintableCompiler //:printableOfCompiler
printableOfCompiler(CM) {
   return (PrintableCompiler){
      .sourceCode = cm->sourceCode.c, .types = cm->types.c, .names = cm->names->c, .a = cm->a
   };
}

private PrintableCompiler //:printableOfCompResult
printableOfCompResult(CompResult* cr) {
   return (PrintableCompiler){
      .types = cr->types.c, .sourceCode = cr->sourceCode.c, .names = cr->names.c, .a = cr->a
   };
}
//}}}
//{{{ Errors
//{{{ Compile errors

#define errMaxId 127 // must be updated. The maximal value of the currently existing errIds below
#define errNonAscii                     0
#define errPrematureEndOfInput          1
#define errUnrecognizedByte             2
#define errWordChunkStart               3
#define errWordCapitalizationOrder      4
#define errWordLengthExceeded           5
#define errWordMutability               6
#define errWordFreeFloatingFieldAcc     7
#define errNumericEndUnderscore         8
#define errNumericWidthExceeded         9
#define errNumericBinWidthExceeded     10
#define errNumericFloatWidthExceeded   11
#define errNumericEmpty                12
#define errNumericMultipleDots         13
#define errNumericIntWidthExceeded     14
#define errPunctuationExtraOpening     15
#define errPunctuationExtraClosing     16
#define errPunctuationCommaNotClause   17
#define errPunctuationOnlyInMultiline  18
#define errPunctuationFnNotInStmt      19
#define errPunctuationUnmatched        20
#define errPunctuationScope            21
#define errOperatorUnknown             22
#define errOperatorAssignmentPunct     23
#define errAssignmentEmptyRight        24
#define errOperatorTypeDeclPunct       25
#define errOperatorMutationInDef       26
#define errCoreNotInsideStmt           27
#define errCoreMisplacedElse           28
#define errCoreMissingParen            29
#define errBareAtom                    30
#define errImportsNonUnique            31
#define errCannotMutateImmutable       32
#define errPrematureEndOfTokens        33
#define errUnexpectedToken             34
#define errCoreFormTooShort            35
#define errCoreFormUnexpected          36
#define errCoreFormAssignment          37
#define errCoreFormInappropriate       38
#define errIfLeft                      39
#define errIfRight                     40
#define errIfEmpty                     41
#define errIfMalformed                 42
#define errIfElseMustBeLast            43
#define errFnParamList                 44
#define errFnDuplicateParams           45
#define errFnEntrypoint                46
#define errFnMissingBody               47
#define errFnOperatorOverlArity        48
#define errFnOperatorNotOverloadable  125
#define errLoopSyntaxError             49
#define errLoopNoCondition             50
#define errLoopEmptyStepBody           51
#define errLoopWrongFormInStepper      52
#define errLoopBreakOutside            53
#define errBreakContinueTooComplex     54
#define errBreakContinueInvalidDepth   55
#define errEachLoopWrongSyntax         56
#define errEachLoopInvalidValue        57
#define errEachNotACollection          58
#define errDuplicateFunction           59
#define errExpressionError             60
#define errExpressionExpectedWord     124
#define errExpressionWrongArgCount     61
#define errExpressionCannotContain     62
#define errExpressionFunctionless      63
#define errTypeDefCountNames           64
#define errTypeDefCannotContain        65
#define errTypeExpr                    66
#define errTypeDefError                67
#define errTypeParamsResolve           68
#define errOperatorWrongArity          69
#define errUnknownBinding              70
#define errUnknownFunction             71
#define errOperatorUsedInappropriately 72
#define errAssignment                  73
#define errListDifferentEltTypes       74
#define errListUnknownEltType          75
#define errMutation                    76
#define errAssignmentShadowing         77
#define errAssignmentLeftSide          78
#define errAssignmentAccessOnToplevel  79
#define errAssignmentToFunctionVar     80
#define errFnSignature                 81
#define errFnTypeArrows                82
#define errArrowOutOfPlace             83
#define errReturn                      84
#define errScope                       85
#define errMetaOnlyInArr               86
#define errMetaArrSyntax               87
#define errTemp                        88
#define errEmptySourceCode             89
#define errUnknownType                 90
#define errUnexpectedType              91
#define errExpectedType                92
#define errUnknownTypeConstructor      93
#define errTypeUnknownFirstArg         94
#define errTypeOverloadsIntersect      95
#define errTypeOverloadsOnlyOneZero    96
#define errTypeNoMatchingOverload      97
#define errTypeOverloadWrongArity     122
#define errTypeWrongArgumentType       98
#define errTypeWrongReturnType         99
#define errTypeMismatch               100
#define errTypeMustBeBool             101
#define errTypeConstructorWrongArity  102
#define errTypeTooManyParameters      103
#define errTypeOfNotList              104
#define errTypeOfListIndex            105
#define errTypePolymorphicAssignment  106
#define errTypeGenericCallDoesntUnify 107
#define errTypeGenericWrongArity      123
#define errTypeFieldNotFound          108
#define errTypeFieldNotSpecified      126
#define errTypeStructDefinition       127
// internal errors:
#define ierrInconsistentSpans         109 // Inconsistent span length / structure of token
                                          // scopes!
#define ierrImportedFnNotInScope      110 // There is a -1 or something else in the
                                          // @activeBindings for an imported function
#define ierrParsedFunctionNotInScope  111 // There is a -1 or something else in the
                                          // @activeBindings for a parsed function
#define ierrOverloadsOverflow         112 // There were more overloads for a function than
                                          // what was allocated
#define ierrOverloadsNotFull          113 // There were fewer overloads for a function than
                                          // what was allocated
#define ierrOverloadsIncoherent       114 // The overloads table is incoherent
#define ierrExpressionIsNotAnExpr     115 // What is supposed to be an expression in the AST is
                                          // not a nodExpr
#define ierrComplexExpression         116 // Error in a complex expression's internal definitions
#define ierrGenericTypesInconsistent  117 // Two generic types have inconsistent layout
                                          // (premature end of type)
#define ierrOuterTypeOfParam          118 // Tried to get an outer type of param or generic
#define ierrInconsistentTypeExpr      119 // Reduced type expression has != 1 elements
#define ierrNotAFunction              120 // Expected to find a function type here
#define ierrIllegalEmit               121 // This entity cannot have this emit type in codegen

private char const* const
compileErrors[] = {
   "Non-ASCII symbols are not allowed in code - only inside comments & string literals!",
   "Premature end of input",
   "Unrecognized Byte in source code!",
   "In an identifier, each word piece must start with a letter. Tilde may come only after an identifier",
   "An identifier may not contain a capitalized piece after an uncapitalized one!",
   "I don't know why you want an identifier of more than 128 chars, but they aren't supported",
   "Mutable variable declarations should look like `asdf'` with no spaces in between",
   "Free-floating field accessor",
   "Numeric literal cannot end with underscore!",
   "Numeric literal width is exceeded!",
   "Integer literals cannot exceed 64 bit!", // 10
   "Floating-point literals cannot exceed 2**53 in the significant bits, and 22 in the decimal power!",
   "Could not lex a numeric literal, empty sequence!",
   "Multiple dots in numeric literals are not allowed!",
   "Integer literals must be within the range [-9,223,372,036,854,775,808, 9,223,372,036,854,775,807]!",
   "Extra opening punctuation",
   "Extra closing punctuation",
   "The comma is only allowed inside clauses!",
   "The statement ender `,` is not allowed inside subexpressions!",
   "Function definitions must be directly in a statement",
   "Unmatched closing punctuation", // 20
   "Scopes may only be opened in multi-line syntax forms or in `for`, `if` forms",
   "Unknown operator",
   "Incorrect assignment operator: must be directly inside an ordinary statement, after the binding"
      " name(s) or l-value!",
   "Assignment or definition with empty right side",
   "Incorrect type declaration operator placement: must be the first in a statement!",
   "Mutation (e.g. `+=`) is not allowed for defs which signify compile-time known constants",
   "Core form must be directly inside statement",
   "The else statement must be inside an if, ifEq, ifPr or match form",
   "Core form requires opening parenthesis/curly brace immediately after keyword!",
   "Malformed token stream (atoms and parentheses must not be bare)", // 30
   "Import names must be unique!",
   "Variable $0 is immutable and cannot be reassigned",
   "Premature end of tokens",
   "Unexpected token",
   "Core syntax form too short",
   "Unexpected core form",
   "A core form may not contain any assignments!",
   "Inappropriate reserved word!",
   "A left-hand clause in an if can only contain variables, boolean literals and expressions!",
   "A right-hand clause in an if can only contain atoms, expressions, scopes and some "
      "core forms!", // 40
   "Empty `if` expression",
   "Malformed `if` expression, should look like (if pred: `true case` else `default`)",
   "An `else` subexpression must be the last thing in an `if`",
   "Function parameter list must look like this: `{x y ->  body...}`",
   "Duplicate parameter names in a function are not allowed",
   "The entrypoint must be named `main` and this function name must be unique!",
   "Function definition must contain a body which must be a Scope immediately following its "
      "parameter list!",
   "Operator overloads must respect the arity of the operator! Expected arity $0 but got $1",
   "A loop should look like `for {x = 0, x < 101; x++ -> loopBody } `",
   "A loop header should contain a condition", // 50
   "Empty loop step code & body, but at least one must be present!",
   "A for loop's stepper can only contain assignments, expressions and asserts",
   "The break keyword can only be used inside a loop scope!",
   "This statement is too complex! Continues and breaks may contain"
      " one thing only: the positive number of enclosing loops to continue/break!",
   "Invalid depth of break/continue $0! It must be a positive 32-bit integer!",
   "Wrong syntax of an 'each' loop",
   "Invalid value provided for an 'each' loop: $0",
   "Collection name $0 not found among any active 'each' loops",
   "Duplicate function declaration: a function with same name and arity already exists in this scope!",
   "Cannot parse expression!", // 60
   "Wrong argument count for a function",
   "Expressions cannot contain scopes or statements!",
   "Expected to see a word naming a function",
   "Wrong count of names in a type definition!",
   "Type declarations may only contain types (like Int),"
      " type params (like A), type constructors (like List) and parentheses!",
   "Cannot parse type expression!",
   "Cannot parse type declaration!",
   "Error resolving type params. Param $0 has an unknown value",
   "Wrong number of arguments for operator!",
   "Unknown binding $0", // 70
   "Unknown function!",
   "Operator used in an inappropriate location!",
   "Cannot parse assignment, it must look like `freshIdentifier` = `expression`",
   "An array or list's elements must all be of the same type. "
      "Element type $0 but previous element $1",
   "Could not determine the element type of an array or list!",
   "Cannot parse mutation, it must look like `freshIdentifier` += `expression`",
   "Assignment error: existing identifier is being shadowed",
   "Assignment error: left side must be a var name, a type name, or an existing var with one or"
      " more accessors",
   "Accessor on the left side of an assignment at toplevel",
   "Assignment to a function variable should look like `fn F(Int -> Long) = overloadedName,`", // 80
   "A function signature should look like `fn [Int -> Long] f{a-> ...},`",
   "A function type should contain exactly one arrow and return type (unless "
      "it's void): `F[Par1 Par2 -> ReturnType]`, `F[Par1 ->]`",
   "Arrows must be either in a function type: `F[Param -> ReturnType]` or "
   "closing a parameter list: `f{ param -> ...body...}`" ,
   "Cannot parse return statement, it must look like `return ` {expression}",
   "A scope may consist only of expressions, assignments, function definitions and other scopes!",
   "Meta blocks are only allowed in array expressions!",
   "Meta blocks must have 2 parts: type and length: `@(Int 2)`, and if present, "
      "must be the only thing in the collection declaration",
   "Not implemented yet",
   "Empty source code",
   "Unknown type", // 90
   "Unexpected to find a type here",
   "Expected to find a type here",
   "Unknown type constructor: $0",
   "The type of first argument to a call must be known, otherwise can't resolve the function"
      " overload! Function: $0",
   "Two or more overloads of a single function intersect (impossible to choose one over the other)",
   "Only one nullary function version is possible, otherwise I can't disambiguate the overloads!",
   "No matching function overload was found for name $0 and first parameter type $1",
   "Wrong argument type, got $0 but expected $1",
   "Wrong return type",
   "Declared type doesn't match actual type. $0 vs $1", // 100
   "Expression must have the Bool type, but has: $0",
   "Wrong arity for the type constructor",
   "Only up to 254 type parameters are supported",
   "Trying to get the element of a type which is not a list",
   "The type of a list/array index must be Int",
   "Assignments and constants must be monomorphic (no type params)",
   "Generic function's type cannot be unified with its argument types",
   "Field access error: cannot find field $0 in the type $1",
   // internal errors
   "Inconsistent span length / structure of token scopes",
   "There is a -1 or something else in the @activeBindings for an imported function", // 110
   "There is a -1 or something else in the @activeBindings for a parsed function",
   "There were more overloads for a function than what was allocated",
   "There were fewer overloads for a function than what was allocated",
   "The overloads table is incoherent",
   "What is supposed to be an expression in the AST is not a nodExpr",
   "Error in a complex expression's internal definitions",
   "Two generic types have inconsistent layout (premature end of type)",
   "Tried to get an outer type of param or generic",
   "Reduced type expression has != 1 elements",
   "Expected to find a function type here", // 120
   "This entity cannot have this emit type in codegen",
   "The matching function overload for name $0 has the wrong arity, $1. Type $2",
   "Generic function's type has wrong arity $0 but should be $1",
   "Expected a word",
   "Operator $0 is not overloadable",
   "Not all fields specified for struct $0, for example $1 is missing",
   "Incorrect struct definition, should look like `Foo = struct :id Int :name Str;`"
};

struct libeyr_CompilationErrors { //:libeyr_CompilationErrors
   Arr(CompileError) c;
   Int len;
};

//}}}
//{{{ Types & utils


typedef struct { //:ErrorPosition
   Int count;
   Int indices[3]; // indices in @sourceCode. First index is outer span, the other two (optional)
                   // refer to tokens within that span
} ErrorPosition;

typedef enum { //:ErrorTextKind
   errtxtType, // for indices into @types
   errtxtName,  // indices into @names
   errtxtNumber, // just ordinary numbers
   errtxtOper // operators
} ErrorTextKind;

typedef struct { //:ErrTextSumType
   ErrorTextKind tp; // "errtp" constants
   Int c;
} ErrTextSumType;

typedef struct { //:ErrorText
   Int count;
   ErrTextSumType c[3];
} ErrorText;

struct CompileError { //:CompileError
   Int id; // one of the "err" constants
   ErrorPosition positional;
   ErrorText textual;
#ifdef DEBUG
   Int codeLine;
#endif
};

DEFINE_LIST(CompileError) //:createLCompileError

void libeyr_printError(Int errId) {
   d("%s", compileErrors[errId]);
}

Int
libeyr_getFirstErrorId(CompResult* cr) {
   if (cr->errors->len == 0)
      { return -1; }
   return cr->errors->c[0].id;
}

private void //:printPositionalError
printPositionalError(ErrorPosition e, CompResult* cr) {
   if (e.count == 0)
      { return; }
   Int len = e.indices[1] - e.indices[0];
   printf("-----------\n");
   fwrite(cr->sourceCode.c + e.indices[0], 1, len, stdout);
   printf("\n-----------\n");
}

private void //:printTextualError
printTextualError(Int errId, ErrorText e, CompResult* cr) {
   char const* text = compileErrors[errId];
   if (e.count == 0) {
      d("%s", text);
      return;
   }

   char const* prev = text;
   char const* curr = text;

   for (; *curr != '\0'; curr++) {
      if (*curr == '$' && *(curr + 1) >= aDigit0 && *(curr + 1) < (aDigit0 + 3)) {
         Int ind = *(curr + 1) - aDigit0;
         if (ind >= e.count) {
            continue;
         }
         fwrite(prev, 1, curr - prev, stdout);

         ErrTextSumType printable = e.c[ind];
         switch (printable.tp) {
         case errtxtType: {
            printType(typeOf(printable.c), printableOfCompResult(cr));
            break;
         }
         case errtxtName: {
            Unt unsign = cr->names.c[printable.c];
            Int startBt = unsign & LOWER24BITS;
            Int len = (unsign >> 24) & 0xFF;
            fwrite(cr->sourceCode.c + startBt, 1, len, stdout);
            break;
         }
         case errtxtNumber: {
            d("printing number");
            printf("%d", printable.c);
            break;
         }
         case errtxtOper:
            NameLoc nameLoc = OPERATORS[printable.c].name;
            Int startBt = nameLoc & LOWER24BITS;
            Int len = (nameLoc >> 24) & 0xFF;
            fwrite(cr->sourceCode.c + startBt, 1, len, stdout);
            break;
         }
         prev = curr + 2; // skipping the `$1`
         curr++;
      }
   }
   if (curr > prev)
      { fwrite(prev, 1, curr - prev, stdout); }
   printf("\n");
}

private void //:printError
printError(CompileError err, CompResult* cr) {
   printPositionalError(err.positional, cr);
   printTextualError(err.id, err.textual, cr);

#ifdef DEBUG
   d("Code line: %d", err.codeLine);
#endif
}

void
libeyr_printErrors(CompResult* cr) {
   if (cr->errors->len == 0)
      { return; }

   for (Int i = 0; i < cr->errors->len; i++) {
      printError(cr->errors->c[i], cr);
   }
}

//}}}
//}}}
//{{{ Lexer
//{{{ Lexer utils

#define CURR_BT source[lx->i]
#define NEXT_BT source[lx->i + 1]
#define IND_BT (lx->i - lx->stats.standardTextLen)

#ifdef DEBUG
#define VALIDATEI(cond, errInd) if (!(cond)) { throwExcInternal0(errInd, __LINE__, cm); }
#endif
#if !defined(DEBUG)
#define VALIDATEI(cond, errInd)
#endif
#define VALIDATEL(cond, err) if (!(cond)) { throwExcLexer0(err, __LINE__, lx); }


#ifdef DEBUG

Int pos(Compiler* lx);
void dbgLexBtrack(Compiler* lx);

#endif

typedef union { //:FloatingBits
   uint64_t i;
   double   d;
} FloatingBits;

private String //:readSourceFile
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

NameId //:nameOfStd
nameOfStd(Int strId) {
// Converts a standard string to its nameId.
   return (NameId)((Unt)(strId + countOperators));
}

private void //:skipSpaces
skipSpaces(Arr(char const) source, LX) {
   while (lx->i < lx->stats.inpLength) {
      Byte currBt = CURR_BT;
      if (!isSpace(currBt))
         { return; }
      lx->i++;
   }
}

private void //:ensureCapacityTokenBuf
ensureCapacityTokenBuf(Int neededSpace, LToken* st, Compiler* cm) {
// Reserve space in the temp buffer used to shuffle tokens
   st->len = neededSpace;
   if (neededSpace >= st->cap) {
      Arr(Token) newContent = allocateArray(neededSpace, Token, cm->a);
      st->cap = neededSpace;
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

[[noreturn]] private void
throwExcInternal0(Int errId, Int lineNumber, CM) {
#ifdef DEBUG
   printf("Internal error %d at line %d\n", errId, lineNumber);
#endif
   d("%s", compileErrors[errId]);
   longjmp(excBuf, 1);
}

#define throwExcInternal(errInd) throwExcInternal0(errInd, __LINE__, cm) //:throwExcInternal

[[noreturn]] private void
throwExcLexer0(CompileError err, Int lineNumber, LX) {
// Sets i to beyond input's length to communicate to callers that lexing is over
#ifdef DEBUG
   err.codeLine = lineNumber;
#endif
   add(err, lx->errors);
   longjmp(excBuf, 1);
}

#define throwExcLexer(err) throwExcLexer0(err, __LINE__, lx)

#define lexError(errId) lexError0(errId, lx)

private CompileError //:lexError
lexError0(Int errId, LX) {
// An error where there is only a positional part, and it's built using current lexer position
   Int indStatement = -1;
   for (Int j = lx->lexBtrack->len - 1; j > -1; j--) {
      if (lx->lexBtrack->c[j].tp == tokStmt) {
         indStatement = j;
         break;
      }
   }

   Int startBt, endBt;
   if (indStatement > -1) {
      startBt = lx->tokens.c[lx->lexBtrack->c[indStatement].tokenInd].startBt;
   } else {
      startBt = MAX(lx->i - 10, 0);
   }
   endBt = MIN(lx->i + 1, lx->sourceCode.len);

   return (CompileError){
      .id = errId,
      .positional = (ErrorPosition){.count = 2, .indices = {startBt, endBt}},
      .textual = (ErrorText){.count = 0}
   };
}

//}}}
//{{{ Lexer proper

private void //:checkPrematureEnd
checkPrematureEnd(Int requiredSymbols, LX) {
// Checks that there are at least 'requiredSymbols' symbols left in the input
   VALIDATEL(lx->i + requiredSymbols <= lx->stats.inpLength, lexError(errPrematureEndOfInput))
}

private void //:setSpanLengthLexer
setSpanLengthLexer(Int tokenInd, LX) {
// Finds the top-level punctuation opener by its index, and sets its lengths.
// Called when the matching closer is lexed. Does not pop anything from @lexBtrack
   lx->tokens.c[tokenInd].lenBts = lx->i - lx->tokens.c[tokenInd].startBt + 1;
   lx->tokens.c[tokenInd].pl2 = lx->tokens.len - tokenInd - 1;
}

private void //:setStmtSpanLength
setStmtSpanLength(Int spanInd, LX) {
// Correctly calculates the lenBts for a single-line, statement-type span.
   lx->tokens.c[spanInd].lenBts = lx->i - lx->tokens.c[spanInd].startBt;
   lx->tokens.c[spanInd].pl2 = lx->tokens.len - spanInd - 1;
}

private void //:addStatementSpan
addStatementSpan(Unt stmtType, Int startBt, LX) {
   add(((BtToken){ .tp = stmtType, .tokenInd = lx->tokens.len, .spanLevel = slStmt }),
               lx->lexBtrack);
   pushIntokens((Token){ .tp = stmtType, .startBt = startBt, .lenBts = 0 }, lx);
}

private void //:wrapInAStatement
wrapInAStatement(Int startBt, Arr(char const) source, LX) {
// Wraps a new token in a statement. Sets the startBt to a specific value
   if (lx->lexBtrack->len == 0) {
      addStatementSpan(tokStmt, startBt, lx);
      return;
   }
   BtToken const top = last(lx->lexBtrack);
   if (top.tp == tokToplevelFn) {
      return;
   } ei (top.spanLevel == slScope || top.spanLevel == slUnbraced) {
      // the second case is for the conditions of "if" statements
      addStatementSpan(tokStmt, startBt, lx);
   } ei (top.spanLevel == slClauseList) {
      addStatementSpan(tokClause, startBt, lx);
   }
}

private int64_t //:calcIntegerWithinLimits
calcIntegerWithinLimits(LX) {
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

private Bool //:integerWithinDigits
integerWithinDigits(const Byte* b, Int bLength, LX) {
// Is the current numeric <= b if they are regarded as arrays of decimal digits (0 to 9)?
   if (lx->numeric.len != bLength) return (lx->numeric.len < bLength);
   for (Int j = 0; j < lx->numeric.len; j++) {
      if (lx->numeric.c[j] < b[j]) return true;
      if (lx->numeric.c[j] > b[j]) return false;
   }
   return true;
}

private Int //:calcInteger
calcInteger(int64_t* result, LX) {
   if (lx->numeric.len > 19 || !integerWithinDigits(maxInt, sizeof(maxInt), lx)) return -1;
   *result = calcIntegerWithinLimits(lx);
   return 0;
}

private Long //:calcHexNumber
calcHexNumber(LX) {
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

private void //:hexNumber
hexNumber(Arr(char const) source, LX) {
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
         throwExcLexer(lexError(errNumericEndUnderscore));
      } else {
         break;
      }
      VALIDATEL(lx->numeric.len <= 16, lexError(errNumericBinWidthExceeded))
      j++;
   }
   int64_t resultValue = calcHexNumber(lx);
   pushIntokens((Token){ .tp = tokInt, .pl1 = resultValue >> 32, .pl2 = resultValue & LOWER32BITS,
            .startBt = lx->i, .lenBts = j - lx->i }, lx);
   lx->numeric.c = 0;
   lx->i = j; // CONSUME the hex number
}

private Int //:calcFloating
calcFloating(double* result, Int powerOfTen, SRC, LX) {
// Parses the floating-point numbers using just the "fast path" of David Gay's
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

int64_t //:longOfDoubleBits
longOfDoubleBits(double d) {
   FloatingBits un = {.d = d};
   return un.i;
}

private double //:doubleOfLongBits
doubleOfLongBits(int64_t i) {
   FloatingBits un = {.i = i};
   return un.d;
}

private void //:decNumber
decNumber(bool isNegative, SRC, LX) {
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
            lexError(errNumericEndUnderscore)
         )
      } ei (cByte == aDot) {
         if (j == lx->stats.inpLength - 1 || !isDigit(source[j + 1])) {
            // this dot is not a part of the number
            break;
         }
         VALIDATEL(!metDot, lexError(errNumericMultipleDots))
         metDot = true;
      } else {
         break;
      }
      j++;
   }

   VALIDATEL(j >= lx->stats.inpLength || !isDigit(source[j]), lexError(errNumericWidthExceeded))

   if (metDot) {
      double resultValue = 0;
      Int errorCode = calcFloating(&resultValue, -digitsAfterDot, source, lx);
      VALIDATEL(errorCode == 0, lexError(errNumericFloatWidthExceeded))

      Long bitsOfFloat = longOfDoubleBits((isNegative) ? (-resultValue) : resultValue);
      pushIntokens((Token){ .tp = tokDouble, .pl1 = (bitsOfFloat >> 32),
               .pl2 = (bitsOfFloat & LOWER32BITS), .startBt = lx->i, .lenBts = j - lx->i}, lx);
   } else {
      int64_t resultValue = 0;
      Int errorCode = calcInteger(&resultValue, lx);
      VALIDATEL(errorCode == 0, lexError(errNumericIntWidthExceeded))

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

private void //:openPunctuation
openPunctuation(Unt tType, Unt spanLevel, Int startBt, LX) {
// Adds a token which serves punctuation purposes, i.e. either a ( or  a [
// These tokens are used to define the structure, that is, nesting within the AST.
// Upon addition, they are saved to the backtracking stack to be updated with their length
// once it is known. Consumes no bytes
   add(((BtToken){ .tp = tType, .tokenInd = lx->tokens.len, .spanLevel = spanLevel}),
         lx->lexBtrack);
   pushIntokens((Token) {.tp = tType, .pl1 = (tType < firstScopeTokenType) ? 0 : spanLevel,
                    .startBt = startBt }, lx);
}

private void //:lexIf
lexIf(Unt reservedWordType, Int startBt, SRC, LX) {
   if (reservedWordType == tokElse) {
      openPunctuation(tokElse, slScope, startBt, lx);
   } else {
      openPunctuation(reservedWordType, slUnbraced, startBt, lx);
   }
}

private void //:lexReservedScope
lexReservedScope(Int reservedWordType, SRC, LX) {
// A reserved word must be the first inside parentheses, but parentheses are always
// wrapped in statements, so we need to check the TWO last tokens and two top BtTokens
   LBtToken* bt = lx->lexBtrack;

   VALIDATEL(bt->len >= 2 && last(bt).tp == tokParens
      && bt->c[bt->len - 2].tp == tokStmt, lexError(errCoreFormInappropriate))

   Int const indLastToken = lx->tokens.len - 1;
   VALIDATEL(lx->tokens.c[indLastToken].tp == tokParens
      && lx->tokens.c[indLastToken - 1].tp == tokStmt, lexError(errCoreFormInappropriate))
   lx->tokens.c[indLastToken - 1].tp = reservedWordType;
   lx->tokens.c[indLastToken - 1].pl1 = slScope;
   lx->tokens.len--;
   bt->c[bt->len - 2].tp = reservedWordType;
   bt->c[bt->len - 2].spanLevel = slScope;
   bt->len--;
   skipSpaces(source, lx);
}

private void //:lexProcessSyntaxForm
lexProcessSyntaxForm(Unt reservedWordType, Int startBt, SRC, LX) {
// Lexer action for a paren-type or statement-type syntax form.
// Precondition: we are looking at the character immediately after the keyword
// We must NOT consume any characters here - that's been done in {wordInternal}
   if (reservedWordType >= tokIf && reservedWordType <= tokElse) {
      lexIf(reservedWordType, startBt, source, lx);
   } ei (reservedWordType == tokToplevelFn) {
      openPunctuation(tokToplevelFn, slToplevel, startBt, lx);
   } ei (reservedWordType == tokFor || reservedWordType == tokEach) {
      skipSpaces(source, lx);
      VALIDATEL(lx->i < lx->stats.inpLength && CURR_BT == aCurlyLeft, lexError(errLoopSyntaxError))
      openPunctuation(reservedWordType, slScope, startBt, lx);
      lx->i++; // CONSUME the `{`
      // placeholder, will be reordered in {pFor}
      pushIntokens(((Token){.tp = tokMisc, .pl1 = miscLoopStep0, .startBt = lx->i}), lx);
   } ei (reservedWordType >= firstScopeTokenType) {
      lexReservedScope(reservedWordType, source, lx);
   } ei (reservedWordType >= firstSpanTokenType) {
      LBtToken* bt = lx->lexBtrack;
      VALIDATEL(bt->len == 0 || last(bt).spanLevel == slScope, lexError(errCoreNotInsideStmt))
      addStatementSpan(reservedWordType, startBt, lx);
   }
}

private Bool //:wordChunk
wordChunk(SRC, LX) {
// Lexes a single chunk of a word, i.e. the characters between two minuses (or the whole word
// if there are no minuses). Returns True if the lexed chunk was capitalized
   Bool result = false;
   checkPrematureEnd(1, lx);

   Byte currBt = CURR_BT;
   if (isCapitalLetter(currBt)) {
      result = true;
   } else VALIDATEL(isLowercaseLetter(currBt), lexError(errWordChunkStart))

   lx->i++; // CONSUME the first letter of the word
   while (lx->i < lx->stats.inpLength && isAlphanumeric(CURR_BT)) {
      lx->i++; // CONSUME alphanumeric characters
   }
   return result;
}

private void //:mbCloseAssignRight
mbCloseAssignRight(BtToken* top, CM) {
// Handles the case we are closing a tokAssignRight: we need to close its parent tokAssignment!
   if (top->tp != tokAssignRight)
      { return; }
   setStmtSpanLength(top->tokenInd, cm);
   VALIDATEI(cm->lexBtrack->len > 0 && (last(cm->lexBtrack).tp == tokAssignment),
           ierrInconsistentSpans
   )
   *top = removeLast(cm->lexBtrack);
   setStmtSpanLength(top->tokenInd, cm);
}

private void //:lxCloseFnDef
lxCloseFnDef(BtToken* top, CM) {
// Handles the case we are closing a function definition: we need to close its parent tokAssignment!
   LBtToken* bt = cm->lexBtrack;
   setStmtSpanLength(top->tokenInd, cm);
   if (bt->len == 0 || last(bt).tp != tokAssignRight)
      { return; }
   *top = removeLast(bt); // the tokAssignRight
   setStmtSpanLength(top->tokenInd, cm);

   VALIDATEI(bt->len > 0 && last(bt).tp == tokAssignment, ierrInconsistentSpans)

   *top = removeLast(bt); // the tokAssignment
   setStmtSpanLength(top->tokenInd, cm);
}

private void //:closeStatement
closeStatement(LX) {
// Closes the current statement. Consumes no tokens
   BtToken top = last(lx->lexBtrack);
   VALIDATEL(top.spanLevel == slStmt, lexError(errPunctuationExtraOpening))
   setStmtSpanLength(top.tokenInd, lx);
   removeLast(lx->lexBtrack);
   mbCloseAssignRight(&top, lx);
}

private void //:wordNormal
wordNormal(
   Unt wordType, Int uniqueStringId, Int startBt, Int realStartBt, Bool wasCapitalized, SRC, LX
) {
// RealStartBt is the word-initial "$", "." etc if any, startBt is the first letter of the word
// Consumes the word and, for some symbols, the following symbol
   Int const lenBts = lx->i - realStartBt;
   Token newToken = (Token){ .tp = wordType, .pl1 = uniqueStringId, .pl2 = 0,
         .startBt = realStartBt, .lenBts = lenBts };
   if (wordType == tokWord && wasCapitalized) { // a type name
      if (lenBts == 1 && lx->i < lx->stats.inpLength && CURR_BT == aBracketLeft // `F[...]`
         && uniqueStringId == nameOfStd(strF)
      ) {
         add(((BtToken){ .tp = tokType, .tokenInd = lx->tokens.len, .spanLevel = slFnTp}),
               lx->lexBtrack
         );
         lx->i++; // CONSUME the left bracket
      } ei (lx->i < lx->stats.inpLength && CURR_BT == aParenLeft) { // struct literal `Foo()`
         newToken.tp = tokStruct;
         openPunctuation(tokStruct, slSubexpr, realStartBt, lx);
         lx->i++; // CONSUME the left parenthesis
         return;
      } ei (lx->tokens.len > 0) { // a type name may need to change the outer []
         Token prevToken = lx->tokens.c[lx->tokens.len - 1];
         if (prevToken.tp == tokData && prevToken.pl1 < BIG) { // convert a [] to a type span
            lx->tokens.c[lx->tokens.len - 1] = (Token){
               .tp = tokType, .pl1 = uniqueStringId, .startBt = prevToken.startBt
            };
            return;
         } ei (prevToken.tp == tokType && prevToken.pl1 == -1) {
            lx->tokens.c[lx->tokens.len - 1].pl1 = uniqueStringId;
            return;
         }
      }
      newToken.tp = tokType;
   } ei (lx->i < lx->stats.inpLength) {
      if (CURR_BT == aBracketLeft && wordType == tokWord) { // `a[5]`
         openPunctuation(tokAccessor, slSubexpr, realStartBt, lx);
         pushIntokens(newToken, lx);
         openPunctuation(tokAccessorIn, slSubexpr, lx->i, lx);
         lx->i++; // CONSUME the left bracket
         return;
      } ei (CURR_BT == aApostrophe) { // mutable var definition
         newToken.pl2 = 1;
         lx->i++; // CONSUME the `'`
      } ei (CURR_BT == aCurlyLeft && lenBts == 1 && source[startBt] == aFLower) {// fn body `f{..}`
         openPunctuation(tokFn, slScope, realStartBt, lx);
         lx->i++; // CONSUME the left curly brace
         return;
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
      } ei (keywordTp == keywNot) {
         pushIntokens((Token) {.tp = tokOperator, .pl1 = opBoolNot, .pl2 = precUnary,
            .startBt = realStartBt, .lenBts = 3 }, lx
         );
      }
   } else {
      lexProcessSyntaxForm(keywordTp, realStartBt, source, lx);
   }
}

private void //:wordInternal
wordInternal(Unt wordType, SRC, LX) {
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
            VALIDATEL(!wasCapitalized, lexError(errWordCapitalizationOrder))
            wasCapitalized = isCurrCapitalized;
         } else {
            break;
         }
      } else {
         break;
      }
   }

   // accounting for the initial ".", ":" or other symbol
   Int const realStartBt = (wordType == tokWord) ? startBt : (startBt - 1);
   Int lenString = lx->i - startBt;
   VALIDATEL(lenString <= maxWordLength, lexError(errWordLengthExceeded))

   Int stringId = addStringDict(source, startBt, lenString, lx->names, lx->stringDict);
   if (stringId - countOperators < strFirstNonReserved && wordType == tokWord) {
      wordReserved(wordType, stringId - countOperators, startBt, realStartBt, source, lx);
   } else {
      wrapInAStatement(realStartBt, source, lx);
      wordNormal(wordType, stringId, startBt, realStartBt, wasCapitalized, source, lx);
   }
}

private void //:lexWord
lexWord(SRC, LX) {
   wordInternal(tokWord, source, lx);
}

private void //:lexAt
lexAt(SRC, LX) {
// `[@Int 15]`
   VALIDATEL(lx->i < lx->stats.inpLength, lexError(errPrematureEndOfInput));
   VALIDATEL(lx->lexBtrack->len > 0 && last(lx->lexBtrack).tp == tokData,
      lexError(errMetaOnlyInArr));
   BtToken top = last(lx->lexBtrack);
   VALIDATEL(lx->tokens.c[top.tokenInd].pl1 < BIG, lexError(errMetaArrSyntax));
   lx->tokens.c[top.tokenInd].pl1 += BIG;
   lx->i++; // CONSUME the `@`
}

private void //:lexComma
lexComma(SRC, LX) {
   lx->i++;  // CONSUME the ",". Doing it at the start so that span will calc len right
   VALIDATEL(lx->lexBtrack->len > 1 && last(lx->lexBtrack).tp == tokClause,
           lexError(errPunctuationCommaNotClause));

   BtToken top = removeLast(lx->lexBtrack);
   setStmtSpanLength(top.tokenInd, lx);
}

private void //:lexDot
lexDot(SRC, LX) {
// The dot is a start of a field accessor (if glued to prev token) or a function call.
   VALIDATEL(lx->tokens.len > 0, lexError(errUnexpectedToken));
   Bool isCall = lx->i > 0 && (source[lx->i - 1] == aSpace || source[lx->i - 1] == aNewline);
   lx->i++; // CONSUME the dot
   VALIDATEL(lx->i < lx->stats.inpLength, lexError(errPrematureEndOfInput))
   if (isLetter(CURR_BT)) {
      wordInternal((isCall ? tokOperator : tokFieldAcc), source, lx);
   } ei (CURR_BT == aAt || CURR_BT == aSharp) { // `coll.@` or `coll.#` in an "each" loop
      Token prevTok = lx->tokens.c[lx->tokens.len - 1];
      VALIDATEL(prevTok.tp == tokWord, lexError(errUnexpectedToken));

      pushIntokens(prevTok, lx);
      lx->tokens.c[lx->tokens.len - 2] = (Token){
         .tp = tokMisc, .pl1 = CURR_BT == aAt ? miscEachElem : miscEachInd,
         .startBt = lx->i, .lenBts = 2
      };
      lx->i++; // CONSUME the @ or #
   } else {
      throwExcLexer(lexError(errPrematureEndOfInput));
   }
}

private void //:lexColon
lexColon(SRC, LX) {
// The colon marks symbols (struct fields, keyword args in function calls)
   lx->i++;  // CONSUME the ":". Doing it at the start so that span will calc len right
   wordInternal(tokKey, source, lx);
}

private void //:lexSemicolon
lexSemicolon(SRC, LX) {
// The semicolon is the statement ender.
   lx->i++;  // CONSUME the ";". Doing it at the start so that span will calc len right
   if (lx->lexBtrack->len == 0)
      { return; }
   BtToken top = last(lx->lexBtrack);
   VALIDATEL(top.spanLevel != slSubexpr, lexError(errPunctuationOnlyInMultiline));
   if (top.spanLevel == slStmt) {
      closeStatement(lx);
   }
}

private Int //:lConvertToAssignment
lConvertToAssignment(Int const opType, LX) {
   BtToken currSpan = last(lx->lexBtrack);
   VALIDATEL(currSpan.tp == tokStmt, lexError(errOperatorAssignmentPunct));
   Int const assignmentStartInd = currSpan.tokenInd;
   Token* tok = (lx->tokens.c + assignmentStartInd);
   
   tok->tp = tokAssignment;
   lx->lexBtrack->c[lx->lexBtrack->len - 1].tp = tokAssignment;
   if (lx->tokens.c[assignmentStartInd + 1].tp == tokType){
      // type definition
      tok->pl1 = assiTypeDefinition;
   }

   openPunctuation(tokAssignRight, slStmt, lx->i, lx);
   return assignmentStartInd;
}

private void //:lCreateAssignment
lCreateAssignment(Int const opType, LX) {
// Params: opType is the operator for mutations (like `*=`), -1 for normal assignments.
// Handles the "=", and "+=" tokens (for the latter, inserts the operator and duplicates the
// tokens from the left side). Changes existing stmt token into tokAssignment and opens up a new
// tokAssignRight span. Doesn't consume anything
   Int assignmentStartInd = lConvertToAssignment(opType, lx);

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
   while (k < countSignOperators && OPERATORS[k].firstSymbol < firstSymbol) {
      k++;
   }
   while (k < countSignOperators && OPERATORS[k].firstSymbol == firstSymbol) {
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
      if (opByte == sentinel) {
         opType = k;
         break;
      } ei (*opByte != thirdSymbol) {
         k++;
         continue;
      }
      opType = k;
      break;
   }
   VALIDATEL(opType > -1, lexError(errOperatorUnknown))

   OpDef opDef = OPERATORS[opType];
   bool isAssignment = false;

   Int lengthOfOper = opDef.name >> 24;
   Int j = lx->i + lengthOfOper;
   if (opDef.assignable && j < lx->stats.inpLength && source[j] == aEqual) {
      isAssignment = true;
      j++;
   }
   if (isAssignment) { // mutation operators like "*=" or "*.="
      lCreateAssignment(opType, lx);
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
      lCreateAssignment(-1, lx);
      lx->i++; // CONSUME the =
   }
}

private void //:lexUnderscore
lexUnderscore(SRC, LX) {
   if ((lx->i < lx->stats.inpLength - 1) && NEXT_BT == aUnderscore) {
      pushIntokens((Token){ .tp = tokMisc, .pl1 = miscUnderscore, .pl2 = 2,
                .startBt = lx->i - 1, .lenBts = 2 }, lx);
      lx->i += 2; // CONSUME the "__"
      return;
   }
   if (lx->lexBtrack->len > 0) {
      BtToken top = last(lx->lexBtrack);
      if (top.tp == tokType && top.spanLevel == slFnReturn) {
         pushIntokens((Token){ .tp = tokType, .pl1 = voidType, .pl2 = 0,
                   .startBt = lx->i - 1, .lenBts = 1 }, lx);
         lx->i++; // CONSUME the "_"
         return;
      }
   }
   pushIntokens((Token){ .tp = tokMisc, .pl1 = miscUnderscore, .pl2 = 1,
             .startBt = lx->i - 1, .lenBts = 2 }, lx);
   lx->i++; // CONSUME the "_"
}

private void //:lexNewline
lexNewline(SRC, LX) {
   pushInnewlines(lx->i + 1, lx); // +1 because it's the start of next line

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

private void //:lInDeCrement
lInDeCrement(Bool isIncrement, SRC, LX) {
// Converts the `++` and `--` into mutations `+= 1` and `-= 1`
   Int assignmentStartInd = lConvertToAssignment(opPlus, lx);

   // -2 because we've already opened the right side span
   Int const countLeftSide = lx->tokens.len - assignmentStartInd - 2;
   ensureCapacityTokens(countLeftSide + 1, lx); // + 1 for the operator

   memcpy(lx->tokens.c + lx->tokens.len,
         lx->tokens.c + assignmentStartInd + 1, countLeftSide*sizeof(Token));
   lx->tokens.len += countLeftSide;
   pushIntokens(((Token){
         .tp = tokOperator, .pl1 = isIncrement ? opPlus : opMinus, .pl2 = 0,
         .startBt = lx->i, .lenBts = 1
      }), lx
   );
   pushIntokens((Token){ .tp = tokInt, .pl1 = 0, .pl2 = 1, .startBt = lx->i + 1, .lenBts = 1}, lx);

   lx->i += 2; // CONSUME the `++` or `--`
}

private void //:lexArrow
lexArrow(SRC, LX) {
   LBtToken* bt = lx->lexBtrack;
   VALIDATEL(bt->len > 0, lexError(errFnTypeArrows))
   BtToken top = last(bt);
   if (top.spanLevel == slFnTp) { // `F[G -> H]`
      add(((BtToken){ .tp = tokType, .tokenInd = lx->tokens.len, .spanLevel = slFnReturn}),
         lx->lexBtrack);
   } ei (top.spanLevel == slFnReturn) {
      throwExcLexer(lexError(errFnTypeArrows));
   } ei (top.tp == tokFn) { // `f{ -> ... }`
      goto consumeArrow;
   } ei (top.tp == tokFor) {
      lx->tokens.c[top.tokenInd + 1].pl2 = lx->tokens.len; // write loop body start ind to tokMisc
   } ei ((top.tp == tokStmt && bt->len > 1 && bt->c[bt->len - 2].tp == tokEach)) {
      setSpanLengthLexer(top.tokenInd, lx);
      removeLast(bt);
      lx->tokens.c[last(bt).tokenInd + 1].pl2 = lx->tokens.len; // write loop body start ind
   } else { // `f{ a -> ...}`
      VALIDATEL(top.tp == tokStmt && lx->lexBtrack->len > 1
            && lx->lexBtrack->c[lx->lexBtrack->len - 2].tp == tokFn, lexError(errArrowOutOfPlace)
      );
      Token prevTok = lx->tokens.c[lx->tokens.len - 1];
      Int endBt = prevTok.startBt + prevTok.lenBts;

      lx->tokens.c[top.tokenInd].lenBts = endBt - lx->tokens.c[top.tokenInd].startBt;
      lx->tokens.c[top.tokenInd].pl2 = lx->tokens.len - top.tokenInd - 1;
      removeLast(bt);
   }
consumeArrow:
   lx->i += 2; // CONSUME the `->`
}

private void //:lexPlus
lexPlus(SRC, LX) {
// Handles the binary operator and the increment
   VALIDATEL(lx->i < lx->stats.inpLength - 1, lexError(errPrematureEndOfInput))
   Byte nextBt = NEXT_BT;
   if (nextBt == aPlus) {
      lInDeCrement(true, source, lx);
   } else {
      lexOperator(source, lx);
   }
}

private void //:lexMinus
lexMinus(SRC, LX) {
// Handles the binary operator, the unary negation operator, decrement and the arrow
   VALIDATEL(lx->i < lx->stats.inpLength - 1, lexError(errPrematureEndOfInput))
   Byte nextBt = NEXT_BT;
   if (isDigit(nextBt)) {
      wrapInAStatement(lx->i, source, lx);
      decNumber(true, source, lx);
      lx->numeric.len = 0;
   } ei (nextBt == aSpace) {
      pushIntokens((Token){ .tp = tokOperator, .pl1 = opMinus, .pl2 = OPERATORS[opMinus].prec,
                            .startBt = lx->i, .lenBts = 1 }, lx);
      lx->i += 2; // CONSUME the "-" and the space
   } ei (nextBt == aColon || nextBt == aEqual) {
      lexOperator(source, lx);
   } ei (nextBt == aMinus) {
      lInDeCrement(false, source, lx);
   } ei (nextBt == aGT)  {
      lexArrow(source, lx);
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
   VALIDATEL(j < lx->stats.inpLength, lexError(errPunctuationExtraOpening))
   wrapInAStatement(lx->i, source, lx);
   openPunctuation(tokParens, slSubexpr, lx->i, lx);
   lx->i++; // CONSUME the left parenthesis
}

private Bool //:lexIsFnType
lexIsFnType(BtToken bt, LX) {
   return lx->tokens.c[bt.tokenInd].pl1 == nameOfStd(strF);
}

private void //:lexParenRight
lexParenRight(SRC, LX) {
// A closing parenthesis may close the following configurations of lexer backtrack:
   LBtToken* bt = lx->lexBtrack;
   VALIDATEL(bt->len > 0, lexError(errPunctuationExtraClosing))
   BtToken top = removeLast(bt);

   VALIDATEL(top.spanLevel == slSubexpr, lexError(errPunctuationUnmatched))

   mbCloseAssignRight(&top, lx);
   setSpanLengthLexer(top.tokenInd, lx);
   lx->i++; // CONSUME the closing ")"
}

private void //:preambleFor
preambleFor(
   Int forStart, Int bodyInd, Int sentinel, OUT Int* condInd, OUT Int* stepInd, TOKENS, LX
) {
// Analyzes a "for" loop and finds its key tokens: the loop condition, the stepper and body.
// Consumes no tokens
// A "for" syntax form is quadripartite:
// 1) var inits (they must all be assignments),
// 2) the condition (must be an expression),
// 3) statements for stepping to the next iteration (must be expressions, assignments or asserts),
// 4) loop body (arbitrary syntax forms).
// Precondition: looking at the tokScope right after tokFor.
// Postcond: at least 1 of "condInd" & "stepInd" is guaranteed to be found (=> positive)
   Int j = forStart + 2; // skipped tokFor and tokMisc

   for (Token currTok = tokens[j];
        (currTok.tp == tokAssignment || currTok.tp == tokAssignRight);
        currTok = tokens[j]) {
      j = calcSentinel(currTok, j);
      VALIDATEL(j < sentinel, lexError(errLoopEmptyStepBody))
   }
   VALIDATEL(j < sentinel && (bodyInd == 0 || j < bodyInd), lexError(errLoopNoCondition))

   Token condTok = tokens[j];
   VALIDATEL((condTok.tp == tokStmt && condTok.pl2 > 0) || condTok.tp == tokBool,
             lexError(errLoopNoCondition)
   )
   *condInd = j;

   j = calcSentinel(condTok, j); // skipping the cond
   VALIDATEL(j < sentinel, lexError(errLoopEmptyStepBody));
   *stepInd = j;
   Int const stepSentinel = bodyInd > 0 ? bodyInd : sentinel;
   for (Token currTok = tokens[j]; j < stepSentinel; currTok = tokens[j]) {
      VALIDATEL(currTok.tp == tokStmt || currTok.tp == tokAssignment || currTok.tp == tokAssert,
                lexError(errLoopWrongFormInStepper)
      )
      j = calcSentinel(currTok, j);
   }
}

private void //:reorderFor
reorderFor(Int forStart, Int sentinel, TOKENS, LX) {
// Reorders tokens in a "for" loop. Preconditions: we are looking at (tokFor + 2),
// one or both of stepIndInitial, bodyInd is positive.
// BEFORE: tokFor tokMisc (inits) cond   steps body
// AFTER:  tokFor (inits)    cond body tokMisc steps
   Int condInd, stepInd ;
   Int bodyInd = tokens[forStart + 1].pl2; // getting it from tokMisc

   preambleFor(forStart, bodyInd, sentinel, OUT &condInd, OUT &stepInd, tokens, lx);

   VALIDATEL(stepInd + bodyInd > 0, lexError(errLoopEmptyStepBody))

   Int totalLen = sentinel - forStart; // +1 for the tokMisc which is before the scope

   LToken* const buf = lx->reorderBuf;

   ensureCapacityTokenBuf(totalLen, buf, lx);
   Int sndInd = minPositiveOf(2, stepInd, bodyInd);

   Int const piece1Start = forStart + 2; // piece1 = assignments (if any) and condition
   Int const piece1Len = sndInd - piece1Start;
   Int const stepStart = stepInd;
   Int const stepLen = stepInd > 0 ? (minPositiveOf(2, bodyInd, sentinel) - stepInd) : 0;
   Int const stepStartBt = stepInd > 0 ? tokens[stepInd].startBt : 0;
   Int const bodyStart = bodyInd;
   Int const bodyLen = bodyInd > 0 ? sentinel - bodyInd : 0;

   memcpy(buf->c, tokens + piece1Start, piece1Len*sizeof(Token));
   if (bodyLen > 0)
      { memcpy(buf->c + piece1Len, tokens + bodyStart, bodyLen*sizeof(Token)); }
   buf->c[piece1Len + bodyLen] = (Token){.tp = tokMisc, .pl1 = miscLoopStep, .startBt = stepStartBt};
   if (stepLen > 0)
      { memcpy(buf->c + piece1Len + bodyLen + 1, tokens + stepStart, stepLen*sizeof(Token)); }
   memcpy(tokens + forStart + 1, buf->c, totalLen*sizeof(Token)); // -1 because into tokMisc

   condInd--;
   bodyInd = forStart  + piece1Len;
}

private void //:lexFn
lexFn(SRC, LX) {
   if (lx->lexBtrack->len > 0) {
      BtToken top = last(lx->lexBtrack);
      VALIDATEL(top.spanLevel == slStmt, lexError(errPunctuationFnNotInStmt))
   }

   openPunctuation(tokFn, slScope, lx->i, lx);
   lx->i += 2; // CONSUME the "{{"
}

private void
lexCurlyLeft(SRC, LX) { //:lexCurlyLeft
// Handles scope openings and decorative braces in "if" and "for" forms
   if (lx->lexBtrack->len == 0)
      { goto punctuation; }
   BtToken const top = last(lx->lexBtrack);
   if (top.spanLevel == slStmt) {
      // process the first curly brace in an "if ... {" form. If all is right,
      // updates its span level to slScope, so further curly braces work as usual
      Int const len = lx->lexBtrack->len;
      VALIDATEL(len > 1 && lx->lexBtrack->c[len - 2].spanLevel == slUnbraced,
              lexError(errPunctuationScope)
      )
      removeLast(lx->lexBtrack); // pop the top statement (if cond) because it's over
      setStmtSpanLength(top.tokenInd, lx);
      BtToken const second = last(lx->lexBtrack);
      lx->lexBtrack->c[len - 2].spanLevel = slScope;
      lx->tokens.c[second.tokenInd].pl1 = slScope;
      goto consumption;
   } ei (top.tp == tokElse) {
      goto consumption;
   }
   punctuation:
   openPunctuation(tokScope, slScope, lx->i, lx);
   consumption:
   lx->i++; // CONSUME the "{"
}

private void //:lexCurlyRight
lexCurlyRight(SRC, LX) {
   LBtToken* bt = lx->lexBtrack;
   VALIDATEL(bt->len > 0, lexError(errPunctuationExtraClosing))
   BtToken top = removeLast(bt);
   VALIDATEL(top.spanLevel == slScope, lexError(errPunctuationUnmatched))

   if (top.tp == tokEach) { // marker token to trigger {pLoopStepMarker}
      pushIntokens((Token){.tp = tokMisc, .pl1 = miscLoopStep,
         .startBt = lx->i, .lenBts = 0 }, lx);
   }

   setSpanLengthLexer(top.tokenInd, lx);

   if (top.tp == tokFor) {
      reorderFor(top.tokenInd, lx->tokens.len, lx->tokens.c, lx);
   } ei (bt->len > 0 && top.tp == tokFn && last(bt).tp == tokToplevelFn) {
      // close the function (maybe toplevel) if we are in one
      top = removeLast(bt);
      setSpanLengthLexer(top.tokenInd, lx);
   }

   lx->i++; // CONSUME the "}"
}

private Bool //:lMaybeOpenFnType
lMaybeOpenFnType(SRC, LX) {
// In a toplevel signature, an opening bracket implies the start of a function type
   if (lx->lexBtrack->len == 0 || last(lx->lexBtrack).spanLevel != slToplevel)
      { return false; }

   add(((BtToken){ .tp = tokType, .tokenInd = lx->tokens.len, .spanLevel = slFnTp}), lx->lexBtrack);
   pushIntokens((Token){ .tp = tokType, .pl1 = nameOfStd(strF), .startBt = lx->i }, lx);
   return true;
}

private void //:lexBracketLeft
lexBracketLeft(SRC, LX) {
   if (lMaybeOpenFnType(source, lx)) {
   } ei (lx->lexBtrack->len > 0 && last(lx->lexBtrack).tp == tokType) {
      add(((BtToken){ .tp = tokType, .tokenInd = lx->tokens.len, .spanLevel = slSubexpr}),
            lx->lexBtrack);
      pushIntokens((Token) {.tp = tokType, .pl1 = -1,
                       .startBt = lx->i }, lx);
   } else {
      wrapInAStatement(lx->i, source, lx);
      openPunctuation(tokData, slSubexpr, lx->i, lx);
   }
   lx->i++; // CONSUME the `[`
}

private Bool //:lMaybeCloseFnType
lMaybeCloseFnType(SRC, LX) {
// Close an fn type properly if we are in one
   LBtToken* bt = lx->lexBtrack;
   Int const len = bt->len;
   if (len == 0)
      { return false; }

   BtToken top = last(bt);
   if (top.spanLevel == slFnReturn) { // `F[A -> B]`
      VALIDATEL(len > 1 && bt->c[len - 2].spanLevel == slFnTp, lexError(errFnTypeArrows)
      )
      BtToken returnPart = removeLast(bt);
      Int returnSentinel = calcSentinel(lx->tokens.c[returnPart.tokenInd], returnPart.tokenInd);

      // a return type should be empty or a single type
      VALIDATEL(returnPart.tokenInd == lx->tokens.len || returnSentinel == lx->tokens.len,
         lexError(errFnSignature)
      );
      if (returnPart.tokenInd == lx->tokens.len) { // empty return type - gotta insert "void"
         pushIntokens(((Token){
            .tp = tokType, .pl1 = nameOfStd(strVoid), .pl2 = 0, .startBt = lx->i, .lenBts = 0}), lx
         );
      }
   } ei (top.spanLevel == slFnTp) { // `F[]` Push a void token for its return type
      VALIDATEL(len > 1 && bt->c[len - 1].tokenInd == lx->tokens.len - 1, lexError(errFnTypeArrows)
      );
      pushIntokens(((Token){
         .tp = tokType, .pl1 = nameOfStd(strVoid), .pl2 = 0, .startBt = lx->i, .lenBts = 0}), lx
      );
   } else {
      return false;
   }

   BtToken fnType = removeLast(bt);
   setSpanLengthLexer(fnType.tokenInd, lx);
   return true;
}

private void //:lexBracketRight
lexBracketRight(SRC, LX) {
   LBtToken* const bt = lx->lexBtrack;
   VALIDATEL(bt->len > 0, lexError(errPunctuationExtraClosing))
   if (lMaybeCloseFnType(source, lx))
      { goto consumption; }

   BtToken top = removeLast(bt);
   VALIDATEL(
      top.tp == tokData || top.tp == tokAccessorIn || top.tp == tokType,
      lexError(errPunctuationUnmatched)
   )
   setSpanLengthLexer(top.tokenInd, lx);
   if (lx->i + 1 < lx->stats.inpLength && NEXT_BT == aBracketLeft) { // `a[i][j]`
      openPunctuation(tokAccessorIn, slSubexpr, lx->i + 1, lx);
      lx->i++; // CONSUME the `]` so the `[` will be consumed in this fn
   } ei (bt->len > 0 && last(bt).tp == tokAccessor) {
      top = removeLast(bt);
      setSpanLengthLexer(top.tokenInd, lx);
   }
   consumption:
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
   VALIDATEL(j != lx->stats.inpLength, lexError(errPrematureEndOfInput))
   pushIntokens((Token){.tp = tokString, .startBt = (lx->i), .lenBts = (j - lx->i + 1)}, lx);
   lx->i = j + 1; // CONSUME the string literal, including the closing quote character
}

private void //:lexUnexpectedSymbol
lexUnexpectedSymbol(SRC, LX) {
   printLexer(lx);
   throwExcLexer(lexError(errUnrecognizedByte));
}

private void //:lexNonAsciierr
lexNonAsciierr(SRC, LX) {
   throwExcLexer(lexError(errNonAscii));
}

private void //:tabulateLexer
tabulateLexer() {
   LexerFn* p = LEX_TABLE;
   for (Int i = 0; i < 128; i++) {
      p[i] = &lexUnexpectedSymbol;
   }
   for (Int i = 128; i < 256; i++) {
      p[i] = &lexNonAsciierr;
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
   p[aAt] = &lexAt;
   p[aColon] = &lexColon;
   p[aSemicolon] = &lexSemicolon;
   p[aEqual] = &lexEqual;
   p[aUnderscore] = &lexUnderscore;

   for (Int i = sizeof(operatorStartSymbols)/4 - 1; i > -1; i--) {
      p[operatorStartSymbols[i]] = &lexOperator;
   }
   p[aPlus] = &lexPlus; // to handle ++
   p[aMinus] = &lexMinus; // to handle --, -> and literal negation
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
//{{{ Parser utils

#define VALIDATEP(cond, error) if (!(cond)) { throwExcParser0(error, __LINE__, cm); }

private TypeId exprUpTo(Int sentinelToken, ChInterval loc, TOKENS, CM);
private void eClose(Expr* s, CM);
private void addBinding(NameId nameId, Int bindingId, Compiler* cm);
private void closeParseFrames(CM);
private void createBuiltins(Compiler* cm);
internal Compiler* createLexer(String sourceCode, Bool prependStandard, Arena* a);
private void eParse(Int sentinel, TOKENS, CM);
private TypeId exprHeadless(Int sentinel, ChInterval loc, TOKENS, CM);
private TypeId pExprWorker(Token tk, Int sentinel, TOKENS, CM);

#define TYPE_CREATE_START(typeHeader) TypeId const tentativeType = typeOf(cm->types.len);\
       pushIntypes(0, cm);\
       typeAddHeader(typeHeader, cm)

#define TYPE_CREATE_END cm->types.c[tentativeType.v] = cm->types.len - tentativeType.v - 1

//{{{ Parse error utils

#define pError0(errId) pError0_0(errId, cm)

private CompileError //:pError0
pError0_0(Int errId, CM) {
// An error where there is only a positional part, and it's built using current parser position
   Int indSpan = -1;
   for (Int j = cm->parseFrames->len - 1; j > -1; j--) {
      ParseFrame fr = cm->parseFrames->c[j];
      if (cm->ast.c[fr.startNodeInd].tp >= nodScope) {
         indSpan = fr.startNodeInd;
         break;
      }
   }

   Int startBt;
   if (indSpan > -1) {
      ParseFrame fr = cm->parseFrames->c[indSpan];
      startBt = cm->tokens.c[fr.startTokenInd].startBt;
   } else {
      Int startToken = MAX(cm->tokens.len - 10, 0);
      startBt = cm->tokens.c[startToken].startBt;
   }
   Int endBt = cm->tokens.c[cm->i].startBt + cm->tokens.c[cm->i].lenBts;

   return (CompileError){
      .id = errId,
      .positional = (ErrorPosition){.count = 2, .indices = {startBt, endBt}},
      .textual = (ErrorText){.count = 0}
   };
}

#define pError(errId, errorText) pError_0(errId, errorText, cm)

private CompileError //:pError
pError_0(Int errId, ErrorText errorText, CM) {
// A full parser error, and it's built using current parser position
   CompileError err = pError0_0(errId, cm);
   err.textual = errorText;
   return err;
}

private ErrorText //:typeErr
typeErr(TypeId t) {
   return (ErrorText){
      .count = 1,
      .c = {(ErrTextSumType){.tp = errtxtType, .c = t.v } }
   };
}

private ErrorText //:typeErr2
typeErr2(TypeId t1, TypeId t2) {
   return (ErrorText){
      .count = 2,
      .c = {(ErrTextSumType){.tp = errtxtType, .c = t1.v },
            (ErrTextSumType){.tp = errtxtType, .c = t2.v }
      }
   };
}

private ErrorText //:nameErr
nameErr(Int name) {
   return (ErrorText){
      .count = 1,
      .c = {(ErrTextSumType){.tp = errtxtName, .c = name } }
   };
}

private ErrorText //:nameErr2
nameErr2(Int name, Int name2) {
   return (ErrorText){
      .count = 2,
      .c = {
         (ErrTextSumType){.tp = errtxtName, .c = name },
         (ErrTextSumType){.tp = errtxtName, .c = name2 }
      }
   };
}

private ErrorText //:nameAndTypeErr
nameAndTypeErr(Int name, TypeId t) {
   return (ErrorText){
      .count = 2,
      .c = {
         (ErrTextSumType){.tp = errtxtName, .c = name },
         (ErrTextSumType){.tp = errtxtType, .c = t.v }
      }
   };
}

private ErrorText //:nameNumberTypeErr
nameNumberTypeErr(Int name, Int n, TypeId t) {
   return (ErrorText){
      .count = 3,
      .c = {
         (ErrTextSumType){.tp = errtxtName, .c = name },
         (ErrTextSumType){.tp = errtxtNumber, .c = n },
         (ErrTextSumType){.tp = errtxtType, .c = t.v }
      }
   };
}

private ErrorText //:numberErr
numberErr(Int n) {
   return (ErrorText){
      .count = 1,
      .c = {(ErrTextSumType){.tp = errtxtNumber, .c = n } }
   };
}

private ErrorText //:numberErr2
numberErr2(Int n, Int n2) {
   return (ErrorText){
      .count = 1,
      .c = {(ErrTextSumType){.tp = errtxtNumber, .c = n },
            (ErrTextSumType){.tp = errtxtNumber, .c = n2 }
      }
   };
}

private ErrorText //:operatorErr
operatorErr(Int operId) {
   return (ErrorText){
      .count = 1,
      .c = {(ErrTextSumType){.tp = errtxtOper, .c = operId } }
   };
}

[[noreturn]] private void
throwExcParser0(CompileError error, Int lineNumber, CM) {

#ifdef DEBUG
   error.codeLine = lineNumber;
#endif
   add(error, cm->errors);
   longjmp(excBuf, 1);
}

#define throwExcParser(errId) throwExcParser0(errId, __LINE__, cm)

//}}}

private ChInterval //:interOf
interOf(Token tk) {
   return (ChInterval) {.startBt = tk.startBt, .lenBts = tk.lenBts};
}

private SourceLoc //:locOf
locOf(ChInterval chi, CM) {
// Line numbers and chars within line are all 0-based. Also, standard text is not included
   Int startLine = binaryIntervalSearch(chi.startBt, cm->newlines.len, cm->newlines.c) - 1;
   Int startChar = chi.startBt - cm->newlines.c[startLine + 1];
   Int endLine =
      binaryIntervalSearch(chi.startBt + chi.lenBts, cm->newlines.len, cm->newlines.c) - 1;
   Int endChar = chi.startBt + chi.lenBts - cm->newlines.c[endLine + 1];

   return (SourceLoc){
      .startLine = startLine, .startChar = startChar, .endLine = endLine, .endChar = endChar
   };
}

private Node //:createNodVarForName
createNodVarForName(NameId name, CM) {
// Resolves an active binding, throws if it's not active
   Int rawValue = cm->activeBindings[name];

   VALIDATEP(rawValue > -1 && rawValue < BIG, pError(errUnknownBinding, nameErr(name)))
   Var v = cm->vars.c[rawValue];
   if (v.fnId == -1) {
      return (Node){ .tp = nodVar, .pl1 = rawValue, .pl2 = 0, .pl3 = 0 };
   } else {
      return (Node){ .tp = nodVar, .pl1 = rawValue, .pl2 = v.fnId, .pl3 = assiFnVarUse };
   }
}

private VarId //:createVar
createVar(NameId name, Byte access, FunctionId fnId, CM) {
// Creates a Var (without validating that the name is free) & adds it to the current scope
// "fnId" should be -1 for ordinary (non-function) local vars
// Consumes no tokens
   VarId newVarId = cm->vars.len;
   pushInvars(((Var){ .name = name, .access = access, .fnId = fnId }), cm);
   if (name > -1) // nameId == -1 for built-in operators or nameless locals
      { addBinding(name, newVarId, cm); }
   return newVarId;
}

private VarId //:createVarWithType
createVarWithType(NameId name, TypeId typeId, Byte access, FunctionId fnId, CM) {
// Validates a new binding (that it is unique), creates a Var for it & adds it to the current scope
// "fnId" should be -1 for ordinary (non-function) local vars
// Consumes no tokens
   Int mbBinding = cm->activeBindings[name];
   VALIDATEP(mbBinding == -1, pError(errAssignmentShadowing, nameErr(name)))
   VarId newVarId = createVar(name, access, fnId, cm);
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
newNode(Node node, ChInterval chi, CM) {
   pushInast(node, cm);
   add(locOf(chi, cm), cm->sourceLocs);
}

private void //:eOperatorCall
eOperatorCall(Token tok, Int precedence, Bool isVarCall, CM) {
// Pushes a call to the temporary lists during expression parsing
   Expr* e = &(cm->expr);
   VALIDATEP(e->frames->len > 0, pError0(errExpressionError))
   ExprFrame frame = last(e->frames);

   if (frame.tp == exfrParen) { // for infix operators
      VALIDATEP(frame.argCount == 1, pError0(errExpressionWrongArgCount))
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
         .argCount = 1, .chi = interOf(tok), .isVarCall = isVarCall
      }),
      e->frames
   );
}

private void //:eSaveDataLiteralNodes
eSaveDataLiteralNodes(Int startInd, LNode scr, LChInterval locsScr, CM) {
// Pushes the tail of scratch space (from a specified index onward) into the main AST
   Int const pushCount = scr.len - startInd;
   if (pushCount == 0)
      { return; }

   if (cm->ast.len + pushCount + 1 < cm->ast.cap) {
      memcpy((Node*)(cm->ast.c) + (cm->ast.len), scr.c + startInd,
             pushCount*sizeof(Node));
      memcpy((SourceLoc*)(cm->sourceLocs->c) + (cm->sourceLocs->len),
             locsScr.c + startInd,
             pushCount*sizeof(SourceLoc));
   } else {
      Int const newCap = 2*(cm->ast.cap) + pushCount;
      Arr(Node) newContent = allocateArray(newCap, Node, cm->a);
      memcpy(newContent, cm->ast.c + startInd, cm->ast.len*sizeof(Node));
      memcpy((Node*)(newContent) + (cm->ast.len),
            scr.c + startInd,
            pushCount*sizeof(Node));
      cm->ast.cap = newCap;
      cm->ast.c = newContent;

      Arr(SourceLoc) newLocs = allocateArray(newCap, SourceLoc, cm->a);
      memcpy(newLocs, cm->sourceLocs->c + startInd, pushCount*sizeof(SourceLoc));
      memcpy((SourceLoc*)(newLocs) + (cm->sourceLocs->len), locsScr.c + startInd,
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
// Remove one binding from Scopes
   if (s->curr == s->currChunk->c) {
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
   scopesMoveBackward(s, cm);
   for (; s->curr != s->start; scopesMoveBackward(s, cm)) {
      cm->activeBindings[*(s->curr)] = -1;
   }

   // rewind start
   Int const lenPrev = *(s->start);
   s->currScopeLen = lenPrev;


   s->countScopes--;
   ScopeChunk* backChunk = s->currChunk;
   for (Int j = -1; j < lenPrev; j++, s->start--) {
      if (s->start == backChunk->c) {
         if (backChunk->prev) {
            backChunk = backChunk->prev;
            s->start = backChunk->c + SCOPE_CHUNK_SZ;
         }
      }
   }
}

void //:scopesNewLexicalScope
scopesNewLexicalScope(CM) {
   Scopes* const s = &(cm->scopes);
   *(s->curr) = s->currScopeLen; // length of the old scope
   s->start = s->curr;
   scopesMoveForward(s, cm);
   s->currScopeLen = 0;
   s->countScopes++;
}

internal void //:updateStats
updateStats(Compiler* restrict cm) {
   cm->stats.toksLen = cm->tokens.len;
   cm->stats.astLen = cm->ast.len;
   cm->stats.typesLen = cm->types.len;
}

private Int
getBinding(Int id, CM) { return cm->activeBindings[id]; }


//}}}
//{{{ Forward decls

private TypeId pTypeDef(Int sentinel, TOKENS, CM);
private Bool tIsList(TypeId t, CM);
private TypeId typeGetTypeByName(Int t, CM);

internal Int getOper(Int opName, Int operandType, Compiler* cm);

#ifdef DEBUG
void printIntArrayOff(Int startInd, Int count, Arr(Int) arr);
#endif

//}}}

private void
openParsedScopeWorker(ParseFrame fr, Node nd, ChInterval chi, CM) {
// Performs coordinated insertions to start a scope within the parser
   add(fr, cm->parseFrames);
   scopesNewLexicalScope(cm);
   newNode(nd, chi, cm);
}

private void //:openParsedScope
openParsedScope(Int sentinelToken, Node nd, ChInterval chi, CM) {
// Performs coordinated insertions to start a scope within the parser
   openParsedScopeWorker(((ParseFrame){
         .level = nd.tp == nodFor ? pfrLoop : pfrScope,
         .startNodeInd = cm->ast.len, .sentinel = sentinelToken, .typeId = 0
      }),
      nd, chi, cm
   );
}

private void //:openFnScope
openFnScope(Int funcOrMonoId, TypeId fnType, Token tk, Int sentinel, CM) {
// Performs coordinated insertions to start a function definition
   add(((ParseFrame){
      .level = pfrFn, .startNodeInd = cm->ast.len, .sentinel = sentinel,
      .typeId = fnType }), cm->parseFrames);
   scopesNewLexicalScope(cm); // a function body is also a lexical scope
   newNode((Node){ .tp = nodToplevelFn, .pl1 = funcOrMonoId, .pl3 = callNormal}, interOf(tk), cm);
}

private void //:pScope
pScope(Token tok, Int sentinel, TOKENS, CM) {
   openParsedScope(sentinel, (Node){.tp = nodScope}, interOf(tok), cm);
}

private void //:parseTry
parseTry(Token tok, TOKENS, CM) {
   throwExcParser(pError0(errTemp));
}

private void //:ifOpenSpan
ifOpenSpan(Unt tp, Int sentinel, Int ifcl, ChInterval chi, CM) {
   add(((ParseFrame){
      .level = pfrScope, .startNodeInd = cm->ast.len, .sentinel = sentinel }), cm->parseFrames
   );
   scopesNewLexicalScope(cm);
   newNode((Node){ .tp = tp, .pl3 = ifcl }, chi, cm);
}

private Int //:pIfDetermineSentinel
pIfDetermineSentinel(Int ifSentinel, TOKENS, CM) {
    Int j = ifSentinel;
    Int const toksLen = cm->stats.toksLen;
    for (; j < toksLen && tokens[j].tp == tokElseIf; j += (tokens[j].pl2 + 1)) {}
    if (j < toksLen && tokens[j].tp == tokElse)    { j += (tokens[j].pl2 + 1); }
    return j;
}

private void //:pElse
pElse(Token tok, Int sentinel, TOKENS, CM) {
// "Else" is a special case of "ElseIf" marked with .pl3 = 0
   closeParseFrames(cm);
   ifOpenSpan(nodIfClause, sentinel, ifclElse, interOf(tok), cm);
}

private void //:pIfClause
pIfClause(Token tok, Int ifcl, TOKENS, CM) {
   Int const clauseSentinel = cm->i + tok.pl2;
   ifOpenSpan(nodIfClause, clauseSentinel, ifcl, interOf(tok), cm);

   // The condition
   Token stmtTok = tokens[cm->i];
   cm->i++; // CONSUME the stmt token
   TypeId typeLeft = pExprWorker(stmtTok, cm->i + stmtTok.pl2, tokens, cm);
   VALIDATEP(eq(typeLeft, boolTy), pError(errTypeMustBeBool, typeErr(typeLeft)))
   closeParseFrames(cm);
}

private void //:pElseIf
pElseIf(Token tok, Int sentinel, TOKENS, CM) {
   pIfClause(tok, ifclElseIf, tokens, cm);
}

private void //:pIf
pIf(Token tok, Int sentinel, TOKENS, CM) {
// Parses and "if" expression with any following else-if clauses and an ending else clause.
   Int const firstClauseSentinel = calcSentinel(tok, cm->i - 1);
   Int const ifSentinel = pIfDetermineSentinel(firstClauseSentinel, tokens, cm);

   ifOpenSpan(nodIf, ifSentinel, 0, interOf(tok), cm);
   pIfClause(tok, ifclIf, tokens, cm);
}

private void //:pAssignmentFnVar
pAssignmentFnVar(Assignment assignment, Token leftNameTk, TypeId leftType, CM) {
// Resolution of an overloaded function into a local var.
// Validates that the right side consists of one word
   VALIDATEP(assignment.rightTokenInd + 2 == assignment.sentinel,
      pError0(errAssignmentToFunctionVar)
   )
   Token rightTk = cm->tokens.c[assignment.rightTokenInd + 1];
   NameId fnName = rightTk.pl1;

   FunctionId fnId = findOverload(
      fnName, libeyr_typeGetGenericArg(leftType, typeReadHeader(leftType, cm), 0, cm->types.c), cm
   );
   TypeId fnType = cm->functions.c[fnId].typeId;
   VALIDATEP(eq(leftType, fnType), pError(errTypeMismatch, typeErr2(leftType, fnType)));

   NameId varName = leftNameTk.pl1;
   Int pl3;
   Int varId = createVarWithType(
      varName, fnType,
      (leftNameTk.pl2 == 1 ? accessPrivMut : accessPrivImm), fnId, cm
   );
   pl3 = assiFnVarDef;
   newNode((Node){ .tp = nodVar, .pl1 = varId, .pl2 = fnId, .pl3 = assiFnVarDef },
      interOf(leftNameTk), cm
   );
}

private void //:pAssignmentValidateLeftAccessors
pAssignmentValidateLeftAccessors(Int start, Int sentinel, TOKENS, CM) {
// A complex left side must 1) have a variable as first node 2) have a field/array access as last
// node 3) the first node must be consumed only by field/array accesses
// OK: `x.a.b[15] = ...`, `x[15].a.b = ...`
// NOT OK: `(foo x)[15] = ...`, `(foo x).a.b = ...`
   Node lastNode = cm->ast.c[sentinel - 1];

   VALIDATEP(cm->ast.c[start].tp == nodVar, pError0(errAssignmentLeftSide))
   VALIDATEP(
      lastNode.tp == nodCall && (lastNode.pl3 == callField || lastNode.pl3 == callGetElem),
      pError0(errAssignmentLeftSide)
   )
   Int stackLen = 1; // for the leftmost nodVar which is the l-value of the expression
   for (Int j = start + 1; j < sentinel; j++) {
      Node nd = cm->ast.c[j];
      if (nd.tp == nodCall) {
         // only array and field accesses may affect our l-value
         VALIDATEP(
            nd.pl3 == callField || nd.pl3 == callGetElem || nd.pl2 < stackLen,
            pError0(errAssignmentLeftSide)
         )
         switch (nd.pl3) {
         case callField: break;
         case callGetElem:  {
            stackLen--; break;
         }
         default: {
            stackLen -= (nd.pl2 - 1); break;
         }
         }
      } else {
         stackLen++;
      }
   }
}

private TypeId //:pAssignmentLeftComplexExpr
pAssignmentLeftComplexExpr(Token firstTok, Int sentinel, TOKENS, CM) {
// Complex left side in an assignment like `a[i][j] = ...` or `a.b = ...`.
// It gets transformed like this:
// arr[i][j*2][k + 3] ==> arr i .getElem j 2 *(2) .getElem k 3 +(2) .getElem
   LInt* sc = cm->expr.exp;
   sc->len = 0;
   Int const startBt = firstTok.startBt;
   Int const lastBt = tokens[cm->i - 1].startBt + tokens[cm->i - 1].lenBts;
   Token locTk = (Token){.startBt = startBt, .lenBts = lastBt - startBt};
   Int start = cm->ast.len + 1;

   VALIDATEP(tokens[cm->i + 1].tp == tokWord, pError0(errAssignmentLeftSide))
   for (Int j = cm->i + 2; j < sentinel; ){
      Token accessorTk = tokens[j];
      VALIDATEP(accessorTk.tp == tokAccessorIn, pError0(errAssignmentLeftSide))
      j = calcSentinel(accessorTk, j);
      add(j, sc);
   }

   TypeId leftType = exprUpToWithFrame((ParseFrame){
      .level = 0, .startNodeInd = cm->ast.len, .sentinel = sentinel }, interOf(locTk), tokens, cm
   );

   pAssignmentValidateLeftAccessors(start, cm->ast.len, tokens, cm);
   return leftType;
}

private TypeId //:pAssignmentLeftWithType
pAssignmentLeftWithType(Token firstTok, Assignment assignment, Int sentinel, OUT Bool* isAFnVar,
      TOKENS, CM) {
// Typechecks a complex left side like `x (Foo Int) = ...` in an assignment, consumes tokens,
// inserts nodes. Returns the type of the left side.
// Precondition: we are looking right past tokAssignment
   LInt* sc = cm->expr.exp;
   sc->len = 0;

   cm->i++; // CONSUME the var name
   Token nextTk = tokens[cm->i]; // +1 is safe because we know left side is long
   // when the left side is a var definition with its type declared

   Bool isGeneric;
   TypeId leftType = tParse(sentinel, OUT &isGeneric, tokens, cm);
   VALIDATEP(!isGeneric, pError(errTypePolymorphicAssignment, typeErr(leftType)))

   if (nextTk.pl1 == nameOfStd(strF)) {
      pAssignmentFnVar(assignment, firstTok, leftType, cm);
      *isAFnVar = true;
      cm->i = assignment.sentinel; // CONSUME the whole assignment
   } else {
      VarId varId = createVarWithType(
         assignment.name, leftType, (firstTok.pl2 == 1 ? accessPrivMut : accessPrivMut), -1, cm
      );
      newNode((Node){.tp = nodVar, .pl1 = varId, .pl2 = 0, .pl3 = assiVarAssignment},
            interOf(firstTok), cm
      );
   }
   return leftType;
}

private TypeId //:pAssignmentRight
pAssignmentRight(TypeId leftType, Token rightTk, Int sentinel, TOKENS, CM) {
// The right side of an assignment
   if (rightTk.tp == tokFn) {
      return ZERO_ARITY_TYPE;
   } else {
      TypeId rightType = exprUpToWithFrame((ParseFrame){
        .level = 0, .startNodeInd = cm->ast.len, .sentinel = sentinel }, interOf(rightTk),
        tokens, cm
      );
      return rightType;
   }
}

private void //:pAssignmentWorker
pAssignmentWorker(Token tok, Assignment assignment, TOKENS, CM) {
// Main assignment parsing function
   Unt const tp = (tok.tp == tokToplevelFn) ? nodToplevelFn : nodAssignment;
   TypeId leftType = ZERO_ARITY_TYPE;
   Int const countLeftSide = assignment.rightTokenInd - assignment.nameTokenInd;

   Token rightTk = tokens[assignment.rightTokenInd];
   VALIDATEP(assignment.rightTokenInd < assignment.sentinel && rightTk.pl2 > 0,
      pError0(errAssignmentEmptyRight)
   )

   VarId varId = -1;
   Int const assignmentNodeInd = cm->ast.len;
   add(((ParseFrame){
      .level = 0, .startNodeInd = assignmentNodeInd, .sentinel = assignment.sentinel}),
      cm->parseFrames
   );
   newNode((Node){ .tp = tp}, interOf(tok), cm);

   Token firstTok = tokens[cm->i];
   if (countLeftSide == 1)  {
      varId = cm->activeBindings[assignment.name];
      Byte assiSort = assiVarAssignment;
      if (varId > -1) {
         VALIDATEP(cm->vars.c[varId].access == accessPrivMut,
            pError(errCannotMutateImmutable, nameErr(assignment.name))
         )

         leftType = cm->vars.c[varId].typeId;
         if (tIsFunction(leftType, cm) > -1) { // reassignment of a function var
            NameId fnName = cm->tokens.c[assignment.rightTokenInd + 1].pl1;
            FunctionId newFnId = findOverload(
               fnName,
               libeyr_typeGetGenericArg(leftType, typeReadHeader(leftType, cm), 0, cm->types.c),
               cm
            );
            cm->vars.c[varId].fnId = newFnId;
            cm->i = assignment.sentinel;
            newNode(((Node){.tp = nodVar, .pl1 = varId, .pl2 = newFnId, .pl3 = assiFnVarReassign}),
               interOf(firstTok), cm
            );
            goto closeSpans;
         }
         assiSort = assiReassignment;
      } else {
         varId =
            createVar(assignment.name, firstTok.pl2 == 1 ? accessPrivMut : accessPrivImm, -1, cm);
      }
      newNode((Node){ .tp = nodVar, .pl1 = varId, .pl2 = 0, .pl3 = assiSort },
            interOf(firstTok), cm
      );
   } ei (tokens[cm->i + 1].tp == tokType) {
      Bool isAFnVar = false;
      leftType = pAssignmentLeftWithType(firstTok, assignment, cm->i + countLeftSide,
            OUT &isAFnVar, tokens, cm);
      if (isAFnVar)
         { goto closeSpans; }
   } else {
      leftType = pAssignmentLeftComplexExpr(firstTok, assignment.rightTokenInd, tokens, cm);
   }

   cm->i = assignment.rightTokenInd + 1; // CONSUME everything up to body of right side
   cm->ast.c[assignmentNodeInd].pl3 = cm->ast.len - assignmentNodeInd;

   TypeId const rightType = pAssignmentRight(leftType, rightTk, assignment.sentinel, tokens, cm);
   if (varId > -1 && rightType.v > -1 && eq(leftType, ZERO_ARITY_TYPE)) {
      cm->vars.c[varId].typeId = rightType; // inferring the type of left binding
   } ei (leftType.v > -1 && rightType.v > -1) {
      VALIDATEP(eq(leftType, rightType), pError(errTypeMismatch, typeErr2(leftType, rightType)))
   }
closeSpans:
   closeParseFrames(cm);
}

private Assignment //:pPreparseAssignment
pPreparseAssignment(Int start, Int sentinel, TOKENS, CM) {
// Looks at a tokToplevelFn or tokAssignment to determine its key points: where is the right side,
// is it a function definition, is the right side empty etc. Consumes no tokens.
// Note: "start" is 1 past the tokAssignment

   Int indRight = start;
   NameId firstTokenName = tokens[start].pl1;
   for (;
       indRight < sentinel && tokens[indRight].tp != tokAssignRight;
       indRight++) {}

   VALIDATEP((indRight < sentinel && tokens[indRight].pl2 > 0), pError0(errAssignmentEmptyRight));

   return (Assignment){
      .nameTokenInd = start, .rightTokenInd = indRight, .sentinel = sentinel,
      .name = firstTokenName, .isFunction = tokens[indRight + 1].tp == tokFn
   };
}

private void //:pAssignment
pAssignment(Token tok, Int sentinel, TOKENS, CM) {
// Parses both assignments and compile-time defs
   if (tok.pl1 == assiTypeDefinition) {
      pTypeDef(sentinel, tokens, cm);
   } else {
      Assignment assi = pPreparseAssignment(cm->i, sentinel, tokens, cm);
      pAssignmentWorker(tok, assi, tokens, cm);
   }
}

private void //:pFor
pFor(Token forTk, Int sentinel, TOKENS, CM) {
// For loops. Look like "for x' = 0;  x < 100; x++ {  ... }"
//                           ^initInd ^condInd ^stepInd ^bodyInd
// At least a step or a body is syntactically required.
// End result of a parse looks like:
// nodFor
//    scope (pl3 = length of nodes to inner scope)
//       initializations
//       expr evaluating to a bool (the cond - if present)
//       scope (if body not empty)
//          body
//          step(s)
   Int const forNodeInd = cm->ast.len;

   openParsedScope(sentinel, (Node){.tp = nodFor }, interOf(forTk), cm);

   // variable initializations
   for (; cm->i < sentinel && tokens[cm->i].tp == tokAssignment;) {
      Token tok = tokens[cm->i];
      Int const assignmentSentinel = calcSentinel(tok, cm->i);
      cm->i++; // CONSUME the assignment span marker
      pAssignment(tok, assignmentSentinel, tokens, cm);
   }

   // loop condition
   Token condTok = tokens[cm->i];
   Int const condSentinel = calcSentinel(condTok, cm->i);
   cm->i++; // +1 cause the expression parser needs to be 1 past the exprToken
   Int const condNodeInd = cm->ast.len;
   TypeId condType = exprUpToWithFrame((ParseFrame){
         .level = 0, .startNodeInd = cm->ast.len, .sentinel = condSentinel
      },
      interOf(condTok), tokens, cm
   );

   VALIDATEP(eq(condType, boolTy), pError(errTypeMustBeBool, typeErr(condType)))

   cm->i = condSentinel; // CONSUME the "for" until the loop body
   // readying to parse the body + step statements
   Int bodyStartBt = tokens[cm->i].startBt;

   cm->ast.c[forNodeInd].pl1 = condNodeInd - forNodeInd; // distance to the condition
   openParsedScope(
      sentinel, (Node){.tp = nodScope },
      (ChInterval){.startBt = bodyStartBt, .lenBts = forTk.lenBts - bodyStartBt + forTk.startBt },
      cm
   );
}

private void //:pLoopStepMarker
pLoopStepMarker(Token tok, Int sentinel, TOKENS, CM) {
// tokMisc as a span token must be the marker for stepping code in loops
   VALIDATEI(tok.pl1 == miscLoopStep && cm->parseFrames->len > 0, ierrInconsistentSpans);

   Int j = cm->parseFrames->len - 1;
   for (; j > -1; j--) {
      if (cm->parseFrames->c[j].level == pfrLoop)
         { break; }
   }
   VALIDATEI(j > -1, ierrInconsistentSpans); // we must be inside a loop
   ParseFrame loop = cm->parseFrames->c[j];
   cm->ast.c[loop.startNodeInd].pl3 = cm->ast.len - loop.startNodeInd;

   if (loop.eachData.collVar + loop.eachData.indexVar > 0) {
      eachLoopAddStep(loop, tok.pl2, tokens, cm);
   }
}

private EachData //:eachLoopProcess
eachLoopProcess(
   Int collVarId, TypeId eltType, Int headerStart, Int headerSentinel,
   Int sentinel, TOKENS, CM,
   OUT Int* skip, OUT Int* step, OUT Int* balk
) {
// Processes the heading of the each loop (the part between the `{` and the arrow).
// Determines the start (how many elements to skip), the step (increment, may be negative)
// and the balk (how many elements at the end to stop before)
// Step is written to the tokMisc.pl2 that terminates the "each" loop in tokens!
   Int indVarId = cm->vars.len;
   pushInvars((Var){.access = accessPrivImm, .typeId = typeOf(tokInt), .fnId = -1}, cm);
   Int elementVarId = cm->vars.len;
   pushInvars((Var){.access = accessPrivImm, .typeId = eltType, .fnId = -1}, cm);
   EachData eachData = (EachData){
      .collVar = collVarId, .indexVar = indVarId, .elementVar = elementVarId
   };

   Int j = headerStart + 1;
   if (j + 1 < headerSentinel && tokens[j].tp == tokWord && tokens[j].pl1 == nameOfStd(strSkip)) {
      VALIDATEP(tokens[j + 1].tp == tokInt, pError0(errEachLoopWrongSyntax));
      *skip = tokens[j + 1].pl2;
      VALIDATEP(*skip >= 0, pError(errEachLoopInvalidValue, numberErr(*skip)))
      j += 2;
   } else
      { *skip = 0; }

   if (j + 1 < headerSentinel && tokens[j].tp == tokWord && tokens[j].pl1 == nameOfStd(strStep)) {
      VALIDATEP(tokens[j + 1].tp == tokInt, pError0(errEachLoopWrongSyntax));
      *step = tokens[j + 1].pl2;
      VALIDATEP(step != 0, pError(errEachLoopInvalidValue, numberErr(*step)))
      j += 2;
   } else {
      *step = 1;
   }
   tokens[sentinel - 1].pl2 = *step; // will be read by {pLoopStepMarker}

   if (j + 1 < headerSentinel && tokens[j].tp == tokWord && tokens[j].pl1 == nameOfStd(strBalk)) {
      VALIDATEP(tokens[j + 1].tp == tokInt, pError0(errEachLoopWrongSyntax));
      *balk = tokens[j + 1].pl2;
      VALIDATEP(*balk >= 0, pError(errEachLoopInvalidValue, numberErr(*balk)))
      j += 2;
   } else
      { *balk = 0; }

   VALIDATEP(j == headerSentinel, pError0(errEachLoopWrongSyntax))
   return eachData;
}

private void //:eachLoopAddHeader
eachLoopAddHeader(ParseFrame fr, TypeId collType, Int skip, Int step, Int balk, SourceLoc loc,
   TOKENS, CM
) {
// The `i = 0; i < coll.len` part of "each" loops

   Int countInserted = 0;
   if (step > 0) {
      pushInast((Node){.tp = nodAssignment, .pl2 = 2, .pl3 = 2 }, cm); // i = start
      pushInast((Node){.tp = nodVar, .pl1 = fr.eachData.indexVar, .pl3 = assiVarAssignment }, cm);
      pushInast((Node){.tp = tokInt, .pl2 = skip }, cm);

      countInserted = 3;
      cm->ast.c[fr.startNodeInd].pl1 = countInserted + 1; // how many nodes from nodFor to condition

      Int const lt = getOper(opLessTh, tokInt, cm);
      if (balk > 0) { // i < coll.len - balk
         Int const minus = getOper(opMinus, tokInt, cm);
         pushInast((Node){.tp = nodExpr, .pl1 = 0, .pl2 = 6 }, cm);
         pushInast((Node){.tp = nodVar, .pl1 = fr.eachData.indexVar }, cm);
         pushInast((Node){.tp = nodVar, .pl1 = fr.eachData.collVar, }, cm);
         pushInast((Node){.tp = nodCall, .pl1 = collType.v, .pl2 = 1, .pl3 = callField }, cm);
         pushInast((Node){.tp = tokInt, .pl2 = balk }, cm);
         pushInast((Node){.tp = nodCall, .pl1 = minus, .pl2 = 2, .pl3 = callNormal }, cm);
         pushInast((Node){.tp = nodCall, .pl1 = lt, .pl2 = 2, .pl3 = callNormal }, cm);
         countInserted  += 7;
      } else { // i < coll.len
         pushInast((Node){.tp = nodExpr, .pl1 = 0, .pl2 = 4 }, cm);
         pushInast((Node){.tp = nodVar, .pl1 = fr.eachData.indexVar }, cm);
         pushInast((Node){.tp = nodVar, .pl1 = fr.eachData.collVar, }, cm);
         pushInast((Node){.tp = nodCall, .pl1 = collType.v, .pl2 = 1, .pl3 = callField }, cm);
         pushInast((Node){.tp = nodCall, .pl1 = lt, .pl2 = 2, .pl3 = callNormal }, cm);
         countInserted += 5;
      }
   } else {
      Int const minus = getOper(opMinus, tokInt, cm);
      pushInast((Node){.tp = nodAssignment, .pl2 = 6, .pl3 = 2 }, cm); // i = coll.len - start
      pushInast((Node){.tp = nodVar, .pl1 = fr.eachData.indexVar, .pl3 = assiVarAssignment }, cm);
      pushInast((Node){.tp = nodExpr,       .pl2 = 4 }, cm);
      pushInast((Node){.tp = nodVar, .pl1 = fr.eachData.collVar, .pl3 = assiVarAssignment }, cm);
      pushInast((Node){.tp = nodCall, .pl1 = collType.v, .pl2 = 1, .pl3 = callField }, cm);
      pushInast((Node){.tp = tokInt, .pl1 = 0, .pl2 = (skip + 1) }, cm);
      pushInast((Node){.tp = nodCall, .pl1 = minus, .pl2 = 2, .pl3 = callNormal }, cm);

      countInserted = 7;
      cm->ast.c[fr.startNodeInd].pl1 = countInserted + 1; // how many nodes from nodFor to condition

      pushInast((Node){.tp = nodExpr, .pl1 = 0, .pl2 = 3 }, cm);
      pushInast((Node){.tp = nodVar, .pl1 = fr.eachData.indexVar }, cm);
      if (balk > 0) { // i > (balk - 1)
         Int const gt = getOper(opGreaterTh, tokInt, cm);
         pushInast((Node){.tp = tokInt, .pl2 = (balk - 1) }, cm);
         pushInast((Node){.tp = nodCall, .pl1 = gt, .pl2 = 2, .pl3 = callNormal }, cm);
      } else { // i >= 0
         Int const gtEq = getOper(opGTEQ, tokInt, cm);
         pushInast((Node){.tp = tokInt, .pl2 = 0 }, cm);
         pushInast((Node){.tp = nodCall, .pl1 = gtEq, .pl2 = 2, .pl3 = callNormal }, cm);
      }
      countInserted += 4;
   }

   for (Int l = 0; l < countInserted; l++) {
      add(loc, cm->sourceLocs);
   }
}

private void //:eachLoopAddBody
eachLoopAddBody(ParseFrame fr, TypeId collType, Int headerSentinel, Token eachTk, SourceLoc loc,
   TOKENS, CM
) {
// The inner scope and `elem = coll[ind]` part of "each" loops
   Int bodyStartBt = tokens[headerSentinel].startBt;
   openParsedScope(
      fr.sentinel, (Node){.tp = nodScope },
      (ChInterval){.startBt = bodyStartBt, .lenBts = eachTk.lenBts - bodyStartBt + eachTk.startBt },
      cm
   );

   pushInast((Node){.tp = nodAssignment, .pl2 = 5, .pl3 = 2 }, cm); // elem = coll[ind]
   pushInast((Node){.tp = nodVar, .pl1 = fr.eachData.elementVar, .pl3 = assiVarAssignment }, cm);
   pushInast((Node){.tp = nodExpr, .pl2 = 3 }, cm);
   pushInast((Node){.tp = nodVar, .pl1 = fr.eachData.collVar }, cm);
   pushInast((Node){.tp = nodVar, .pl1 = fr.eachData.indexVar }, cm);
   pushInast((Node){.tp = nodCall, .pl1 = collType.v, .pl2 = 2, .pl3 = callGetElem }, cm);

   for (Int l = 0; l < 7; l++) {
      add(loc, cm->sourceLocs);
   }
}

private void //:eachLoopAddNodes
eachLoopAddNodes(Token eachTk, ParseFrame fr, TypeId collType, Int skip, Int step, Int balk,
   Int headerSentinel, TOKENS, CM
) {
// Inserts nodes for the initializer and condition of an "each" loop
   SourceLoc loc = locOf(interOf(eachTk), cm);
   eachLoopAddHeader(fr, collType, skip, step, balk, loc, tokens, cm);
   eachLoopAddBody(fr, collType, headerSentinel, eachTk, loc, tokens, cm);
}

private void //:eachLoopAddStep
eachLoopAddStep(ParseFrame fr, Int step, TOKENS, CM) {
// Inserts nodes for the stepping statement of an "each" loop
   Token eachTk = tokens[fr.startTokenInd];
   EachData ed = fr.eachData;
   ChInterval interv = interOf(eachTk);
   if (step > 0) { // i = i + step
      Int plus = getOper(opPlus, tokInt, cm);
      newNode((Node){.tp = nodAssignment, .pl2 = 5, .pl3 = 2 }, interv, cm);
      newNode((Node){.tp = nodVar, .pl1 = ed.indexVar }, interv, cm);
      newNode((Node){.tp = nodExpr, .pl2 = 3, }, interv, cm);
      newNode((Node){.tp = nodVar, .pl1 = ed.indexVar }, interv, cm);
      newNode((Node){.tp = tokInt, .pl2 = step, }, interv, cm);
      newNode((Node){.tp = nodCall, .pl1 = plus, .pl2 = 2, .pl3 = callNormal }, interv, cm);
   } else { // i = i - |step|
      Int minus = getOper(opMinus, tokInt, cm);
      Int absStep = -step;
      newNode((Node){.tp = nodAssignment, .pl2 = 5, .pl3 = 2 }, interv, cm);
      newNode((Node){.tp = nodVar, .pl1 = ed.indexVar }, interv, cm);
      newNode((Node){.tp = nodExpr, .pl2 = 3, }, interv, cm);
      newNode((Node){.tp = nodVar, .pl1 = ed.indexVar }, interv, cm);
      newNode((Node){.tp = tokInt, .pl2 = absStep, }, interv, cm);
      newNode((Node){.tp = nodCall, .pl1 = minus, .pl2 = 2, .pl3 = callNormal }, interv, cm);
   }
}

private void //:pEach
pEach(Token eachTk, Int sentinel, TOKENS, CM) {
   Token miscTk = tokens[cm->i];

   VALIDATEP(miscTk.tp == tokMisc && miscTk.pl1 == miscLoopStep0 && miscTk.pl2 > 0,
      pError0(errEachLoopWrongSyntax)); // pl2 will be 0 if there was no arrow inside the loop

   Token nameTk = tokens[cm->i + 2]; // skipping the tokMisc and tokStmt
   VALIDATEP(nameTk.tp == tokWord, pError0(errEachLoopWrongSyntax));
   NameId collName = nameTk.pl1;

   Int collVarId = cm->activeBindings[collName];
   VALIDATEP(collVarId > -1, pError(errUnknownBinding, nameErr(collName)));
   Var collVar = cm->vars.c[collVarId];
   TypeId collType = collVar.typeId;
   VALIDATEP(tIsList(collType, cm), pError(errTypeOfNotList, typeErr(collType)));

   TypeId eltType =
      libeyr_typeGetGenericArg(collType, typeReadHeader(collType, cm), 0, cm->types.c);

   Int headerStart = cm->i + 2;
   Int headerSentinel = calcSentinel(tokens[cm->i + 1], cm->i + 1);

   ParseFrame eachFrame = (ParseFrame){
      .startTokenInd = cm->i - 1,
      .startNodeInd = cm->ast.len, .level = pfrLoop, .sentinel = sentinel
   };
   Int skip, step, balk;
   eachFrame.eachData = eachLoopProcess(
      collVarId, eltType, headerStart, headerSentinel, sentinel, tokens, cm,
      OUT &skip, OUT &step, OUT &balk
   );
   openParsedScopeWorker(eachFrame, (Node){.tp = nodFor}, interOf(eachTk), cm);

   eachLoopAddNodes(eachTk, eachFrame, collType, skip, step, balk, headerSentinel, tokens, cm);
   cm->i = headerSentinel; // CONSUME the tokMisc at start of an "each" loop
}

private void //:parseErrorBareAtom
parseErrorBareAtom(Token tok, Int sentinel, TOKENS, CM) {
   throwExcParser(pError0(errTemp));
}

private ParseFrame //:popAParseFrame
popAParseFrame(CM) {
// Pops a frame from the scopes. For a scope type of frame, also deactivates its bindings.
// Returns pointer to previous frame (which will be top after this call) or null if there isn't any
   ParseFrame frame = removeLast(cm->parseFrames); // matched by scopes->len-- below
   if (frame.level < pfrScope)
      { goto finishUp; }

   rewindLexicalScope(cm);
finishUp:
   cm->ast.c[frame.startNodeInd].pl2 = cm->ast.len - frame.startNodeInd - 1;
   return frame;
}

private TypeId //:exprSingleItem
exprSingleItem(Token tk, CM) {
// A single-item expression, like "foo". Consumes no tokens, inserts 2 nodes.
// Pre-condition: we are 1 token past the token we're parsing.
// Returns the type of the single item
   TypeId typeId = ZERO_ARITY_TYPE;

   if (tk.tp == tokWord) {
      Node node = createNodVarForName(tk.pl1, cm);
      typeId = cm->vars.c[node.pl1].typeId;
      newNode(node, interOf(tk), cm);
   } ei (tk.tp == tokOperator) {
      Int operId = tk.pl1;
      OpDef operDefinition = OPERATORS[operId];
      VALIDATEP(operDefinition.prec == precUnary,
         pError(errOperatorWrongArity, operatorErr(operId))
      )
      newNode((Node){ .tp = nodVar, .pl1 = operId }, interOf(tk), cm);
      // TODO add the type when we support first-class functions
   } ei (tk.tp == tokString) {
      newNode((Node){.tp = tokString, .pl1 = tk.startBt, .pl2 = tk.lenBts}, interOf(tk), cm);
      typeId = typeOf(tokString);
   } ei (tk.tp <= topVerbatimType) {
      newNode((Node){.tp = tk.tp, .pl1 = tk.pl1, .pl2 = tk.pl2}, interOf(tk), cm);
      typeId = typeOf(tk.tp);
   } ei (tk.tp == tokData) { // `[]`
      newNode((Node){.tp = nodDataLit, .pl1 = -1, .pl2 = 0, .pl3 = 0}, interOf(tk), cm);
   } else {
      throwExcParser(pError0(errUnexpectedToken));
   }
   return typeId;
}

private TypeId //:eEachVariable
eEachVariable(Token miscTk, CM) {
// A single `coll.@` or `coll.#`. Writes exactly 1 node. Consumes no tokens
   Token nameTk = cm->tokens.c[cm->i + 1];
   NameId name = nameTk.pl1;
   ChInterval interv = (ChInterval){
      .startBt = miscTk.startBt, .lenBts = miscTk.lenBts + nameTk.lenBts
   };
   Int collVarId = cm->activeBindings[name];
   VALIDATEP(collVarId > -1, pError(errUnknownBinding, nameErr(name)))
   LParseFrame* bt = cm->parseFrames;
   Int indEachFrame = bt->len - 1;
   for (; indEachFrame > -1; indEachFrame--) {
      if (bt->c[indEachFrame].eachData.collVar == collVarId)
         { break; }
   }

   VALIDATEP(indEachFrame > -1, pError(errEachNotACollection, nameErr(name)))
   Int varId, typeId;
   if (miscTk.pl1 == miscEachElem) { // `coll.@`
      varId =  bt->c[indEachFrame].eachData.elementVar;
      typeId = cm->vars.c[varId].typeId.v;
   } else {  // `coll.#`
      varId =  bt->c[indEachFrame].eachData.indexVar;
      typeId = tokInt;
   }

   newNode((Node){ .tp = nodVar, .pl1 = varId, .pl2 = 0, .pl3 = 0}, interv, cm);
   return typeOf(typeId);
}

private void //:subexSaveDataLiteral
subexSaveDataLiteral(ExprFrame frame, Expr* e, CM) {
// We are at end of a data literal. Creates an assignment in main. Then walks over the data literal
// nodes and counts elements that are subexpressions. Then copies the nodes from scratch to main,
// careful to wrap subexpressions in a nodExpr. Finally, replaces the copied nodes in scr with
// an id linked to the new entity
   LNode* scr = &(e->scr);  // ((ind in scr) (count of nodes in subexpr))

   const VarId newVarId = cm->vars.len;
   pushInvars(((Var) { .access = accessPrivImm, .fnId = -1 }), cm);

   Int countNodes = scr->len - frame.startNode - 1;
   Node allocNd = scr->c[frame.startNode];
   switch (allocNd.pl3) { // see {eDataLiteral}. arrLitKnownElements is not possible here
   case arrLitRuntimeLength: {
      scr->c[frame.startNode].pl2 = countNodes;
      break;
   }
   case arrLitKnownElements: {
      Int countElements = 0;
      for (Int j = frame.startNode + 1; j < scr->len; ++j) {// +1 to skip the array itself
         Node nd = scr->c[j];
         countElements++;

         if (nd.tp == nodExpr)
            { j += nd.pl2; }
      }
      scr->c[frame.startNode].pl2 = countNodes;
      scr->c[frame.startNode].pl3 = countElements;
      break;
   }
   }

   ChInterval const rawLoc = frame.chi;
   newNode((Node){.tp = nodAssignment, .pl1 = 0, .pl2 = countNodes + 2, .pl3 = 2}, rawLoc, cm);
   newNode((Node){.tp = nodVar, .pl1 = newVarId, .pl2 = 0, .pl3 = assiVarAssignment}, rawLoc, cm);
   Int const astInd = cm->ast.len;

   eSaveDataLiteralNodes(frame.startNode, *scr, e->locsScr, cm);

   TypeId collType = typecheckList(cm->ast.c[astInd], astInd, cm);

   cm->vars.c[newVarId].typeId = collType;
   // replace the nodes in @scr with a single var
   e->scr.c[frame.startNode] = (Node){ .tp = nodVar, .pl1 = newVarId, .pl2 = 0, .pl3 = 0 };
   scr->len = frame.startNode + 1;
   e->locsScr.len = frame.startNode + 1;
}

private void //:eBumpArgCount
eBumpArgCount(LExprFrame* frames) {
   Int const ind = frames->len - 1;
   Int const tp = frames->c[ind].tp;
   if (tp == exfrCall || tp == exfrDataLit || tp == exfrParen || tp == exfrExWrapper)
      { frames->c[ind].argCount++; }
}

private void //:eWriteUnaryCalls
eWriteUnaryCalls(Expr* e) {
   ExprFrame* zero = e->frames->c;
   ExprFrame* const initFrame = zero + (e->frames->len - 1);
   ExprFrame* frame = initFrame;
   for (; frame >= zero && frame->tp == exfrUnaryCall; frame--) {
      add(((Node){.tp = nodCall, .pl1 = frame->name, .pl2 = 1, .pl3 = 0}), &(e->scr));
      add(frame->chi, &(e->locsScr));
   }
   if (frame < initFrame)
      { e->frames->len = frame - zero + 1; }
}

private void //:eWriteCallToScratch
eWriteCallToScratch(ExprFrame frame, Expr* e) {
// Writes a call to the nodes scratch space.
// Precondition: the ExprFrame has already been popped
   LNode* scr = &(e->scr);
   Node call = {
      .tp = nodCall,
      .pl1 = frame.name,
      .pl2 = frame.argCount,
      .pl3 = (frame.isVarCall ? callVar : callNormal)
   };

   add(call, scr);
   add(frame.chi, &(e->locsScr));
}

private void //:reorderStructFindUnspecifiedKeys
reorderStructFindUnspecifiedKeys(
   Arr(Int) reorderKeys, Int fieldCount, Arr(FieldName) fields, Int structName, CM
) {
   for (Int j = 0; j < fieldCount; j += 2) {
      if (reorderKeys[j] == -1) {
         pError(errTypeFieldNotSpecified, nameErr2(fields[j/2].name, structName));
      }
   }
}

private void //:reorderStructInitBuffers
reorderStructInitBuffers(Int fieldCount, Int nodeCount, Expr* restrict e, Arena* aTmp) {
   Int const lenKeys = 2*fieldCount;
   if (e->reorderKeys.cap < lenKeys) {
      e->reorderKeys.c = allocateArray(lenKeys, Int, aTmp);
      e->reorderKeys.cap = lenKeys;
   }
   e->reorderKeys.len = 0;

   Int const nodeCountAfter = nodeCount - fieldCount; // there will be no nodes for the fields
   d("REORDER len keys %d node count after %d", lenKeys, nodeCountAfter);

   if (e->reorderBuf.cap < nodeCountAfter) {
      e->reorderBuf.c = allocateArray(nodeCountAfter, Node, aTmp);
      e->reorderBuf.cap = nodeCountAfter;
   }
   e->reorderBuf.len = 0;
}

private Int //:binarySearchFieldName
binarySearchFieldName(Int needle, Arr(FieldName) haystack, Int len) {
   if (len < 1)
      { return -1; }

   Int j = len - 1;
   if (haystack[0].name == needle) {
      return 0;
   } ei (haystack[j].name == needle) {
      return j;
   }

   for (Int i = 0; i < j; ) {
      if (i == j - 1)
         { return -1; }
      Int midInd = (i + j)/2;
      Int mid = haystack[midInd].name;
      if (mid > needle) {
         j = midInd;
      } ei (mid < needle) {
         i = midInd;
      } else {
         return midInd;
      }
   }
   return -1;
}

private void //:reorderStructLocateKeys
reorderStructLocateKeys(
   ExprFrame frame, Int sentNode, Arr(FieldName) fields, Int fieldCount, CM
) { // Finds where each struct key is located in @e.scr and validates key completeness
   Int fieldsFound = 0;
   Arr(Int) reorderKeys = cm->expr.reorderKeys.c;
   for (Int f = 0; f < fieldCount; f += 2) {
      reorderKeys[f] = -1;
   }
   for (Int j = frame.startNode + 1; j < sentNode;) {
      Node fieldNd = cm->expr.scr.c[j];
      Int fieldInd = binarySearchFieldName(fieldNd.pl1, fields, fieldCount);
      VALIDATEP(fieldInd > -1, pError(errTypeFieldNotFound, nameErr2(fieldNd.pl1, frame.name)));

      Int sentinel = calcNodeSentinel(fieldNd, j);
      reorderKeys[2*fieldInd] = j + 1;
      reorderKeys[2*fieldInd + 1] = sentinel;
      fieldsFound++;
      j = sentinel;
   }
   if (fieldsFound < fieldCount) {
      reorderStructFindUnspecifiedKeys(reorderKeys, fieldCount, fields, frame.name, cm);
   }
}

private void //:reorderStructMoveNodes
reorderStructMoveNodes(Int startNode, Expr* e) {
// Actually reorders nodes in @e.scr
// (:key2 expr2 :key1 expr1) --> (expr1 expr2)
   LInt reorderKeys = e->reorderKeys;
   Node* tgt = e->reorderBuf.c;
   for (Int j = 0; j < reorderKeys.len; j += 2) {
      Int blockLen = reorderKeys.c[j + 1] - reorderKeys.c[j];
      memcpy(tgt, e->scr.c + reorderKeys.c[j], blockLen * sizeof(Node));
      tgt += blockLen;
   }
   Int const newLen = tgt - e->reorderBuf.c;
   memcpy(e->scr.c + startNode, e->reorderBuf.c, newLen*sizeof(Node));
   e->scr.len -= (e->scr.len - startNode - newLen);
   d("REORD new len %d", e->scr.len);
}

private void //:reorderStruct
reorderStruct(ExprFrame frame, Expr* restrict e, CM) {
// Validates that keys in a struct initializer match the type, and replaces the nodes in @exp.
// (:key value :key value) => (value value) in the order of the struct fields
   Node nd = e->scr.c[frame.startNode];
   TypeId t = typeGetTypeByName(nd.pl1, cm);
   TypeHeader hdr = typeReadHeader(t, cm);

   Int nodeCount = e->scr.len - frame.startNode;
   Int const fieldCount = hdr.arity;
   reorderStructInitBuffers(fieldCount, nodeCount, e, cm->aTmp);

   Int startField = t.v + TYPE_PREFIX + hdr.arity;
   Arr(FieldName) fields = cm->genericFields.c + startField;
   
   reorderStructLocateKeys(frame, cm->i, fields, fieldCount, cm);
   reorderStructMoveNodes(frame.startNode, e);
   
   add(((Node){.tp = nodStruct, .pl1 = frame.name, .pl2 = fieldCount}), &(e->scr));
   add(frame.chi, &(e->locsScr));
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
      case exfrDataLit:
         subexSaveDataLiteral(frame, e, cm); break;
      case exfrParen:
         eWriteUnaryCalls(e);
         eBumpArgCount(e->frames);
         break;
      case exfrAccessIn:
         add(((Node){.tp = nodCall, .pl1 = frame.name, .pl2 = 2, .pl3 = callGetElem}), &(e->scr));
         add(frame.chi, &(e->locsScr));
         break;
      case exfrAccessor:
         eWriteUnaryCalls(e);
         eBumpArgCount(e->frames);
         break;
      case exfrExWrapper: // a nodExpr inside a nodDataLit
         eWriteUnaryCalls(e);
         e->scr.c[frame.startNode - 1].pl2 = e->scr.len - frame.startNode;
         break;
      case exfrStructField:
         e->scr.c[frame.startNode].pl2 = e->scr.len - frame.startNode; break;
      case exfrStruct:
         reorderStruct(frame, e, cm); break;
      }
   }
}

private void //:eSaveNodes
eSaveNodes(Int startNodeInd, CM) {
// Copy nodes from scratch into main AST
   Expr* restrict e = &(cm->expr);
   LNode scr = e->scr;
   LChInterval chis = e->locsScr;
   Int const oldLen = cm->ast.len;
   Int const addLen = chis.len;
   if (e->metAnAllocation)
      { cm->ast.c[startNodeInd].pl1 = 1; }
   if (cm->ast.len + scr.len + 1 < cm->ast.cap) {
      memcpy((Node*)(cm->ast.c) + (cm->ast.len), scr.c, scr.len*sizeof(Node));
      for (Int j = 0; j < addLen; j++) {
         cm->sourceLocs->c[oldLen + j] = locOf(chis.c[j], cm);
      }
   } else {
      Int newCap = 2*(cm->ast.cap) + scr.len;
      Arr(Node) newContent = allocateArray(newCap, Node, cm->a);
      memcpy(newContent, cm->ast.c, cm->ast.len*sizeof(Node));
      memcpy((Node*)(newContent) + (cm->ast.len), scr.c, scr.len*sizeof(Node));
      cm->ast.cap = newCap;
      cm->ast.c = newContent;

      Arr(SourceLoc) newLocs = allocateArray(newCap, SourceLoc, cm->a);
      memcpy(newLocs, cm->sourceLocs->c, cm->sourceLocs->len*sizeof(SourceLoc));
      for (Int j = 0; j < oldLen; j++) {
         newLocs[oldLen + j] = locOf(chis.c[j], cm);
      }

      cm->sourceLocs->cap = newCap;
      cm->sourceLocs->c = newLocs;
   }
   cm->ast.len += scr.len;
   cm->sourceLocs->len += scr.len;
}

Int //:subexSkipFirstThing
subexSkipFirstThing(Int const start, Int const subSentinel, TOKENS, CM) {
// Skips a lump of tokens consisting of:
// - possibly unary operator calls with definitely, an atom or a span
// - OR
// - an atom or a span with possibly field accessors.
// Examples: `$a`, `a.b.c` but NOT `$a.b.c`
   Int j = start;
   for (; j < subSentinel && tokens[j].tp == tokOperator && tokens[j].pl2 == precUnary;
         j++
   ) {}
   Bool foundPrefix = j > start;

   // expression consisting solely of unary opers
   VALIDATEP(j < subSentinel, pError0(errExpressionError))
   j = calcSentinel(tokens[j], j);

   if (foundPrefix)
      { return j; }
   for (; j < subSentinel && tokens[j].tp == tokFieldAcc; j++) {
   }
   return j;
}

void //:subexProcessFirstTokenIfItsACall
subexProcessFirstTokenIfItsACall(Int start, Int subSentinel, TOKENS, CM) {
// Pre-parses the start of a complex subexpression. Consumes 0 or 1 tokens.
// Iff the second thing is a non-prefix operator or .call, returns false; otherwise the first
// token must be a word and is processed.
   Int j = subexSkipFirstThing(start, subSentinel, tokens, cm);
   if (j == subSentinel)
      { return; } // the `(call)` case

   Token secondThing = tokens[j];
   if (secondThing.tp == tokOperator && secondThing.pl2 != precUnary) {
      // the `a + b` or `(..) .func b` case
      return;
   } else {
      // the `foo a b c` case
      Token theCall = tokens[start];
      VALIDATEP(theCall.tp == tokWord, pError0(errExpressionFunctionless))
      add(
         ((ExprFrame) {
            .tp = exfrCall, .name = theCall.pl1, .sentinel = subSentinel, .precedence = precFn,
            .argCount = 0, .chi = interOf(theCall),
            .isVarCall = cm->activeBindings[theCall.pl1] > -1
         }),
         cm->expr.frames
      );
      cm->i++; // CONSUME the first token because it's a call and had been processed
   }
}

private void //:eParens
eParens(Token cTk, ExprFrame parent, Expr* e, TOKENS, CM) {
// Precondition: we are pointing at tokParens
// Consumes 0 or 1 tokens.
   Int parensSentinel = calcSentinel(cTk, cm->i);
   ChInterval loc = interOf(cTk);
   if (parensSentinel == cm->i + 2) { // A nullary call like `(call)`
      Token callTk = tokens[cm->i + 1];
      ChInterval callLoc = interOf(callTk);
      if (parent.tp == exfrDataLit) {
         // inside a data allocator, subexprs need to be wrapped in nodExpr for t-checking & codegen
         add(((Node){ .tp = nodExpr, .pl1 = 1 }), &(e->scr));
         add(loc, &(e->locsScr));
      }
      add(((Node){ .tp = nodCall, .pl1 = callTk.pl1, .pl2 = 0 }), &(e->scr));
      add(callLoc, &(e->locsScr));

      eWriteUnaryCalls(e);
      eBumpArgCount(e->frames);
      cm->i++; // CONSUME the tokParens (and the loop in eParse will consume the call)
   } else {
      Unt tp = exfrParen;
      if (parent.tp == exfrDataLit) {
         // inside a data allocator, subexprs need to be wrapped in nodExpr for t-checking & codegen
         tp = exfrExWrapper;
         add(((Node){ .tp = nodExpr, .pl1 = 0 }), &(e->scr));
         add(loc, &(e->locsScr));
      }
      add(((ExprFrame){
            .tp = tp, .startNode = e->scr.len, .sentinel = parensSentinel,
            .argCount = 0, .chi = loc }), e->frames);
      subexProcessFirstTokenIfItsACall(cm->i + 1, parensSentinel, tokens, cm);
   }
}

private void //:eKey
eKey(Token cTk, ExprFrame parent, Expr* restrict e, TOKENS, CM) {
   add(((ExprFrame) {
         .tp = exfrStructField, .name = cTk.pl1,
         .sentinel = calcSentinel(cTk, cm->i), .startNode = e->scr.len,
         .chi = interOf(cTk) }),
        e->frames
   );
   add(((Node){ .tp = nodStruct, .pl1 = cTk.pl1, .pl3 = 1 }), &(e->scr));
   add(interOf(cTk), &(e->locsScr));
}

private void //:eStructLiteral
eStructLiteral(Token cTk, Expr* restrict e, TOKENS, CM) {
   add(((ExprFrame) {
         .tp = exfrStruct, .name = cTk.pl1,
         .sentinel = calcSentinel(cTk, cm->i), .startNode = e->scr.len,
         .chi = interOf(cTk)  }),
        e->frames
   );
}

private void //:eDataLiteral
eDataLiteral(Token cTk, Expr* restrict e, TOKENS, CM) {
   e->metAnAllocation = true;
   eBumpArgCount(e->frames);
   Node newDataAlloc = (Node){.tp = nodDataLit };
   if (cTk.pl1 >= BIG) { // `[@...]`
      VALIDATEP(cTk.pl2 >= 2 && tokens[cm->i + 1].tp == tokType, pError0(errMetaArrSyntax))
      Int sentinel = calcSentinel(cTk, cm->i);

      cm->i++; // CONSUME the tokData
      Token typeTk = tokens[cm->i];
      Int const typeSentinel = calcSentinel(typeTk, cm->i);
      Bool isGeneric;
      TypeId elemType = tParse(typeSentinel, &isGeneric, tokens, cm);
      VALIDATEP(!isGeneric, pError(errTypePolymorphicAssignment, typeErr(elemType)))

      Token countTk = tokens[typeSentinel];

      newDataAlloc.pl1 = tCreateSingleParamTypeCall(nameOfStd(strArr), elemType, cm).v;
      if (countTk.tp == tokInt) {
         newDataAlloc.pl3 = arrLitKnownLength;
         newDataAlloc.pl2 = countTk.pl2;
         cm->i = sentinel - 1; // CONSUME the whole data allocation
      } else {
         newDataAlloc.pl3 = arrLitRuntimeLength; // we don't know the length to allocate at compile time
         add(((ExprFrame) {
            .tp = exfrDataLit, .name = nameOfStd(strArr), .sentinel = sentinel,
            .startNode = e->scr.len, .chi = interOf(cTk)  }),
           e->frames
         );
         cm->i = typeSentinel - 1;
      }
      add(newDataAlloc, &(e->scr));
      add(interOf(cTk), &(e->locsScr));
   } ei (cTk.pl2 == 0) { // `[]`
      add(((Node){.tp = nodDataLit, .pl1 = -1, .pl2 = 0, .pl3 = arrLitKnownLength}), &(e->scr));
      add(interOf(cTk), &(e->locsScr));
   } else { //`[...elements...]`
      newDataAlloc.pl3 = arrLitKnownElements; // we don't know the length to allocate at compile time
      add(((ExprFrame) {
            .tp = exfrDataLit, .name = nameOfStd(strArr),
            .sentinel = calcSentinel(cTk, cm->i), .startNode = e->scr.len,
            .chi = interOf(cTk)  }),
           e->frames
      );
      add(newDataAlloc, &(e->scr));
      add(interOf(cTk), &(e->locsScr));
   }
}

private void //:eProcessToken
eProcessToken(Token cTk, Int sentinel, Expr* restrict e, TOKENS, CM) {
   ExprFrame parent = last(e->frames);
   ChInterval loc = interOf(cTk);
   NameId name = cTk.pl1;
   Byte tokTp = cTk.tp;
   switch (tokTp) {
   case tokOperator:
      Int precedence = OPERATORS[name].prec;
      if (precedence == precUnary) {
         add(((ExprFrame) {
               .tp = exfrUnaryCall, .name = name, .sentinel = parent.sentinel,
               .precedence = precUnary, .argCount = 1, .chi = loc, .startNode = -1,  }),
            e->frames);
      } else {
         eOperatorCall(cTk, precedence, false, cm);
      }
      break;
   case tokAccessor:
      add(((ExprFrame) {
            .tp = exfrAccessor, .name = opGetElem, .sentinel = calcSentinel(cTk, cm->i),
            .chi = loc
         }),
         e->frames
      );
      cm->i++; // CONSUME the tokAccessor
      Token varTk = tokens[cm->i];
      VALIDATEP(varTk.tp == tokWord, pError0(errExpressionExpectedWord));

      Node node = createNodVarForName(varTk.pl1, cm);
      add(node, &(e->scr));
      add(interOf(varTk), &(e->locsScr));
      break;
   case tokAccessorIn:
      add(((ExprFrame) {
            .tp = exfrAccessIn, .name = opGetElem, .sentinel = calcSentinel(cTk, cm->i), .chi = loc
         }),
         e->frames); break;
   case tokFieldAcc:
      add(((Node){.tp = nodCall, .pl1 = name, .pl3 = callField}), &(e->scr)); break;
   case tokString:
      add(((Node){ .tp = cTk.tp, .pl1 = loc.startBt, .pl2 = loc.lenBts }), &(e->scr));
      add(loc, &(e->locsScr));
      eWriteUnaryCalls(e);
      eBumpArgCount(e->frames);
      break;
   case tokInt:
   case tokLong:
   case tokDouble:
   case tokBool:
      add(((Node){ .tp = cTk.tp, .pl1 = name, .pl2 = cTk.pl2 }), &(e->scr));
      //-fallthrough
   case tokWord:
      if (tokTp == tokWord)
         { add(createNodVarForName(name, cm), &(e->scr)); }
      add(loc, &(e->locsScr));
      eWriteUnaryCalls(e);
      eBumpArgCount(e->frames);
      break;
   case tokParens:
      eParens(cTk, parent, e, tokens, cm); break;
   case tokKey:
      eKey(cTk, parent, e, tokens, cm); break;
   case tokStruct:
      eStructLiteral(cTk, e, tokens, cm); break;
   case tokData:
      eDataLiteral(cTk, e, tokens, cm); break;
   case tokMisc:
      eEachVariable(cTk, cm);
      eBumpArgCount(e->frames);
      cm->i++; // CONSUME the tokMisc (and the tokWord will be consumed in {eParse})
      break;
   default:
      throwExcParser(pError0(errExpressionCannotContain));
   }
}

private void //:eParse
eParse(Int sentinel, TOKENS, CM) {
// The core code of the general, long expression parse. Starts at cm->i and parses until
// "sentinel". Produces a linear sequence of operands and calls with arg counts in
// Reverse Polish Notation. Handles data allocations, too. But not single-item exprs.
// Consumes the whole expression
// Pre-condition: we are 1 past the nodExpr, if any (but NOT past nodData if it's the whole exp)
   Expr* e = &(cm->expr);
   e->metAnAllocation = false;
   LNode* scr = &(e->scr);
   LChInterval* locsScr = &(cm->expr.locsScr);
   LExprFrame* frames = cm->expr.frames;
   frames->len = 0;
   scr->len = 0;
   locsScr->len = 0;
   if (tokens[cm->i].tp != tokParens || calcSentinel(tokens[cm->i], cm->i) < sentinel)
      { add(((ExprFrame){ .tp = exfrParen, .sentinel = sentinel}), frames); }

   subexProcessFirstTokenIfItsACall(cm->i, sentinel, tokens, cm);
   for (; cm->i < sentinel; cm->i++) { // CONSUME any expression token
      eClose(e, cm);
      eProcessToken(tokens[cm->i], sentinel, e, tokens, cm);
   }
   eClose(e, cm);
}

private TypeId //:exprUpToWithFrame
exprUpToWithFrame(ParseFrame frame, ChInterval chi, TOKENS, CM) {
// The main "big" expression parser. Parses an expression whether there is a
// token or not. Starts from cm->i and goes up to the sentinel. Returns the expression's type
// Precondition: we are looking 1 past the tokExpr or tokParens
// CONSUMES the whole expression
   if (cm->i + 1 == frame.sentinel) { // the [stmt 1, tokInt] case
      Token singleToken = tokens[cm->i];
      if (singleToken.tp <= topVerbatimTokenVariant || singleToken.tp == tokWord
            || singleToken.tp == tokData) {
         cm->i++;
         return exprSingleItem(singleToken, cm);
      }
   } ei (cm->i + 2 == frame.sentinel && tokens[cm->i].tp == tokMisc) { // `coll.@`, `coll.#`
      cm->i += 2;
      return eEachVariable(tokens[cm->i], cm);
   }
   Int const startNodeInd = cm->ast.len;
   add(frame, cm->parseFrames);
   newNode((Node){ .tp = nodExpr}, chi, cm);

   eParse(frame.sentinel, tokens, cm);
   eSaveNodes(startNodeInd, cm);
   TypeId exprType = typeCheckBigExpr(startNodeInd, cm->ast.len, cm);
   closeParseFrames(cm);
   return exprType;
}

private TypeId //:exprUpTo
exprUpTo(Int sentinelToken, ChInterval loc, TOKENS, CM) {
// The main "big" expression parser. Parses an expression whether there is a token or not.
// Precondition: we are looking 1 past the tokExpr or tokParens.
// Starts from cm->i and goes up to the sentinel token. Emits a nodExpr and opens a corresponding
// parse frame. Returns the expression's type
   Int startNodeInd = cm->ast.len;
   add(
      ((ParseFrame){ .startNodeInd = startNodeInd, .sentinel = sentinelToken }), cm->parseFrames
   );
   newNode((Node){ .tp = nodExpr}, loc, cm);
   eParse(sentinelToken, tokens, cm);
   eSaveNodes(startNodeInd, cm);

   TypeId exprType = typeCheckBigExpr(startNodeInd, cm->ast.len, cm);
   closeParseFrames(cm);
   return exprType;
}

private TypeId //:exprHeadless
exprHeadless(Int sentinel, ChInterval loc, TOKENS, CM) {
// Precondition: we are looking at the first token of expr which does not have a
// tokStmt/tokParens header.
// Consumes 1 or more tokens. Returns the type of parsed expression
   if (cm->i + 1 == sentinel) { // the [stmt 1, tokInt] case
      Token singleToken = tokens[cm->i];
      if (singleToken.tp <= topVerbatimTokenVariant || singleToken.tp == tokWord) {
         cm->i++; // CONSUME the single literal
         return exprSingleItem(singleToken, cm);
      }
   }
   return exprUpTo(sentinel, loc, tokens, cm);
}

private TypeId //:pExprWorker
pExprWorker(Token tok, Int sentinel, TOKENS, CM) {
// Precondition: we are looking 1 past the first token of expr, which is the first parameter.
// Consumes 1 or more tokens. Handles single items also Returns the type of parsed expression
   if (tok.tp == tokStmt || tok.tp == tokParens) {
      if (tok.pl2 == 1) {
         Token singleToken = tokens[cm->i];
         if (singleToken.tp <= topVerbatimTokenVariant || singleToken.tp == tokWord) {
            // [stmt 1, tokInt]
            cm->i++; // CONSUME the single literal token
            return exprSingleItem(singleToken, cm);
         }
      }

      return exprUpTo(sentinel, interOf(tok), tokens, cm);
   } else {
      return exprSingleItem(tok, cm);
   }
}

private void //:pExpr
pExpr(Token tok, Int sentinel, TOKENS, CM) { pExprWorker(tok, sentinel, tokens, cm); }

private void //:closeParseFrames
closeParseFrames(CM) {
// When we are at the end of a function parsing a parse frame, we might be at the end of said frame
// (otherwise => we've encountered a nested frame, like in "1 + { x = 2; x + 1}"),
// in which case this function handles all the corresponding stack poppin'.
// It also always handles updating all inner frames with consumed tokens
// This is safe to call anywhere, pretty much
   while (cm->parseFrames->len > 0) { // loop over subscopes and expressions inside FunctionDef
      ParseFrame frame = last(cm->parseFrames);
      if (cm->i < frame.sentinel)
         { return; }
#ifdef DEBUG //{{{
      if (cm->i > frame.sentinel) {
         d("Span inconsistency i %d  frame.level %d frame.sentinelToken %d startInd %d",
            cm->i, frame.sentinel, frame.level, frame.startNodeInd);
      }
      VALIDATEI(cm->i == frame.sentinel, ierrInconsistentSpans)
#endif //}}}
      popAParseFrame(cm);
   }
}

private void //:parseUpTo
parseUpTo(Int sentinelToken, TOKENS, CM) {
// Parses anything from current cm->i to "sentinelToken"
   while (cm->i < sentinelToken) {
      Token currTok = tokens[cm->i];
      Int sentinel = calcSentinel(currTok, cm->i);
      cm->i++;
      (PARSE_TABLE[currTok.tp])(currTok, sentinel, tokens, cm);
      closeParseFrames(cm);
   }
}

private void //:pAlias
pAlias(Token tok, Int sentinel, TOKENS, CM) {
   throwExcParser(pError0(errTemp));
}

private void //:pAssert
pAssert(Token tok, Int sentinel, TOKENS, CM) {
   throwExcParser(pError0(errTemp));
}

private Node //:breakContinue
breakContinue(Token tok, TOKENS, CM) {
// Returns the number of levels to break/continue to, or 1 if there weren't any specified
// For continue, the number is increased by BIG. Consumes no nodes.
   VALIDATEP(tok.pl2 <= 1, pError0(errBreakContinueTooComplex));
   Bool const isContinue = tok.pl1 == 1;

   Int unwindDepth = 1;
   if (tok.pl2 > 0) {
      Token nextTok = tokens[cm->i];
      VALIDATEP(nextTok.tp == tokInt && nextTok.pl1 == 0 && nextTok.pl2 > 0,
                pError0(errBreakContinueInvalidDepth)
      )
      unwindDepth = nextTok.pl2;
   }
   Int const fullUnwindDepth = unwindDepth;

   Int j = cm->parseFrames->len - 1;
   for (; j > -1 && unwindDepth > 0; j--) {
      if (cm->parseFrames->c[j].level == pfrLoop)
         { unwindDepth--; }
   }
   if (unwindDepth > 0)
      { throwExcParser(pError(errBreakContinueInvalidDepth, nameErr(fullUnwindDepth))); }

   if (isContinue) {
      // for codegen, we need to mark any loop with steppers being "continue"d to
      ParseFrame loopFrame = cm->parseFrames->c[j + 1];
      Node forNode = cm->ast.c[loopFrame.startNodeInd];
      if (forNode.pl1 < BIG) {
         cm->ast.c[loopFrame.startNodeInd].pl1 += BIG;
      }
   }
   return (Node){.tp = nodBreakCont, .pl1 = fullUnwindDepth, .pl2 = 0, .pl3 = isContinue ? 1 : 0 };
}

private void //:pBreakCont
pBreakCont(Token tok, Int sentinel, TOKENS, CM) {
   Node breakContNode = breakContinue(tok, tokens, cm);
   newNode(breakContNode, interOf(tok), cm);
   cm->i = calcSentinel(tok, cm->i - 1); // CONSUME the whole break statement
}

private void //:pMeta
pMeta(Token tok, Int sentinel, TOKENS, CM) {
   throwExcParser(pError0(errMetaOnlyInArr));
}

private void
parseCatch(Token tok, Int sentinel, TOKENS, CM) {
   throwExcParser(pError0(errTemp));
}

private void parseDefer(Token tok, Int sentinel, TOKENS, CM) {
   throwExcParser(pError0(errTemp));
}


private void pTrait(Token tok, Int sentinel, TOKENS, CM) {
   throwExcParser(pError0(errTemp));
}


private void parseImpl(Token tok, TOKENS, CM) {
   throwExcParser(pError0(errTemp));
}


private void parseLambda(Token tok, TOKENS, CM) {
   throwExcParser(pError0(errTemp));
}


private void parseLambda1(Token tok, TOKENS, CM) {
   throwExcParser(pError0(errTemp));
}


private void parseLambda2(Token tok, TOKENS, CM) {
   throwExcParser(pError0(errTemp));
}


private void parsePackage(Token tok, TOKENS, CM) {
   throwExcParser(pError0(errTemp));
}

private void //:pReturn
pReturn(Token tok, Int sentinel, TOKENS, CM) {
   Int lenTokens = tok.pl2;
   if (lenTokens == 0) {
      newNode((Node){.tp = nodReturn}, interOf(tok), cm);
      return;
   }

   Int j = cm->parseFrames->len - 1;

   while (j > -1 && cm->parseFrames->c[j].level != pfrFn)
      { j--; }
   TypeId fnTy = cm->parseFrames->c[j].typeId;
   add(((ParseFrame){ .level = 0, .startNodeInd = cm->ast.len,
                  .sentinel = sentinel }), cm->parseFrames);
   newNode((Node){.tp = nodReturn}, interOf(tok), cm);

   Token rTk = tokens[cm->i];
   ChInterval loc = {.startBt = rTk.startBt, .lenBts = tok.lenBts - rTk.startBt + tok.startBt};
   TypeId const exprTy = exprHeadless(sentinel, loc, tokens, cm);
   VALIDATEP(exprTy.v > -1, pError0(errReturn))
   TypeId const returnType = tFunctionReturnType(fnTy, cm);
   VALIDATEP(eq(returnType, exprTy),
      pError(errTypeWrongReturnType, typeErr2(returnType, exprTy))
   );
}

private void //:importVars
importVars(Arr(Var) impts, Int const countVars, CM) {
   for (int j = 0; j < countVars; j++) {
      Var const ent = impts[j];

      if (cm->activeBindings[ent.name] != -1) {
         d("already active @ %d bind %d", ent.name, cm->activeBindings[ent.name]);
#ifdef DEBUG
         printName(ent.name, cm);
#endif
      }
      VALIDATEP(cm->activeBindings[ent.name] == -1,
         pError(errAssignmentShadowing, nameErr(ent.name))
      )
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
   lx->stats.toksLen = lx->tokens.len;
   VALIDATEL(lx->lexBtrack->len == 0, lexError(errPunctuationExtraOpening))
}

private Compiler* //:lexicallyAnalyzeInner
lexicallyAnalyzeInner(Compiler* lx, Arena* a) {
// Main lexer function. Precondition: the input Byte array has been prepended
// with StandardText
   Int const inpLength = lx->stats.inpLength;
   Arr(char const) inp = lx->sourceCode.c;
   VALIDATEL(inpLength > 0, lexError(errEmptySourceCode))

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

internal Compiler* //:lexicallyAnalyze
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
      .currChunk = firstChunk, .currScopeLen = 0, .start = firstChunk->c, .curr = firstChunk->c,
      .countScopes = 0
   };
}

private void //:addBinding
addBinding(NameId name, Int bindingId, CM) {
   Scopes* const s = &(cm->scopes);
   *(s->curr) = name;
   scopesMoveForward(s, cm);
   s->currScopeLen++;
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
   TypeId r = mergeTypeWorker(startInd, lenInts, cm);
   return r;
}

private TypeId //:addConcrFnType
addConcrFnType(Int arity, Arr(Int) paramsAndReturn, CM) {
// Function types are stored as: (paramType1, paramType2, ..., returnType)
   TypeId newInd = typeOf(cm->types.len);
   pushIntypes(TYPE_PREFIX + arity, cm);
   typeAddHeader(
      (TypeHeader){ .sort = sorDeclare, .arity = arity + 1,
         .name = nameOfStd(strF), .isGeneric = false, .size = 8 },
      cm
   );
   for (Int k = 0; k <= arity; k++) { // <= because there are (arity + 1) elts - the return type!
      pushIntypes(paramsAndReturn[k], cm);
   }
   return mergeType(newInd, cm);
}

private void //:importGenericTypesForLists
importGenericTypesForLists(OUT TypeId* arrayLength, OUT TypeId* listLength, OUT TypeId* listAdd, CM) {
// Types for the generic array and its `#` function: `A $T -> Int`. Also for
// generic list and its `add` function: `L $T, $T -> Void` as well as its `#` function
   // the $0 type
//~   TypeId tentativeType = typeOf(cm->types.len);
//~   pushIntypes(TYPE_PREFIX - 1, cm);
//~   typeAddHeader(((TypeHeader){ .sort = sorGenericParam, .arity = 0, .name = 0,
//~        .isGeneric = true }), cm);
//~   TypeId p0 = mergeType(tentativeType, cm);

   // # (length): A $0 -> Int
   TypeId tentativeType = typeOf(cm->types.len);
   pushIntypes(TYPE_PREFIX + 1, cm);
   typeAddHeader(
      ((TypeHeader){ .sort = sorTypeCall, .arity = 2, .name = nameOfStd(strF),
         .isGeneric = true, .size = 8 }),
      cm
   );
   pushIntypes(cm->stats.arrayType, cm);
   pushIntypes(tokInt, cm);
   *arrayLength = mergeType(tentativeType, cm);


   // # (length) L $T -> Int
   tentativeType = typeOf(cm->types.len);
   pushIntypes(TYPE_PREFIX + 1, cm);
   typeAddHeader(
      ((TypeHeader){ .sort = sorTypeCall, .arity = 2, .name = nameOfStd(strF),
         .isGeneric = true, .size = 8 }),
         cm
   );
   pushIntypes(cm->stats.listType, cm);
   pushIntypes(tokInt, cm);
   *listLength = mergeType(tentativeType, cm);

   // the type of add: [L $T] $T -> Void
   tentativeType = typeOf(cm->types.len);
   pushIntypes(TYPE_PREFIX + 2, cm);
   typeAddHeader(
      ((TypeHeader){ .sort = sorTypeCall, .arity = 3, .name = nameOfStd(strF),
         .isGeneric = true, .size = 8 }),
         cm
   );
   pushIntypes(cm->stats.listType, cm);
   pushIntypes(-nameOfStd(strTypeVarT) - 1, cm); // the generic param $T
   pushIntypes(voidType, cm);
   *listAdd = mergeType(tentativeType, cm);
}

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

private void //:buildPreludeTypes
buildPreludeTypes(CM) {
// Creates the built-in types in the proto compiler
   // primitive types up to topVerbatimType (inclusive)
   for (int i = strInt; i <= strVoid; i++) {
      cm->activeBindings[nameOfStd(i)] = i - strInt;
      pushIntypes(0, cm);
   }
   pushIntypes(0, cm); //empty type for "outerTypeForTypeParam"

   // Array
   Int typeIndA = cm->types.len;
   pushIntypes(TYPE_PREFIX + 3, cm); // 3 = 4 - 1, since header size = TYPE_PREFIX - 1
   NameId name = nameOfStd(strArr);
   typeAddHeader(((TypeHeader){
      .sort = sorDeclare, .isGeneric = true, .arity = 2, .name = name, .size = 16}), cm
   );
   pushIntypes(-1, cm); // dummy value for the raw pointer, not to be used within Eyr
   pushIntypes(tokInt, cm);
   pushIntypes(cm->genericFields.len, cm);
   pushIntypes(-nameOfStd(strTypeVarT) - 1, cm); // the generic param $T
   pushInfieldNames(((FieldName){.name = -1, .access = accessPrivImm}), cm);
   pushInfieldNames(((FieldName){.name = nameOfStd(strLen), .access = accessPubImm}), cm);

   cm->activeBindings[name] = typeIndA;
   cm->stats.arrayType = typeIndA;

   // List
   Int typeIndL = cm->types.len;
   pushIntypes(TYPE_PREFIX + 4, cm); // 4 = 5 - 1, since header size = TYPE_PREFIX - 1
   name = nameOfStd(strL);
   typeAddHeader(((TypeHeader){
      .sort = sorDeclare, .arity = 3, .isGeneric = true, .name = name, .size = 16 }),
      cm
   );
   pushIntypes(-1, cm); // dummy value for the raw pointer, not to be used within Eyr
   pushIntypes(tokInt, cm);
   pushIntypes(tokInt, cm);
   pushIntypes(cm->genericFields.len, cm);
   pushIntypes(-nameOfStd(strTypeVarT) - 1, cm); // the generic param $T
   pushInfieldNames(((FieldName){.name = -1, .access = accessPrivImm}), cm);
   pushInfieldNames(((FieldName){.name = nameOfStd(strLen), .access = accessPubImm}), cm);
   pushIngenericFields(((FieldName){.name = nameOfStd(strCap), .access = accessPubImm}), cm);
   cm->activeBindings[name] = typeIndL;
   cm->stats.listType = typeIndL;
   // no need to merge the types as they are surely unique

   cm->stats.voidToVoidType = addConcrFnType(0, (Int[]){ voidType}, cm).v;
}

private void //:buildOper
buildOper(Int operId, TypeId typeId, Emit emit, CM) {
//Creates an entity, pushes it to @rawOverloads and activates its name
   FunctionId newFnId = cm->functions.len;
   pushInfunctions(
      (Function){ .typeId = typeId, .name = OPERATORS[operId].name, .emit = emit, },
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
   TypeId douOfDouDou    = addConcrFnType(2, (Int[]){ tokDouble, tokDouble, tokDouble}, cm);
   TypeId douOfDou       = addConcrFnType(1, (Int[]){ tokDouble, tokDouble}, cm);

   // !. // dummy host name
   buildOper(opBitwiseNeg,   intOfInt, emitBitNegate, cm);
   buildOper(opNotEqual,     boolOfIntInt, emitNotEq, cm);
   buildOper(opNotEqual,     boolOfDoubDoub, emitNotEq, cm);
   buildOper(opNotEqual,     boolOfStrStr, emitNotEq, cm); //TODO
   buildOper(opSize,         intOfStr, emitNotEq, cm); // #
   buildOper(opSize,         intOfInt, emitNotEq, cm);
   buildOper(opToString,     strOfInt, emitNotEq, cm); // $
   buildOper(opToString,     strOfBool, emitNotEq, cm);
   buildOper(opToString,     strOfFloat, emitNotEq, cm);
   buildOper(opRemainder,    intOfIntInt, emitModulo, cm); // %
   buildOper(opBitwiseAnd,   intOfIntInt, emitBitAnd, cm); // &&.
   buildOper(opBoolAnd,      boolOfBoolBool, emitLogicAnd, cm); // &&
   // ' dummy type, this oper is type-level
   buildOper(opRef,          intOfIntInt, emitNotEq, cm);
   buildOper(opTimesExt,     douOfDouDou, emitNotEq, cm);
   buildOper(opTimes,        intOfIntInt, emitMultiply, cm);
   buildOper(opTimes,        douOfDouDou, emitMultiply, cm);
   buildOper(opPlusExt,      strOfStrStr, emitNotEq, cm);
   buildOper(opPlus,         intOfIntInt, emitAdd, cm);
   buildOper(opPlus,         douOfDouDou, emitAdd, cm);
   buildOper(opPlus,         strOfStrStr, emitAdd, cm);
   buildOper(opMinusExt,     intOfIntInt, emitNotEq, cm);
   buildOper(opMinus,        intOfIntInt, emitSubtract, cm);
   buildOper(opMinus,        douOfDouDou, emitSubtract, cm);
   buildOper(opNegate,       intOfInt, emitNegate, cm);
   buildOper(opNegate,       douOfDou, emitNegate, cm);
   buildOper(opDivByExt,     intOfIntInt, emitNotEq, cm);
   // dummy, oper is type-level
   buildOper(opIntersect,    intOfIntInt, emitNotEq, cm);
   buildOper(opDivBy,        intOfIntInt, emitDivide, cm);
   buildOper(opDivBy,        douOfDouDou, emitDivide, cm);
   buildOper(opBitShiftL,    intOfIntInt, emitBitLeftShift, cm);
   buildOper(opComparator,   intOfIntInt, emitNotEq, cm);
   buildOper(opComparator,   intOfDoubDoub, emitNotEq, cm);
   buildOper(opComparator,   intOfStrStr, emitNotEq, cm);
   buildOper(opLTZero,       boolOfInt, emitNotEq, cm);
   buildOper(opLTZero,       boolOfDoub, emitNotEq, cm);
   buildOper(opLTZero,       boolOfStr, emitNotEq, cm);
   buildOper(opLTEQ,         boolOfIntInt, emitLessThanOrEq, cm);//<=
   buildOper(opLTEQ,         boolOfDoubDoub, emitLessThanOrEq, cm);
   buildOper(opLTEQ,         boolOfStrStr, emitLessThanOrEq, cm);
   buildOper(opLessTh,       boolOfIntInt, emitLessThan, cm);
   buildOper(opLessTh,       boolOfDoubDoub, emitLessThan, cm);
   buildOper(opLessTh,       boolOfStrStr, emitLessThan, cm);
   buildOper(opRefEquality,  boolOfIntInt, emitNotEq, cm);
   buildOper(opEquality,     boolOfIntInt, emitEq, cm);
   buildOper(opBitShiftR,    boolOfBoolBool, emitNotEq, cm);
   buildOper(opGTZero,       boolOfInt, emitNotEq, cm);
   buildOper(opGTZero,       boolOfDoub, emitNotEq, cm);
   buildOper(opGTZero,       boolOfStr, emitNotEq, cm);
   buildOper(opGTEQ,         boolOfIntInt, emitGreaterThanEq, cm);
   buildOper(opGTEQ,         boolOfDoubDoub, emitGreaterThanEq, cm);
   buildOper(opGTEQ,         boolOfStrStr, emitNotEq, cm);
   buildOper(opGreaterTh,    boolOfIntInt, emitGreaterThan, cm);
   buildOper(opGreaterTh,    boolOfDoubDoub, emitGreaterThan, cm);
   buildOper(opGreaterTh,    boolOfStrStr, emitGreaterThan, cm);
   buildOper(opNullCoalesce, intOfIntInt, emitNotEq, cm); // ?:
   buildOper(opQuestionMark, intOfIntInt, emitNotEq, cm); // dummy, type
   buildOper(opBitwiseXor,   intOfIntInt, emitBitXor, cm);
   buildOper(opBitwiseOr,    intOfIntInt, emitBitOr, cm);
   buildOper(opBoolOr,       boolOfBoolBool, emitLogicOr, cm);
   buildOper(opBoolNot,      boolOfBool, emitNegate, cm); // not
   buildOper(opGetElem,      douOfDou, emitNotEq, cm); // dummy
}

private void //:createBuiltinsForProto
createBuiltinsForProto(CM) {
// Entities and functions for the built-in operators, types and functions
   buildStandardStrings(cm);
   cm->activeBindings = allocateArray(cm->names->len, Int, cm->a),
   memset(cm->activeBindings, 0xFF, 4*cm->names->len);

   buildPreludeTypes(cm);
   buildOperators(cm);
   cm->stats.countOperatorFns = cm->functions.len;
}

private void //:importPrelude
importPrelude(CM) {
// Imports the standard, Prelude stuff into the compiler immediately after the lexing phase
   TypeId const strToVoid = addConcrFnType(1, (Int[]){ tokString, voidType }, cm);
   TypeId const intToVoid = addConcrFnType(1, (Int[]){ tokInt, voidType }, cm);
   TypeId const douToVoid = addConcrFnType(1, (Int[]){ tokDouble, voidType }, cm);

   // Generic functions
   TypeId arrayLength, listAdd, listLength;
   importGenericTypesForLists(OUT& arrayLength, OUT &listLength, OUT &listAdd, cm);

   // Array length
   Int lengthFnId = cm->functions.len;
   pushInfunctions(
      (Function){ .name = opSize, .typeId = arrayLength, .genericInd = -1, .tokenInd = -1,
                  .access = accessPubImm, .emit = emitArrayLen },
      cm
   );
   addRawOverload(opSize, arrayLength, lengthFnId, cm);

   // List length
   lengthFnId = cm->functions.len;
   pushInfunctions(
      (Function){ .name = opSize, .typeId = listLength, .genericInd = -1, .tokenInd = -1,
                  .access = accessPubImm, .emit = emitListLen },
      cm
   );
   addRawOverload(opSize, listLength, lengthFnId, cm);

   //TypeId intToDoub = addConcrFnType(1, (Int[]){ tokInt, tokDouble}, cm);
   //TypeId doubToInt = addConcrFnType(1, (Int[]){ tokDouble, tokInt}, cm);
   Var constImports[2] = {
      (Var){
         .name = nameOfStd(strMathPi), .typeId = tokDouble, .access = accessPrivImm, .fnId = -1
      },
      (Var){
         .name = nameOfStd(strMathE), .typeId = tokDouble, .access = accessPrivImm, .fnId = -1
      },
   };

   Int const genericInd = listCreateMultiAssocList(cm->functionMonos); // for the generic list "add"
   Function fnImports[5] =  {
      (Function){ .name = nameOfStd(strPrint), .access = accessPrivImm, .emit = emitPrintInt,
         .typeId = intToVoid, .needsMangling = true },
      (Function){ .name = nameOfStd(strPrint), .access = accessPrivImm, .emit = emitPrintDou,
         .typeId = douToVoid, .needsMangling = true },
      (Function){ .name = nameOfStd(strPrint), .access = accessPrivImm, .emit = emitPrintStr,
         .typeId = strToVoid, .needsMangling = true },
      (Function){ .name = nameOfStd(strAdd), .typeId = listAdd, .genericInd = genericInd,
         .tokenInd = -1, .access = accessPrivImm, .emit = emitParsed },
      (Function){ .name = nameOfStd(strPrintErr),
         .access = accessPrivImm, .typeId = strToVoid }
      // TODO functions for casting (int, double, unsigned)
   };

   // These primitive types occupy the first places in the names and in the types table.
   // So for them nameId == typeId, unlike type funcs like L(ist) and A(rray)
   for (Int j = strInt; j <= strVoid; j++) {
      cm->activeBindings[j - strInt + countOperators] = j - strInt;
   }
   importVars(AARG(constImports, Var), cm);
   importFns(AARG(fnImports, Function), cm);
   cm->stats.firstParsedType = cm->types.len;
}

internal Compiler* //:createLexer
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
      .reorderBuf = createLToken(16, a),
      .stats = PROTO.stats,
      .errors = createLCompileError(4, a),
      .a = a, .aTmp = aTmp
   };
   pushInnewlines(0, lx);
   pushInnewlines(sizeof(standardText) - 1, lx);
   lx->stats.inpLength = sourceCode.len + (prependStandardText ? (sizeof(standardText) - 1) : 0);
   return lx;
}

internal void //:initializeParser
initializeParser(Compiler* lx, Arena* a) {
// Turns a lexer into a parser. Initializes all the parser & typer stuff after lexing is done

   if (lx->errors->len > 0)
      { return; }

   Compiler* cm = lx;
   Arena* aTmp = lx->aTmp;
   Int initNodeCap = lx->tokens.len > 64 ? lx->tokens.len : 64;
   cm->scopes = createScopes(aTmp);
   cm->parseFrames = createLParseFrame(16, aTmp);
   cm->i = 0;

   cm->ast = createInListNode(initNodeCap, a);
   cm->sourceLocs = createLSourceLoc(initNodeCap, a);
   cm->functionMonos = createMultiAssocList(a);

   cm->expr = (Expr) {
      .exp = createLInt(16, cm->aTmp),
      .frames = createLExprFrame(16, aTmp),
      .scr = (LNode){.c = allocateArray(16, Node, aTmp), .len = 0, .cap = 16},
      .locsScr = (LChInterval){.c = allocateArray(16, ChInterval, aTmp), .len = 0, .cap = 16},
      .reorderKeys = (LInt){.c = allocateArray(4, Int, aTmp), .len = 0, .cap = 4},
      .reorderBuf = (LNode){.c = allocateArray(16, Node, aTmp), .len = 0, .cap = 4}
   };

   cm->rawOverloads = copyMultiAssocList(PROTO.rawOverloads, cm->aTmp);
   cm->overloads = (InListInt){.len = 0, .c = null};

   cm->activeBindings = allocateArray(lx->names->len, Int, lx->aTmp);
   memcpy(cm->activeBindings, PROTO.activeBindings, 4*PROTO.names->len);
   // need to write "-1" to all the bindings not present in the proto compiler
   memset(cm->activeBindings + PROTO.names->len, 0xFF, 4*(lx->names->len - PROTO.names->len));

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

   cm->fieldNames = createInListFieldName(PROTO.fieldNames.len, a);
   memcpy(cm->fieldNames.c, PROTO.fieldNames.c, PROTO.fieldNames.len*sizeof(FieldName));
   cm->fieldNames.len = PROTO.fieldNames.len;
   
   cm->fieldTypes = createInListInt(PROTO.fieldTypes.len, a);
   memcpy(cm->fieldTypes.c, PROTO.fieldTypes.c, PROTO.fieldTypes.len*4);
   cm->fieldTypes.len = PROTO.fieldTypes.len;

   cm->typesDict = copyStringDict(PROTO.typesDict, a);

   cm->importNames = createInListInt(8, lx->aTmp);
   cm->toplevels = createInListInt(8, lx->a);
   cm->monos = createLMonomorphization(16, lx->a);

   cm->tParse = (TParse) {
      .exp = createLInt(16, cm->aTmp),
      .frames = createLTypeFrame(16, cm->aTmp),
      .names = (LInt){.c = allocateArray(16, Int, cm->aTmp), .len = 0, .cap = 16},
      .paramNames = (LInt){.c = allocateArray(4, Int, cm->aTmp), .len = 0, .cap = 4},
      .tParams = (LInt){.c = allocateArray(16, Int, cm->aTmp), .len = 0, .cap = 16},
      .tmp = createLInt(16, cm->aTmp),
      .genericWalk = createLTypeLoc(16, cm->aTmp),
      .concreteWalk = createLTypeLoc(16, cm->aTmp),
   };
   cm->entrypoint = -1;

   importPrelude(cm);
}

private void //:validateNameOverloads
validateNameOverloads(Int listId, Int countOverloads, NameId name, CM) {
// Validates the overloads for a name don't intersect via their outer types
// 1. First parameter outer types must be unique
// 2. A nullary function, if any, must be unique
// 3. If a blanket overload (outerTypeForTypeParam) then the only other acceptable one is 0-arity
   Arr(Int) ov = cm->overloads.c;
   Int start = listId + 1;
   Int const outerSentinel = start + countOverloads;
   if (ov[start] == outerTypeForTypeParam)
      { VALIDATEP(outerSentinel == start + 1, pError0(errTypeOverloadsIntersect)); }

   Int o = start + 1;
   for (Int prevOuter = ov[start]; o < outerSentinel; prevOuter = ov[o], o++) {
#if defined(VERBOSE) && defined(DEBUG) //{{{
      if (ov[o] == prevOuter) {
         d("Overload intersection for name %d ov[k] %d prevOuter %d @o = %d countOvers %d",
            name, ov[o], prevOuter, o, countOverloads);
         printf("Name: ");
         printName(name, cm);
         dbgOverloads(name, cm);
      }
#endif //}}}
      VALIDATEP(ov[o] != prevOuter, pError0(errTypeOverloadsIntersect))
   }
}

private Int //:createNameOverloads
createNameOverloads(NameId name, CM) {
// Creates a final subtable in @overloads for a name and returns the index of said subtable.
// Precondition: @rawOverloads contain twoples of (typeId fnId)
// (typeId = the full type of a function).
// (yes, "twople" = tuple of two)
// Postcondition: @overloads will contain a subtable of length(outerTypeIds)(fnIds)

   Arr(Int) raw = cm->rawOverloads->c;
   Int const listId = -cm->activeBindings[name] - 2;

   Int const rawStart = listId + 2;

   VALIDATEI(rawStart != -1, ierrImportedFnNotInScope)
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
      ov[k + countOverloads] = raw[j + 1]; // fnId
   }
   Int const sentinel = newInd + 1 + 2*countOverloads;
   sortPairsDistant(newInd + 1, sentinel, countOverloads, ov);

   if (countOverloads > 1) {
      validateNameOverloads(newInd, countOverloads, name, cm);
      for (Int j = newInd + 2 + countOverloads; j < sentinel; j++) {
         // +2 because in every overload group one function may keep its original name
         cm->functions.c[ov[j]].needsMangling = true;
      }
   }
   return newInd;
}

internal void //:createOverloads
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
      if (name < countOperators) // operator overloads were created above
         { continue; }

      Int newIndex = createNameOverloads(name, cm);
      cm->activeBindings[name] = -newIndex - 2;
   }
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
      if (tok.tp == tokAssignment && tok.pl1 == assiTypeDefinition) {
         Int sentinel = calcSentinel(tok, cm->i);
         cm->i++; // CONSUME the def token
         pTypeDef(sentinel, toks, cm);
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
      Int sentinel = calcSentinel(tok, cm->i);
      if (tok.tp == tokAssignment) {
         cm->i++; // CONSUME the tokAssignment
         Assignment assi = pPreparseAssignment(cm->i, sentinel, toks, cm);
         pAssignmentWorker(tok, assi, toks, cm);
      } else { // tokToplevelFn
         cm->i = sentinel;
      }
   }
}

#ifdef DEBUG

private void
validateOverloadsFull(CM) {
/*
   Int lenTypes = cm->types.len; Int lenEntities = cm->vars.len;
   for (Int i = 1; i < cm->overloadIds.len; i++) {
      Int currInd = cm->overloadIds.c[i - 1];
      Int nextInd = cm->overloadIds.c[i];

      VALIDATEI((nextInd > currInd + 2) && (nextInd - currInd) % 2 == 1, ierrOverloadsIncoherent)

      Int countOverloads = (nextInd - currInd - 1)/2;
      Int countConcreteOverloads = cm->overloads.c[currInd];
      VALIDATEI(countConcreteOverloads <= countOverloads, ierrOverloadsIncoherent)
      for (Int j = currInd + 1; j < currInd + countOverloads; j++) {
         if (cm->overloads.c[j] < 0) {
            throwExcInternal(ierrOverloadsNotFull);
         }
         if (cm->overloads.c[j] >= lenTypes) {
            throwExcInternal(ierrOverloadsIncoherent);
         }
      }
      for (Int j = currInd + countOverloads + 1; j < nextInd; j++) {
         if (cm->overloads.c[j] < 0) {
            print("ERR overload missing entity currInd %d nextInd %d j %d cm->overloads.c[j] %d", currInd, nextInd,
               j, cm->overloads.c[j])
            throwExcInternal(ierrOverloadsNotFull);
         }
         if (cm->overloads.c[j] >= lenEntities) {
            throwExcInternal(ierrOverloadsIncoherent);
         }
      }
   }
*/
}

#endif

private void //:pFnSignature
pFnSignature(Token tokToplevel, TOKENS, CM) {
// Parses a function signature. Emits no nodes, adds data to @toplevels, @functions, @overloads.
// Pre-condition: we are right past tokToplevelFn
   Int const tokenInd = cm->i - 1;

   Token nameTk = tokens[cm->i];
   VALIDATEP(nameTk.tp == tokWord && nameTk.pl2 == 0 || nameTk.tp == tokOperator,
      pError0(errFnSignature)
   )
   NameId name = nameTk.pl1;

   cm->i++; // CONSUME the function name
   Token secondTk = tokens[cm->i];

   TypeId fnType = typeOf(cm->stats.voidToVoidType);

   Bool isGeneric = false;
   Int arity = 0;
   if (secondTk.tp == tokType) {
      fnType = tParse(calcSentinel(secondTk, cm->i), OUT &isGeneric, tokens, cm);
      TypeHeader hdr = typeReadHeader(fnType, cm);
      isGeneric = hdr.isGeneric;
      arity = hdr.arity - 1;
   }
   if (nameTk.tp == tokOperator) {
      Int operArity = OPERATORS[name].prec == precUnary ? 1 : 2;
      VALIDATEP(OPERATORS[name].overloadable,
         pError(errFnOperatorNotOverloadable, operatorErr(name))
      )
      VALIDATEP(arity == operArity, pError(errFnOperatorOverlArity, numberErr2(operArity, arity)))
   }

   FunctionId const newFnId = cm->functions.len;

   Int genericInd = isGeneric ? listCreateMultiAssocList(cm->functionMonos) : -1;
   pushInfunctions(((Function){
         .name = nameTk.pl1, .typeId = fnType, .genericInd = genericInd, .tokenInd = tokenInd,
         .access = accessPrivImm, .emit = emitParsed
      }),
      cm
   );
   if (name == nameOfStd(strMain)) {
      VALIDATEP(cm->entrypoint == -1, pError0(errFnEntrypoint));
      cm->entrypoint = newFnId;
   }
   addRawOverload(name, fnType, newFnId, cm);
   pushIntoplevels(newFnId, cm);
}

private void //:pToplevelBodyWorker
pToplevelBodyWorker(
      Int tokenInd, Int funcOrMonoId, TypeId concreteType, Int arity, TOKENS, CM
) {
   cm->i = tokenInd + 2; // skipping the tokToplevelFn and tokWord (fn name)
   if (tokens[cm->i].tp == tokType)
      { cm->i = calcSentinel(tokens[cm->i], cm->i); } // skipping the function type

   Token fnTk = tokens[cm->i];
   Int const fnSentinel = calcSentinel(fnTk, cm->i);
   openFnScope(funcOrMonoId, concreteType, fnTk, fnSentinel, cm);
   cm->i++; // CONSUME the tokFn token

   if (arity > 0) {
      VALIDATEP(tokens[cm->i].tp == tokStmt && tokens[cm->i].pl2 == arity,
         pError0(errFnParamList)
      );
   } else {
      goto bodyParsing;
   }
   Int const paramsSentinel = calcSentinel(tokens[cm->i], cm->i);
   cm->i++; // CONSUME the tokStmt for param list

   for (
      Int t = tGetIndexOfFnFirstParam(concreteType, cm).v;
      cm->i < paramsSentinel;
      cm->i++, t++
   ) {// must get params type from the concrete function type we got, not
      // from tokens (where they may be generic)
      Token paramNameTk = tokens[cm->i];
      TypeId paramType = typeOf(cm->types.c[t]);
      NameId name = paramNameTk.pl1;
      VarId newVarId = createVarWithType(
            name, paramType, paramNameTk.pl2 == 1 ? accessPrivMut : accessPrivImm, -1, cm
      );
      newNode(
            ((Node){.tp = nodVar, .pl1 = newVarId, .pl2 = 0, .pl3 = assiFnParam}),
            interOf(paramNameTk), cm
      );
   }

   bodyParsing:
   parseUpTo(fnSentinel, tokens, cm);
}

private void //:pToplevelBody
pToplevelBody(FunctionId fnId, TOKENS, CM) {
// Parses a top-level function. The result is the AST [ FnDef ParamList body... ]
// Uses the function's type to introduce local vars for the function params
   cm->functions.c[fnId].nodeInd = cm->ast.len;
   Function fn = cm->functions.c[fnId];
   TypeId fnType = fn.typeId;
   TypeHeader hdr = typeReadHeader(fnType, cm);
   if (hdr.isGeneric) // generic functions arn't parsed, only their monomorphizations
      { return; }

   pToplevelBodyWorker(fn.tokenInd, fnId, fnType, hdr.arity - 1, tokens, cm);
}

void //:generateMonomorphizations
generateMonomorphizations(TOKENS, CM) {
// Generate function bodies for monomorphizations
   for (Monomorphization* m = cm->monos->c; m < cm->monos->c + cm->monos->len; m++) {
      if (m->tokenInd != -1) { // parsed functions
         m->nodeInd = cm->ast.len;
         TypeId concrete = cm->functions.c[m->fnId].typeId;
         pToplevelBodyWorker(
            m->tokenInd, m - cm->monos->c, concrete, typeReadHeader(concrete, cm).arity - 1,
            tokens, cm
         );
      } else { // imported host functions
         Int newFnId = cm->functions.len;
         pushInfunctions(
            ((Function){ .name = cm->functions.c[m->fnId].name,
                         .typeId = cm->functions.c[m->fnId].typeId,
                         .emit = cm->functions.c[m->fnId].emit,
                         .nodeInd = -1, .genericInd = -1, .tokenInd = -1 }),
            cm
         );
         pushIntoplevels(newFnId, cm);
         m->fnId = newFnId;
      }
   }
}

private void //:pFunctionBodies
pFunctionBodies(TOKENS, CM) {
// Parses top-level function params and bodies
   for (int j = 0; j < cm->toplevels.len; j++) {
      pToplevelBody(cm->toplevels.c[j], tokens, cm);
   }
}

private void //:pToplevelSignatures
pToplevelSignatures(TOKENS, CM) {
// Walks the top-level functions' signatures (but not bodies). Increments counts of overloads
// Result: the overload counts and the list of toplevel functions to parse. No nodes emitted
   cm->i = 0;
   Int const len = cm->tokens.len;

   Int nextI = 0;
   for (Token tok = tokens[cm->i]; cm->i < len; cm->i = nextI, tok = tokens[nextI]) {
      nextI = calcSentinel(tok, cm->i);
      if (tok.tp != tokToplevelFn)
         { continue; }
      cm->i++; // CONSUME the tokToplevelFn
      pFnSignature(tok, tokens, cm);
   }
}

void //:parseMain
parseMain(CM, Arena* a) {
   if (setjmp(excBuf) == 0) {
      Arr(Token) toks = cm->tokens.c;
      //printLexer(cm);

      pToplevelTypes(cm);
      // This gives the complete overloads & overloadIds tables + list of toplevel functions
      pToplevelSignatures(toks, cm);
      createOverloads(cm);
      pToplevelConstants(cm);

#ifdef DEBUG
      validateOverloadsFull(cm);
#endif

      // The main parse (all top-level function bodies)
      pFunctionBodies(toks, cm);
      // Parse & typecheck all the necessary monomorphized versions of generic functions
      generateMonomorphizations(toks, cm);
      updateStats(cm);

      //printParser(cm);
      //dbgAllTypes(cm);
   } else {
#ifndef DEBUG
      d("Exception!");
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
         ((Unt)hdr.arity << 8)), cm);
   pushIntypes(hdr.name, cm);
   pushIntypes(hdr.size, cm);
}

private void //:typeExpAddHeader
typeExpAddHeader(TypeHeader hdr, TParse* te) {
// Writes the bytes for the type header to the tail of the cm->types table.
// Adds one 4-byte element
   add((Int)((Unt)((Unt)hdr.sort << 16) + ((Unt)hdr.arity << 8)), te->exp);
   add(hdr.name, te->exp);
   add(hdr.size, te->exp);
}

private TypeHeader //:typeReadHeader
typeReadHeader(TypeId t, CM) {
// Reads a type header from the type array. Does not work for primitive types
   Int tag = cm->types.c[t.v + 1];
   return (TypeHeader){ .isGeneric = (tag >> 24) > 0, .sort = ((Unt)tag >> 16) & LOWER16BITS,
         .arity = (tag >> 8) & 0xFF,
         .name = cm->types.c[t.v + 2],
         .size = cm->types.c[t.v + 3]
   };
}

TypeHeader //:libeyr_readTypeHeader
libeyr_readTypeHeader(TypeId t, Arr(Int) types) {
// Reads a type header from the type array. Does not work for primitive types
   Int tag = types[t.v + 1];
   return (TypeHeader){ .isGeneric = (tag >> 24) > 0, .sort = ((Unt)tag >> 16) & LOWER16BITS,
         .arity = (tag >> 8) & 0xFF, .name = types[t.v + 2],
         .size = types[t.v + 3]
   };
}

Int //:libeyr_sizeOfType
libeyr_sizeOfType(TypeId t, Arr(Int) types) {
   switch (t.v) {
   case tokInt: return 4;
   case tokLong:
   case tokDouble: return 8;
   case tokBool: return 1;
   case tokString: return 16;
   case tokMisc: return -1;
   default: {
      return types[t.v + 3];
   }
   }


}

Int //:libeyr_getFieldNameInd
libeyr_getFieldNameInd(TypeId t, TypeHeader hdr, Arr(Int) types) {
   return types[t.v + TYPE_PREFIX + hdr.arity];
}

TypeId //:libeyr_typeGetGenericArg
libeyr_typeGetGenericArg(TypeId t, TypeHeader hdr, Int indArg, Arr(Int) types) {
// (S Foo) => Foo. (F A -> B) => A
   if (hdr.name == nameOfStd(strF)) {
      if (hdr.arity == 1) {
         return typeOf(tokMisc); // void type
      } else {
         return typeOf(types[t.v + TYPE_PREFIX + indArg]);
      }
   } else if (hdr.sort == sorTypeCall) {
      // need to skip the prefix, field types, and the index in @genericFields
      // the +2 is: 1 for the index in @genericFields, and 1 for the generic outer type
      Int ind = t.v + TYPE_PREFIX + hdr.arity + 2 + indArg;
      return typeOf(types[ind]);
   } else {
      return typeOf(-1);
   }
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
   if (hdr.name == nameOfStd(strF) || hdr.sort == sorDeclare)
      { return t; }
   else { // sorTypeCall which is a struct or union
      // need to skip the prefix, field types, and the index in @genericFields
      Int ind = t.v + TYPE_PREFIX + hdr.arity + 1;
      return typeOf(cm->types.c[ind]);
   }
}

private TypeId //:tGetIndexOfFnFirstParam
tGetIndexOfFnFirstParam(TypeId fnType, CM) {
#ifdef DEBUG //{{{
   NameId name = typeReadHeader(fnType, cm).name;
   if (name != nameOfStd(strF))
      { d("A function is not a function! TypeId = %d", fnType); }
   VALIDATEI(name == nameOfStd(strF), ierrNotAFunction);
#endif //}}}
   return typeOf(fnType.v + TYPE_PREFIX);
}

private Int //:tIsFunction
tIsFunction(TypeId t, CM) {
// Returns the function's arity if the type is a function type, -1 otherwise
   if (t.v < topVerbatimType)
      { return -1; }
   TypeHeader hdr = typeReadHeader(t, cm);
   return (hdr.name == nameOfStd(strF)) ? (hdr.arity - 1) : -1;
}

private Bool //:tIsList
tIsList(TypeId t, CM) {
   TypeId outer = typeGetOuter(t, cm);
   return outer.v == cm->stats.listType || outer.v == cm->stats.arrayType;
}

private Int //:tGetBodyStart
tGetBodyStart(TypeId t, TypeHeader hdr) {
   if (hdr.name == nameOfStd(strF)) {
      return t.v + TYPE_PREFIX; // even in struct defs, fields are the body
   } else { // structs or struct type calls, need to skip the fields and field ind
      return t.v + TYPE_PREFIX + hdr.arity + 1 + (hdr.sort == sorTypeCall ? 1 : 0);
   }
}

private TypeLoc //:tGetBody
tGetBody(TypeId ty, TypeHeader hdr, CM) {
// A type location that covers the internal content of a type
// (i.e. type params etc, but not struct fields or field inds)
   return (TypeLoc){
      .currPos = tGetBodyStart(ty, hdr),
      .sentinel = ty.v + cm->types.c[ty.v] + 1//TYPE_PREFIX + hdr.arity
   };
}

void //:printType
printType(TypeId typeId, PrintableCompiler prc) {
// Print a single type fully for error-reporting purposes
   Int t = typeId.v;

   if (t <= topVerbatimType) {
      printNameNoLnCr(nameOfStd(strInt) + t, prc);
      printf(" ");
      return;
   } else {
      LTypeLoc* st = createLTypeLoc(16, prc.a);
      TypeLoc* top = null;

      add(((TypeLoc){ .currPos = t, .sentinel = t + prc.types[t] + 1 }), st);
      top = st->c;

      Bool atHeader = true;
      for (Int countIters = 0; top != null && countIters < 10; countIters++)  {
         Int typeVal = prc.types[top->currPos];
         if (atHeader) {
            TypeHeader currHdr = libeyr_readTypeHeader(typeOf(top->currPos), prc.types);
            top->currPos = tGetBodyStart(typeOf(top->currPos), currHdr);
            atHeader = false;

            if (currHdr.name == nameOfStd(strF)) {
               printf("F[");
            } else {
               printf("[");
               printNameNoLnCr(currHdr.name, prc);
               printf(" ");
            }
         } ei (typeVal < 0) { // type parameter
            printf("$");
            if (typeVal == -1) {
               printf("E");
            } else {
               printNameNoLnCr(-typeVal - 1, prc);
            }
            printf(" ");
            top->currPos++;
         } ei(typeVal <= topVerbatimType)  {
            printNameNoLnCr(nameOfStd(strInt) + typeVal, prc);
            printf(" ");
            top->currPos++;
         } else {
            top->currPos++;
            add(((TypeLoc){
                  .currPos = typeVal, .sentinel = typeVal + prc.types[typeVal] + 1}
               ),
               st
            );
            top = &last(st);
            atHeader = true;
            continue;
         }

         nextIter:
         // closing open type spans
         while (top != null && top->currPos == top->sentinel) {
            st->len--;
            top = st->len > 0 ? &last(st) : null;
            printf("]");
         }
      }
   }
}

private Int //:calcSizeOfStruct
calcSizeOfStruct(TypeId t, CM) {
   return 8;
}

//}}}
//{{{ Parsing type names

private TypeId //:typeGetTypeByName
typeGetTypeByName(Int t, CM) {
   Int const mbTypeId = cm->activeBindings[t];
   VALIDATEP(mbTypeId > -1, pError(errUnknownType, nameErr(t)));
   return typeOf(mbTypeId);
}

//}}}
//{{{ Type expressions

private void //:tFreshState
tFreshState(TParse* te) {
   te->frames->len = 0;
   te->exp->len = 0;
}

private void //:teClose
teClose(TParse* te, CM) {
// Flushes the finished subexpr frames from the top of the type stack.
   LInt* exp = te->exp;
   LTypeFrame* frames = te->frames;
   while (frames->len > 0 && last(frames).sentinel == cm->i) {
      TypeFrame frame = removeLast(frames);
      Int startInd = exp->len - frame.countArgs;
      TypeId newType = ZERO_ARITY_TYPE;

      if (frame.tp == tfrFunction)  {
         newType = tCreateFnTypeCall(te, startInd, frame, cm);
      } ei (frame.tp == tfrTypeCall) {
         newType = tCreateTypeCall(te, sorTypeCall, startInd, frame, cm);
      } else { // tyeParamCall, a call of a type which is a parameter
         // TODO higher-kinded types
         throwExcParser(pError0(errTemp));
      }
      exp->c[startInd] = newType.v;
      exp->len = startInd + 1; // +1 because we've put one type for the call we've reduced
   }
}

private TypeId //:tParseComplexType
tParseComplexType(TParse* te, Int sentinel, OUT Bool* isGeneric, TOKENS, CM) {
// Precondition: we are looking at the first tokType (`L` in this example),
// while the first one has been added as a type call.
   LInt* exp = te->exp;
   exp->len = 0;
   LTypeFrame* frames = te->frames;
   teOpenTypeCall(tokens[cm->i].pl1, sentinel, te, cm);
   cm->i++; // CONSUME the outer TypeCall
   while (cm->i < sentinel) {
      teClose(te, cm);
      Token cTk = tokens[cm->i];

      VALIDATEP(frames->len > 0, pError0(errTypeDefError))
      frames->c[frames->len - 1].countArgs++;

      if (cTk.tp == tokType) {
         if (cTk.pl2 == 0) {
            add(typeGetTypeByName(cTk.pl1, cm).v, exp);
         } else {
            Int const typeCallSent = calcSentinel(cTk, cm->i);
            teOpenTypeCall(cTk.pl1, typeCallSent, te, cm);
         }
      } ei (cTk.tp == tokTypeVar) {
         teMergeParam(cTk.pl1, te, cm);
      } else {
         throwExcParser(pError0(errTypeExpr));
      }
      cm->i++; // CONSUME the current token
   }
   teClose(te, cm);

   VALIDATEI(exp->len == 1, ierrInconsistentTypeExpr);
   return typeOf(exp->c[0]);
}

private TypeId //:tParse
tParse(Int sentinel, OUT Bool* isGeneric, TOKENS, CM) {
// Parse a type expression like `(L Double)`. Produces a linear, RPN sequence. Consumes all tokens,
// populates @te.exp and @te.paramNames.
// Precondition: we are looking at the first type token (e.g. `[L ...]`).
   Token firstTypeTk = tokens[cm->i];
   VALIDATEP(firstTypeTk.tp == tokType || firstTypeTk.tp == tokTypeVar,
      pError0(errTypeDefError)
   )
   TParse* te = &(cm->tParse);
   if (cm->i + 1 == sentinel) { // single-name type
      if (firstTypeTk.tp == tokType)  {
         TypeId simpleType = typeGetTypeByName(firstTypeTk.pl1, cm);
         *isGeneric = typeReadHeader(simpleType, cm).isGeneric;
         add(simpleType.v, te->exp);
         return simpleType;
      } else { // tokTypeParam
         return typeOf(teMergeParam(firstTypeTk.pl1, te, cm));
      }
   }
   return tParseComplexType(te, sentinel, OUT isGeneric, tokens, cm);
}

private Int //:tSubexValidateNamesUnique
tSubexValidateNamesUnique(TParse* te, Int start, CM) {
// Validates that the names in a record are unique.
// Returns function/record's arity
   Int const end = te->names.len;
   if (end == 0)
      { return 0; }
   LInt names = te->names;
   LInt* tmp = te->tmp;
   // copy from names to tmp
   if (tmp->cap < names.len) {
      Arr(Int) arr = allocateArray(names.len, Int, cm->aTmp);
      tmp->c = arr;
      tmp->cap = names.len;
   }
   memcpy(tmp->c, names.c, names.len);
   tmp->len = names.len;

   sortLInts(tmp);
   NameId prev = tmp->c[0];
   for (Int j = 1; j < tmp->len; j++) {
      if (tmp->c[j] == prev)
         { throwExcParser(pError0(errFnDuplicateParams)); }
   }
   Int const countNames = names.len;
   te->names.len = 0;
   return countNames;
}

/*
private TypeId //:typeCreateRecord
typeCreateRecord(TParse* st, Int startInd, Unt nameAndLen, CM) {
// Creates/merges a new record type from a sequence of pairs in @exp and a list of type params
// in @params. The sequence must be flat, i.e. not include any nested structs, and be in the
// final position of @exp. "nameAndLen" may be -1 if it's an anonymous record.
// Returns the typeId of the new/existing type
   TYPE_DEFINE_EXP;
   tSubexValidateNamesUnique(st, startInd, exp->len, cm);
   Int tentativeTypeId = cm->types.len;
   pushIntypes(0, cm);
   Int sentinel = exp->len;

#ifdef DEBUG
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
#ifdef DEBUG
      VALIDATEP(exp->c[j - 1] == tyeType, "not a type")
#endif
      pushIntypes(exp->c[j], cm);
   }
   cm->types.c[tentativeTypeId] = cm->types.len - tentativeTypeId - 1;
   return mergeType(tentativeTypeId, cm);
}
*/

private TypeId //:tCreateTypeCall
tCreateTypeCall(TParse* te, Byte sort, Int startInd, TypeFrame frame, CM) {
// Creates/merges a new type call from a sequence of types in @exp
// Handles ordinary type calls like `[L Int]`, NOT function types. Returns the new type's id
   TypeId genericId = frame.id;
   TypeHeader genericHdr = typeReadHeader(genericId, cm);
   TYPE_DEFINE_EXP;

   Int const sentinel = exp->len;

   TYPE_CREATE_START(
      ((TypeHeader){ .sort = sort,
         .arity = (sentinel - startInd + 1),
         .name = genericHdr.name, .isGeneric = frame.isGeneric, .size = genericHdr.size })
   );
   for (Int j = genericId.v + TYPE_PREFIX;
        j < genericId.v + TYPE_PREFIX + genericHdr.arity + 1; // +1 to include index in @fields
        j++
   ) {
      pushIntypes(cm->types.c[j], cm); // TODO substitute the generic params
   }
   pushIntypes(frame.id.v, cm);
   for (Int j = startInd; j < sentinel; j++) { // The concrete values of the params
      pushIntypes(exp->c[j], cm);
   }

   TYPE_CREATE_END;
   TypeId r = mergeType(tentativeType, cm);
   return r;
}


private Int //:teMergeParam
teMergeParam(NameId name, TParse* restrict te, CM) {
   for (Int j = te->frames->len; j > -1 && !te->frames->c[j].isGeneric; j--) {
      te->frames->c[j].isGeneric = true;
   }
   add(-name - 1, te->exp);
   for (Int j = 0; j < te->paramNames.len; j++) {
      if (te->paramNames.c[j] == name)
         { return -name - 1; }
   }
   add(name, &(te->tParams));
   return -name - 1;
}

private TypeId //:tCreateFnTypeCall
tCreateFnTypeCall(TParse* te, Int startInd, TypeFrame frame, CM) {
// Creates an `F[A B -> C]` type
   TYPE_DEFINE_EXP;

   Int const depth = exp->len - startInd; // this isn't function arity, it's type arity
   Int const sentinel = exp->len;
   TYPE_CREATE_START(
      ((TypeHeader){ .sort = sorDeclare, .arity = depth, .name = nameOfStd(strF),
               .isGeneric = frame.isGeneric, .size = 8 })
   );
   for (Int j = startInd; j < sentinel; j++) {
      pushIntypes(exp->c[j], cm);
   }

   TYPE_CREATE_END;
   TypeId res = mergeType(tentativeType, cm);
   return res;
}

private TypeId //:tCreateSingleParamTypeCall
tCreateSingleParamTypeCall(NameId nameOfOuter, TypeId typeArg, CM) {
// Creates a type like (L Int). Precondition: struct/union, not a function type!
   TypeId outer = typeOf(cm->activeBindings[nameOfOuter]);
   TypeHeader outerHdr = typeReadHeader(outer, cm);

   TYPE_CREATE_START(
      ((TypeHeader){ .sort = sorTypeCall, .arity = 2,
         .name = nameOfOuter, .isGeneric = false, .size = 16 })
   );
   for (Int j = outer.v + TYPE_PREFIX;
        j < outer.v + TYPE_PREFIX + 1 + outerHdr.arity; // +1 to include index in @fields
        j++
   ) {
      pushIntypes(cm->types.c[j], cm);
   }
   pushIntypes(outer.v, cm);
   pushIntypes(typeArg.v, cm);

   TYPE_CREATE_END;
   TypeId res = mergeType(tentativeType, cm);
   return res;
}


#define maxTypeParams 254

private void //:teOpenTypeCall
teOpenTypeCall(NameId typeName, Int sentinel, TParse* te, CM) {
// Adds a new type call to @exp during type expression parsing
   if (typeName == nameOfStd(strF)) { // F ...
      add(((TypeFrame){
            .tp = tfrFunction, .sentinel = sentinel,
         }),
         te->frames
      );
   } else { // ordinary type call
      TypeId const typeId = typeOf(cm->activeBindings[typeName]);
      VALIDATEP(typeId.v > -1, pError(errUnknownTypeConstructor, nameErr(typeName))
      )
      add(((TypeFrame){
            .tp = tfrTypeCall, .id = typeId, .sentinel = sentinel
         }),
         te->frames
      );
   }
}

//~private TypeId //:teClauseComplexType
//~teClauseComplexType(TParse* te, Int sentinel, TOKENS, CM) {
// For a clause like `lst L Double`, parses the `L Double` part.
// Precondition: we are looking JUST PAST the first type token (`Double` in this example),
// while the first one has been added as a type call.
//~   LInt* exp = te->exp;
//~   LTypeFrame* frames = te->frames;
//~   while (cm->i < sentinel) {
//~      teClose(te, cm);
//~      Token cTk = tokens[cm->i];
//~      cm->i++; // CONSUME the current token
//~
//~      VALIDATEP(frames->len > 0, pError0(errTypeDefError))
//~      if (cTk.tp == tokWord) { // name of a field in a struct/variant
//~         VALIDATEP(cm->i < sentinel, errTypeDefError)
//~         Int ctxType = last(frames).tp;
//~         VALIDATEP(ctxType == sorDeclare, errTypeDefError)
//~
//~         Token nextTk = cm->tokens.c[cm->i];
//~         VALIDATEP(nextTk.tp == tokType, errTypeDefError)
//~         add(cTk.pl1, te->names);
//~         continue;
//~      }
//~
//~      frames->c[frames->len - 1].countArgs++;
//~
//~      if (cTk.tp == tokType) {
//~         if (cTk.pl2 == 0) {
//~            add(typeGetTypeByName(cTk.pl1, cm).v, exp);
//~         } else {
//~            Int const typeCallSent = calcSentinel(cTk, cm->i - 1);
//~            teOpenTypeCall(cTk.pl1, typeCallSent, frames, cm);
//~         }
//~      } ei (cTk.tp == tokTypeVar) {
//~         // create/reuse a type of sorGenericParam for a newly encountered type param
//~         NameId name = cTk.pl1;
//~         teMergeParam(name, te, cm);
//~      } else {
//~         throwExcParser(pError0(errTypeDefError));
//~      }
//~   }
//~
//~   teClose(te, cm);
//~
//~   VALIDATEI(exp->len == 1, ierrInconsistentTypeExpr);
//~   return typeOf(exp->c[0]);
//~}
//~
//~private TypeId //:teClause
//~teClause(TParse* te, Int sentinel, TOKENS, CM) {
// Parses `lst S Double`.
// Precondition: we are looking at the name token (e.g. `lst`).
// @te.frames, @te.exp etc must be empty. Produces a linear, RPN sequence.
//~   tFreshState(te);
//~   Token nameTk = tokens[cm->i];
//~   VALIDATEP(nameTk.tp == tokWord, errTypeDefError)
//~   add(nameTk.pl1, te->names);
//~   cm->i++; // CONSUME the name of the clause
//~   return teParse(sentinel, tokens, cm);
//~}



private TypeId //:pStructDef
pStructDef(Int name, Int sentinel, TOKENS, CM) {
// Precondition: pointing at the first tokKey of the struct definition
// Populates genericFields and types
//

   d("struct def")
   printName(name, cm);
   TParse* te = &(cm->tParse);
   
   TypeHeader hdr = (TypeHeader){ .sort = sorDeclare, .arity = 0, // will fill in at end of function
         .name = name, .isGeneric = false, .size = 0 };
   TYPE_CREATE_START(hdr);
   
   Int initFieldLen = cm->genericFields.len;
   te->paramNames.len = 0; // clear param names before parsing a struct definition
   Bool isGeneric = false;
   for (Int j = cm->i; j < sentinel;) {
      Token tk = tokens[j];
      VALIDATEP(tk.tp == tokKey, pError0(errTypeStructDefinition))
      Int keyName = tk.pl1;
      pushIngenericFields(((FieldName){
            .name = keyName, .access = tk.pl2 > 0 ? accessPubMut : accessPubImm
         }),
         cm
      );
      j++;
      
      Int fieldSentinel = calcSentinel(tokens[j], j);
      TypeId fieldType = tParse(fieldSentinel, &isGeneric, tokens, cm);
      pushIntypes(fieldType.v, cm);
      
      j = fieldSentinel;
   }
   pushIntypes(initFieldLen, cm); // index of the first field in @genericFields
   hdr.isGeneric = isGeneric;
   for (Int j = 0; j < te->paramNames.len; j++) {
      pushIntypes(te->paramNames.c[j], cm); // unique param names go to the end of the type
   }
   TYPE_CREATE_END;
   
   Int fieldCount = cm->genericFields.len - initFieldLen;
   
   hdr.arity = fieldCount;
   hdr.size = calcSizeOfStruct(tentativeType, cm);
   cm->types.c[cm->types.len + 1] = 
      ((Int)((Unt)((Unt)hdr.sort << 16) + ((Unt)hdr.arity << 8)));
      
   cm->types.c[cm->types.len + 3] = hdr.size;
   TypeId newStruct = mergeType(tentativeType, cm);
   
   d("resulting type:")
   dbgType(newStruct);
   return newStruct;
}

private TypeId //:pTypeDef
pTypeDef(Int sentinel, TOKENS, CM) {
// Builds a type expression from a type definition or a function signature.
// Example 1: `Foo = struct :id Int :name String;`
// Example 2: `Bar = F[Double Bool -> String];`
//
// Accepts a name or -1 for nameless type exprs (like function signatures).
// Uses cm->exp to build a "type expression" and cm->params for the type parameters
// Produces no AST nodes, but potentially lots of new types
// Consumes the whole type assignment right side, or the whole function signature
// Data format: see "Type expression data format"
// Precondition: we are 1 past the tokAssignmentRight token
   VALIDATEP(tokens[cm->i + 1].tp == tokAssignRight, pError0(errAssignmentLeftSide))
   cm->tParse.frames->len = 0;

   Token nameTk = tokens[cm->i];
   Int name = nameTk.pl1;
   cm->i += 2; // CONSUME the type name and the tokAssignmentRight

   VALIDATEP(cm->i < sentinel, pError0(errTypeDefError))
   Token taggingTk = tokens[cm->i];
   cm->i++; // skip the tag of the type 
   
   TypeId newType = VOID_TYPE;
   if (taggingTk.pl1 == nameOfStd(strStruct)) {
      newType = pStructDef(name, sentinel, tokens, cm);
   }
   cm->activeBindings[name] = newType.v;
   cm->i = sentinel; // CONSUME the whole definition
   return newType;
}

void //:tGenericTryUnifyTreeNodes
tGenericTryUnifyTreeNodes(TypeId gener, TypeId concr,
      LTypeLoc* genericWalk, LTypeLoc* concreteWalk, CM
) {
// Unification of a single node pair in the type trees. Possibly pushes TypeLocs to the stacks,
// or sets param values in @tParse.params

   if (eq(gener, concr))
      { return; }
   if (gener.v < -1) {
      LInt* tParams = &(cm->tParse.tParams);
      NameId nameParam = -gener.v - 1;
      for (Int j = 0; j < tParams->len; j += 2) {
         if (tParams->c[j] == nameParam) {
            VALIDATEP(tParams->c[j + 1] == concr.v,
               pError(errTypeGenericCallDoesntUnify, typeErr2(typeOf(tParams->c[j + 1]), concr))
            )
            return;
         }
      }
      add(nameParam, tParams);
      add(concr.v, tParams);
      return;
   }
   TypeHeader generHdr = typeReadHeader(gener, cm);
   TypeHeader concrHdr = typeReadHeader(concr, cm);
   VALIDATEP(generHdr.name == concrHdr.name,
      pError(errTypeGenericCallDoesntUnify, typeErr2(gener, concr))
   )
   VALIDATEP(generHdr.arity == concrHdr.arity,
      pError(errTypeOverloadWrongArity, numberErr2(generHdr.arity, concrHdr.arity))
   )
   add(tGetBody(gener, generHdr, cm), genericWalk);
   add(tGetBody(concr, concrHdr, cm), concreteWalk);
}

TypeId //:tGlueReturnTypeOntoFn
tGlueReturnTypeOntoFn(TypeId args, TypeId returnType, CM) {
// `F Int Double -> String`, `Foo` -> `F Int Double String -> Foo`. Used for generic resolutions.
   Int const sizeArgs = cm->types.c[args.v];
   Int const tentativeType = cm->types.len;
   ensureCapacityTypes(sizeArgs + 2, cm); // +2 for the size (in front) and return type (in back)

   cm->types.c[tentativeType] = sizeArgs + 1;
   TypeHeader argsHdr = typeReadHeader(args, cm);
   TypeHeader fullHdr = argsHdr;
   cm->types.len++;
   fullHdr.arity++; // for the return type
   typeAddHeader(fullHdr, cm);

   memcpy(
      cm->types.c + tentativeType + TYPE_PREFIX,
      cm->types.c + args.v + TYPE_PREFIX,
      4*sizeArgs - sizeof(TypeHeader)
   );
   cm->types.c[tentativeType + sizeArgs + 1] = returnType.v;
   cm->types.len += (sizeArgs - sizeof(TypeHeader)/4 + 1); // +1 for the size

   return mergeType(typeOf(tentativeType), cm);
}

private LInt //:tGenericUnify
tGenericUnify(TypeLoc generic, TypeLoc concrete, CM) {
// Returns: @te.tParams with type parameters fully resolved: [(name type)]
// Precondition: for both "generic" and "concrete", sentinel - currPos must be same length
// Throws if not types not unifiable
   TParse* restrict te = &(cm->tParse);
   te->genericWalk->len = 0;
   te->concreteWalk->len = 0;
   te->tParams.len = 0;

   add(generic, te->genericWalk);
   add(concrete, te->concreteWalk);
   for (; te->genericWalk->len > 0 && te->concreteWalk->len > 0; ) {
      TypeLoc* genericLoc = &last(te->genericWalk);
      TypeLoc* concreteLoc = &last(te->concreteWalk);
      TypeId g = { .v = cm->types.c[genericLoc->currPos] };
      TypeId c = { .v = cm->types.c[concreteLoc->currPos] };

      // next step in the tree-walk
      genericLoc->currPos++;
      concreteLoc->currPos++;
      if (genericLoc->currPos == genericLoc->sentinel) {
         te->genericWalk->len--;
         te->concreteWalk->len--;
      }

      tGenericTryUnifyTreeNodes(g, c, te->genericWalk, te->concreteWalk, cm);
   }
   return te->tParams;
}

private TypeId //:monomorphizeStruct
monomorphizeStruct(
   TypeId generic, TypeHeader genericHdr, LInt fieldTypes, LInt resolvedParams, CM
) {
// Creates a sorTypeCall type
   Int const fieldCount = genericHdr.arity;
   ensureCapacityTypes(fieldCount + TYPE_PREFIX + 1 + resolvedParams.len, cm);
   
   Int genericFieldInd = libeyr_getFieldNameInd(generic, genericHdr, cm->types.c);
   
   TypeHeader monoHdr = (TypeHeader){ .sort = sorTypeCall, .arity = fieldCount,
      .name = genericHdr.name, .isGeneric = false, .size = 0 };
   TYPE_CREATE_START(monoHdr);
   memcpy(cm->types.c + cm->types.len, fieldTypes.c, fieldCount*4);
   cm->types.len += fieldCount;
   cm->types.c[cm->types.len] = genericFieldInd;
   cm->types.len++;
   
   Int sentinel = generic.v + cm->types.c[generic.v] + 1;
   paramLoop:
   for (Int j = tGetBodyStart(generic, genericHdr), k = cm->types.len; j < sentinel; j++, k++) {
      Int paramName = - cm->types.c[k] - 1;
      for (Int p = 0; p < resolvedParams.len; p += 2) {
         if (resolvedParams.c[p] == paramName) {
            cm->types.c[k] = resolvedParams.c[p + 1];
            cm->types.len++;
            continue paramLoop;
         }
      }
      // should never happen: all the type params should be already resolved in {tGenericUnify}
      throwExcParser0(pError(errTypeParamsResolve, nameErr(paramName)), __LINE__, cm);
      
   }
   TYPE_CREATE_END;
   
   TypeId concrete = mergeType(tentativeType, cm);
   dbgType(concrete);
   return concrete;
}

//}}}
//{{{ Overloads, type check & resolve

private TypeId //:getFirstParamType
getFirstParamType(TypeId t, CM) {
// Gets the type of the first param of a type. Returns -1 iff it's zero-arity
   TypeHeader hdr = typeReadHeader(t, cm);
   if (hdr.arity == 0)
      { return ZERO_ARITY_TYPE; }
   ei (hdr.arity == 1 && hdr.name == nameOfStd(strF))
      { return VOID_TYPE; }
   else
      { return typeOf(cm->types.c[t.v + TYPE_PREFIX]); }
}

private TypeId //:getFirstParamInd
getFirstParamInd(TypeId funcTypeId, CM) {
// Gets the ind of the first param of a function. Precondition: function is not nullary!
   return typeOf(funcTypeId.v + TYPE_PREFIX);
}

private TypeId //:tFunctionReturnType
tFunctionReturnType(TypeId funcTypeId, CM) {
   TypeHeader hdr = typeReadHeader(funcTypeId, cm);
   return typeOf(cm->types.c[funcTypeId.v + TYPE_PREFIX + hdr.arity - 1]);
}


private Bool //:tFindOverload
tFindOverload(TypeId typeId, Int ovInd, CM, OUT FunctionId* fn) {
// Params: typeId = type of the first function parameter, or -1 if it's 0-arity
//         ovInd = ind in @overloads, which is found via @activeBindings
//         entityId = address where to store the result, if successful
// We have 4 scenarios here, sorted from left to right in the outerType part of [overloads]:
// 1. outerType = -1 => 0-arity function
// 2. outerType = outerTypeForTypeParam => a blanket overload
// 3. default
   Int const start = ovInd + 1;
   Arr(Int) overs = cm->overloads.c;

   Int const countOverloads = overs[ovInd]/2;
   Int const sentinel = ovInd + countOverloads + 1;

   if (eq(typeId, typeOf(tokMisc))) { // scenario 1
      Int j = ovInd + 1;
      if (j < sentinel && overs[j] == voidType) {
         (*fn) = overs[j + countOverloads];
         return true;
      } else {
         return false;
      }
   }

   TypeId const outerType = typeGetOuter(typeId, cm);

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

private FunctionId //:findOverload
findOverload(NameId name, TypeId tpFstArg, CM) {
   Int indOverl = -cm->activeBindings[name] - 2;
   VALIDATEP(tpFstArg.v > -1, pError(errTypeUnknownFirstArg, nameErr(name)))
   Int fnId;
   Bool ovFound = tFindOverload(tpFstArg, indOverl, cm, OUT &fnId);
#if defined(DEBUG) //{{{
   if (!ovFound) {
      d("Overload not found: indOverl %d name %d tpFirstArg %d j %d",
         indOverl, name, tpFstArg.v, cm->j)
      d("exp:");
      printLInt(cm->expr.exp);
      d("name:")
      printName(name, cm);
   }
#endif //}}}
   VALIDATEP(ovFound, pError(errTypeNoMatchingOverload, nameAndTypeErr(name, tpFstArg)))
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
         d("can't get first type of type %d name %d cmj %d", a, name, cm->j);
      } //}}}
      VALIDATEP(tpFstArg.v > -1, pError(errTypeUnknownFirstArg, nameErr(name)))
   }
   return findOverload(name, tpFstArg, cm);
}

internal Int //:getOper
getOper(Int opName, Int operandType, Compiler* cm) {
// Try to find convert test value to operator entityId
   Int ovInd = -getBinding(opName, cm) - 2;
   Int fnId;
   Bool foundOv UNUSED = tFindOverload(typeOf(operandType), ovInd, cm, OUT &fnId);

   tFindOverload(typeOf(operandType), ovInd, cm, OUT &fnId);
   VALIDATEI(foundOv, ierrParsedFunctionNotInScope);
   return fnId;
}

private void //:typeCheckFnGenericCall
typeCheckFnGenericCall(Int fnId, Int argCount, LInt* restrict exp, CM) {
// Resolves a generic function's concrete type and adds it to the list of monomorphizations
// if it wasn't already there
   Function fn = cm->functions.c[fnId];

   TypeId concreteType = tResolveGenericFnCall(fn, exp->c + exp->len - argCount, argCount, cm);

   Int concreteFn = searchMultiAssocList(concreteType.v, fn.genericInd, cm->functionMonos);

   if (concreteFn == -1) {
      concreteFn = cm->functions.len; // function body coming in {generateMonomorphizations}

      Int newListInd =
         addMultiAssocList(concreteType.v, concreteFn, fn.genericInd, cm->functionMonos);
      if (newListInd != -1)
         { cm->functions.c[fnId].genericInd = newListInd; }

      Int const tokenInd = cm->functions.c[fnId].tokenInd;
      add(((Monomorphization){ .fnId = concreteFn, .tokenInd = tokenInd }), cm->monos);
      pushInfunctions(((Function){
               .name = fn.name, .typeId = concreteType, .emit = emitParsed,
               .nodeInd = -1, .genericInd = -1, .tokenInd = tokenInd, .needsMangling = true
         }),
         cm
      );
      pushIntoplevels(concreteFn, cm);
   }
   cm->ast.c[cm->j].pl1 = concreteFn;
}

private void //:typeCheckFnCall
typeCheckFnCall(Node nd, LInt* restrict exp, CM) {
   // A function call. cont[j] contains the argument count, cont[j + 1] index in @overloads
   Bool isVarCall = nd.pl3 == callVar;
   Int const argCount = nd.pl2;
   Int const name = nd.pl1; // name for function calls, varId for var calls

   // # on lists and arrays
   if (name == opSize) {
      Int argumentType = exp->c[exp->len - 1];
      TypeId outer = typeGetOuter(typeOf(argumentType), cm);
      if (outer.v == cm->stats.arrayType || outer.v == cm->stats.listType || outer.v == tokString) {
         // field 0 is content, 1 is len. see {buildPreludeTypes}
         cm->ast.c[cm->j] = (Node){.tp = nodCall, .pl1 = argumentType, .pl2 = 1, .pl3 = callField};
         exp->c[exp->len - 1] = tokInt;
         return;
      }
   }

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

#ifdef VERBOSE //{{{
   if (typeReadHeader(typeOfFunc, cm).arity != argCount + 1) {
      d("arity error %d type %d argc %d", typeReadHeader(typeOfFunc, cm).arity, typeOfFunc.v,
         (argCount == 0 ? 1 : argCount) + 1);
   }
#endif //}}}
   // first param matches, but does arity?
   VALIDATEP(typeReadHeader(typeOfFunc, cm).arity == argCount + 1,
      pError(errTypeNoMatchingOverload,
         nameNumberTypeErr(name, typeReadHeader(typeOfFunc, cm).arity, typeOfFunc)
      )
   )

   TypeId firstParamInd = getFirstParamInd(typeOfFunc, cm);
   if (isGeneric) {
      typeCheckFnGenericCall(fnId, argCount, exp, cm);
   } else {
      // We know the type of the function, now to validate arg types against param types
      for (Int k = exp->len - argCount, l = firstParamInd.v; k < exp->len; k++, l++) {
         VALIDATEP(exp->c[k] == cm->types.c[l],
            pError(errTypeWrongArgumentType, typeErr2(typeOf(exp->c[k]), typeOf(cm->types.c[l])))
         )
      }
      cm->ast.c[cm->j].pl1 = isVarCall ? varId : fnId;
   }

   exp->len -= (argCount - 1);
   TypeId retType = tFunctionReturnType(typeOfFunc, cm);
   exp->c[exp->len - 1] = retType.v;
}

private void //:typeCheckCall
typeCheckCall(Node nd, LInt* restrict exp, CM) {
// Handles the various sorts of calls: function calls, function var calls, field and
// array accesses
// Transforms the `#` operator for arrays and lists to a field access
   if (nd.pl3 == callGetElem) {
      VALIDATEP(exp->len >= 2, pError0(errExpressionError))

      TypeId typeColl = typeOf(exp->c[exp->len - 2]);
      VALIDATEP(tIsList(typeColl, cm), pError(errTypeOfNotList, typeErr(typeColl)))

      // list index must be Int
      VALIDATEP(eq(typeOf(exp->c[exp->len - 1]), intTy),
         pError(errTypeOfListIndex, typeErr(typeOf(exp->c[exp->len - 1])))
      )

      TypeId typeElt =
         libeyr_typeGetGenericArg(typeColl, typeReadHeader(typeColl, cm), 0, cm->types.c);
      cm->ast.c[cm->j].pl1 = typeColl.v;
      exp->len -= 2; // replace collection and its index type (Int) with element type
      add(typeElt.v, exp);
   } ei (nd.pl3 == callField) { // a field accessor
      VALIDATEP(exp->len >= 1, pError0(errExpressionError))
      NameId name = nd.pl1;

      Int structType = exp->c[exp->len - 1];
      VALIDATEP(structType > topVerbatimType,
         pError(errTypeFieldNotFound, nameErr2(name, typeReadHeader(typeOf(structType), cm).name))
      );

      Int fieldInd;
      TypeId fieldType = typeTryGetField(name, typeOf(structType), OUT &fieldInd, cm);

      cm->ast.c[cm->j].pl1 = structType;
      cm->ast.c[cm->j].pl2 = fieldInd;
      exp->c[exp->len - 1] = fieldType.v;
   } else {
      typeCheckFnCall(nd, exp, cm);
   }
}

private TypeId //:typeCheckGenericStruct
typeCheckGenericStruct(TypeId generic, TypeHeader genericHdr, Arr(Int) args, CM) {
   Int const fieldCount = genericHdr.arity;
   ensureCapacityTypes(fieldCount + TYPE_PREFIX + 1, cm);
   
   TypeHeader concreteHdr = (TypeHeader){ .sort = sorDeclare, .arity = fieldCount,
      .name = -1, .isGeneric = false, .size = 0 };
   TYPE_CREATE_START(concreteHdr);
   memcpy(cm->types.c + cm->types.len, args, fieldCount*4);
   cm->types.len += fieldCount;
   TYPE_CREATE_END;
   
   TypeId concrete = mergeType(tentativeType, cm);
   Int concreteSent = concrete.v + TYPE_PREFIX + fieldCount;
   TypeLoc concreteLoc = (TypeLoc){
      .currPos = tGetBodyStart(concrete, concreteHdr), .sentinel = concreteSent
   };
   
   TypeLoc genericLoc = (TypeLoc){
      .currPos = tGetBodyStart(generic, genericHdr), 
      .sentinel = generic.v + TYPE_PREFIX + fieldCount
   };
   
   LInt resolvedParams = tGenericUnify(genericLoc, concreteLoc, cm);
   return monomorphizeStruct(
      generic, genericHdr, ((LInt){.c = args, .len = fieldCount}), resolvedParams, cm
   );
}

private void //:typeCheckStruct
typeCheckStruct(Node nd, LInt* restrict exp, CM) {
// Handles struct initializers: resolves it to a concrete type and stores it in the nodStruct
   Int const fieldCount = nd.pl2;
   Arr(Int) args = exp->c + exp->len - fieldCount;
   TypeId structType = typeGetTypeByName(nd.pl1, cm);
   TypeHeader structHdr = typeReadHeader(structType, cm);
   
   // Note that we do NOT need to check for arity or that @exp.len is sufficient
   // because it has been done in {reorderStructLocateKeys}
   if (structHdr.isGeneric) {
      structType = typeCheckGenericStruct(structType, structHdr, args, cm);
   } else {
      for (Int k = 0, l = structType.v + TYPE_PREFIX; k < fieldCount; k++, l++) {
         VALIDATEP(args[k] == cm->types.c[l],
            pError(errTypeMismatch, typeErr2(typeOf(cm->types.c[l]), typeOf(args[k])))
         )
      }
   }
   
   cm->ast.c[cm->j].pl1 = structType.v;
   exp->len -= (fieldCount - 1);
   exp->c[exp->len] = structType.v;
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
   LInt* exp = cm->expr.exp;
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
      } ei (nd.tp <= topVerbatimTokenVariant) {
         add((Int)nd.tp, exp);
      } ei (nd.tp == nodVar) {
         add(cm->vars.c[nd.pl1].typeId.v, exp);
      } ei (nd.tp == nodStruct) {
         typeCheckStruct(nd, exp, cm);
      } else { // overloadId or nodDataLit
         add(nd.pl1, exp); // overloadId
      }
   }
}

private TypeId //:typeCheckBigExpr
typeCheckBigExpr(Int indExpr, Int sentinelNode, CM) {
// Typechecks and resolves overloads in a single expression. "Big" refers to
// the fact that this expr may contain sub-assignments for data allocation.
// "indExpr" is the index of nodExpr or nodAssignmentRight
// CONSUMES the whole expression
   LInt* exp = cm->expr.exp;
   typeReduceExpr(indExpr, cm);
   if (exp->len == 1) {
      return typeOf(exp->c[0]); // the last remaining stack elt is the type of the whole expression
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
   } ei (nd.tp == nodDataLit) { // an empty data literal with compile-time known length
      return nd.pl1 == -1 ? ZERO_ARITY_TYPE : typeOf(nd.pl1);
   } else {
      Int sentinel = (*j) + nd.pl2 + 1;
      TypeId exprType = typeCheckBigExpr(*j, sentinel, cm);
      *j = sentinel;
      return exprType;
   }
}

private TypeId //:typecheckList
typecheckList(Node nd, Int startInd, CM) {
// node = nodDataLit, startInd = index of the nodDataLit, not the first element
// Returns the concrete collection type of the list/array. Fills in the missing element types.

   // Elements haven't been specified but the type has been declared with `@`
   if ((nd.pl2 == 0 && nd.pl1 != -1))
      { return typeOf(nd.pl1); }
   ei (nd.pl3 == BIG) {
      Int sentinel = calcNodeSentinel(nd, startInd);
      TypeId exprType;
      if (startInd + 2 == sentinel) {
         Node singleNode = cm->ast.c[startInd + 1];
         VALIDATEP(singleNode.tp == nodVar, pError0(errMetaArrSyntax))
         exprType = cm->vars.c[singleNode.pl1].typeId;
      } else {
         exprType = typeCheckBigExpr(startInd + 1, sentinel, cm);
      }

      VALIDATEP(eq(exprType, typeOf(tokInt)), pError0(errMetaArrSyntax));
      return typeOf(nd.pl1);
   }

   // The case where the elements are specified
   TypeId commonEltType = VOID_TYPE;
   for (Int j = startInd + 1; j < cm->ast.len; j++) {
      TypeId eltType = typecheckAndProcessListElt(&j, cm);
      if (eq(eltType, ZERO_ARITY_TYPE))
         { continue; }
      if (eq(commonEltType, VOID_TYPE))
         { commonEltType = eltType; }
      else {
         VALIDATEP(eq(eltType, commonEltType),
            pError(errListDifferentEltTypes, typeErr2(eltType, commonEltType))
         )
      }
   }
   VALIDATEP(!eq(commonEltType, VOID_TYPE), pError0(errListUnknownEltType));
   for (Int j = startInd + 1; j < cm->ast.len; ) {
      Node elem = cm->ast.c[j];
      if (elem.tp == nodDataLit && elem.pl1 == -1) {
         cm->ast.c[j].pl1 = commonEltType.v;
      }
      if (elem.pl3 == arrLitKnownLength) {
         cm->ast.c[j].pl3 = elem.pl2;
         cm->ast.c[j].pl2 = 0;
         j++;
         continue;
      }
      j = calcNodeSentinel(elem, j);
   }
   TypeId collType = tCreateSingleParamTypeCall(nameOfStd(strArr), commonEltType, cm);
   cm->ast.c[startInd].pl1 = collType.v;
   return collType;
}

private TypeId //:typeTryGetField
typeTryGetField(NameId fieldName, TypeId t, OUT Int* fieldInd, CM) {
// Searches for a field within a struct by its name. Returns the index of that field
// (within the type, so 0-based), and its type.
   TypeHeader hdr = typeReadHeader(t, cm);

   Int const indInGenericFields = libeyr_getFieldNameInd(t, hdr, cm->types.c);
   Int j = indInGenericFields;
   for (; j < indInGenericFields + hdr.arity; j++) {
      if (cm->genericFields.c[j].name == fieldName)
         { break; }
   }
   VALIDATEP(j < indInGenericFields + hdr.arity,
      pError(errTypeFieldNotFound, nameErr2(fieldName, hdr.name))
   );
   *fieldInd = j - indInGenericFields;

   return typeOf(cm->types.c[t.v + TYPE_PREFIX + (*fieldInd)]);
}

//}}}
//{{{ Generic types

TypeId //:tGenericSubstituteParams
tGenericSubstituteParams(TypeId t, LInt resolvedParams, CM) {
// Performs the substitutions of type params into generic types according to @resolvedParams
// "resolvedParams" = [(name type)] of type parameters
   if (t.v <= topVerbatimType)
      { return t; }
   TypeHeader hdr = typeReadHeader(t, cm);
   if (!hdr.isGeneric)
      { return t; }
   TParse* te = &(cm->tParse);
   te->tmp->len = 0;  // used to store start inds of type subexpressions

   Int arity = hdr.arity;
   Int genericSent = t.v + TYPE_PREFIX + arity + 1;

   add(((TypeLoc){.currPos = t.v + TYPE_PREFIX, .sentinel = genericSent}),
      te->genericWalk);
   for (; te->genericWalk->len > 0; ) {
      TypeLoc* genericLoc = &last(te->genericWalk);
      TypeId currNode = { .v = cm->types.c[genericLoc->currPos] };

      genericLoc->currPos++;
      if (genericLoc->currPos == genericLoc->sentinel) {
         Int startOfSubExp = removeLast(te->tmp);
         te->genericWalk->len--;

         Int countOfNewElts = te->exp->len - startOfSubExp;
         TypeId newType = typeOf(cm->types.len);
         pushIntypes(countOfNewElts + TYPE_PREFIX + 1, cm);
         memcpy(cm->types.c + cm->types.len, te->exp + startOfSubExp, 4*countOfNewElts);

         add(mergeType(newType, cm).v, te->tmp);
      }

      if (currNode.v < -1) {
         Int nameParam = -currNode.v - 1;
         for (Int j = 0; j < resolvedParams.len; j += 2) {
            if (resolvedParams.c[j] == nameParam) {
               add(resolvedParams.c[j + 1], te->tmp);
               break;
            }
         }
      } ei (currNode.v <= topVerbatimType) {
         add(currNode.v, te->tmp);
      } else {
         TypeHeader currHdr = typeReadHeader(currNode, cm);
         add(te->exp->len, te->tmp);
         typeExpAddHeader( // it will stop being generic once we substitute all params
            ((TypeHeader){.sort = currHdr.sort, .arity = currHdr.arity,
               .isGeneric = false, .size = currHdr.size
            }),
            te
         );

         add(tGetBody(currNode, currHdr, cm), te->genericWalk);
      }
   }
   VALIDATEI(te->exp->len == 1, ierrInconsistentTypeExpr)
   return typeOf(te->exp->c[0]);
}


TypeId //:tResolveGenericFnCall
tResolveGenericFnCall(Function fn, Arr(Int) argTypes, Int argCount, CM) {
// Finds or creates a concrete type for a generic function call.
// Example: `F[Int [A $E] -> $E]` with `F[Int [A Str] -> Str]`
// 1. Copies the argument types into @types to build an actual type
// 2. Walks two trees in depth-first fashion, left-to-right
// 3. Two corresponding nodes must be either equal, or one of them is a type param
// The result is either the function's full concrete type or a type exception (iff this generic
// function type is not unifiable with the arg types).

   ensureCapacityTypes(argCount + TYPE_PREFIX + 1, cm);

   // Define a type just for the args
   // For `F Int Str -> Double` this will look like `F Int -> Str`, i.e. the return type is missing
   TypeHeader concreteHdr = (TypeHeader){ .sort = sorDeclare, .arity = argCount,
      .name = nameOfStd(strF), .isGeneric = false, .size = 8 };
   TYPE_CREATE_START(concreteHdr);
   memcpy(cm->types.c + cm->types.len, argTypes, argCount*4);
   cm->types.len += argCount;
   TYPE_CREATE_END;
   
   TypeId args = mergeType(tentativeType, cm);
   Int concreteSent = args.v + TYPE_PREFIX + argCount; // already not a full fn type => no -1
   TypeLoc concreteLoc = (TypeLoc){
      .currPos = tGetBodyStart(args, concreteHdr), .sentinel = concreteSent
   };
   
   // Now for the generic fn type
   TypeHeader genericHdr = typeReadHeader(fn.typeId, cm);
   VALIDATEP(genericHdr.arity == argCount + 1,
      pError(errTypeGenericCallDoesntUnify, numberErr2(genericHdr.arity, argCount + 1))
   )
   Int genericSent = fn.typeId.v + TYPE_PREFIX + argCount - 1; //-1 to exclude fn return type
   TypeLoc genericLoc = (TypeLoc){
      .currPos = tGetBodyStart(fn.typeId, genericHdr), .sentinel = genericSent
   };
   
   LInt resolvedParams = tGenericUnify(genericLoc, concreteLoc, cm);
   TypeId genericReturnType = tFunctionReturnType(fn.typeId, cm);
   TypeId returnType = tGenericSubstituteParams(genericReturnType, resolvedParams, cm);
   return tGlueReturnTypeOntoFn(args, returnType, cm);
}

//}}}
//}}}
//{{{ Utils for tests & debugging

#ifdef DEBUG
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

void //:printAssocList
printAssocList(Int listInd, MultiAssocList* ml) {
   printIntArrayOff(listInd, ml->c[listInd] + 2, ml->c);
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

void //:printNameAndLenCr
printNameAndLenCr(Unt unsign, Arr(Byte) sourceCode) {
   Int startBt = unsign & LOWER24BITS;
   Int len = (unsign >> 24) & 0xFF;
   fwrite(sourceCode + startBt, 1, len, stdout);
}

void //:printNameNoLnCr
printNameNoLnCr(NameId nameId, PrintableCompiler prc) {
   Unt unsign = prc.names[nameId];
   printNameAndLenCr(unsign, prc.sourceCode);
}

Int
getFirstErrId(CM) {
   if (cm->errors->len > 0) {
      return -1;
   }
   return cm->errors->c[0].id;
}

//}}}
//{{{ Lexer testing

// Must agree in order with Token types in eyr.internal.h
char const* tokNames[] = {
   "Int", "Long", "Double", "Bool", "String", "misc",
   "word", "@TVar", ":key", "oper", ".field",
   "stmt", "clause", "TOPLEVEL", "()",
   "Type", "Struct()", "data", "a[b][c]", "[]",
   "=", "=...", "@()", "alias", "assert", "breakCont",
   "trait", "import", "return",
   "{", "if...", "eif ...", "else {", "match", "f{",
   "try{", "{catch", "impl", "for{", "{each"
};


Int
pos(LX) { return lx->i - sizeof(standardText) + 1; }


void pushIntokens0(Token t, Compiler* cm) {
   pushIntokens(t, cm);
}

Int
posInd(Int ind) { return ind - sizeof(standardText) + 1; }

void
dbgLexBtrack(LX) { //:dbgLexBtrack
   LBtToken* bt = lx->lexBtrack;

   printf("[");
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
   if (a->errors->len != b->errors->len) {
      return -1;
   }
   if (b->errors->len > 0) {
      for (Int j = 0; j < a->errors->len; j++) {
         if (a->errors->c[j].id != b->errors->c[j].id)
            { return -1; }
      }
      return -2;
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
   if (lx->errors->len > 0) {
      printf("Error: ");
      d("%s", compileErrors[lx->errors->c[0].id]);
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
   "Int", "Long", "Double", "Bool", "String", "misc",
   "var", "call",
   "{", "Expr", "=", "[data]", "Struct()",
   "assert", "breakCont", "catch", "import",
   "f{ }", "trait", "return", "try",
   "for{}", "if", "if clause", "impl", "match"
};

CompStats
getStats(CM) { return cm->stats; }

void
setLexerError(Int errId, CM) {
   add((CompileError){.id = errId}, cm->errors);
}

void
setParserError(Int errId, CM) {
   add((CompileError){.id = errId}, cm->errors);
}

void //:printParser
printParser(CM) {
   if (cm->errors->len > 0) {
      printf("Error: ");
      d("%s", compileErrors[cm->errors->c[0].id]);
   }
   Arena* a = cm->a;
   Int indent = 0;
   LInt* sentinels = createLInt(16, a);
   //CompStats stats = getStats(cm);
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
      if (nod.tp == nodCall) {
         printf("call %d argc = %d c %d [%d:%d; %d:%d] \n", nod.pl1, nod.pl2, nod.pl3,
            loc.startLine, loc.startChar, loc.endLine, loc.endChar);
      } ei (nod.pl1 != 0 || nod.pl2 != 0) {
         if (nod.pl3 != 0)  {
            printf("%s %d %d %d [%d:%d; %d:%d]\n", nodeNames[nod.tp], nod.pl1, nod.pl2, nod.pl3,
                  loc.startLine, loc.startChar, loc.endLine, loc.endChar);
         } else {
            printf("%s %d %d [%d:%d; %d:%d]\n", nodeNames[nod.tp], nod.pl1, nod.pl2,
                  loc.startLine, loc.startChar, loc.endLine, loc.endChar);
         }
      } else {
         printf("%s [%d:%d; %d:%d]\n", nodeNames[nod.tp],
                  loc.startLine, loc.startChar, loc.endLine, loc.endChar);
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
   d("]");
   printf("types: ");
   for (Int j = 0; j < len; j++) {
      dbgType(typeOf(ml->c[listInd + 2 + 2*j]));
      printf("\n");
   }
}

void //:dbgExprFrames
dbgExprFrames(CM) {
   LExprFrame* st = cm->expr.frames;
   d("Expr frames<<<");
   for (Int j = 0; j < st->len; j++) {
      ExprFrame fr = st->c[j];
      if (fr.tp == exfrCall) {
         printf("Call %d", fr.name);
      } ei (fr.tp == exfrUnaryCall) {
         printf("Unary %d", fr.name);
      } ei (fr.tp == exfrDataLit) {
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
      printf(" arg %d start %d sent %d; ", fr.argCount, fr.startNode, fr.sentinel);
      if (j % 6 == 0) {
          d("\n");
      }
   }
   printf("\n>>>\n\n");
}

void //:dbgNodes
dbgNodes(LNode* nodes) {
   for (int i = 0; i < nodes->len; i++) {
      Node nod = nodes->c[i];

      printf("%d: ", i);
      if (nod.tp == nodCall) {
         printf("call %d argc = %d call %d type = \n", nod.pl1, nod.pl2, nod.pl3);
      } ei (nod.pl1 != 0 || nod.pl2 != 0) {
         if (nod.pl3 != 0)  {
            printf("%s %d %d %d\n", nodeNames[nod.tp], nod.pl1, nod.pl2, nod.pl3);
         } else {
            printf("%s %d %d \n", nodeNames[nod.tp], nod.pl1, nod.pl2);
         }
      } else {
         printf("%s\n", nodeNames[nod.tp]);
      }
   }
}

void
setLoc(ChInterval loc, Int j, CM) { cm->sourceLocs->c[j] = locOf(loc, cm); }

void //:dbgScopes
dbgScopes(CM) {
   Scopes* s = &(cm->scopes);
   d("Scope Stack<<<");
   if (!(s->currChunk->prev) && s->curr - s->currChunk->c <= 1)
      { goto closing; }
   ScopeChunk* ch = s->currChunk;

   Int currScopeLen = s->currScopeLen;
   printf("Scope with %d bindings: [", currScopeLen);

   Int* p = s->curr;
   if (p > ch->c) {
      p--; // sc->curr points to the place for next binding, not to last existing binding
   } else {
      ch = ch->prev;
      p = ch->c + SCOPE_CHUNK_SZ;
   }
   for (; p >= ch->c || ch->prev; p--) {
      if (currScopeLen == 0) {
         d("]");
         currScopeLen = *p;
         if ((p - 1) > ch->c || ch->prev) {
            printf("Scope with %d bindings: [", currScopeLen);
         }
      } else {
         printf("%d ", *p);
         currScopeLen--;
      }
      if (p == ch->c) {
         ch = ch->prev;
         p = ch->c + SCOPE_CHUNK_SZ;
      }
   }
   d("]");
closing:
   printf(">>>\n\n");
}

void
dbgPrintScope(Int** p, Int* scopeLen, ScopeChunk* scChunk, Scopes* sc) {
   Bool hasBindings = *scopeLen > 0;
   if (hasBindings) {
      printf("   %d bindings: [", *scopeLen);
   } else {
      d("   no bindings");
   }
   Bool stillSameScope = true;
   for (; ((*p) >= scChunk->c || scChunk->prev) && stillSameScope; (*p)--) {
      if (*scopeLen == 0) {
         if (hasBindings)
            { d("]"); }
         *scopeLen = **p;
         hasBindings = *scopeLen > 0;
         stillSameScope = false;
      } else {
         printf("%d ", **p);
         (*scopeLen)--;
      }
      if (*p == scChunk->c) {
         scChunk = scChunk->prev;
         *p = scChunk->c + SCOPE_CHUNK_SZ;
      }
   }
}

void //:dbgParseFrames
dbgParseFrames(CM) {
   Scopes* sc = &(cm->scopes);
   ScopeChunk* scChunk = sc->currChunk;
   Int* p = sc->curr;
   Int scopeLen = sc->currScopeLen;
   if (p > scChunk->c) {
      p--; // sc->curr points to the place for next binding, not to last existing binding
   } else {
      scChunk = scChunk->prev;
      p = scChunk->c + SCOPE_CHUNK_SZ;
   }
   printIntArrayOff(0, 7, scChunk->c);
   d("p init %d", p - scChunk->c);

   d("Parse frames (%d scopes) <<<", sc->countScopes);
   for (Int indFrame = cm->parseFrames->len - 1;
        indFrame > -1;
        indFrame--
   ) {
      ParseFrame fr = cm->parseFrames->c[indFrame];
      switch (fr.level) {
      case pfrScope: printf("Scope "); break;
      case pfrLoop: printf("Loop "); break;
      case pfrFn: printf("Fn "); break;
      default: printf("Scopeless frame "); break;
      }
      d("sent %d", fr.sentinel);

      if (fr.level > 0) {
         dbgPrintScope(&p, &scopeLen, scChunk, sc);
      }
   }

   d(">>>\n");
}

//}}}
//{{{ Types testing


void
dbgTypeFrames(TParse* te) { //:dbgTypeFrames
   LTypeFrame* frames = te->frames;
   d(">>> Type frames cnt %d", frames->len);
   for (Int j = 0; j < frames->len; j++) {
      TypeFrame fr = frames->c[j];
      if (fr.tp == tfrFunction) {
         printf("Func ");
      } ei (fr.tp == tfrTypeCall) {
         printf("TypeCall ");
      }
      if (fr.isGeneric)
         { printf("generic "); }
      printf("typeArgs: %d ", fr.countArgs);
      printf("sent: %d \n", fr.sentinel);
   }
   printf(">>>\n\n");
}

void
dbgOverloads(Int nameId, CM) { //:dbgOverloads
   Int listId = -cm->activeBindings[nameId] - 2;
   if (listId < 0) {
      d("Overloads for name %d not found", nameId)
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
   printf("\n[fnId = ");
   sentinel += countOverloads;
   for (; j < sentinel; j++) {
      printf("%d ", overs[j]);
   }
   printf("]\n\n");
}

void
dbgAllTypes(CM) {
   for (Int j = outerTypeForTypeParam + 1; j < cm->types.len; j += (cm->types.c[j] + 1)) {
      dbgType(typeOf(j));
   }
}

//}}}
#endif

//{{{ Tests only

#ifdef DEBUG

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

Arr(TypeId) //:importTestTypes
importTestTypes(Arr(Int) types, Int countTypes, CM, Arena* aTmp) {
// Importing simple function types for testing purposes
   Int countImportedTypes = 0;
   for (Int j = 0; j < countTypes; j += (types[j] + 1)) {
      if (types[j] == 0)
         { return NULL; } // should never happen
      countImportedTypes++;
   }
   Arr(TypeId) typeIds = allocateOnArena(countImportedTypes*4, aTmp);
   Int t = 0;
   for (Int j = 0; j < countTypes; t++) {
      const Int importLen = types[j];
      const Int typeSentinel = j + importLen + 1;
      TypeId initTypeId = typeOf(cm->types.len);

      pushIntypes(importLen + TYPE_PREFIX - 1, cm);

      typeAddHeader((TypeHeader){
         .sort = sorDeclare, .arity = importLen, .name = nameOfStd(strF), .size = 8 },
         cm
      );
      for (Int k = j + 1; k < typeSentinel; k++) {
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
         .name = nameOfStd(strSentinel - 3 + imports[j].nameInd),
         .typeId = typeIds[imports[j].typeInd]
      };
   }
   importFns(importedFns, countImports, cm);
}

CompResult*
getCompResult(CM) {
   CompResult* cr = allocate(CompResult, cm->a);
   fillInCompilationResult(cm, OUT cr);
   return cr;
}

Int //:equalityParser
equalityParser(/* test specimen */Compiler* a, /* expected */Compiler* b, Bool compareLocsToo) {
// Returns -2 if parsers are equal, -1 if they differ in errorfulness, and the index of the first
// differing node otherwise
   if (a->errors->len != b->errors->len) {
      return -1;
   }
   if (b->errors->len > 0) {
      for (Int j = 0; j < a->errors->len; j++) {
         if (a->errors->c[j].id != b->errors->c[j].id)
            { return -1; }
      }
      return -2;
   }

   CompResult* statsA = getCompResult(a);
   CompResult* statsB = getCompResult(b);
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
         if (locA.startLine != locB.startLine || locA.startChar != locB.startChar
          || locA.endLine != locB.endLine || locA.endChar != locB.endChar
         ) {
            printf("\n\nUNEQUAL SOURCE LOCS on %d\n", i);
            if (locA.startLine != locB.startLine || locA.startChar != locB.startChar) {
               printf("Diff in start pos, %d:%d but was expected %d:%d\n",
                  locA.startLine, locA.startChar, locB.startLine, locB.startChar
               );
            }
            if (locA.endLine != locB.endLine || locA.endChar != locB.endChar) {
               printf("Diff in end pos, %d:%d but was expected %d:%d\n",
                  locA.endLine, locA.endChar, locB.endLine, locB.endChar
               );
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
// - function declarations for the built-in operator overloads
// - raw overloads with counts
   (*proto) = (Compiler){
      .vars = createInListVar(32, a),
      .functions = createInListFunction(8, a),
      .sourceCode = str(standardText),
      .names = createLUnt(16, a), .stringDict = createStringDict(128, a),
      .types = createInListInt(64, a), .typesDict = createStringDict(128, a),
      .rawOverloads = createMultiAssocList(a),
      .fieldNames = createInListFieldName(16, a),
      .fieldTypes = createInListInt(16, a),
      .stats = (CompStats) {
         .standardTextLen = sizeof(standardText) - 1,
         .firstParsedName = (strSentinel + countOperators),
         .firstBuiltin = countOperators,
         .countOverloads = PROTO.stats.countOverloads,
         .countOverloadedNames = PROTO.stats.countOverloadedNames
      },
      .a = a
   };

   // operators are always active, and take up the initial chunk of names
   createBuiltinsForProto(proto);
}

private void //:initCompiler
initCompiler() {
// Definition of the operators, lexer dispatch, parser dispatch etc tables for the compiler.
// This function should only be called once, at compiler init.
// Its results are global shared const.
   static_assert(TYPE_PREFIX == sizeof(TypeHeader)/4 + 1, "Sizeof TypeHeader check");
   static_assert(sizeof(TypeId) == 4, "C has added useless some padding to opaque id TypeId!");
   static_assert(sizeof(compileErrors)/sizeof(char*) == errMaxId + 1,
      "CompilerErrors are out of sync with errMaxId!"
   );

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

CompResult* //:libeyr_compile
libeyr_compile(String sourceCode) {
   Arena* a = createArena();
   CompResult* cr = allocate(CompResult, a);
   if (sourceCode.len == 0) {
      cr->wasLexerError = true;
      cr->errors = allocate(libeyr_CompilationErrors, a);
      cr->errors->len = 1;
      cr->errors->c[0] = ((CompileError){
         .id = errPrematureEndOfInput,
         .positional = (ErrorPosition){.count = 0},
         .textual = (ErrorText){.count = 0}
      });
      return cr;
   }

   initCompiler();
   Compiler* cm = lexicallyAnalyze(sourceCode, a);
   if (cm->errors->len > 0) {
#if defined(DEBUG)
      d("%s", compileErrors[cm->errors->c[0].id]);
#endif

      cr->wasLexerError = true;
      cr->errors = allocate(libeyr_CompilationErrors, a);
      *(cr->errors) = (libeyr_CompilationErrors){.c = cm->errors->c, .len = cm->errors->len};
      return cr;
   }

   cm = parse(cm, a);
   if (cm->errors->len > 0) {

#if defined(DEBUG)
   d("%s", compileErrors[cm->errors->c[0].id]);
#endif
      cr->wasParserError = true;
      cr->errors = getCompilationErrors(cm);
      return cr;
   }
   fillInCompilationResult(cm, OUT cr);
   return cr;
}

CompResult* //:libeyr_compileFile
libeyr_compileFile(String filename) {
   Arena* a = createArena();
   CompResult* cr = allocate(CompResult, a);
   cr->a = a;
   if (filename.len == 0) {

      cr->errors = allocate(libeyr_CompilationErrors, cr->a);
      CompileError* err = allocate(CompileError, cr->a);
      *err = (CompileError){.id = errEmptySourceCode};
      *(cr->errors) = (libeyr_CompilationErrors){.c = err, .len = 1 };
      cr->wasLexerError = true;
      return cr;
   }
   initCompiler();

   String sourceCode = readSourceFile(filename, a);

   Compiler* cm = lexicallyAnalyzeFromFile(sourceCode, a);
   if (cm->errors->len > 0) {
      fillInCompilationResult(cm, cr);
      cr->wasLexerError = true;
      return cr;
   }
   cm = parse(cm, a);
   if (cm->errors->len > 0) {
      fillInCompilationResult(cm, cr);
      cr->errors = getCompilationErrors(cm);
      cr->wasParserError = true;
      return cr;
   }

#ifdef VERBOSE
   //printParser(cm);
#endif

   fillInCompilationResult(cm, OUT cr);
   return cr;
}

private libeyr_CompilationErrors* //:getCompilationErrors
getCompilationErrors(CM) {
   libeyr_CompilationErrors* errors = allocate(libeyr_CompilationErrors, cm->a);
   *errors = (libeyr_CompilationErrors){.c = cm->errors->c, .len = cm->errors->len};
   return errors;
}

private void //:fillInCompilationResult
fillInCompilationResult(CM, OUT CompResult* cr) {
   *cr = (CompResult) {
      .sourceCode = (StringBuilder){
         .c = cm->sourceCode.c, .len = cm->sourceCode.len, .cap = cm->sourceCode.len
      },
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
      .genericFields = sliceOfInternal(cm->genericFields),
      .names = cm->names != null
            ? ((SliUnt){.len = cm->names->len, .c = cm->names->c})
            : ((SliUnt){.len = 0, .c = null}),
      .errors = getCompilationErrors(cm),
      .a = cm->a,
      .stats = cm->stats,
      .wasLexerError = (cm->ast.c == null ? (cm->errors->len > 0) : false),
      .wasParserError = (cm->ast.c == null ? false : (cm->errors->len > 0))
   };
}

//}}}

