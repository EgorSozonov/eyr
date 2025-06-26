#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include "../include/libeyr.h"
#include "lexerTest.h"

//{{{ Utils

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


#define S   70000000 // A constant larger than the largest allowed file size. Separates parsed
                     // names from others


private Compiler* buildExpectedLexer(Arena *a, int totalTokens, Arr(Token) tokens) {
    Compiler* result = createLexer(empty, true, a);
    if (result == NULL) return result;

    CompResult* testRes = getCompResult(result);
    if (tokens == NULL) {
        return result;
    }
    for (int i = 0; i < totalTokens; i++) {
        Token tok = tokens[i];
        // offset nameIds and startBts for the standardText and standard nameIds correspondingly
        tok.startBt += testRes->stats.standardTextLen;
        if (tok.tp == tokWord || tok.tp == tokKwArg || tok.tp == tokType
             || (tok.tp == tokOperator && tok.pl2 == 10)
             || tok.tp == tokTypeVar || tok.tp == tokFieldAcc
        ) {
            if (tok.pl1 < S) { // parsed words
                tok.pl1 += testRes->stats.firstParsedName;
            } else { // built-in words
                tok.pl1 = nameOfStd(tok.pl1 - S);
            }
        }
        pushIntokens(tok, result);
    }

    return result;
}

#define expect(toks) buildExpectedLexer(a, sizeof(toks)/sizeof(Token), toks)
#define expectEmpty(toks) buildExpectedLexer(a, 0, NULL)


private Compiler*
expectError0(String errMsg, Arena *a, Int totalTokens, Arr(Token) tokens) {
    Compiler* result = buildExpectedLexer(a, totalTokens, tokens);
    setLexerError(errMsg, result);
    return result;
}

