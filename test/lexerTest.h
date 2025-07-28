//{{{ Common for tests

typedef struct {
    Int countTests;
    Int countPassed;
    Int singleId; // if > -1, then only test with this id will be run
    Bool ranSingle; // did we run the single test? Or we didn't encounter that id?
    Bool wasSuccess;
    Arena* a;
} TestContext;

int
getReturnCode(TestContext ct) {
   if (ct.countPassed > 0 
         && ((ct.ranSingle && ct.countPassed == 1) || ct.countPassed == ct.countTests)) {
      return 0;
   } else {
      return -1;
   }
}

typedef libeyr_String String;
typedef libeyr_CompResult CompResult;
#define BIG 70000000

//{{{ Errors

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
#define errExpressionWrongArgCount     61
#define errExpressionCannotContain     62
#define errExpressionFunctionless      63
#define errTypeDefCountNames           64
#define errTypeDefCannotContain        65
#define errTypeExpr                    66
#define errTypeDefError                67
#define errTypeDefParamsError          68
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
#define errTypeFieldNotFound          108

Int
getFirstErrId(CM);

//}}}

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

#define tokMisc         5  // pl1 = see the misc* constants. pl2 = underscore count iff
                           // miscUnderscore, step iff nonzero and miscLoopStep
                           // Also stands for "Void" among the primitive types
                           // Also works as a marker in "for" loops: initially it's placed after
                           // tokFor and pl2 = token ind of body start.
                           // After {reorderFor}, it's placed right between body and stepping code.
#define tokWord         6  // pl1 = nameId (index in @names). pl2 = 1 iff followed by '
#define tokTypeVar      7  // pl1 same as tokWord. `$A`
#define tokKey          8  // pl1 = same as tokWord. `:argName` or `:structField` or `:dictKey`
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
#define tokAssignment  20
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
void printRawOverload(Int listInd, Compiler* cm);
void printName(Int name, Compiler* cm);
void setLexerError(Int errId, CM);

//}}}
//{{{ Lexer

void printLexer(Compiler* restrict a);
Int equalityLexer(Compiler* a, Compiler* b);
void pushIntokens0(Token, Compiler*);

// Span levels, must all be more than 0
#define slScope        1 // scopes (denoted by brackets): newlines and commas have no effect there
#define slStmt         2 // single-line statements: newlines and semicolons break 'em
#define slSubexpr      3 // parenthesized forms: newlines have no effect, semi-colons error out
#define slClauseList   4 // a comma-separated list
#define slUnbraced     5 // A scope that hasn't met its first brace, like an "if" before its "{"
#define slSingleBraced 6 // A "for" scope that has met exactly 1 curly brace

#define miscPub        0    // pub. It must be 0 because it's the only one denoted by a keyword
#define miscUnderscore 1    // _
#define miscArrow      2    // ->
#define miscForStep0   3    // token that provides space for a "for" loop reorganization
#define miscForStep    4    // token that marks stepping code in a "for" loop
#define miscEachElem   5    // `coll.@` in an "each" loop - element
#define miscEachInd    6    // `coll.#` in an "each" loop - index of element

//}}}
