#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include "../include/libeyr.h"
#include "lexerTest.h"

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
#define firstSpanTokenType  tokStmt
#define firstScopeTokenType tokScope
#define countSyntaxForms    (tokEach + 1)

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

typedef struct {
    String name;
    String input;
    Compiler* expectedOutput;
} LexerTest;


typedef struct {
    String name;
    int totalTests;
    LexerTest* tests;
} LexerTestSet;

//{{{ Utils

#define S   70000000 // A constant larger than the largest allowed file size. Separates parsed
                     // names from others

private Compiler* buildExpectedLexer(Arena *a, int totalTokens, Arr(Token) tokens) {
    Compiler* result = createLexer(empty, true, a);
    if (result == NULL) return result;

    CompStats stats = getStats(result);
    if (tokens == NULL) {
        return result;
    }
    for (int i = 0; i < totalTokens; i++) {
        Token tok = tokens[i];
        // offset nameIds and startBts for the standardText and standard nameIds correspondingly
        tok.startBt += stats.standardTextLen;
        if (tok.tp == tokWord || tok.tp == tokKwArg || tok.tp == tokTypeName||
             (tok.tp == tokOperator && tok.pl2 == 10)
             || tok.tp == tokTypeVar || tok.tp == tokFieldAcc) {
            if (tok.pl1 < S) { // parsed words
                tok.pl1 += stats.firstParsedName;
            } else { // built-in words
                tok.pl1 -= S;
                tok.pl1 += countOperators;
            }
        }
        pushIntokens(tok, result);
    }

    return result;
}

#define expect(toks) buildExpectedLexer(a, sizeof(toks)/sizeof(Token), toks)
#define expectEmpty(toks) buildExpectedLexer(a, 0, NULL)


private Compiler* buildLexerWithError0(String errMsg, Arena *a,
                                       Int totalTokens, Arr(Token) tokens) {
    Compiler* result = buildExpectedLexer(a, totalTokens, tokens);
    setLexerError(errMsg, result);
    return result;
}

#define buildLexerWithError(msg, toks) buildLexerWithError0(msg, a, sizeof(toks)/sizeof(Token), toks)
#define expectEmptyWithError(msg) buildLexerWithError0(msg, a, 0, NULL)


private LexerTestSet* createTestSet0(String name, Arena *a, int count, Arr(LexerTest) tests) {
    LexerTestSet* result = allocateOnArena(sizeof(LexerTestSet), a);
    result->name = name;
    result->totalTests = count;
    result->tests = allocateOnArena(count*sizeof(LexerTest), a);
    if (result->tests == NULL) return result;
    for (int i = 0; i < count; i++) {
        result->tests[i] = tests[i];
    }
    return result;
}

#define createTestSet(n, a, tests) createTestSet0(n, a, sizeof(tests)/sizeof(LexerTest), tests)


void runLexerTest(LexerTest test, TestContext* ct) {
// Runs a single lexer test and prints err msg to stdout in case of failure. Returns error code
    ct->countTests += 1;
    Compiler* result = lexicallyAnalyze(test.input, ct->a);

    int equalityStatus = equalityLexer(result, test.expectedOutput);
    if (equalityStatus == -2) {
        ct->countPassed += 1;
        return;
    } else if (equalityStatus == -1) {
        printf("\n\nERROR IN [");
        printStringNoLn(test.name);
        printf("]\nError msg: ");
        CompStats stats = getStats(result);
        CompStats expectedStats = getStats(test.expectedOutput);
        printString(stats.errMsg);
        if (expectedStats.wasError) {
            printf("\nBut was expected: ");
            printString(expectedStats.errMsg);
        } else {
            printf("\nBut was expected to be error-free\n");
        }
        printLexer(result);
    } else {
        printf("ERROR IN [");
        printStringNoLn(test.name);
        printf("]\nOn token %d\n", equalityStatus);
        printLexer(result);
    }
}
//}}}
//{{{ Word

LexerTestSet* wordTests(Arena* a) {
    return createTestSet(s("Word lexer test"), a, ((LexerTest[]) {
        (LexerTest) {
            .name = s("Simple word lexing"),
            .input = s("asdf abc;"),
            .expectedOutput = expect(((Token[]){
                    (Token){ .tp = tokStmt, .pl2 = 2, .startBt = 0, .lenBts = 9 },
                    (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 4 },
                    (Token){ .tp = tokWord, .pl1 = 1, .startBt = 5, .lenBts = 3 }
            }))
        },
        (LexerTest) {
            .name = s("Word correct capitalization 1"),
            .input = s("asdf:Abc"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 8  },
                (Token){ .tp = tokTypeName, .pl2 = 0, .startBt = 0, .lenBts = 8  }
            }))
        },
        (LexerTest) {
            .name = s("Word correct capitalization 2"),
            .input = s("asdf:abcd:zyui"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 14  },
                (Token){ .tp = tokWord, .startBt = 0, .lenBts = 14  }
            }))
        },
        (LexerTest) {
            .name = s("Word correct capitalization 3"),
            .input = s("asdf:Abcd"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 9 },
                (Token){ .tp = tokTypeName, .startBt = 0, .lenBts = 9 }
            }))
        },
        (LexerTest) {
            .name = s("Field accessor"),
            .input = s("a.field"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 2, .startBt = 0, .lenBts = 7 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokFieldAcc, .pl1 = 1, .startBt = 1, .lenBts = 6 }
            }))
        },
        (LexerTest) {
            .name = s("Function call"),
            .input = s("call a;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt,           .pl2 = 2, .startBt = 0, .lenBts = 7 },
                (Token){ .tp = tokWord, .pl1 = 0, .pl2 = 0, .startBt = 0, .lenBts = 4 },
                (Token){ .tp = tokWord, .pl1 = 1,           .startBt = 5, .lenBts = 1 }
            }))
        },
        (LexerTest) {
            .name = s("Word starts with reserved word"),
            .input = s("ifter"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 5 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 5 }
            }))
        }
    }));
}

//}}}
//{{{ Numeric

