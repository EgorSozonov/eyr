//{{{ Common for tests

typedef struct {
    Int countTests;
    Int countPassed;
    Arena* a;
} TestContext;

//}}}
//{{{ Tokens

typedef struct {
   Unt tp : 6;
   Unt lenBts: 26;
   Unt startBt;
   Unt pl1;
   Unt pl2;
} Token;

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
#define firstSpanTokenType  tokStmt
#define firstScopeTokenType tokScope
#define countSyntaxForms    (tokEach + 1)

//}}}
//{{{ Lexer tests

String prepareInput(char const* content, Arena* a);
Compiler* lexicallyAnalyze(String input, Arena*);
void printLexer(Compiler* a);
int64_t longOfDoubleBits(double d);
void printIntArray(Int count, Arr(Int) arr);
void printIntArrayOff(Int startInd, Int count, Arr(Int) arr);
void initializeParser(Compiler* lx, Arena* a);
Compiler* createLexer(String sourceCode, Bool prependStandard, Arena* a);
NameId nameOfStandard(Int strId);
void printRawOverload(Int listInd, Compiler* cm);
void printName(Int name, Compiler* cm);
void setLexerError(String errMsg, CM);

//}}}
//{{{ Lexer



void printLexer(Compiler* restrict a);
Int equalityLexer(Compiler* a, Compiler* b);
void pushIntokens(Token, Compiler*);

extern char const errNonAscii[];
extern char const errPrematureEndOfInput[];
extern char const errUnrecognizedByte[];
extern char const errWordChunkStart[];
extern char const errWordCapitalizationOrder[];
extern char const errWordUnderscoresOnlyAtStart[];
extern char const errWordWrongAccessor[];
extern char const errWordLengthExceeded[];
extern char const errWordMutability[];
extern char const errWordFreeFloatingFieldAcc[];
extern char const errWordInMeta[];
extern char const errNumericEndUnderscore[];
extern char const errNumericWidthExceeded[];
extern char const errNumericBinWidthExceeded[];
extern char const errNumericFloatWidthExceeded[];
extern char const errNumericEmpty[];
extern char const errNumericMultipleDots[];
extern char const errNumericIntWidthExceeded[];
extern char const errPunctuationExtraOpening[];
extern char const errPunctuationExtraClosing[];
extern char const errPunctuationCommaNotClause[];
extern char const errPunctuationOnlyInMultiline[];
extern char const errPunctuationFnNotInStmt[];
extern char const errPunctuationUnmatched[];
extern char const errPunctuationScope[];
extern char const errOperatorUnknown[];
extern char const errOperatorAssignmentPunct[];
extern char const errAssignmentEmptyRight[];
extern char const errOperatorTypeDeclPunct[];
extern char const errOperatorMutationInDef[];
extern char const errCoreNotInsideStmt[];
extern char const errCoreMisplacedElse[];
extern char const errCoreMissingParen[];
extern char const errIndentation[];

// Span levels, must all be more than 0
#define slScope        1 // scopes (denoted by brackets): newlines and commas have no effect there
#define slStmt         2 // single-line statements: newlines and semicolons break 'em
#define slSubexpr      3 // parenthesized forms: newlines have no effect, semi-colons error out
#define slClauseList   4 // a comma-separated list
#define slUnbraced     5 // A scope that hasn't met its first brace, like an "if" before its "{"
#define slSingleBraced 6 // A "for" scope that has met exactly 1 curly brace


//}}}