#define expectError(msg, toks) expectError0(msg, a, sizeof(toks)/sizeof(Token), toks)
#define expectEmptyWithError(msg) expectError0(msg, a, 0, NULL)


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
   ct->countTests++;
   if (ct->singleId > -1) {
      if (ct->countTests - 1 == ct->singleId)
         { ct->ranSingle = true; }
      else
         { return; }
   }
   Compiler* result = lexicallyAnalyze(test.input, ct->a);
   int equalityStatus = equalityLexer(result, test.expectedOutput);
   Int testId = ct->countTests - 1;
   if (equalityStatus == -2) {
      ct->countPassed += 1;
      return;
   } else if (equalityStatus == -1) {
      printf("\n\nERROR IN [%d][", testId);
      printStringNoLn(test.name);
      printf("]\nError msg: ");
      CompResult* testRes = getCompResult(result);
      CompResult* expectedRes = getCompResult(test.expectedOutput);
      printString(testRes->errMsg);
      if (expectedRes->wasLexerError) {
          printf("\nBut was expected: ");
          printString(expectedRes->errMsg);
      } else {
          printf("\nBut was expected to be error-free\n");
      }
      printLexer(result);
   } else {
      printf("ERROR IN [%d][", testId);
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
            .input = s("asdf:Abc;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 9 },
                (Token){ .tp = tokType, .pl2 = 0, .startBt = 0, .lenBts = 8 }
            }))
        },
        (LexerTest) {
            .name = s("Word correct capitalization 2"),
            .input = s("asdf:abcd:zyui;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 15 },
                (Token){ .tp = tokWord, .startBt = 0, .lenBts = 14  }
            }))
        },
        (LexerTest) {
            .name = s("Word correct capitalization 3"),
            .input = s("asdf:Abcd;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 10 },
                (Token){ .tp = tokType, .startBt = 0, .lenBts = 9 }
            }))
        },
        (LexerTest) {
            .name = s("Field accessor"),
            .input = s("a.field;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 2, .startBt = 0, .lenBts = 8 },
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
            .input = s("ifter;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 6 },
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
            .input = s("0x15;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 5 },
                (Token){ .tp = tokInt, .pl2 = 21, .startBt = 0, .lenBts = 4 }
            }))
        },
        (LexerTest) {
            .name = s("Hex numeric 2"),
            .input = s("0x05;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 5 },
                (Token){ .tp = tokInt, .pl2 = 5, .startBt = 0, .lenBts = 4 }
            }))
        },
        (LexerTest) {
            .name = s("Hex numeric 3"),
            .input = s("0xFFFFFFFFFFFFFFFF;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 19 },
                (Token){ .tp = tokInt, .pl1 = ((int64_t)-1 >> 32), .pl2 = ((int64_t)-1 & LOWER32BITS),
                        .startBt = 0, .lenBts = 18  }
            }))
        },
        (LexerTest) {
            .name = s("Hex numeric 4"),
            .input = s("0xFFFFFFFFFFFFFFFE;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 19 },
                (Token){ .tp = tokInt, .pl1 = ((int64_t)-2 >> 32), .pl2 = ((int64_t)-2 & LOWER32BITS),
                        .startBt = 0, .lenBts = 18  }
            }))
        },
        (LexerTest) {
            .name = s("Hex numeric too long"),
            .input = s("0xFFFFFFFFFFFFFFFF0"),
            .expectedOutput = expectError(s(errNumericBinWidthExceeded), ((Token[]) {
                (Token){ .tp = tokStmt }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric 1"),
            .input = s("1.234;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 6 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(1.234) >> 32,
                         .pl2 = longOfDoubleBits(1.234) & LOWER32BITS, .startBt = 0, .lenBts = 5 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric 2"),
            .input = s("00001.234;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 10 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(1.234) >> 32,
                         .pl2 = longOfDoubleBits(1.234) & LOWER32BITS, .startBt = 0, .lenBts = 9 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric 3"),
            .input = s("10500.01;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 9 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(10500.01) >> 32,
                         .pl2 = longOfDoubleBits(10500.01) & LOWER32BITS, .startBt = 0, .lenBts = 8 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric 4"),
            .input = s("0.9;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 4 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(0.9) >> 32,
                         .pl2 = longOfDoubleBits(0.9) & LOWER32BITS, .startBt = 0, .lenBts = 3 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric 5"),
            .input = s("100500.123456;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 14 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(100500.123456) >> 32,
                         .pl2 = longOfDoubleBits(100500.123456) & LOWER32BITS,
                         .startBt = 0, .lenBts = 13 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric big"),
            .input = s("9007199254740992.0;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 19 },
                (Token){ .tp = tokDouble,
                    .pl1 = longOfDoubleBits(9007199254740992.0) >> 32,
                    .pl2 = longOfDoubleBits(9007199254740992.0) & LOWER32BITS,
                    .startBt = 0, .lenBts = 18 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric too big"),
            .input = s("9007199254740993.0"),
            .expectedOutput = expectError(s(errNumericFloatWidthExceeded), ((Token[]) {
                (Token){ .tp = tokStmt }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric big exponent"),
            .input = s("1005001234560000000000.0;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 25 },
                (Token){ .tp = tokDouble,
                        .pl1 = longOfDoubleBits(1005001234560000000000.0) >> 32,
                        .pl2 = longOfDoubleBits(1005001234560000000000.0) & LOWER32BITS,
                        .startBt = 0, .lenBts = 24 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric tiny"),
            .input = s("0.0000000000000000000003;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 25 },
                (Token){ .tp = tokDouble,
                        .pl1 = longOfDoubleBits(0.0000000000000000000003) >> 32,
                        .pl2 = longOfDoubleBits(0.0000000000000000000003) & LOWER32BITS,
                        .startBt = 0, .lenBts = 24 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric negative 1"),
            .input = s("-9.0;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 5 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(-9.0) >> 32,
                        .pl2 = longOfDoubleBits(-9.0) & LOWER32BITS, .startBt = 0, .lenBts = 4 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric negative 2"),
            .input = s("-8.775_807;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 11 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(-8.775807) >> 32,
                    .pl2 = longOfDoubleBits(-8.775807) & LOWER32BITS, .startBt = 0, .lenBts = 10 }
            }))
        },
        (LexerTest) {
            .name = s("Float numeric negative 3"),
            .input = s("-1005001234560000000000.0;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 26 },
                (Token){ .tp = tokDouble, .pl1 = longOfDoubleBits(-1005001234560000000000.0) >> 32,
                          .pl2 = longOfDoubleBits(-1005001234560000000000.0) & LOWER32BITS,
                        .startBt = 0, .lenBts = 25 }
            }))
        },
        (LexerTest) {
            .name = s("Int numeric 1"),
            .input = s("3;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 2 },
                (Token){ .tp = tokInt, .pl2 = 3, .startBt = 0, .lenBts = 1 }
            }))
        },
        (LexerTest) {
            .name = s("Int numeric 2"),
            .input = s("12;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 3 },
                (Token){ .tp = tokInt, .pl2 = 12, .startBt = 0, .lenBts = 2,  }
            }))
        },
        (LexerTest) {
            .name = s("Int numeric 3"),
            .input = s("0987_12;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 8 },
                (Token){ .tp = tokInt, .pl2 = 98712, .startBt = 0, .lenBts = 7 }
            }))
        },
        (LexerTest) {
            .name = s("Int numeric 4"),
            .input = s("9_223_372_036_854_775_807;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 26 },
                (Token){ .tp = tokInt, .pl1 = ((int64_t)9223372036854775807 >> 32),
                        .pl2 = ((int64_t)9223372036854775807 & LOWER32BITS),
                        .startBt = 0, .lenBts = 25 }
            }))
        },
        (LexerTest) {
            .name = s("Int numeric negative 1"),
            .input = s("-1;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 3 },
                (Token){ .tp = tokInt, .pl1 = (((int64_t)-1) >> 32),
                        .pl2 = (((int64_t)-1) & LOWER32BITS),
                        .startBt = 0, .lenBts = 2 }
            }))
        },
        (LexerTest) {
            .name = s("Int numeric negative 2"),
            .input = s("-775_807;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 9 },
                (Token){ .tp = tokInt, .pl1 = ((int64_t)(-775807) >> 32),
                         .pl2 = ((int64_t)(-775807) & LOWER32BITS), .startBt = 0, .lenBts = 8 }
            }))
        },
        (LexerTest) {
            .name = s("Int numeric negative 3"),
            .input = s("-9_223_372_036_854_775_807;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 27 },
                (Token){ .tp = tokInt, .pl1 = ((int64_t)(-9223372036854775807) >> 32),
                        .pl2 = ((int64_t)(-9223372036854775807) & LOWER32BITS),
                        .startBt = 0, .lenBts = 26 }
            }))
        },
        (LexerTest) {
            .name = s("Int numeric error 1"),
            .input = s("3_"),
            .expectedOutput = expectError(s(errNumericEndUnderscore), ((Token[]) {
                (Token){ .tp = tokStmt }
        }))},
        (LexerTest) { .name = s("Int numeric error 2"),
            .input = s("9_223_372_036_854_775_808"),
            .expectedOutput = expectError(s(errNumericIntWidthExceeded), ((Token[]) {
                (Token){ .tp = tokStmt }
        }))}
    }));
}
//}}}
//{{{ String