LexerTestSet* numericTests(Arena* a) {
    return createTestSet(s("Numeric lexer test"), a, ((LexerTest[]) {
        (LexerTest) {
            .name = s("Hex numeric 1"),
            .input = s("0x15"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 4 },
                (Token){ .tp = tokInt, .pl2 = 21, .startBt = 0, .lenBts = 4 }
            }))
        },
        (LexerTest) {
            .name = s("Hex numeric 2"),
            .input = s("0x05"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 4 },
                (Token){ .tp = tokInt, .pl2 = 5, .startBt = 0, .lenBts = 4 }
            }))
        },
        (LexerTest) {
            .name = s("Hex numeric 3"),
            .input = s("0xFFFFFFFFFFFFFFFF"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 18 },
                (Token){ .tp = tokInt, .pl1 = ((int64_t)-1 >> 32), .pl2 = ((int64_t)-1 & LOWER32BITS),
                        .startBt = 0, .lenBts = 18  }
            }))
        },
        (LexerTest) {
            .name = s("Hex numeric 4"),
            .input = s("0xFFFFFFFFFFFFFFFE"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 18 },
                (Token){ .tp = tokInt, .pl1 = ((int64_t)-2 >> 32), .pl2 = ((int64_t)-2 & LOWER32BITS),
                        .startBt = 0, .lenBts = 18  }
            }))
        },
        (LexerTest) {
            .name = s("Hex numeric too long"),
            .input = s("0xFFFFFFFFFFFFFFFF0"),
            .expectedOutput = buildLexerWithError(s(errNumericBinWidthExceeded), ((Token[]) {
                (Token){ .tp = tokStmt }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric 1"),
            .input = s("1.234"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 5 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(1.234) >> 32,
                         .pl2 = longOfDoubleBits(1.234) & LOWER32BITS, .startBt = 0, .lenBts = 5 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric 2"),
            .input = s("00001.234"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 9 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(1.234) >> 32,
                         .pl2 = longOfDoubleBits(1.234) & LOWER32BITS, .startBt = 0, .lenBts = 9 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric 3"),
            .input = s("10500.01"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 8 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(10500.01) >> 32,
                         .pl2 = longOfDoubleBits(10500.01) & LOWER32BITS, .startBt = 0, .lenBts = 8 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric 4"),
            .input = s("0.9"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 3 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(0.9) >> 32,
                         .pl2 = longOfDoubleBits(0.9) & LOWER32BITS, .startBt = 0, .lenBts = 3 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric 5"),
            .input = s("100500.123456"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 13 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(100500.123456) >> 32,
                         .pl2 = longOfDoubleBits(100500.123456) & LOWER32BITS,
                         .startBt = 0, .lenBts = 13 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric big"),
            .input = s("9007199254740992.0"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 18 },
                (Token){ .tp = tokDouble,
                    .pl1 = longOfDoubleBits(9007199254740992.0) >> 32,
                    .pl2 = longOfDoubleBits(9007199254740992.0) & LOWER32BITS,
                    .startBt = 0, .lenBts = 18 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric too big"),
            .input = s("9007199254740993.0"),
            .expectedOutput = buildLexerWithError(s(errNumericFloatWidthExceeded), ((Token[]) {
                (Token){ .tp = tokStmt }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric big exponent"),
            .input = s("1005001234560000000000.0"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 24 },
                (Token){ .tp = tokDouble,
                        .pl1 = longOfDoubleBits(1005001234560000000000.0) >> 32,
                        .pl2 = longOfDoubleBits(1005001234560000000000.0) & LOWER32BITS,
                        .startBt = 0, .lenBts = 24 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric tiny"),
            .input = s("0.0000000000000000000003"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 24 },
                (Token){ .tp = tokDouble,
                        .pl1 = longOfDoubleBits(0.0000000000000000000003) >> 32,
                        .pl2 = longOfDoubleBits(0.0000000000000000000003) & LOWER32BITS,
                        .startBt = 0, .lenBts = 24 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric negative 1"),
            .input = s("-9.0"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 4 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(-9.0) >> 32,
                        .pl2 = longOfDoubleBits(-9.0) & LOWER32BITS, .startBt = 0, .lenBts = 4 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric negative 2"),
            .input = s("-8.775_807"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 10 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(-8.775807) >> 32,
                    .pl2 = longOfDoubleBits(-8.775807) & LOWER32BITS, .startBt = 0, .lenBts = 10 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric negative 3"),
            .input = s("-1005001234560000000000.0"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 25 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(-1005001234560000000000.0) >> 32,
                          .pl2 = longOfDoubleBits(-1005001234560000000000.0) & LOWER32BITS,
                        .startBt = 0, .lenBts = 25 }
            }))
        },
        (LexerTest) {
            .name = s("Int numeric 1"),
            .input = s("3"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokInt, .pl2 = 3, .startBt = 0, .lenBts = 1 }
            }))
        },
        (LexerTest) {
            .name = s("Int numeric 2"),
            .input = s("12"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 2 },
                (Token){ .tp = tokInt, .pl2 = 12, .startBt = 0, .lenBts = 2,  }
            }))
        },
        (LexerTest) {
            .name = s("Int numeric 3"),
            .input = s("0987_12"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 7 },
                (Token){ .tp = tokInt, .pl2 = 98712, .startBt = 0, .lenBts = 7 }
            }))
        },
        (LexerTest) {
            .name = s("Int numeric 4"),
            .input = s("9_223_372_036_854_775_807"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 25 },
                (Token){ .tp = tokInt, .pl1 = ((int64_t)9223372036854775807 >> 32),
                        .pl2 = ((int64_t)9223372036854775807 & LOWER32BITS),
                        .startBt = 0, .lenBts = 25 }
            }))
        },
        (LexerTest) {
            .name = s("Int numeric negative 1"),
            .input = s("-1"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 2 },
                (Token){ .tp = tokInt, .pl1 = (((int64_t)-1) >> 32),
                        .pl2 = (((int64_t)-1) & LOWER32BITS),
                        .startBt = 0, .lenBts = 2 }
            }))
        },
        (LexerTest) {
            .name = s("Int numeric negative 2"),
            .input = s("-775_807"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 8 },
                (Token){ .tp = tokInt, .pl1 = ((int64_t)(-775807) >> 32),
                         .pl2 = ((int64_t)(-775807) & LOWER32BITS), .startBt = 0, .lenBts = 8 }
            }))
        },
        (LexerTest) {
            .name = s("Int numeric negative 3"),
            .input = s("-9_223_372_036_854_775_807"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 26 },
                (Token){ .tp = tokInt, .pl1 = ((int64_t)(-9223372036854775807) >> 32),
                        .pl2 = ((int64_t)(-9223372036854775807) & LOWER32BITS),
                        .startBt = 0, .lenBts = 26 }
            }))
        },
        (LexerTest) {
            .name = s("Int numeric error 1"),
            .input = s("3_"),
            .expectedOutput = buildLexerWithError(s(errNumericEndUnderscore), ((Token[]) {
                (Token){ .tp = tokStmt }
        }))},
        (LexerTest) { .name = s("Int numeric error 2"),
            .input = s("9_223_372_036_854_775_808"),
            .expectedOutput = buildLexerWithError(s(errNumericIntWidthExceeded), ((Token[]) {
                (Token){ .tp = tokStmt }
        }))}
    }));
}
//}}}
//{{{ String

LexerTestSet* stringTests(Arena* a) {
    return createTestSet(s("String literals lexer tests"), a, ((LexerTest[]) {
        (LexerTest) { .name = s("String simple literal"),
            .input = s("`asdfn't`"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 9 },
                (Token){ .tp = tokString, .startBt = 0, .lenBts = 9 }
            }))
        },
        (LexerTest) { .name = s("String literal with non-ASCII inside"),
            .input = s("`hello мир`"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 14 },
                (Token){ .tp = tokString, .startBt = 0, .lenBts = 14 }
        }))},
        (LexerTest) { .name = s("String literal unclosed"),
            .input = s("`asdf"),
            .expectedOutput = buildLexerWithError(s(errPrematureEndOfInput), ((Token[]) {
                (Token){ .tp = tokStmt }
        }))}
    }));
}

