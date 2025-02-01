#ifndef EYR_INTERNAL_H
#define EYR_INTERNAL_H
//{{{ Utils

typedef int32_t Int;
typedef uint32_t Unt;
typedef int64_t Long;
typedef uint64_t Ulong;
typedef int16_t Short;
typedef uint16_t Ushort;
typedef char Byte;
typedef bool Bool;
typedef tech_sozonov_eyr_String String;
#define StackInt Stackint32_t
#define StackUnt Stackuint32_t
#define InListUlong InListuint64_t
#define InListUnt InListuint32_t
#define Any void
#define Arr(T) T*
#define AARG(var, T) var, sizeof(var)/sizeof(T) // For passing array args to functions with length
#define null NULL
#define VarId int32_t
#define FunctionId int32_t
#ifdef TEST
   #define private
#else
   #define private static
#endif
#define OUT // the "out" parameters and args in functions
#define BIG 70000000
#define LOWER24BITS 0x00FFFFFF
#define LOWER26BITS 0x03FFFFFF
#define LOWER16BITS 0x0000FFFF
#define LOWER32BITS 0x00000000FFFFFFFF
#define PENULTIMATE8BITS 0xFF00
#define THIRTYFIRSTBIT 0x40000000
#define MAXTOKENLEN 67108864 // 2^26
#define SIXTEENPLUSONE 65537 // 2^16 + 1
#define LEXER_INIT_SIZE 1000
#define ei else if
#define print(...) \
  printf(__VA_ARGS__);\
  printf("\n");

#define dg(...) \
  printf(__VA_ARGS__);\
  printf("\n");

typedef struct ArenaChunk ArenaChunk;
typedef struct Arena Arena;


private void printStringNoLn(String s);
private void printString(String s);

constexpr String empty = {.cont = null, .len = 0};
private String str(const char* content);
private Bool endsWith(String a, String b);

#define s(lit) str(lit)

//}}}
//{{{ Standard strings :standardStr

#define strAlias     0
#define strAssert    1
#define strBreak     2
#define strCatch     3
#define strContinue  4
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
//{{{ Lexer

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

#define assiVarAssignment  1 // definition of a var
#define assiTypeDefinition 2 // definition of a type
#define assiFnParam        3 // introduction of a function parameter
#define assiReassignment   4 // reassignment to a previously defined var
#define assiFnVarDef       5 // definition of a local var that is a function
#define assiFnVarUse       6 // usage (NOT an assignment) of a variable that is a function

//}}}
//{{{ Parser

// AST nodes
#define nodVar          7  // pl1 = index into @vars.
                           // pl2 = iff pl3 = assiFnVarUse, assiFnVarDef then fnId
                           // pl3 >0 => it's a definition (except if pl3 = assiFnVar...) and is one
                           // of the "assi" constants
#define nodCall         8  // pl1 =
                           //   index into @functions (after type resolution) when pl3 = callNormal,
                           //   into @monos if pl3 = callMonomorph,
                           //   into @vars if pl3 = callVar
                           //     pl2 = arg count, pl3 = one of "call" constants.
                           // iff pl3 = callField, then pl1 = nameId, pl2 = 0

// Punctuation (inner node). pl2 = node count inside (so for [span node1 node2], span.pl2 = 2)
#define nodScope        9  // if it's the outer scope of a forNode, then pl3 = length of nodes till
                           // inner scope. See parser tests for examples
#define nodExpr        10  // pl1 = 1 iff it's a composite expression (has internal var decls)
#define nodAssignment  11  // Followed by binding or complex left side. pl3 = distance to the inner
                           // right side, which is always an atom, nodExpr or a nodDataAlloc
#define nodDataAlloc   12  // pl1 = name of collection type, pl3 = count of elements

#define nodAssert      13  // pl1 = 1 iff it's a debug assert
#define nodBreakCont   14  // pl1 = number of label to break or continue to, -1 if none needed
                           // It's a continue iff it's >= BIG
#define nodCatch       15  // `catch e {`
#define nodImport      16  // This is for test files only, no need to import anything in main
#define nodFnDef       17  // pl1 = index into @functions
#define nodDef         18  // pl1 = entityId, pl3 = nameId. For non-function compile-time consts
#define nodTrait       19
#define nodReturn      20
#define nodTry         21
#define nodFor         22  // pl1 = id of loop (unique within a function) if it needs to
                           // have a label in codegen; pl3 = number of nodes to skip to get to body

#define nodIf          23
#define nodIfClause    24  // pl3 = "ifcl" constants
#define nodImpl        25
#define nodMatch       26  // pattern matching on sum type tag
#define countAstForms  27  // sentinel

#define countSpanForms (countAstForms - nodScope)

#define metaDoc         1  // Doc comments
#define metaDefault     2  // Default values for type arguments