LexerTestSet* stringTests(Arena* a) {
    return createTestSet(s("String literals lexer tests"), a, ((LexerTest[]) {
        (LexerTest) { .name = s("String simple literal"),
            .input = s("`asdfn't`;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 10 },
                (Token){ .tp = tokString, .startBt = 0, .lenBts = 9 }
            }))
        },
        (LexerTest) { .name = s("String literal with non-ASCII inside"),
            .input = s("`hello мир`;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 15 },
                (Token){ .tp = tokString, .startBt = 0, .lenBts = 14 }
        }))},
        (LexerTest) { .name = s("String literal unclosed"),
            .input = s("`asdf"),
            .expectedOutput = expectError(s(errPrematureEndOfInput), ((Token[]) {
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
            .expectedOutput = expectError(s(errPunctuationOnlyInMultiline), ((Token[]) {
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
            .input = s("awu; arn baz;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 4 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 3 },
                (Token){ .tp = tokStmt, .pl2 = 2, .startBt = 5, .lenBts = 8 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 5, .lenBts = 3 },
                (Token){ .tp = tokWord, .pl1 = 2, .startBt = 9, .lenBts = 3 }
        }))},
        (LexerTest) { .name = s("Stmt separator usage error"),
            .input = s("asdf (zoogle; baz)"),
            .expectedOutput = expectError(s(errPunctuationOnlyInMultiline), ((Token[]) {
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
                    (Token){ .tp = tokAccessorIn, .pl2 = 1, .startBt = 4, .lenBts = 5 },
                    (Token){ .tp = tokWord, .pl1 = 1, .startBt = 5, .lenBts = 3 }
            }))
        },
        (LexerTest) { .name = s("Accessors"),
            .input = s("arr[i] arr[i - 1] brr[j][k];"),
            .expectedOutput = expect(((Token[]) {
                (Token){ .tp = tokStmt,  .pl2 = 16, .startBt = 0, .lenBts = 28 },

                (Token){ .tp = tokAccessor, .pl2 = 3, .startBt = 0, .lenBts = 6 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 3 },
                (Token){ .tp = tokAccessorIn, .pl2 = 1, .startBt = 3, .lenBts = 3 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 4, .lenBts = 1 },

                (Token){ .tp = tokAccessor,   .pl2 = 5,  .startBt = 7, .lenBts = 10 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 7, .lenBts = 3 },
                (Token){ .tp = tokAccessorIn,   .pl2 = 3,  .startBt = 10, .lenBts = 7 },
                (Token){ .tp = tokWord, .pl1 = 1,        .startBt = 11, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opMinus, .pl2 = 8, .startBt = 13, .lenBts = 1 },
                (Token){ .tp = tokInt, .pl2 = 1, .startBt = 15, .lenBts = 1 },

                (Token){ .tp = tokAccessor,   .pl2 = 5,  .startBt = 18, .lenBts = 9 },
                (Token){ .tp = tokWord, .pl1 = 2,  .startBt = 18, .lenBts = 3 },
                (Token){ .tp = tokAccessorIn, .pl2 = 1, .startBt = 21, .lenBts = 3 },
                (Token){ .tp = tokWord, .pl1 = 3, .startBt = 22, .lenBts = 1 },
                (Token){ .tp = tokAccessorIn, .pl2 = 1, .startBt = 24, .lenBts = 3 },
                (Token){ .tp = tokWord, .pl1 = 4, .startBt = 25, .lenBts = 1 },
        }))},
        (LexerTest) {
            .name = s("Array numeric index"),
            .input = s("a[5];"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 4, .startBt = 0, .lenBts = 5 },
                (Token){ .tp = tokAccessor, .pl2 = 3, .startBt = 0, .lenBts = 4 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAccessorIn, .pl2 = 1, .startBt = 1, .lenBts = 3 },
                (Token){ .tp = tokInt, .pl2 = 5, .startBt = 2, .lenBts = 1 }
            }))
        },
        (LexerTest) {
            .name = s("Array variable index"),
            .input = s("a[ind];"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt,         .pl2 = 4, .startBt = 0, .lenBts = 7 },
                (Token){ .tp = tokAccessor,     .pl2 = 3, .startBt = 0, .lenBts = 6 },
                (Token){ .tp = tokWord, .pl1 = 0,         .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAccessorIn, .pl2 = 1, .startBt = 1, .lenBts = 5 },
                (Token){ .tp = tokWord, .pl1 = 1,     .startBt = 2, .lenBts = 3 }
            }))
        },
        (LexerTest) {
            .name = s("Array complex index"),
            .input = s("a[i + 1];"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt,         .pl2 = 6, .startBt = 0, .lenBts = 9 },
                (Token){ .tp = tokAccessor, .pl2 = 5, .startBt = 0, .lenBts = 8 },
                (Token){ .tp = tokWord, .pl1 = 0,         .startBt = 0, .lenBts = 1 }, // a
                (Token){ .tp = tokAccessorIn, .pl2 = 3, .startBt = 1, .lenBts = 7 },
                (Token){ .tp = tokWord, .pl1 = 1,   .startBt = 2, .lenBts = 1 }, // i
                (Token){ .tp = tokOperator, .pl1 = opPlus, .pl2 = 8, .startBt = 4, .lenBts = 1 },
                (Token){ .tp = tokInt,         .pl2 = 1, .startBt = 6, .lenBts = 1 }
            }))
        },
        (LexerTest) {
            .name = s("Array with metadata"),
            .input = s("[@Int 14];"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt,         .pl2 = 3, .startBt = 0, .lenBts = 10 },
                (Token){ .tp = tokData, .pl1 = BIG, .pl2 = 2, .startBt = 0, .lenBts = 9 },
                (Token){ .tp = tokType, .pl1 = (strInt + S),   .startBt = 2, .lenBts = 3 },
                (Token){ .tp = tokInt,         .pl2 = 14, .startBt = 6, .lenBts = 2 }
            }))
        }
    }));
}