//}}}
//{{{ Meta
/*
LexerTestSet* metaTests(Arena* a) {
    return createTestSet(s("Metaexpressions lexer tests"), a, ((LexerTest[]) {
        (LexerTest) { .name = s("Comment simple"),
            .input = s("[`this is a comment`]"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 2, .startBt = 0, .lenBts = 21 },
                (Token){ .tp = tokMeta, .pl1 = slSubexpr, .pl2 = 1, .startBt = 0, .lenBts = 21 },
                (Token){ .tp = tokString, .startBt = 1, .lenBts = 19 }
            }))
            }
    }));
}
*/

//}}}
//{{{ Punctuation

LexerTestSet* punctuationTests(Arena* a) {
    return createTestSet(s("Punctuation lexer tests"), a, ((LexerTest[]) {
        (LexerTest) { .name = s("Parens simple"),
            .input = s("(car cdr);"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 3, .startBt = 0, .lenBts = 10 },
                (Token){ .tp = tokParens, .pl2 = 2, .startBt = 0, .lenBts = 9 },
                (Token){ .tp = tokWord, .pl1 = 0,  .startBt = 1, .lenBts = 3 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 5, .lenBts = 3 }
        }))},
        (LexerTest) { .name = s("Parens nested"),
            .input = s("(car (other car) cdr);"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt,   .pl2 = 6, .startBt = 0, .lenBts = 22 },
                (Token){ .tp = tokParens, .pl2 = 5, .startBt = 0, .lenBts = 21 },
                (Token){ .tp = tokWord,   .pl1 = 0, .startBt = 1, .lenBts = 3 },  // car

                (Token){ .tp = tokParens, .pl2 = 2, .startBt = 5, .lenBts = 11 },
                (Token){ .tp = tokWord,   .pl1 = 1, .startBt = 6, .lenBts = 5 },  // other
                (Token){ .tp = tokWord,   .pl1 = 0, .startBt = 12, .lenBts = 3 }, // car

                (Token){ .tp = tokWord,   .pl1 = 2, .startBt = 17, .lenBts = 3 }  // cdr
        }))},
        (LexerTest) { .name = s("Parens unclosed"),
            .input = s("(car (other car) cdr;"),
            .expectedOutput = buildLexerWithError(s(errPunctuationOnlyInMultiline), ((Token[]) {
                (Token){ .tp = tokStmt },
                (Token){ .tp = tokParens, .pl2 = 0, .startBt = 0, .lenBts = 0 },
                (Token){ .tp = tokWord, .pl1 = 0,   .startBt = 1, .lenBts = 3 },
                (Token){ .tp = tokParens, .pl2 = 2, .startBt = 5, .lenBts =  11 },
                (Token){ .tp = tokWord,   .pl1 = 1, .startBt = 6, .lenBts = 5 },
                (Token){ .tp = tokWord, .pl1 = 0,   .startBt = 12, .lenBts = 3 },
                (Token){ .tp = tokWord, .pl1 = 2,   .startBt = 17, .lenBts = 3 }
        }))},
        (LexerTest) { .name = s("Call simple"),
            .input = s("call var otherVar;"),
            .expectedOutput = expect(((Token[]) {
                (Token){ .tp = tokStmt,   .pl2 = 3, .lenBts = 18 },
                (Token){ .tp = tokWord, .pl1 = 0,   .startBt = 0, .lenBts = 4 },
                (Token){ .tp = tokWord, .pl1 = 1,   .startBt = 5, .lenBts = 3 },
                (Token){ .tp = tokWord, .pl1 = 2,   .startBt = 9, .lenBts = 8 }
        }))},
        (LexerTest) { .name = s("Scope simple"),
            .input = s("{ car;\n"
                       "    cdr;}"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokScope, .pl1 = slScope, .pl2 = 4, .startBt = 0, .lenBts = 16 },
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 2, .lenBts = 4 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 2, .lenBts = 3 },
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 11, .lenBts = 4 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 11, .lenBts = 3 }
        }))},
        (LexerTest) { .name = s("Scopes nested"),
            .input = s("{ car;\n"
                       "  { other car; }\n"
                       "  cdr;"
                       "}"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokScope, .pl1 = slScope, .pl2 = 8, .startBt = 0, .lenBts = 31 },

                (Token){ .tp = tokStmt,  .pl2 = 1, .startBt = 2, .lenBts = 4 },
                (Token){ .tp = tokWord,  .pl1 = 0, .startBt = 2, .lenBts = 3 },  // car

                (Token){ .tp = tokScope, .pl1 = slScope, .pl2 = 3, .startBt = 9, .lenBts = 14 },
                (Token){ .tp = tokStmt,  .pl2 = 2, .startBt = 11, .lenBts = 10 },
                (Token){ .tp = tokWord, .pl1 = 1,  .startBt = 11, .lenBts = 5 },  // other
                (Token){ .tp = tokWord, .pl1 = 0,  .startBt = 17, .lenBts = 3 }, // car

                (Token){ .tp = tokStmt,  .pl2 = 1, .startBt = 26, .lenBts = 4 },
                (Token){ .tp = tokWord, .pl1 = 2,  .startBt = 26, .lenBts = 3 }  // cdr
        }))},
        (LexerTest) { .name = s("Parens inside statement"),
            .input = s("loo aa ( asdf );"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 4, .lenBts = 16 },
                (Token){ .tp = tokWord, .pl1 = 0, .lenBts = 3 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 4, .lenBts = 2 },
                (Token){ .tp = tokParens, .pl2 = 1, .startBt = 7, .lenBts = 8 },
                (Token){ .tp = tokWord, .pl1 = 2, .startBt = 9, .lenBts = 4 }
        }))},
        (LexerTest) { .name = s("Multi-line statement"),
            .input = s("owl car (\n"
                       "asdf\n"
                       "bcj\n"
                       ");"
                      ),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 5, .lenBts = 21 },
                (Token){ .tp = tokWord, .pl1 = 0, .lenBts = 3 },                // foo
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 4, .lenBts = 3 },  // bar
                (Token){ .tp = tokParens, .pl2 = 2, .startBt = 8, .lenBts = 12 },
                (Token){ .tp = tokWord, .pl1 = 2, .startBt = 10, .lenBts = 4 }, // asdf
                (Token){ .tp = tokWord, .pl1 = 3, .startBt = 15, .lenBts = 3 }  // bcj
        }))},
        (LexerTest) { .name = s("Empty statements"),
            .input = s("{; fzg x;;}"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokScope, .pl1 = slScope, .pl2 = 3, .lenBts = 11 },
                (Token){ .tp = tokStmt, .pl2 = 2, .startBt = 3, .lenBts = 6 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 3, .lenBts = 3 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 7, .lenBts = 1 },
        }))},
        (LexerTest) { .name = s("Multiple statements"),
            .input = s("moo car;\n"
                       "asdf;\n"
                       "bcj;"
                      ),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 2,               .lenBts = 8 },
                (Token){ .tp = tokWord, .pl1 = 0,               .lenBts = 3 }, // moo
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 4, .lenBts = 3 }, // car

                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 9, .lenBts = 5 },
                (Token){ .tp = tokWord, .pl1 = 2, .startBt = 9, .lenBts = 4 }, // asdf

                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 15, .lenBts = 4 },
                (Token){ .tp = tokWord, .pl1 = 3, .startBt = 15, .lenBts = 3 } // bcj
        }))},

        (LexerTest) { .name = s("Stmt separator"),
            .input = s("awu; arn baz"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 4 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 3 },
                (Token){ .tp = tokStmt, .pl2 = 2, .startBt = 5, .lenBts = 7 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 5, .lenBts = 3 },
                (Token){ .tp = tokWord, .pl1 = 2, .startBt = 9, .lenBts = 3 }
        }))},
        (LexerTest) { .name = s("Stmt separator usage error"),
            .input = s("asdf (zoogle; baz)"),
            .expectedOutput = buildLexerWithError(s(errPunctuationOnlyInMultiline), ((Token[]) {
                (Token){ .tp = tokStmt },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 4 },
                (Token){ .tp = tokParens, .startBt = 5 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 6, .lenBts = 6 }
        }))},
        (LexerTest) { .name = s("Function calls"),
            .input = s("func zoogle 3 (baz 4.2);"),
            .expectedOutput = expect(((Token[]) {
                (Token){ .tp = tokStmt,         .pl2 = 6, .startBt = 0, .lenBts = 24 },
                (Token){ .tp = tokWord, .pl1 = 0,         .startBt = 0, .lenBts = 4 },
                (Token){ .tp = tokWord, .pl1 = 1, .pl2 = 0, .startBt = 5, .lenBts = 6 },
                (Token){ .tp = tokInt, .pl2 = 3,          .startBt = 12, .lenBts = 1 },
                (Token){ .tp = tokParens, .pl2 = 2,          .startBt = 14, .lenBts = 9 },
                (Token){ .tp = tokWord, .pl1 = 2, .pl2 = 0, .startBt = 15, .lenBts = 3 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(4.2) >> 32,
                         .pl2 = longOfDoubleBits(4.2) & LOWER32BITS,
                         .startBt = 19, .lenBts = 3 }
        }))},
        (LexerTest) {
            .name = s("Word with array accessor"),
            .input = s("asdf[abc];"),
            .expectedOutput = expect(((Token[]){
                    (Token){ .tp = tokStmt, .pl2 = 4, .startBt = 0, .lenBts = 10 },
                    (Token){ .tp = tokAccessor, .pl2 = 3, .startBt = 0, .lenBts = 9 },
                    (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 4 },
                    (Token){ .tp = tokAccessIn, .pl2 = 1, .startBt = 4, .lenBts = 5 },
                    (Token){ .tp = tokWord, .pl1 = 1, .startBt = 5, .lenBts = 3 }
            }))
        },
        (LexerTest) { .name = s("Accessors"),
            .input = s("arr[i] arr[i - 1] brr[j][k];"),
            .expectedOutput = expect(((Token[]) {
                (Token){ .tp = tokStmt,  .pl2 = 16, .startBt = 0, .lenBts = 28 },

                (Token){ .tp = tokAccessor, .pl2 = 3, .startBt = 0, .lenBts = 6 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 3 },
                (Token){ .tp = tokAccessIn, .pl2 = 1, .startBt = 3, .lenBts = 3 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 4, .lenBts = 1 },

                (Token){ .tp = tokAccessor,   .pl2 = 5,  .startBt = 7, .lenBts = 10 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 7, .lenBts = 3 },
                (Token){ .tp = tokAccessIn,   .pl2 = 3,  .startBt = 10, .lenBts = 7 },
                (Token){ .tp = tokWord, .pl1 = 1,        .startBt = 11, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opMinus, .pl2 = 8, .startBt = 13, .lenBts = 1 },
                (Token){ .tp = tokInt, .pl2 = 1, .startBt = 15, .lenBts = 1 },

                (Token){ .tp = tokAccessor,   .pl2 = 5,  .startBt = 18, .lenBts = 9 },
                (Token){ .tp = tokWord, .pl1 = 2,  .startBt = 18, .lenBts = 3 },
                (Token){ .tp = tokAccessIn, .pl2 = 1, .startBt = 21, .lenBts = 3 },
                (Token){ .tp = tokWord, .pl1 = 3, .startBt = 22, .lenBts = 1 },
                (Token){ .tp = tokAccessIn, .pl2 = 1, .startBt = 24, .lenBts = 3 },
                (Token){ .tp = tokWord, .pl1 = 4, .startBt = 25, .lenBts = 1 },
        }))},
        (LexerTest) {
            .name = s("Array numeric index"),
            .input = s("a[5]"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 4, .startBt = 0, .lenBts = 4 },
                (Token){ .tp = tokAccessor, .pl2 = 3, .startBt = 0, .lenBts = 4 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAccessIn, .pl2 = 1, .startBt = 1, .lenBts = 3 },
                (Token){ .tp = tokInt, .pl2 = 5, .startBt = 2, .lenBts = 1 }
            }))
        },
        (LexerTest) {
            .name = s("Array variable index"),
            .input = s("a[ind]"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt,         .pl2 = 4, .startBt = 0, .lenBts = 6 },
                (Token){ .tp = tokAccessor,     .pl2 = 3, .startBt = 0, .lenBts = 6 },
                (Token){ .tp = tokWord, .pl1 = 0,         .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAccessIn, .pl2 = 1, .startBt = 1, .lenBts = 5 },
                (Token){ .tp = tokWord, .pl1 = 1,     .startBt = 2, .lenBts = 3 }
            }))
        },
        (LexerTest) {
            .name = s("Array complex index"),
            .input = s("a[i + 1]"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt,         .pl2 = 6, .startBt = 0, .lenBts = 8 },
                (Token){ .tp = tokAccessor, .pl2 = 5, .startBt = 0, .lenBts = 8 },
                (Token){ .tp = tokWord, .pl1 = 0,         .startBt = 0, .lenBts = 1 }, // a
                (Token){ .tp = tokAccessIn, .pl2 = 3, .startBt = 1, .lenBts = 7 },
                (Token){ .tp = tokWord, .pl1 = 1,   .startBt = 2, .lenBts = 1 }, // i
                (Token){ .tp = tokOperator, .pl1 = opPlus, .pl2 = 8, .startBt = 4, .lenBts = 1 },
                (Token){ .tp = tokInt,         .pl2 = 1, .startBt = 6, .lenBts = 1 }
            }))
        }
    }));
}