// :OperatorType
// Values must exactly agree in order with the operatorSymbols array in the tl.c file.
// The order is defined by ASCII. Operator is bitwise <=> it ends with dot
#define opBitwiseNeg      0 // !. bitwise negation
#define opNotEqual        1 // !=
#define opBoolNeg         2 // !
#define opSize            3 // #
#define opToString        4 // $
#define opRemainder       5 // %
#define opBitwiseAnd      6 // &&. bitwise "and"
#define opBoolAnd         7 // &&  logical "and"
#define opRef             8 // '  References
#define opTimesExt        9 // *:
#define opTimes          10 // * Multiplication and nullable pointers
#define opIncrement      11 // ++
#define opPlusExt        12 // +:
#define opPlus           13 // +
#define opDecrement      14 // --
#define opMinusExt       15 // -:
#define opMinus          16 // -
#define opNegate         17 // -
#define opDivByExt       18 // /:
#define opIntersect      19 // /\   type-level trait intersection ?
#define opDivBy          20 // /
#define opBitShiftL      21 // <<.
#define opComparator     22 // <=>
#define opLTZero         23 // <0   less than zero
#define opLTEQ           24 // <=
#define opLessTh         25 // <
#define opRefEquality    26 // ===
#define opEquality       27 // ==
#define opBitShiftR      28 // >>.  unsigned right bit shift
#define opGTZero         29 // >0   greater than zero
#define opGTEQ           30 // >=
#define opGreaterTh      31 // >
#define opNullCoalesce   32 // ?:   null coalescing operator
#define opQuestionMark   33 // ?   Initially nullable pointers
#define opAwait          34 // @
#define opBitwiseXor     35 // ^.   bitwise XOR
#define opBitwiseOr      36 // ||.  bitwise or
#define opBoolOr         37 // ||   logical or
#define opGetElem        38 // Get list element
#define opGetElemPtr     39 // Get pointer to list element
#define countOperators   40 // sentinel

constexpr Int countRealOperators = countOperators - 2; // The "unreal" ones are `a[..]`

typedef struct Compiler Compiler;

typedef struct { // :Node
   Unt tp : 6;
   Unt pl3: 26;
   Int pl1;
   Int pl2;
} Node;

typedef struct { // :SourceLoc
   Int startBt;
   Int lenBts;
} SourceLoc;


typedef struct StandardText StandardText;
typedef struct Entity Entity;

typedef struct ScopeChunk ScopeChunk;

typedef struct { // :CompStats
   Int inpLength;
   Bool wasLexerError;

   Int countNonparsedVars;
   Int countNonparsedFns;
   Int countOverloads;
   Int countOverloadedNames;
   Int countOperatorFns;
   Int toksLen;
   Int nodesLen;
   Int typesLen;
   Int loopCounter;
   Bool wasError;
   String errMsg;
   Int listType;

   Int standardTextLen; // length of standardText
   Int firstParsedName; // the name index for the first parsed word
   Int firstBuiltin;    // the name for the first built-in word in standardStrings
} CompStats;

//}}}
//{{{ Interpreter

// Instructions (opcodes)
// An instruction is 8 byte long and consists of 6-bit opcode and some data
// Notation: [A] is a 2-byte stack address, it's signed and is measured relative to currFrame
//         [~A] is a 3-byte constant or offset
//         {A} is a 4-byte constant or address
//         {{A}} is an 8-byte constant (i.e. it takes up a whole second instruction slot)
#define iPlus              0 // [Dest] [Operand1] [Operand2]
#define iMinus             1
#define iTimes             2
#define iDivBy             3
#define iPlusFl            4
#define iMinusFl           5
#define iTimesFl           6
#define iDivByFl           7
#define iPlusConst         8 // [Src=Dest] {Increment}
#define iMinusConst        9
#define iTimesConst       10
#define iDivByConst       11
#define iPlusFlConst      12 // [Src=Dest] {{Double constant}}
#define iMinusFlConst     13
#define iTimesFlConst     14
#define iDivByFlConst     15
#define iConcatStrs       16 // [Dest] [Operand1] [Operand2]
#define iLoadConstString  17 // [Dest] {addr}
#define iSubstring        18 // [Dest] [Src] {{ {Start} {Len}  }}
#define iReverseString    19 // [Dest] [Src]
#define iIndexOfSubstring 20 // [Dest] [String] [Substring]
#define iGetFld           21 // [Dest] [Obj] [~Offset]
#define iNewList          22 // [Dest] {Capacity}
#define iGetElemPtr       23 // [Dest] [ArrAddress] {{ {0} {Elem index} }}
#define iAddToList        24 // [List] {Value or reference}
#define iRemoveFromList   25 // [List] {Elem Index}
#define iSwap             26 // [List] {{ {Index1} {Index2} }}
#define iConcatLists      27 // [Dest] [Operand1] [Operand2]
#define iJump             28 // { Code pointer }
#define iBranchLt         29 // [Operand] { Code pointer }
#define iBranchEq         30
#define iBranchGt         31
#define iShortCircuit     32 // if [B] == [C] then [A] = [B] else ip += 1
#define iCall             33 // [New frame start pointer] { New instruction pointer }
#define iBuiltinCall      34 // [Builtin index]
#define iReturn           35 // [ address to return ] [Size of return value = 0, 1 or 2]
#define iSetLocal         36 // [Dest] {Value}
#define iSetBigLocal      37 // [Dest] {{Value}}
#define iPrint            38 // [String]
#define iPrintInt         39 // [Local]
#define iPrintErr         40 // [String]
#define iFn               41 // {len of body, not including this instruction} Start of a function
#define countInstructions 42 // sentinel value

//}}}
#endif