//}}}
//{{{ Operator

LexerTestSet* operatorTests(Arena* a) {
    return createTestSet(s("Operator lexer tests"), a, ((LexerTest[]) {
        (LexerTest) { .name = s("Operator simple 1"),
            .input = s("+;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 0, .lenBts = 2 },
                (Token){ .tp = tokOperator, .pl1 = opPlus, .pl2 = 8, .startBt = 0, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operators extended"),
            .input = s("+: *: -: /:;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 4, .lenBts = 12 },
                (Token){ .tp = tokOperator, .pl1 = opPlusExt, .pl2 = 8, .startBt = 0, .lenBts = 2 },
                (Token){ .tp = tokOperator, .pl1 = opTimesExt, .pl2 = 9, .startBt = 3, .lenBts = 2 },
                (Token){ .tp = tokOperator, .pl1 = opMinusExt, .pl2 = 8, .startBt = 6, .lenBts = 2 },
                (Token){ .tp = tokOperator, .pl1 = opDivByExt, .pl2 = 9, .startBt = 9, .lenBts = 2 }
        }))},
        (LexerTest) { .name = s("Operators bitwise"),
            .input = s("!. ||. >>. &&. <<. ^.;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 6, .lenBts = 22 },
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
            .input = s("+ - / * && || ? <=> $ ' /\\ # <0 >0 not;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt,                .pl2 = 15, .startBt = 0, .lenBts = 39 },
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
                (Token){ .tp = tokOperator, .pl1 = opBoolNot, .pl2 = 100, .startBt = 35, .lenBts = 3 }
        }))},
        (LexerTest) { .name = s("Operator expression"),
            .input = s("a - b;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 3, .startBt = 0, .lenBts = 6 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opMinus, .pl2 = 8, .startBt = 2, .lenBts = 1 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 4, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operator assignment 1"),
            .input = s("a += b;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokAssignment, .pl1 = 0, .pl2 = 5,
                    .startBt = 0, .lenBts = 7 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAssignRight, .pl2 = 3, .startBt = 2, .lenBts = 5 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opPlus, .startBt = 2, .lenBts = 1 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 5, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operator assignment 2"),
            .input = s("a ||= b;"),
            .expectedOutput = expect(((Token[]) {
                (Token){ .tp = tokAssignment, .pl1 = 0, .pl2 = 5,
                         .startBt = 0, .lenBts = 8 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAssignRight, .pl2 = 3, .startBt = 2, .lenBts = 6 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opBoolOr, .startBt = 2, .lenBts = 2 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 6, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operator assignment 3"),
            .input = s("a*:= b;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokAssignment, .pl1 = 0, .pl2 = 5,
                         .startBt = 0, .lenBts = 7 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAssignRight, .pl2 = 3, .startBt = 1, .lenBts = 6 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opTimesExt, .startBt = 1, .lenBts = 2 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 5, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operator assignment 4"),
            .input = s("a ^.= b;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokAssignment, .pl1 = 0, .pl2 = 5, .startBt = 0,
                         .lenBts = 8 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAssignRight, .pl2 = 3, .startBt = 2, .lenBts = 6 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opBitwiseXor, .startBt = 2, .lenBts = 2 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 6, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operator assignment 5 (increment)"),
            .input = s("a++;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokAssignment, .pl1 = 0, .pl2 = 5, .startBt = 0,
                         .lenBts = 4 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAssignRight, .pl2 = 3, .startBt = 1, .lenBts = 3 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opPlus, .startBt = 1, .lenBts = 1 },
                (Token){ .tp = tokInt, .pl1 = 0, .pl2 = 1, .startBt = 2, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operator assignment in parens error"),
            .input = s("(x += y + 5)"),
            .expectedOutput = expectError(s(errOperatorAssignmentPunct), ((Token[]) {
                (Token){ .tp = tokStmt },
                (Token){ .tp = tokParens },
                (Token){ .tp = tokWord, .startBt = 1, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operator assignment with parens"),
            .input = s("x -:= (y + 5);"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokAssignment, .pl1 = 0, .pl2 = 8, .lenBts = 14 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAssignRight, .pl2 = 6, .startBt = 2, .lenBts = 12 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 }, // x
                (Token){ .tp = tokOperator, .pl1 = opMinusExt, .startBt = 2, .lenBts = 2 },
                (Token){ .tp = tokParens, .pl2 = 3, .startBt = 6, .lenBts = 7 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 7, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opPlus, .pl2 = 8, .startBt = 9, .lenBts = 1 },
                (Token){ .tp = tokInt, .pl2 = 5, .startBt = 11, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Operator assignment in parens error"),
            .input = s("x (+= y) + 5"),
            .expectedOutput = expectError(s(errOperatorAssignmentPunct), ((Token[]) {
                (Token){ .tp = tokStmt },
                (Token){ .tp = tokWord, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokParens, .startBt = 2 }
        }))},
        (LexerTest) { .name = s("Operator assignment multiple error"),
            .input = s("x = y = 7"),
            .expectedOutput = expectError(s(errOperatorAssignmentPunct), ((Token[]) {
                (Token){ .tp = tokAssignment, .pl2 = 0, .lenBts = 0 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokAssignRight, .pl2 = 0, .startBt = 2 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 4, .lenBts = 1 }
        }))},
        (LexerTest) { .name = s("Boolean operators"),
            .input = s("a && b || c;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 5, .lenBts = 12 },
                (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opBoolAnd, .pl2 = 1, .startBt = 2, .lenBts = 2 },
                (Token){ .tp = tokWord, .pl1 = 1, .startBt = 5, .lenBts = 1 },
                (Token){ .tp = tokOperator, .pl1 = opBoolOr, .pl2 = 0, .startBt = 7, .lenBts = 2 },
                (Token){ .tp = tokWord, .pl1 = 2, .startBt = 10, .lenBts = 1 }
        }))},
       (LexerTest) { .name = s("Negation"),
            .input = s("-5 -x;"),
            .expectedOutput = expect(((Token[]){
                (Token){ .tp = tokStmt, .pl2 = 3, .lenBts = 6 },
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
             .input = s("co = 8;"),
             .expectedOutput = expect(((Token[]){
                 (Token){ .tp = tokAssignment,  .pl2 = 3,             .lenBts = 7 },
                 (Token){ .tp = tokWord,  .pl2 = 0,   .startBt = 0, .lenBts = 2 },
                 (Token){ .tp = tokAssignRight,  .pl2 = 1,   .startBt = 3, .lenBts = 4 },
                 (Token){ .tp = tokInt, .pl2 = 8, .startBt = 5,     .lenBts = 1 },
         }))},
         (LexerTest) { .name = s("Statement-type core form"),
             .input = s("x = 9; assert (x == 55) `Error!`;"),
             .expectedOutput = expect(((Token[]){
                 (Token){ .tp = tokAssignment,  .pl2 = 3,             .lenBts = 6 },
                 (Token){ .tp = tokWord,  .pl2 = 0,             .lenBts = 1 }, // x
                 (Token){ .tp = tokAssignRight,  .pl2 = 1,   .startBt = 2, .lenBts = 4 },
                 (Token){ .tp = tokInt, .pl2 = 9, .startBt = 4,     .lenBts = 1 },

                 (Token){ .tp = tokAssert, .pl2 = 5, .startBt = 7,  .lenBts = 26 },
                 (Token){ .tp = tokParens, .pl2 = 3, .startBt = 14, .lenBts = 9 },
                 (Token){ .tp = tokWord,                  .startBt = 15, .lenBts = 1 },
                 (Token){ .tp = tokOperator, .pl1 = opEquality, .pl2 = 5, .startBt = 17, .lenBts = 2 },
                 (Token){ .tp = tokInt, .pl2 = 55,  .startBt = 20,  .lenBts = 2 },
                 (Token){ .tp = tokString,               .startBt = 24,  .lenBts = 8 }
         }))},
         (LexerTest) { .name = s("Statement-type core form error"),
             .input = s("x / (assert foo)"),
             .expectedOutput = expectError(s(errCoreNotInsideStmt), ((Token[]) {
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
                 (Token){ .tp = tokAccessorIn,    .pl2 = 1, .startBt = 1, .lenBts = 3 },
                 (Token){ .tp = tokWord,   .pl1 = 1,      .startBt = 2, .lenBts = 1 }, // i
                 (Token){ .tp = tokAccessorIn,    .pl2 = 1, .startBt = 4, .lenBts = 3 },
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
                       "ei <0 (x <=> 7) { 11; }\n"
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
                         .startBt = 23, .lenBts = 23 },
                (Token){ .tp = tokStmt,            .pl2 = 5, .startBt = 26, .lenBts = 13 },
                (Token){ .tp = tokOperator, .pl1 = opLTZero, .pl2 = 100, .startBt = 26, .lenBts = 2 },
                (Token){ .tp = tokParens,   .pl2 = 3, .startBt = 29, .lenBts = 9 },
                (Token){ .tp = tokWord,               .startBt = 30, .lenBts = 1 }, // x
                (Token){ .tp = tokOperator, .pl1 = opComparator, .pl2 = 6, .startBt = 32, .lenBts = 3 },
                (Token){ .tp = tokInt,          .pl2 = 7, .startBt = 36, .lenBts = 1 },
                (Token){ .tp = tokStmt,      .pl2 = 1, .startBt = 41, .lenBts = 3 },
                (Token){ .tp = tokInt,       .pl2 = 11, .startBt = 41, .lenBts = 2 },

                (Token){ .tp = tokElse, .pl1 = slScope, .pl2 = 2,
                         .startBt = 47, .lenBts = 14 },
                (Token){ .tp = tokStmt, .pl2 = 1, .startBt = 54, .lenBts = 5 },
                (Token){ .tp = tokBool, .pl2 = 1, .startBt = 54, .lenBts = 4 }
         }))},
         (LexerTest) { .name = s("Function simple 1"),
             .input = s("fn noo [Int Int -> Int] f{ x y -> return x - y;}"),
             .expectedOutput = expect(((Token[]){
                 (Token){ .tp = tokToplevelFn,         .pl2 = 13,
                          .startBt = 0, .lenBts = 48 },
                 (Token){ .tp = tokWord, .pl1 = 0,        .startBt = 3, .lenBts = 3 }, // noo
                 (Token){ .tp = tokType, .pl1 = (strF + S), .pl2 = 3,  // F(...)
                          .startBt = 7, .lenBts = 16 },
                 (Token){ .tp = tokType, .pl1 = (strInt + S), .pl2 = 0, // Int
                          .startBt = 8, .lenBts = 3 },
                 (Token){ .tp = tokType, .pl1 = (strInt + S), .pl2 = 0, .startBt = 12, .lenBts = 3},
                 (Token){ .tp = tokType, .pl1 = (strInt + S), .pl2 = 0, .startBt = 19, .lenBts = 3},

                 (Token){ .tp = tokFn, .pl1 = slScope,       .pl2 = 7,
                          .startBt = 24, .lenBts = 24 },

                 (Token){ .tp = tokStmt, .pl2 = 2,   .startBt = 27, .lenBts = 3 }, //param list
                 (Token){ .tp = tokWord, .pl1 = 2,   .startBt = 27, .lenBts = 1 }, // x
                 (Token){ .tp = tokWord, .pl1 = 3,   .startBt = 29, .lenBts = 1 }, // y

                 (Token){ .tp = tokReturn,       .pl2 = 3, .startBt = 34, .lenBts = 13 },
                 (Token){ .tp = tokWord, .pl1 = 2,       .startBt = 41, .lenBts = 1 }, // x
                 (Token){ .tp = tokOperator, .pl1 = opMinus, .pl2 = 8, .startBt = 43, .lenBts = 1 },
                 (Token){ .tp = tokWord, .pl1 = 3,       .startBt = 45, .lenBts = 1 } // y
         }))},
         (LexerTest) { .name = s("Function simple 2"),
             .input = s("fn noFBeforeScope [-> Int] f{ return 5;}"),
             .expectedOutput = expect(((Token[]){
                 (Token){ .tp = tokToplevelFn,         .pl2 = 6,
                          .startBt = 0, .lenBts = 40 },
                 (Token){ .tp = tokWord, .pl1 = 0,        .startBt = 3, .lenBts = 14 },
                 (Token){ .tp = tokType, .pl1 = (strF + S), .pl2 = 1,  // F(...)
                          .startBt = 18, .lenBts = 8 },
                 (Token){ .tp = tokType, .pl1 = (strInt + S), .pl2 = 0, .startBt = 22, .lenBts = 3},
                 (Token){ .tp = tokFn, .pl1 = slScope,       .pl2 = 2,
                          .startBt = 27, .lenBts = 13 },
                 (Token){ .tp = tokReturn,       .pl2 = 1, .startBt = 30, .lenBts = 9 },
                 (Token){ .tp = tokInt,          .pl2 = 5, .startBt = 37, .lenBts = 1 }
         }))},
         (LexerTest) { .name = s("For loop simple"),
             .input = s("for {x' = 1; x < 101; x = x + 1; -> print x; }"),
             .expectedOutput = expect(((Token[]) {
                 (Token){ .tp = tokFor, .pl1 = slScope, .pl2 = 18, .lenBts = 46 },

                 (Token){ .tp = tokAssignment,   .pl2 = 3, .startBt = 5, .lenBts = 7 }, // x' = 1
                 (Token){ .tp = tokWord, .pl1 = 0, .pl2 = 1, .startBt = 5, .lenBts = 1 }, // x
                 (Token){ .tp = tokAssignRight,  .pl2 = 1, .startBt = 8, .lenBts = 4 },
                 (Token){ .tp = tokInt,          .pl2 = 1, .startBt = 10, .lenBts = 1 },

                 (Token){ .tp = tokStmt,         .pl2 = 3, .startBt = 13, .lenBts = 8 },
                 (Token){ .tp = tokWord,     .pl1 = 0, .pl2 = 0, .startBt = 13, .lenBts = 1 }, // x
                 (Token){ .tp = tokOperator, .pl1 = opLessTh, .pl2 = 6, .startBt = 15, .lenBts = 1 },
                 (Token){ .tp = tokInt,             .pl2 = 101, .startBt = 17, .lenBts = 3 },

                 (Token){ .tp = tokStmt,           .pl2 = 2, .startBt = 36, .lenBts = 8 }, // loop body
                 (Token){ .tp = tokWord,  .pl1 = (strPrint + S), .pl2 = 0, //print
                            .startBt = 36, .lenBts = 5 },
                 (Token){ .tp = tokWord,                     .startBt = 42, .lenBts = 1 },  // x

                 (Token){ .tp = tokMisc, .pl1 = miscForStep, .startBt = 22 }, // x = x + 1
                 (Token){ .tp = tokAssignment,    .pl2 = 5, .startBt = 22, .lenBts = 10 },
                 (Token){ .tp = tokWord, .pl1 = 0, .pl2 = 0, .startBt = 22, .lenBts = 1 },
                 (Token){ .tp = tokAssignRight, .pl1 = 0, .pl2 = 3,
                             .startBt = 24, .lenBts = 8 },
                 (Token){ .tp = tokWord, .pl1 = 0, .pl2 = 0, .startBt = 26, .lenBts = 1 },
                 (Token){ .tp = tokOperator, .pl1 = opPlus, .pl2 = 8, .startBt = 28, .lenBts = 1 },
                 (Token){ .tp = tokInt,           .pl2 = 1, .startBt = 30, .lenBts = 1 }
         }))},
         (LexerTest) {
            .name = s("For loop error: neither step nor body"),
            .input = s("for {x' = 1; x < 101; -> }"),
            .expectedOutput = expectError(s(errLoopEmptyStepBody),
            ((Token[]) {
                (Token){ .tp = tokFor }
            }))
         },
         (LexerTest) {
            .name = s("For loop error: no condition"),
            .input = s("for {x' = 1; x = x + 1; -> $x .print; }"),
            .expectedOutput = expectError(s(errLoopNoCondition),
            ((Token[]) {
                (Token){ .tp = tokFor }
            }))
         },
         (LexerTest) { .name = s("Each loop simple"),
             .input = s("each { coll -> print coll.@; }"),
             .expectedOutput = expect(((Token[]) {
                 (Token){ .tp = tokEach, .pl1 = slScope, .pl2 = 18, .lenBts = 46 },
                 (Token){ .tp = tokMisc,         .pl2 = miscForStep0, .startBt = 13, .lenBts = 8 },
                 
                 (Token){ .tp = tokStmt,         .pl2 = 1, .startBt = 13, .lenBts = 8 },
                 (Token){ .tp = tokWord,     .pl1 = 0, .pl2 = 0, .startBt = 13, .lenBts = 1 }, // x

                 (Token){ .tp = tokStmt,           .pl2 = 2, .startBt = 36, .lenBts = 8 }, // loop body
                 (Token){ .tp = tokMisc,  .pl1 = miscEachElem, .pl2 = 0, // .@
                            .startBt = 36, .lenBts = 5 },
                 (Token){ .tp = tokWord,                     .startBt = 42, .lenBts = 1 }  // coll

         }))},
    }));
}

//}}}
//{{{ Types

LexerTestSet* typeTests(Arena* a) {
    return createTestSet(s("Type forms lexer tests"), a, ((LexerTest[]) {
         (LexerTest) { .name = s("Type definition"),
             .input = s("Foo = Int;"),
             .expectedOutput = expect(((Token[]){
                 (Token){ .tp = tokAssignment, .pl1 = 0, .pl2 = 3, .startBt = 0, .lenBts = 10 },
                 (Token){ .tp = tokType, .pl1 = 0,   .startBt = 0, .lenBts = 3 },
                 (Token){ .tp = tokAssignRight,    .pl2 = 1,
                             .startBt = 4, .lenBts = 6 },
                 (Token){ .tp = tokType, .pl1 = strInt + S,   .startBt = 6, .lenBts = 3 },
         }))},
         (LexerTest) { .name = s("Simple type call"),
             .input = s("[Foo Bar Baz];"),
             .expectedOutput = expect(((Token[]){
                 (Token){ .tp = tokStmt,           .pl2 = 3, .startBt = 0, .lenBts = 14 },
                 (Token){ .tp = tokType, .pl1 = 0, .pl2 = 2,  .startBt = 0, .lenBts = 13 },
                 (Token){ .tp = tokType, .pl1 = 1,   .startBt = 5, .lenBts = 3 },
                 (Token){ .tp = tokType, .pl1 = 2,   .startBt = 9, .lenBts = 3 }
         }))},
         (LexerTest) { .name = s("Generic function signature"),
             .input = s("fn f [[A $W] $W ->] f{lst w -> print w;}"),
             .expectedOutput = expect(((Token[]) {
                 (Token){ .tp = tokToplevelFn, .pl1 = 0,  .pl2 = 13,    .lenBts = 40 },
                 (Token){ .tp = tokWord, .pl1 = 0,        .pl2 = 0, .startBt = 3,   .lenBts = 1 },
                 (Token){ .tp = tokType, .pl1 = strF + S, .pl2 = 4,
                          .startBt = 5, .lenBts = 14 },
                 (Token){ .tp = tokType, .pl1 = strArr + S, .pl2 = 1,
                          .startBt = 6, .lenBts = 6 },
                 (Token){ .tp = tokTypeVar, .pl1 = 1,     .pl2 = 0, .startBt = 9, .lenBts = 2 },
                 (Token){ .tp = tokTypeVar, .pl1 = 1,     .pl2 = 0, .startBt = 13, .lenBts = 2 },
                 (Token){ .tp = tokType, .pl1 = strVoid + S, .pl2 = 0,
                          .startBt = 18, .lenBts = 0 },

                 (Token){ .tp = tokFn, .pl1 = slScope, .pl2 = 6, .startBt = 20, .lenBts = 20 },
                 (Token){ .tp = tokStmt,           .pl2 = 2, .startBt = 22, .lenBts = 5 },
                 (Token){ .tp = tokWord, .pl1 = 2,           .startBt = 22, .lenBts = 3 }, //lst
                 (Token){ .tp = tokWord, .pl1 = 3,           .startBt = 26, .lenBts = 1 }, // w

                 (Token){ .tp = tokStmt,           .pl2 = 2, .startBt = 31, .lenBts = 8 },
                 (Token){ .tp = tokWord, .pl1 = strPrint + S, .startBt = 31, .lenBts = 5 },
                 (Token){ .tp = tokWord, .pl1 = 3,           .startBt = 37, .lenBts = 1 },
         }))},
         (LexerTest) { .name = s("Function type"),
             .input = s("F[From -> To];"),
             .expectedOutput = expect(((Token[]) {
                 (Token){ .tp = tokStmt, .pl2 = 3,  .lenBts = 14 },
                 (Token){ .tp = tokType, .pl1 = strF + S, .pl2 = 2, .lenBts = 13 },
                 (Token){ .tp = tokType, .pl1 = 0, .startBt = 2, .lenBts = 4 },
                 (Token){ .tp = tokType, .pl1 = 1, .startBt = 10, .lenBts = 2 }
         }))},
         (LexerTest) { .name = s("Function type error: multiple arrows"),
             .input = s("F[Aa -> B -> C]"),
             .expectedOutput = expectError(s(errFnTypeArrows), ((Token[]) {
                 (Token){ .tp = tokStmt, .pl2 = 0,  .lenBts = 0 },
                 (Token){ .tp = tokType, .pl1 = strF + S, .pl2 = 0, .lenBts = 1 },
                 (Token){ .tp = tokType, .pl1 = 0, .startBt = 2, .lenBts = 2 },
                 (Token){ .tp = tokType, .pl1 = 1, .startBt = 8, .lenBts = 1 }
         }))},
         (LexerTest) { .name = s("Function type: no arrows"),
             .input = s("F[Aa B C];"),
             .expectedOutput = expectError(s(errFnTypeArrows), ((Token[]) {
                 (Token){ .tp = tokStmt },
                 (Token){ .tp = tokType, .pl1 = strF + S, .pl2 = 0, .startBt = 0, .lenBts = 1 },
                 (Token){ .tp = tokType, .pl1 = 0, .startBt = 2, .lenBts = 2 },
                 (Token){ .tp = tokType, .pl1 = 1, .startBt = 5, .lenBts = 1 },
                 (Token){ .tp = tokType, .pl1 = 2, .startBt = 7, .lenBts = 1 }
         }))},
         (LexerTest) { .name = s("Function type: empty brackets"),
             .input = s("F[];"),
             .expectedOutput = expect(((Token[]) {
                 (Token){ .tp = tokStmt, .pl2 = 2,  .lenBts = 4 },
                 (Token){ .tp = tokType, .pl1 = strF + S, .pl2 = 1, .lenBts = 3 },
                 (Token){ .tp = tokType, .pl1 = strVoid + S, .startBt = 2, .lenBts = 0 }
         }))},
         (LexerTest) { .name = s("Function type: nullary function"),
             .input = s("F[->Int];"),
             .expectedOutput = expect(((Token[]) {
                 (Token){ .tp = tokStmt, .pl2 = 2,  .lenBts = 9 },
                 (Token){ .tp = tokType, .pl1 = strF + S, .pl2 = 1, .lenBts = 8 },
                 (Token){ .tp = tokType, .pl1 = strInt + S, .startBt = 4, .lenBts = 3 }
         }))},
         (LexerTest) { .name = s("Function type: void-returning function"),
             .input = s("F[Int->];"),
             .expectedOutput = expect(((Token[]) {
                 (Token){ .tp = tokStmt, .pl2 = 3,  .lenBts = 9 },
                 (Token){ .tp = tokType, .pl1 = strF + S, .pl2 = 2, .lenBts = 8 },
                 (Token){ .tp = tokType, .pl1 = strInt + S, .startBt = 2, .lenBts = 3 },
                 (Token){ .tp = tokType, .pl1 = strVoid + S, .startBt = 7, .lenBts = 0 }
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

void
printTestResults(TestContext ct) {
   if (ct.countTests == 0) {
      printf("There were no tests to run!\n");
   } else if (ct.countPassed == ct.countTests) {
      if (ct.countTests > 1) {
         printf("Passed all %d tests!\n", ct.countTests);
      } else {
         printf("The test was passed.\n");
      }
   } else if (ct.singleId > -1) {
      if (ct.ranSingle) {
         printf(ct.countPassed == 1 ? "\nThe test was passed.\n" : "\nFailed the test!\n");
      } else {
         printf("Wanted to run test %d but encountered only %d tests\n",
            ct.singleId, ct.countTests
         );
      }
   } else {
      printf("Failed %d tests out of %d!\n", (ct.countTests - ct.countPassed), ct.countTests);
   }
}

int main(int argc, char** argv) {
   printf("--------------------\n");
   printf("---  LEXER TEST  ---\n");
   printf("--------------------\n");

   TestContext ct = (TestContext){.countTests = 0, .countPassed = 0,
       .singleId = -1, .ranSingle = false, .a = createArena() };

   if (argc == 2) {
      char* tail;
      long testId = strtol(argv[1], &tail, 10);
      if (*tail == '\0' && testId >= 0) {
         ct.singleId = (int)testId;
      }
   }

   runATestSet(&wordTests, &ct);
   runATestSet(&stringTests, &ct);
   runATestSet(&operatorTests, &ct);
   runATestSet(&punctuationTests, &ct);
   runATestSet(&numericTests, &ct);
   runATestSet(&coreFormTests, &ct);
   runATestSet(&typeTests, &ct);

   //runATestSet(&metaTests, &countPassed, &countTests, a);
   printTestResults(ct);

   deleteArena(ct.a);
}