//}}}
//{{{ Operator

LexerTestSet* operatorTests(Arena* a) {
    return createTestSet(s("Operator lexer tests"), a, ((LexerTest[]) {
        (LexerTest) { .name = s("Operator simple 1"),
            .input = s("+"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opPlus, .pl2 = 8, .startBt = 0, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operators extended"),
            .input = s("+: *: -: /:"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 4, .lenBts = 11 },
                (Token){ .tp = tokOperator, .pl1 = opPlusExt, .pl2 = 8, .startBt = 0, .lenBts = 2 },
                (Token){ .tp = tokOperator, .pl1 = opTimesExt, .pl2 = 9, .startBt = 3, .lenBts = 2 },
                (Token){ .tp = tokOperator, .pl1 = opMinusExt, .pl2 = 8, .startBt = 6, .lenBts = 2 },
                (Token){ .tp = tokOperator, .pl1 = opDivByExt, .pl2 = 9, .startBt = 9, .lenBts = 2 }
        }))},
        (LexerTest) { .name = s("Operators bitwise"),
            .input = s("!. ||. >>. &&. <<. ^."),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 6, .lenBts = 21 },
                (Token){ .tp = tokOperator, .pl1 = opBitwiseNeg, .pl2 = 100,
                         .startBt = 0, .lenBts = 2 },
                (Token){ .tp = tokOperator, .pl1 = opBitwiseOr, .pl2 = 2, .startBt = 3, .lenBts = 3 },
                (Token){ .tp = tokOperator, .pl1 = opBitShiftR, .pl2 = 7, .startBt = 7, .lenBts = 3 },
                (Token){ .tp = tokOperator, .pl1 = opBitwiseAnd, .pl2 = 4,
                         .startBt = 11, .lenBts = 3 },
                (Token){ .tp = tokOperator, .pl1 = opBitShiftL, .pl2 = 7, .startBt = 15, .lenBts = 3 },
                (Token){ .tp = tokOperator, .pl1 = opBitwiseXor, .pl2 = 3, .startBt = 19, .lenBts = 2 }
        }))},
        (LexerTest) { .name = s("Operators list"),
            .input = s("+ - / * && || ? <=> $ ' /\\ # <0 >0 ++ --"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt,                .pl2 = 16, .startBt = 0, .lenBts = 40 },
                (Token){ .tp = tokOperator, .pl1 = opPlus, .pl2 = 8, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opMinus, .pl2 = 8, .startBt = 2, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opDivBy, .pl2 = 9, .startBt = 4, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opTimes, .pl2 = 9, .startBt = 6, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opBoolAnd, .pl2 = 1, .startBt = 8, .lenBts = 2 },
                (Token){ .tp = tokOperator, .pl1 = opBoolOr, .pl2 = 0, .startBt = 11, .lenBts = 2 },
                (Token){ .tp = tokOperator, .pl1 = opQuestionMark, .pl2 = 0,
                         .startBt = 14, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opComparator, .pl2 = 6,
                         .startBt = 16, .lenBts = 3 },
                (Token){ .tp = tokOperator, .pl1 = opToString, .pl2 = 100,
                         .startBt = 20, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opRef, .pl2 = 100,
                         .startBt = 22, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opIntersect, .pl2 = 0, .startBt = 24, .lenBts = 2 },
                (Token){ .tp = tokOperator, .pl1 = opSize, .pl2 = 100, .startBt = 27, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opLTZero, .pl2 = 100, .startBt = 29, .lenBts = 2 },
                (Token){ .tp = tokOperator, .pl1 = opGTZero, .pl2 = 100, .startBt = 32, .lenBts = 2 },
                (Token){ .tp = tokOperator, .pl1 = opIncrement, .pl2 = 9, .startBt = 35, .lenBts = 2 },
                (Token){ .tp = tokOperator, .pl1 = opDecrement, .pl2 = 9, .startBt = 38, .lenBts = 2 }
        }))},
        (LexerTest) { .name = s("Operator expression"),
            .input = s("a - b"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 3, .startBt = 0, .lenBts = 5 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opMinus, .pl2 = 8, .startBt = 2, .lenBts = 1 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 4, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operator assignment 1"),
            .input = s("a += b"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokAssignment, .pl1 = 0, .pl2 = 5,
                    .startBt = 0, .lenBts = 6 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAssignRight, .pl2 = 3, .startBt = 2, .lenBts = 4 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opPlus, .startBt = 2, .lenBts = 1 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 5, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operator assignment 2"),
            .input = s("a ||= b"),
            .expectedOutput = expect(((Token[]) {
                (Token){ .tp = tokAssignment, .pl1 = 0, .pl2 = 5,
                         .startBt = 0, .lenBts = 7 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAssignRight, .pl2 = 3, .startBt = 2, .lenBts = 5 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opBoolOr, .startBt = 2, .lenBts = 2 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 6, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operator assignment 3"),
            .input = s("a*:= b"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokAssignment, .pl1 = 0, .pl2 = 5,
                         .startBt = 0, .lenBts = 6 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAssignRight, .pl2 = 3, .startBt = 1, .lenBts = 5 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opTimesExt, .startBt = 1, .lenBts = 2 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 5, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operator assignment 4"),
            .input = s("a ^.= b"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokAssignment, .pl1 = 0, .pl2 = 5, .startBt = 0,
                         .lenBts = 7 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAssignRight, .pl2 = 3, .startBt = 2, .lenBts = 5 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opBitwiseXor, .startBt = 2, .lenBts = 2 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 6, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operator assignment in parens error"),
            .input = s("(x += y + 5)"),
            .expectedOutput = buildLexerWithError(s(errOperatorAssignmentPunct), ((Token[]) {
                (Token){ .tp = tokStmt },
                (Token){ .tp = tokParens },
                (Token){ .tp = tokWord, .startBt = 1, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operator assignment with parens"),
            .input = s("x -:= (y + 5)"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokAssignment, .pl1 = 0, .pl2 = 8, .lenBts = 13 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAssignRight, .pl2 = 6, .startBt = 2, .lenBts = 11 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 }, // x
                (Token){ .tp = tokOperator, .pl1 = opMinusExt, .startBt = 2, .lenBts = 2 },
                (Token){ .tp = tokParens, .pl2 = 3, .startBt = 6, .lenBts = 7 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 7, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opPlus, .pl2 = 8, .startBt = 9, .lenBts = 1 },
                (Token){ .tp = tokInt, .pl2 = 5, .startBt = 11, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operator assignment in parens error"),
            .input = s("x (+= y) + 5"),
            .expectedOutput = buildLexerWithError(s(errOperatorAssignmentPunct), ((Token[]) {
                (Token){ .tp = tokStmt },
                (Token){ .tp = tokWord, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokParens, .startBt = 2 }
        }))},
        (LexerTest) { .name = s("Operator assignment multiple error"),
            .input = s("x = y = 7"),
            .expectedOutput = buildLexerWithError(s(errOperatorAssignmentPunct), ((Token[]) {
                (Token){ .tp = tokAssignment, .pl2 = 0, .lenBts = 0 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAssignRight, .pl2 = 0, .startBt = 2 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 4, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Boolean operators"),
            .input = s("a && b || c"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 5, .lenBts = 11 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opBoolAnd, .pl2 = 1, .startBt = 2, .lenBts = 2 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 5, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opBoolOr, .pl2 = 0, .startBt = 7, .lenBts = 2 },
                (Token){ .tp = tokWord, .pl1 = 2, .startBt = 10, .lenBts = 1 }
        }))},
       (LexerTest) { .name = s("Negation"),
            .input = s("-5 -x"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 3, .lenBts = 5 },
                (Token){ .tp = tokInt, .pl1 = -1, .pl2 = -5, .startBt = 0, .lenBts = 2 },
                (Token){ .tp = tokOperator, .pl1 = opNegate, .pl2 = 100, .startBt = 3, .lenBts = 1 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 4, .lenBts = 1 }
        }))}
    }));
}

//}}}
//{{{ Core forms

LexerTestSet* coreFormTests(Arena* a) {
    return createTestSet(s("Core form lexer tests"), a, ((LexerTest[]) {
         (LexerTest) { .name = s("Top-level definition"),
             .input = s("def co = 8;"),
             .expectedOutput = expect(((Token[]){
                 (Token){ .tp = tokDef,  .pl2 = 3,             .lenBts = 11 },
                 (Token){ .tp = tokWord,  .pl2 = 0,   .startBt = 4, .lenBts = 2 },
                 (Token){ .tp = tokAssignRight,  .pl2 = 1,   .startBt = 7, .lenBts = 4 },
                 (Token){ .tp = tokInt, .pl2 = 8, .startBt = 9,     .lenBts = 1 },
         }))},
         (LexerTest) { .name = s("Statement-type core form"),
             .input = s("x = 9; assert (x == 55) `Error!`"),
             .expectedOutput = expect(((Token[]){
                 (Token){ .tp = tokAssignment,  .pl2 = 3,             .lenBts = 6 },
                 (Token){ .tp = tokWord,  .pl2 = 0,             .lenBts = 1 }, // x
                 (Token){ .tp = tokAssignRight,  .pl2 = 1,   .startBt = 2, .lenBts = 4 },
                 (Token){ .tp = tokInt, .pl2 = 9, .startBt = 4,     .lenBts = 1 },

                 (Token){ .tp = tokAssert, .pl2 = 5, .startBt = 7,  .lenBts = 25 },
                 (Token){ .tp = tokParens, .pl2 = 3, .startBt = 14, .lenBts = 9 },
                 (Token){ .tp = tokWord,                  .startBt = 15, .lenBts = 1 },
                 (Token){ .tp = tokOperator, .pl1 = opEquality, .pl2 = 5, .startBt = 17, .lenBts = 2 },
                 (Token){ .tp = tokInt, .pl2 = 55,  .startBt = 20,  .lenBts = 2 },
                 (Token){ .tp = tokString,               .startBt = 24,  .lenBts = 8 }
         }))},
         (LexerTest) { .name = s("Statement-type core form error"),
             .input = s("x / (assert foo)"),
             .expectedOutput = buildLexerWithError(s(errCoreNotInsideStmt), ((Token[]) {
                 (Token){ .tp = tokStmt },
                 (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },                // x
                 (Token){ .tp = tokOperator, .pl1 = opDivBy, .pl2 = 9, .startBt = 2, .lenBts = 1 },
                 (Token){ .tp = tokParens, .startBt = 4 }
         }))},
         (LexerTest) { .name = s("Definition of a mutable var"),
             .input = s("w' = 9;"),
             .expectedOutput = expect(((Token[]) {
                 (Token){ .tp = tokAssignment,  .pl2 = 3,             .lenBts = 7 },
                 (Token){ .tp = tokWord,    .pl1 = 0, .pl2 = 1, .startBt = 0, .lenBts = 1 }, // w$
                 (Token){ .tp = tokAssignRight,  .pl2 = 1,     .startBt = 3, .lenBts = 4 },
                 (Token){ .tp = tokInt,      .pl2 = 9, .startBt = 5, .lenBts = 1 },
         }))},
         (LexerTest) { .name = s("Assignment with complex left side"),
             .input = s("a[i][5] = 9;"),
             .expectedOutput = expect(((Token[]) {
                 (Token){ .tp = tokAssignment,  .pl2 = 8,             .lenBts = 12 },
                 (Token){ .tp = tokAccessor,    .pl2 = 5, .startBt = 0, .lenBts = 7 },
                 (Token){ .tp = tokWord,                  .startBt = 0, .lenBts = 1 }, // a
                 (Token){ .tp = tokAccessIn,    .pl2 = 1, .startBt = 1, .lenBts = 3 },
                 (Token){ .tp = tokWord,   .pl1 = 1,      .startBt = 2, .lenBts = 1 }, // i
                 (Token){ .tp = tokAccessIn,    .pl2 = 1, .startBt = 4, .lenBts = 3 },
                 (Token){ .tp = tokInt,         .pl2 = 5, .startBt = 5, .lenBts = 1 },

                 (Token){ .tp = tokAssignRight, .pl2 = 1, .startBt = 8, .lenBts = 4 },
                 (Token){ .tp = tokInt,         .pl2 = 9, .startBt = 10, .lenBts = 1 },
         }))},
         (LexerTest) { .name = s("Paren-type core form"),
             .input = s("if >0 (<=> x 7) { true; }"),
             .expectedOutput = expect(((Token[]){
                 (Token){ .tp = tokIf, .pl1 = slScope, .pl2 = 8, .startBt = 0, .lenBts = 25 },

                 (Token){ .tp = tokStmt,               .pl2 = 5, .startBt = 3, .lenBts = 13 },
                 (Token){ .tp = tokOperator, .pl1 = opGTZero, .pl2 = 100, .startBt = 3, .lenBts = 2 },
                 (Token){ .tp = tokParens, .pl2 = 3, .startBt = 6, .lenBts = 9 },
                 (Token){ .tp = tokOperator, .pl1 = opComparator, .pl2 = 6,
                          .startBt = 7, .lenBts = 3 },
                 (Token){ .tp = tokWord,            .startBt = 11, .lenBts = 1 }, // x
                 (Token){ .tp = tokInt, .pl2 = 7, .startBt = 13, .lenBts = 1 },

                 (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 18, .lenBts = 5 },
                 (Token){ .tp = tokBool, .pl2 = 1, .startBt = 18, .lenBts = 4 },
         }))},
         (LexerTest) { .name = s("If with else"),
             .input = s("if >0 (x <=> 7) {true;} else {false;}"),
             .expectedOutput = expect(((Token[]){
                 (Token){ .tp = tokIf, .pl1 = slScope, .pl2 = 8, .startBt = 0, .lenBts = 23 },
                 (Token){ .tp = tokStmt,               .pl2 = 5, .startBt = 3, .lenBts = 13 },
                 (Token){ .tp = tokOperator, .pl1 = opGTZero, .pl2 = 100, .startBt = 3, .lenBts = 2 },
                 (Token){ .tp = tokParens,     .pl2 = 3, .startBt = 6, .lenBts = 9 },
                 (Token){ .tp = tokWord,            .startBt = 7, .lenBts = 1 }, // x
                 (Token){ .tp = tokOperator, .pl1 = opComparator, .pl2 = 6,
                          .startBt = 9, .lenBts = 3 },
                 (Token){ .tp = tokInt,        .pl2 = 7, .startBt = 13, .lenBts = 1 },

                 (Token){ .tp = tokStmt,       .pl2 = 1, .startBt = 17, .lenBts = 5 },
                 (Token){ .tp = tokBool,       .pl2 = 1, .startBt = 17, .lenBts = 4 },

                 (Token){ .tp = tokElse, .pl1 = slScope, .pl2 = 2,
                          .startBt = 24, .lenBts = 13 },
                 (Token){ .tp = tokStmt,       .pl2 = 1, .startBt = 30, .lenBts = 6 },
                 (Token){ .tp = tokBool,            .startBt = 30, .lenBts = 5 }
         }))},
        (LexerTest) { .name = s("If with elseif and else"),
            .input = s("if >0 (x <=> 7) { 5; }\n"
                       "eif <0 (x <=> 7) { 11; }\n"
                       "else { true; }"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokIf, .pl1 = slScope, .pl2 = 8, .startBt = 0, .lenBts = 22 },
                (Token){ .tp = tokStmt,               .pl2 = 5, .startBt = 3, .lenBts = 13 },
                (Token){ .tp = tokOperator, .pl1 = opGTZero, .pl2 = 100, .startBt = 3, .lenBts = 2 },
                (Token){ .tp = tokParens,   .pl2 = 3, .startBt = 6, .lenBts = 9 },
                (Token){ .tp = tokWord,            .startBt = 7, .lenBts = 1 }, // x
                (Token){ .tp = tokOperator, .pl1 = opComparator, .pl2 = 6, .startBt = 9, .lenBts = 3 },
                (Token){ .tp = tokInt,          .pl2 = 7, .startBt = 13, .lenBts = 1 },
                (Token){ .tp = tokStmt,      .pl2 = 1, .startBt = 18, .lenBts = 2 },
                (Token){ .tp = tokInt,       .pl2 = 5, .startBt = 18, .lenBts = 1 },

                (Token){ .tp = tokElseIf, .pl1 = slScope, .pl2 = 8,
                         .startBt = 23, .lenBts = 24 },
                (Token){ .tp = tokStmt,            .pl2 = 5, .startBt = 27, .lenBts = 13 },
                (Token){ .tp = tokOperator, .pl1 = opLTZero, .pl2 = 100, .startBt = 27, .lenBts = 2 },
                (Token){ .tp = tokParens,   .pl2 = 3, .startBt = 30, .lenBts = 9 },
                (Token){ .tp = tokWord,            .startBt = 31, .lenBts = 1 }, // x
                (Token){ .tp = tokOperator, .pl1 = opComparator, .pl2 = 6, .startBt = 33, .lenBts = 3 },
                (Token){ .tp = tokInt,          .pl2 = 7, .startBt = 37, .lenBts = 1 },
                (Token){ .tp = tokStmt,      .pl2 = 1, .startBt = 42, .lenBts = 3 },
                (Token){ .tp = tokInt,       .pl2 = 11, .startBt = 42, .lenBts = 2 },

                (Token){ .tp = tokElse, .pl1 = slScope, .pl2 = 2,
                         .startBt = 48, .lenBts = 14 },
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 55, .lenBts = 5 },
                (Token){ .tp = tokBool, .pl2 = 1, .startBt = 55, .lenBts = 4 }
         }))},
         (LexerTest) { .name = s("Function simple 1"),
             .input = s("def noo Int = {{ x Int, y Int, } return x - y;}"),
             .expectedOutput = expect(((Token[]){
                 (Token){ .tp = tokDef,         .pl2 = 15,
                          .startBt = 0, .lenBts = 47 },
                 (Token){ .tp = tokWord, .pl1 = 0,        .startBt = 4, .lenBts = 3 }, // noo
                 (Token){ .tp = tokTypeName, .pl1 = (strInt + S),  // Int
                          .startBt = 8, .lenBts = 3 },
                 (Token){ .tp = tokAssignRight,        .pl2 = 12,
                          .startBt = 12, .lenBts = 35 },

                 (Token){ .tp = tokFn, .pl1 = slScope,       .pl2 = 11,
                          .startBt = 14, .lenBts = 33 },
                 (Token){ .tp = tokFnParams, .pl1 = slClauseList, .pl2 = 6,
                          .startBt = 15, .lenBts = 17 },

                 (Token){ .tp = tokClause,     .pl2 = 2,       .startBt = 17, .lenBts = 6 },
                 (Token){ .tp = tokWord, .pl1 = 1,             .startBt = 17, .lenBts = 1 }, // x
                 (Token){ .tp = tokTypeName, .pl1 = (strInt + S),
                            .startBt = 19, .lenBts = 3 },
                 (Token){ .tp = tokClause,     .pl2 = 2,       .startBt = 24, .lenBts = 6 },
                 (Token){ .tp = tokWord, .pl1 = 2,             .startBt = 24, .lenBts = 1 }, // y
                 (Token){ .tp = tokTypeName, .pl1 = (strInt + S),
                          .startBt = 26, .lenBts = 3 }, // Int

                 (Token){ .tp = tokReturn,       .pl2 = 3, .startBt = 33, .lenBts = 13 },
                 (Token){ .tp = tokWord, .pl1 = 1,       .startBt = 40, .lenBts = 1 }, // x
                 (Token){ .tp = tokOperator, .pl1 = opMinus, .pl2 = 8, .startBt = 42, .lenBts = 1 },
                 (Token){ .tp = tokWord, .pl1 = 2,       .startBt = 44, .lenBts = 1 } // y
         }))},
         (LexerTest) { .name = s("Loop simple"),
             .input = s("for {x' = 1; x < 101; x = x + 1;} { print x; }"),
             .expectedOutput = expect(((Token[]) {
                 (Token){ .tp = tokFor, .pl1 = slScope, .pl2 = 18, .lenBts = 46 },

                 (Token){ .tp = tokScope, .pl1 = slScope, .pl2 = 14,
                          .startBt = 4, .lenBts = 29 },
                 (Token){ .tp = tokAssignment,   .pl2 = 3, .startBt = 5, .lenBts = 7 }, // x$ = 1
                 (Token){ .tp = tokWord, .pl1 = 0, .pl2 = 1, .startBt = 5, .lenBts = 1 }, // x
                 (Token){ .tp = tokAssignRight,  .pl2 = 1, .startBt = 8, .lenBts = 4 },
                 (Token){ .tp = tokInt,          .pl2 = 1, .startBt = 10, .lenBts = 1 },

                 (Token){ .tp = tokStmt,         .pl2 = 3, .startBt = 13, .lenBts = 8 },
                 (Token){ .tp = tokWord,     .pl1 = 0, .pl2 = 0, .startBt = 13, .lenBts = 1 }, // x
                 (Token){ .tp = tokOperator, .pl1 = opLessTh, .pl2 = 6, .startBt = 15, .lenBts = 1 },
                 (Token){ .tp = tokInt,             .pl2 = 101, .startBt = 17, .lenBts = 3 },

                 (Token){ .tp = tokAssignment,    .pl2 = 5, .startBt = 22, .lenBts = 10 },
                 (Token){ .tp = tokWord, .pl1 = 0, .pl2 = 0, .startBt = 22, .lenBts = 1 },  // x
                 (Token){ .tp = tokAssignRight, .pl1 = 0, .pl2 = 3,
                             .startBt = 24, .lenBts = 8 },
                 (Token){ .tp = tokWord, .pl1 = 0, .pl2 = 0, .startBt = 26, .lenBts = 1 },  // x
                 (Token){ .tp = tokOperator, .pl1 = opPlus, .pl2 = 8, .startBt = 28, .lenBts = 1 },
                 (Token){ .tp = tokInt,           .pl2 = 1, .startBt = 30, .lenBts = 1 },

                 (Token){ .tp = tokStmt,           .pl2 = 2, .startBt = 36, .lenBts = 8 },
                 (Token){ .tp = tokWord,  .pl1 = (strPrint + S), .pl2 = 0, //print
                            .startBt = 36, .lenBts = 5 },
                 (Token){ .tp = tokWord,                 .startBt = 42, .lenBts = 1 }  // x
         }))}
    }));
}

//}}}
//{{{ Types

LexerTestSet* typeTests(Arena* a) {
    return createTestSet(s("Type forms lexer tests"), a, ((LexerTest[]) {
         (LexerTest) { .name = s("Type definition"),
             .input = s("def Foo = Int;"),
             .expectedOutput = expect(((Token[]){
                 (Token){ .tp = tokDef, .pl1 = slStmt, .pl2 = 3, .startBt = 0, .lenBts = 14 },
                 (Token){ .tp = tokTypeName, .pl1 = 0,   .startBt = 4, .lenBts = 3 },
                 (Token){ .tp = tokAssignRight,    .pl2 = 1,
                             .startBt = 8, .lenBts = 6 },
                 (Token){ .tp = tokTypeName, .pl1 = strInt + S,   .startBt = 10, .lenBts = 3 },
         }))},
         (LexerTest) { .name = s("Simple type call"),
             .input = s("Foo Bar Baz;"),
             .expectedOutput = expect(((Token[]){
                 (Token){ .tp = tokStmt,        .pl2 = 3, .startBt = 0, .lenBts = 12 },
                 (Token){ .tp = tokTypeName, .pl1 = 0,   .startBt = 0, .lenBts = 3 },
                 (Token){ .tp = tokTypeName, .pl1 = 1,   .startBt = 4, .lenBts = 3 },
                 (Token){ .tp = tokTypeName, .pl1 = 2,   .startBt = 8, .lenBts = 3 }
         }))},
         (LexerTest) { .name = s("Generic function signature"),
             .input = s("{{lst L $W, w $W,} print w;}"),
             .expectedOutput = expect(((Token[]) {
                 (Token){ .tp = tokFn, .pl1 = slScope, .pl2 = 11,    .lenBts = 28 },

                 (Token){ .tp = tokFnParams, .pl1 = slClauseList, .pl2 = 7,
                          .startBt = 1, .lenBts = 17 },

                 (Token){ .tp = tokClause,           .pl2 = 3, .startBt = 2, .lenBts = 9 },
                 (Token){ .tp = tokWord, .pl1 = 0,         .startBt = 2, .lenBts = 3 }, //lst
                 (Token){ .tp = tokTypeName, .pl1 = strL + S, .startBt = 6, .lenBts = 1 },
                 (Token){ .tp = tokTypeVar,         .pl1 = 1, .startBt = 8, .lenBts = 2 }, // $W
                 (Token){ .tp = tokClause,          .pl2 = 2, .startBt = 12, .lenBts = 5 },
                 (Token){ .tp = tokWord,         .pl1 = 2,     .startBt = 12, .lenBts = 1 }, // w
                 (Token){ .tp = tokTypeVar,     .pl1 = 1,     .startBt = 14, .lenBts = 2 }, // $W

                 (Token){ .tp = tokStmt,         .pl2 = 2,     .startBt = 19, .lenBts = 8 },
                 (Token){ .tp = tokWord,  .pl1 = strPrint + S, .startBt = 19, .lenBts = 5 },
                 (Token){ .tp = tokWord,     .pl1 = 2, .startBt = 25, .lenBts = 1 },
         }))},
         (LexerTest) { .name = s("Data allocations"),
             .input = s("[[1 2 3] [-3 4 5]];"),
             .expectedOutput = expect(((Token[]) {
                 (Token){ .tp = tokStmt, .pl2 = 9, .lenBts = 19 },
                 (Token){ .tp = tokData, .pl1 = 0, .pl2 = 8, .startBt = 0, .lenBts=18 },

                 (Token){ .tp = tokData, .pl2 = 3,
                            .startBt = 1, .lenBts = 7 },
                 (Token){ .tp = tokInt,          .pl2 = 1, .startBt = 2, .lenBts = 1 },
                 (Token){ .tp = tokInt,          .pl2 = 2, .startBt = 4, .lenBts = 1 },
                 (Token){ .tp = tokInt,          .pl2 = 3, .startBt = 6, .lenBts = 1 },

                 (Token){ .tp = tokData,     .pl2 = 3, .startBt = 9, .lenBts = 8 },
                 (Token){ .tp = tokInt, .pl1 = (((int64_t)-3) >> 32),
                       .pl2 = (((int64_t)-3) & LOWER32BITS), .startBt = 10, .lenBts = 2 },
                 (Token){ .tp = tokInt,                .pl2 = 4, .startBt = 13, .lenBts = 1 },
                 (Token){ .tp = tokInt,                .pl2 = 5, .startBt = 15, .lenBts = 1 },
         }))}
    }));
}

//}}}


void runATestSet(LexerTestSet* (*testGenerator)(Arena*), TestContext* ct) {
    LexerTestSet* testSet = (testGenerator)(ct->a);
    for (int j = 0; j < testSet->totalTests; j++) {
        LexerTest test = testSet->tests[j];
        runLexerTest(test, ct);
    }
}


int main(int argc, char** argv) {
    printf("----------------------------\n");
    printf("--  LEXER TEST  --\n");
    printf("----------------------------\n");

    TestContext ct = (TestContext){.countTests = 0, .countPassed = 0, .a = createArena() };

    runATestSet(&wordTests, &ct);
    runATestSet(&stringTests, &ct);
    runATestSet(&operatorTests, &ct);
    runATestSet(&punctuationTests, &ct);
    runATestSet(&numericTests, &ct);
    runATestSet(&coreFormTests, &ct);
    runATestSet(&typeTests, &ct);

    //runATestSet(&metaTests, &countPassed, &countTests, a);
    if (ct.countTests == 0) {
        print("\nThere were no tests to run!");
    } else if (ct.countPassed == ct.countTests) {
        print("\nAll %d tests passed!", ct.countTests);
    } else {
        print("\nFailed %d tests out of %d!", (ct.countTests - ct.countPassed), ct.countTests);
    }

    deleteArena(ct.a);
}
