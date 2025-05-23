#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <setjmp.h>
extern jmp_buf excBuf;
#include "../include/libeyr.h"
#include "parserTest.h"


//{{{ Utils

typedef struct Codegen Codegen;

typedef struct { //:ParserTest
   String name;
   Compiler* test;
   Compiler* control;
   Bool compareLocsToo;
} ParserTest;


typedef struct {
   String name;
   Int totalTests;
   Arr(ParserTest) tests;
} ParserTestSet;


private ParserTestSet* createTestSet0(String name, Arena *a, int count, Arr(ParserTest) tests) {
   ParserTestSet* result = allocateOnArena(sizeof(ParserTestSet), a);
   result->name = name;
   result->totalTests = count;
   result->tests = allocateOnArena(count*sizeof(ParserTest), a);
   if (result->tests == NULL) return result;

   for (int i = 0; i < count; i++) {
      result->tests[i] = tests[i];
   }
   return result;
}

#define createTestSet(n, a, tests) createTestSet0(n, a, sizeof(tests)/sizeof(ParserTest), tests)


#define oper(opType, typeId) tryGetOper0(opType, typeId, protoOvs)
#define ty(name) getBinding(name, protoOvs)

private Int
transformFuncId(Int inp, CompStats const* stats) {
   if (inp < S) { // parsed stuff
      return inp + stats->countNonparsedFns;
   } else if (inp < O) { // imported but not operators: "foo", "bar"
      return inp - I + stats->countNonparsedFns;
   } else { // operators
      return inp - O;
   }
}

private Int
transformBindingVarId(Int inp, CompStats const* stats) {
   return inp + stats->countNonparsedVars;
}

private ParserTest //:createTest0
createTest0(String name, String sourceCode, Arr(Node) nodes, Int countNodes, Arr(Int) types,
         Int countTypes, Arr(TestEntityImport) imports, Int countImports, Arena* a) {
// Creates a test with two parsers: one is the init parser (contains all the "imported" bindings and
// pre-defined nodes), and the other is the output parser (with all the stuff parsed from source code).
// When the test is run, the init parser will parse the tokens and then will be compared to the
// expected output parser.
// Nontrivial: this handles binding ids inside nodes, so that e.g. if the pl1 in nodVar is 1,
// it will be inserted as 1 + (the number of built-in bindings) etc
   Compiler* test = lexicallyAnalyze(sourceCode, a);
   Compiler* control = lexicallyAnalyze(sourceCode, a);

   CompResult* controlRes = getCompResult(control);
   if (controlRes->wasLexerError == true) {
      return (ParserTest) {
         .name = name, .test = test, .control = control, .compareLocsToo = false };
   }
   initializeParser(control, a);
   initializeParser(test, a);
   updateStats(control);
   updateStats(test);
   importTestFns(types, countTypes, imports, countImports, a, OUT control);
   importTestFns(types, countTypes, imports, countImports, a, OUT test);

   controlRes = getCompResult(control); // the updated version after entities were imported
   // The control compiler
   for (Int i = 0; i < countNodes; i++) {
      Node nd = nodes[i];
      Unt nodeType = nd.tp;
      // All the node types which contain entityIds in their pl1
      if ((nodeType == nodCall && nd.pl3 == callNormal) || nodeType == nodFnDef) {
         nd.pl1 = transformFuncId(nd.pl1, &controlRes->stats);
      } else if (nodeType == nodVar || (nodeType == nodCall && nd.pl3 == callVar)) {
         nd.pl1 = transformBindingVarId(nd.pl1, &controlRes->stats);
      }
      // transform pl2/pl3 if it holds FuncId
      if (nodeType == nodVar && (nd.pl3 == assiFnVarUse || nd.pl3 == assiFnVarDef)) {
         nd.pl2 = transformFuncId(nd.pl2, &controlRes->stats);
      }
      newNode(nd, (SourceLoc){.startBt = 0, .lenBts = 0}, control);
   }
   updateStats(control);
   return (ParserTest){ .name = name, .test = test, .control = control, .compareLocsToo = false };
}

#define createTest(name, input, nodes, types, entities) \
   createTest0((name), (input), (nodes), sizeof(nodes)/sizeof(Node), (types), sizeof(types)/4, \
   (entities), sizeof(entities)/sizeof(TestEntityImport), a)


private ParserTest createTestWithError0(String name, String message, String input,
      Arr(Node) nodes, Int countNodes, Arr(Int) types, Int countTypes,
      Arr(TestEntityImport) entities, Int countEntities, Arena* a) {
// Creates a test with two parsers where the expected result is an error in parser
   ParserTest theTest = createTest0(name, input, nodes, countNodes, types, countTypes, entities,
                            countEntities, a);
   setParserError(message, theTest.control);
   return theTest;
}

#define createTestWithError(name, errorMessage, input, nodes, types, entities) \
   createTestWithError0((name), errorMessage, (input), (nodes), sizeof(nodes)/sizeof(Node), types,\
   sizeof(types)/4, entities, sizeof(entities)/sizeof(TestEntityImport), a)


private ParserTest createTestWithLocs0(String name, String input, Arr(Node) nodes,
               Int countNodes, Arr(Int) types, Int countTypes, Arr(TestEntityImport) entities,
               Int countEntities, Arr(SourceLoc) locs, Int countLocs,
               Arena* a) {
// Creates a test with two parsers where the source locs are specified (unlike most parser tests)
   ParserTest theTest = createTest0(name, input, nodes, countNodes, types, countTypes, entities,
                            countEntities, a);
   CompResult* controlRes = getCompResult(theTest.control);
   if (controlRes->wasLexerError)
      { return theTest; }
   for (Int j = 0; j < countLocs; ++j) {
      SourceLoc loc = locs[j];
      loc.startBt += controlRes->stats.standardTextLen;
      setLoc(loc, j, theTest.control);
   }
   return theTest;
}

#define createTestWithLocs(name, input, nodes, types, entities, locs) \
   createTestWithLocs0((name), (input), (nodes), sizeof(nodes)/sizeof(Node), types,\
   sizeof(types)/4, entities, sizeof(entities)/sizeof(TestEntityImport),\
   locs, sizeof(locs)/sizeof(SourceLoc), a)


void runTest(ParserTest test, TestContext* ct) {
// Runs a single lexer test and prints err msg to stdout in case of failure. Returns error code
   ct->countTests += 1;
   CompResult* testRes = getCompResult(test.test);
   CompResult* controlRes = getCompResult(test.control);
   if (testRes->stats.toksLen == 0) {
      print("Lexer result empty");
      return;
   } else if (controlRes->wasLexerError) {
      print("Lexer error");
      printLexer(test.control);
      return;
   }
   parseMain(test.test, ct->a);
   int equalityStatus = equalityParser(test.test, test.control, test.compareLocsToo);
   if (equalityStatus == -2) {
      ct->countPassed += 1;
      return;
   } else if (equalityStatus == -1) {
      printf("\n\nERROR IN [");
      printStringNoLn(test.name);
      printf("]\nError msg: ");
      printString(testRes->errMsg);
      printf("\nBut was expected: ");
      printString(controlRes->errMsg);
      printf("\n");
      print("   LEXER:")
      printLexer(test.test);
      print("   PARSER:")
      printParser(test.test);
   } else {
      printf("ERROR IN ");
      printString(test.name);
      printf("On node %d\n", equalityStatus);
      print("   LEXER:")
      printLexer(test.test);
      print("   PARSER:")
      printParser(test.test);
   }
}


private Node doubleNd(double value) {
   return (Node){ .tp = tokDouble, .pl1 = longOfDoubleBits(value) >> 32,
                           .pl2 = longOfDoubleBits(value) & LOWER32BITS };
}

//}}}
//{{{ Assignment tests

ParserTestSet* assignmentTests(Compiler* protoOvs, Arena* a) {
   return createTestSet(s("Assignment test set"), a, ((ParserTest[]){
//~      createTestWithLocs(
//~         s("Simple top-level definition"),
//~         s("x = 12;"),
//~         ((Node[]) {
//~            (Node){ .tp = nodDef, .pl2 = 2, .pl3 = 2 }, // x
//~            (Node){ .tp = nodVar, .pl2 = 0, .pl3 = assiVarAssignment },
//~            (Node){ .tp = tokInt,  .pl2 = 12 }
//~         }),
//~         ((Int[]) {}),
//~         ((TestEntityImport[]) {}),
//~         ((SourceLoc[]) {
//~            { .startBt = 0, .lenBts = 11 },
//~            { .startBt = 4, .lenBts = 1 },
//~            { .startBt = 8, .lenBts = 2 }
//~         })
//~      ),
//~      createTestWithLocs(
//~         s("Double top-level constant"),
//~         s("x = 12;\n"
//~           "def second = x;"
//~         ),
//~         ((Node[]) {
//~            (Node){ .tp = nodDef, .pl2 = 2, .pl3 = 2 }, // x = 12
//~            (Node){ .tp = nodVar, .pl1 = 0, .pl3 = assiVarAssignment },
//~            (Node){ .tp = tokInt,  .pl2 = 12 },
//~            (Node){ .tp = nodDef, .pl2 = 2, .pl3 = 2}, // second
//~            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = assiVarAssignment },
//~            (Node){ .tp = nodVar,        .pl2 = 0 }
//~         }),
//~         ((Int[]) {}),
//~         ((TestEntityImport[]) {}),
//~         ((SourceLoc[]) {
//~            { .startBt =  0, .lenBts = 11 },
//~            { .startBt =  4, .lenBts = 1 },
//~            { .startBt =  8, .lenBts = 2 },
//~            { .startBt =  12, .lenBts = 15 },
//~            { .startBt =  16, .lenBts = 6 },
//~            { .startBt = 25, .lenBts = 1 }
//~         })
//~      ),
//~      createTestWithError(
//~         s("Assignment shadowing error"),
//~         s(errCannotMutateImmutable),
//~         s("x = 12;\n"
//~           "x = 7;"
//~         ),
//~         ((Node[]) {
//~            (Node){ .tp = nodAssignment, .pl2 = 2, .pl3 = 2 },
//~            (Node){ .tp = nodVar, .pl2 = 0, .pl3 = assiVarAssignment },
//~            (Node){ .tp = tokInt,  .pl2 = 12 },
//~            (Node){ .tp = nodAssignment }
//~         }),
//~         ((Int[]) {}),
//~         ((TestEntityImport[]) {})
//~      ),
//~      createTest(
//~         s("Assignment with declared type"),
//~         s("fn main F(->) = f{->\n"
//~           "   x (A Str) = [`foo`];\n"
//~           "}"
//~         ),
//~         ((Node[]) {
//~            (Node){ .tp = nodFnDef,      .pl2 = 8 },
//~            (Node){ .tp = nodAssignment, .pl2 = 7, .pl3 = 2 },   // x$ = `foo`
//~            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment },
//~            (Node){ .tp = nodExpr, .pl1 = 1, .pl2 = 5 },
//~            (Node){ .tp = nodAssignment, .pl2 = 3, .pl3 = 2 },
//~            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = assiVarAssignment },
//~            (Node){ .tp = nodDataAlloc, .pl1 = 163, .pl2 = 1, .pl3 = 1 },
//~            (Node){ .tp = tokString },
//~            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = 0 }
//~         }),
//~         ((Int[]) {}),
//~         ((TestEntityImport[]) {})
//~      ),
//~      createTest(
//~         s("Reassignment"),
//~         s("fn main F(->) = f{->\n"
//~           "   x' = `foo`;\n"
//~           "   x = `bar`;\n"
//~           "}"
//~         ),
//~         ((Node[]) {
//~            (Node){ .tp = nodFnDef,         .pl2 = 6 },
//~            (Node){ .tp = nodAssignment, .pl2 = 2, .pl3 = 2 },   // x$ = `foo`
//~            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment },
//~            (Node){ .tp = tokString },
//~            (Node){ .tp = nodAssignment, .pl2 = 2, .pl3 = 2 }, // x = `bar`
//~            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiReassignment  },
//~            (Node){ .tp = tokString }
//~         }),
//~         ((Int[]) {}),
//~         ((TestEntityImport[]) {})
//~      ),
      createTest(
         s("Mutation simple"),
         s("fn main F() = f{ ->\n"
           "   x' = 12;\n"
           "   x += 55;\n"
           "}"
         ),
         ((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 9 },
            (Node){ .tp = nodAssignment,     .pl2 = 2, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = tokInt,          .pl2 = 12 },
            (Node){ .tp = nodAssignment,     .pl2 = 5, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiReassignment },
            (Node){ .tp = nodExpr,         .pl2 = 3 },
            (Node){ .tp = nodVar,   .pl1 = 0,  .pl2 = 0 },
            (Node){ .tp = tokInt,          .pl2 = 55 },
            (Node){ .tp = nodCall, .pl1 = oper(opPlus, tokInt), .pl2 = 2 }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
//~      createTest(
//~         s("Mutation complex"),
//~         s("fn main = f{->\n"
//~           "   a = [1 2 3];\n"
//~           "   a[1] *= (a[0] + a[2]);\n"
//~           "}"
//~         ),
//~         ((Node[]) {
//~            (Node){ .tp = nodFnDef,         .pl2 = 27 },
//~
//~            (Node){ .tp = nodAssignment,     .pl2 = 9, .pl3 = 2 }, // a = [1 2 3]
//~            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment },
//~            (Node){ .tp = nodExpr, .pl1 = 1,  .pl2 = 7,      },
//~            (Node){ .tp = nodAssignment,     .pl2 = 5, .pl3 = 2  },
//~            (Node){ .tp = nodVar,     .pl1 = 1, .pl2 = 0, .pl3 = assiVarAssignment },
//~            (Node){ .tp = nodDataAlloc, .pl1 = 163, .pl2 = 3, .pl3 = 3 },
//~            (Node){ .tp = tokInt,          .pl2 = 1 },
//~            (Node){ .tp = tokInt,          .pl2 = 2 },
//~            (Node){ .tp = tokInt,          .pl2 = 3 },
//~            (Node){ .tp = nodVar,   .pl1 = 1, .pl2 = 0 },
//~
//~            (Node){ .tp = nodAssignment,     .pl2 = 16, .pl3 = 5 }, // a[1] *= ...
//~
//~            (Node){ .tp = nodExpr,         .pl2 = 3 }, // a[1] on the left
//~            (Node){ .tp = nodVar,   .pl1 = 0,  .pl2 = 0 }, // a
//~            (Node){ .tp = tokInt,          .pl2 = 1 },
//~            (Node){ .tp = nodCall, .pl1 = opGetElem, .pl2 = 2, .pl3 = callGetElem },
//~
//~            (Node){ .tp = nodExpr,         .pl2 = 11 },
//~            (Node){ .tp = nodVar,   .pl1 = 0,  .pl2 = 0 }, // a[0] on the right
//~            (Node){ .tp = tokInt,          .pl2 = 1 },
//~            (Node){ .tp = nodCall,   .pl1 = opGetElem,  .pl2 = 2, .pl3 = callGetElem },
//~            (Node){ .tp = nodVar,   .pl1 = 0,  .pl2 = 0 }, // a[2]
//~            (Node){ .tp = tokInt,          .pl2 = 0 },
//~            (Node){ .tp = nodCall, .pl1 = opGetElem, .pl2 = 2, .pl3 = callGetElem },
//~            (Node){ .tp = nodVar,   .pl1 = 0,  .pl2 = 0 },
//~            (Node){ .tp = tokInt,          .pl2 = 2 },
//~            (Node){ .tp = nodCall, .pl1 = opGetElem, .pl2 = 2, .pl3 = callGetElem },
//~            (Node){ .tp = nodCall, .pl1 = oper(opPlus, tokInt), .pl2 = 2 },
//~            (Node){ .tp = nodCall, .pl1 = oper(opTimes, tokInt), .pl2 = 2 }
//~         }),
//~         ((Int[]) {}),
//~         ((TestEntityImport[]) {})
//~      ),
//~      createTest(
//~         s("Complex left side"),
//~         s("fn main = f{->\n"
//~           "arr = [1 2];\n"
//~           "arr[0] = 21;\n"
//~           "}"
//~          ),
//~         ((Node[]) {
//~            (Node){ .tp = nodFnDef,         .pl2 = 15 },
//~            (Node){ .tp = nodAssignment, .pl2 = 8, .pl3 = 2 },   // arr = [1 2]
//~            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment },
//~            (Node){ .tp = nodExpr, .pl1 = 1, .pl2 = 6 },
//~            (Node){ .tp = nodAssignment, .pl2 = 4, .pl3 = 2 },
//~            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = assiVarAssignment  },
//~            (Node){ .tp = nodDataAlloc, .pl1 = 163, .pl2 = 2, .pl3 = 2 },
//~            (Node){ .tp = tokInt, .pl2 = 1 },
//~            (Node){ .tp = tokInt, .pl2 = 2 },
//~            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0 },
//~
//~            (Node){ .tp = nodAssignment, .pl2 = 5, .pl3 = 5 }, // arr[0] = 21
//~            (Node){ .tp = nodExpr,       .pl2 = 3  },
//~            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0  },
//~            (Node){ .tp = tokInt, .pl2 = 0         },
//~            (Node){ .tp = nodCall, .pl1 = opGetElem, .pl2 = 2, .pl3 = callGetElem },
//~            (Node){ .tp = tokInt,        .pl2 = 21 }
//~         }),
//~         ((Int[]) {}),
//~         ((TestEntityImport[]) {})
//~      ),
//~      createTest(
//~         s("Very complex left side"),
//~         s("fn main = f{->\n"
//~           "arr = [[1 2] [4 3]];\n"
//~           "arr[1][0] = 21;\n"
//~           "}"
//~          ),
//~         ((Node[]) {
//~            (Node){ .tp = nodFnDef,         .pl2 = 27 },
//~            (Node){ .tp = nodAssignment, .pl2 = 18, .pl3 = 2 },   // arr = [1 2]
//~            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment },
//~            (Node){ .tp = nodExpr, .pl1 = 1, .pl2 = 16 },
//~
//~            (Node){ .tp = nodAssignment, .pl2 = 4, .pl3 = 2 },
//~            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = assiVarAssignment },
//~            (Node){ .tp = nodDataAlloc, .pl1 = 163, .pl2 = 2, .pl3 = 2 },
//~            (Node){ .tp = tokInt, .pl2 = 1 },
//~            (Node){ .tp = tokInt, .pl2 = 2 },
//~
//~            (Node){ .tp = nodAssignment, .pl2 = 4, .pl3 = 2 },
//~            (Node){ .tp = nodVar, .pl1 = 2, .pl2 = 0, .pl3 = assiVarAssignment },
//~            (Node){ .tp = nodDataAlloc, .pl1 = 163, .pl2 = 2, .pl3 = 2 },
//~            (Node){ .tp = tokInt, .pl2 = 4 },
//~            (Node){ .tp = tokInt, .pl2 = 3 },
//~
//~            (Node){ .tp = nodAssignment, .pl2 = 4, .pl3 = 2 },
//~            (Node){ .tp = nodVar, .pl1 = 3, .pl2 = 0, .pl3 = assiVarAssignment }, // temporary
//~            (Node){ .tp = nodDataAlloc, .pl1 = 171, .pl2 = 2, .pl3 = 2 },
//~            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0 },
//~            (Node){ .tp = nodVar, .pl1 = 2, .pl2 = 0 },
//~
//~            (Node){ .tp = nodVar, .pl1 = 3, .pl2 = 0 },
//~
//~            (Node){ .tp = nodAssignment, .pl2 = 7, .pl3 = 7 }, // arr[1][0] = 21
//~            (Node){ .tp = nodExpr,       .pl2 = 5  },
//~            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0  },
//~            (Node){ .tp = tokInt,    .pl2 = 1 },
//~            (Node){ .tp = nodCall, .pl1 = opGetElem, .pl2 = 2, .pl3 = callGetElem },
//~            (Node){ .tp = tokInt,    .pl2 = 0 },
//~            (Node){ .tp = nodCall, .pl1 = opGetElem, .pl2 = 2, .pl3 = callGetElem },
//~            (Node){ .tp = tokInt,        .pl2 = 21 }
//~         }),
//~         ((Int[]) {}),
//~         ((TestEntityImport[]) {})
//~      ),
//~      createTestWithError(
//~         s("Illegal left side in assignment"),
//~         s(errAssignmentLeftSide),
//~         s("fn main = f{->\n"
//~           "b' = 12;\n"
//~           "b + 1 = 10;\n"
//~           "}"
//~         ),
//~         ((Node[]) {
//~            (Node){ .tp = nodFnDef, .pl1 = 0, .pl3 = 0 },
//~
//~            (Node){ .tp = nodAssignment, .pl1 = 0, .pl2 = 4, .pl3 = 2 },
//~            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = assiVarAssignment  },
//~            (Node){ .tp = tokInt, .pl1 = 0, .pl2 = 12 },
//~            (Node){ .tp = nodAssignment }
//~         }),
//~         ((Int[]) {}),
//~         ((TestEntityImport[]) {})
//~      ), 
//~      createTest(
//~         s("Assignment to a function var from a function overload"),
//~         s("plus F(Double ->) = print;"),
//~         ((Node[]) {
//~            (Node){ .tp = nodDef,           .pl2 = 1 },
//~            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = I - 4, .pl3 = assiFnVarDef } // {importPrelude}
//~         }),
//~         ((Int[]) {}),
//~         ((TestEntityImport[]) {})
//~      )
   }));
}

//}}}
//{{{ Expression tests

ParserTestSet* expressionTests(Compiler* protoOvs, Arena* a) {
   return createTestSet(s("Expression test set"), a, ((ParserTest[]){
      createTestWithLocs(
         s("Simple function call"),
         s("x = foo 10 2 `hw`;"),
         (((Node[]) {
            (Node){ .tp = nodDef, .pl1 = 0, .pl2 = 6, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = nodExpr, .pl2 = 4 },
            (Node){ .tp = tokInt, .pl2 = 10,     },
            (Node){ .tp = tokInt, .pl2 = 2,      },
            (Node){ .tp = tokString,           },
            (Node){ .tp = nodCall, .pl1 = I - 1, .pl2 = 3 } // foo
         })),
         ((Int[]) { 4, tokInt, tokInt, tokString, tokDouble }),
         ((TestEntityImport[]) {{ .nameInd = 0, .typeInd = 0 }}),
         ((SourceLoc[]) {
            { .startBt = 0, .lenBts = 22 },
            { .startBt = 4, .lenBts = 1 },
            { .startBt = 6, .lenBts = 16 },
            { .startBt = 12, .lenBts = 2 },
            { .startBt = 15, .lenBts = 1 },
            { .startBt = 17, .lenBts = 4 },
            { .startBt = 8, .lenBts = 3 }
         })
      ),
      createTest(
         s("Data allocation"),
         s("x = [1 2 3];"),
         (((Node[]) {
            (Node){ .tp = nodDef, .pl1 = 0, .pl2 = 9, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = nodExpr, .pl1 = 1, .pl2 = 7 },

            (Node){ .tp = nodAssignment, .pl1 = 0, .pl2 = 5, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = nodDataAlloc, .pl1 = 163, .pl2 = 3,
                  .pl3 = 3 },
            (Node){ .tp = tokInt, .pl2 = 1 },
            (Node){ .tp = tokInt, .pl2 = 2 },
            (Node){ .tp = tokInt, .pl2 = 3 },
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0 } // the allocated array
         })),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTestWithError(
         s("Data allocation type error"),
         s(errListDifferentEltTypes),
         s("x = [1 true];"),
         (((Node[]) {
            (Node){ .tp = nodDef, .pl1 = 0, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = nodExpr, .pl1 = 0, .pl2 = 0 },

            (Node){ .tp = nodAssignment, .pl1 = 0, .pl2 = 4, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = nodDataAlloc, .pl1 = 163, .pl2 = 2,
                  .pl3 = 2 },
            (Node){ .tp = tokInt, .pl2 = 1 },
            (Node){ .tp = tokBool, .pl2 = 1 },
         })),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("Data allocation with expression inside"),
         s("x = [4 (2 * 7)];"),
         (((Node[]) {
            (Node){ .tp = nodDef, .pl1 = 0, .pl2 = 11, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = nodExpr, .pl1 = 1, .pl2 = 9 },

            (Node){ .tp = nodAssignment, .pl1 = 0, .pl2 = 7, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = nodDataAlloc, .pl1 = 163, .pl2 = 5, .pl3 = 2 },
            (Node){ .tp = tokInt, .pl2 = 4 },
            (Node){ .tp = nodExpr, .pl1 = 0, .pl2 = 3 },
            (Node){ .tp = tokInt, .pl2 = 2 },
            (Node){ .tp = tokInt, .pl2 = 7 },
            (Node){ .tp = nodCall, .pl1 = oper(opTimes, tokInt), .pl2 = 2 },

            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0 } // the allocated array
         })),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("Nested data allocation with expression inside"),
         s("x = [[1] [4 (2 - 7)] [2 3]];"),
         (((Node[]) {
            (Node){ .tp = nodDef, .pl1 = 0, .pl2 = 26, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = nodExpr, .pl1 = 1, .pl2 = 24 },

            (Node){ .tp = nodAssignment, .pl1 = 0, .pl2 = 3, .pl3 = 2 }, // [1]
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = nodDataAlloc, .pl1 = 163, .pl2 = 1, .pl3 = 1 },
            (Node){ .tp = tokInt, .pl2 = 1 },

            (Node){ .tp = nodAssignment, .pl1 = 0, .pl2 = 7, .pl3 = 2 }, // [2 (2 - 7)]
            (Node){ .tp = nodVar, .pl1 = 2, .pl2 = 0, .pl3 = assiVarAssignment  }, // [2 (2 - 7)]
            (Node){ .tp = nodDataAlloc, .pl1 = 163, .pl2 = 5, .pl3 = 2 },
            (Node){ .tp = tokInt, .pl2 = 4 },
            (Node){ .tp = nodExpr, .pl1 = 0, .pl2 = 3 },
            (Node){ .tp = tokInt, .pl2 = 2 },
            (Node){ .tp = tokInt, .pl2 = 7 },
            (Node){ .tp = nodCall, .pl1 = oper(opMinus, tokInt), .pl2 = 2 },

            (Node){ .tp = nodAssignment, .pl1 = 0, .pl2 = 4, .pl3 = 2 }, // [2 3]
            (Node){ .tp = nodVar, .pl1 = 3, .pl2 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = nodDataAlloc, .pl1 = 163, .pl2 = 2, .pl3 = 2 },
            (Node){ .tp = tokInt, .pl2 = 2 },
            (Node){ .tp = tokInt, .pl2 = 3 },

            (Node){ .tp = nodAssignment, .pl1 = 0, .pl2 = 5, .pl3 = 2 }, // [2 3]
            (Node){ .tp = nodVar, .pl1 = 4, .pl2 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = nodDataAlloc, .pl1 = 171, .pl2 = 3, .pl3 = 3 },
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0 },
            (Node){ .tp = nodVar, .pl1 = 2, .pl2 = 0 },
            (Node){ .tp = nodVar, .pl1 = 3, .pl2 = 0 },

            (Node){ .tp = nodVar, .pl1 = 4, .pl2 = 0 } // the allocated array
         })),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("Nested function call 1"),
         s("x = foo 10 (bar) 3;"),
         (((Node[]) {
            (Node){ .tp = nodDef, .pl2 = 6, .pl3 = 2},
            (Node){ .tp = nodVar, .pl1 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = nodExpr, .pl2 = 4},

            (Node){ .tp = tokInt, .pl2 = 10},
            (Node){ .tp = nodCall, .pl1 = I - 1, .pl2 = 0}, // bar
            (Node){ .tp = tokInt, .pl2 = 3},
            (Node){ .tp = nodCall, .pl1 = I - 2, .pl2 = 3}, // foo
         })),
         ((Int[]) {4, tokInt, tokDouble, tokInt, tokString, // Int Double Int -> String
                   2, voidType, tokDouble}), // () -> Double
         ((TestEntityImport[]) {(TestEntityImport){ .nameInd = 0, .typeInd = 0},
                        (TestEntityImport){ .nameInd = 1, .typeInd = 1}})
      ),
      createTest(
         s("Nested function call 2"),
         s("x = foo 10 (bar);"),
         (((Node[]) {
            (Node){ .tp = nodDef, .pl1 = 0, .pl2 = 5, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = nodExpr,         .pl2 = 3  },
            (Node){ .tp = tokInt,         .pl2 = 10 },
            (Node){ .tp = nodCall, .pl1 = I - 1, .pl2 = 0 }, // bar
            (Node){ .tp = nodCall, .pl1 = I - 2, .pl2 = 2 }  // foo
         })),
         ((Int[]) {3, tokInt, tokBool, tokBool,
                   2, voidType, tokBool }
         ),
         ((TestEntityImport[]) {(TestEntityImport){ .nameInd = 0, .typeInd = 0},
                        (TestEntityImport){ .nameInd = 1, .typeInd = 1}}
         )
      ),
      createTest(
         s("Nested function call 3"),
         s("x = foo #($(bar));"),
         (((Node[]) {
            (Node){ .tp = nodDef, .pl2 = 6, .pl3 = 2},
            (Node){ .tp = nodVar, .pl1 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = nodExpr, .pl2 = 4},

            (Node){ .tp = nodCall, .pl1 = I - 1, .pl2 = 0 }, // bar
            (Node){ .tp = nodCall, .pl1 = oper(opToString, tokDouble), .pl2 = 1 }, // $
            (Node){ .tp = nodCall, .pl1 = oper(opSize, tokString), .pl2 = 1}, // ##
            (Node){ .tp = nodCall, .pl1 = I - 2, .pl2 = 1} // foo
         })),
         ((Int[]) {2, tokInt, tokInt, // Int -> Int
                   2, voidType, tokDouble}),    // () -> Double
         ((TestEntityImport[]) {(TestEntityImport){ .nameInd = 0, .typeInd = 0},
                        (TestEntityImport){ .nameInd = 1, .typeInd = 1}})
      ),
      createTest(
         s("Triple function call"),
         s("x = foo (foo (bar 2 `hw`));"),
         (((Node[]) {
            (Node){ .tp = nodDef,    .pl2 = 7, .pl3 = 2 },
            (Node){ .tp = nodVar,      .pl1 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = nodExpr,         .pl2 = 5 },
            (Node){ .tp = tokInt,         .pl2 = 2 },
            (Node){ .tp = tokString,                 },
            (Node){ .tp = nodCall, .pl1 = I - 1, .pl2 = 2 }, // bar
            (Node){ .tp = nodCall, .pl1 = I - 2, .pl2 = 1 }, // foo
            (Node){ .tp = nodCall, .pl1 = I - 2, .pl2 = 1 }, // foo
         })),
         ((Int[]) {3, tokInt, tokString, tokString,
                   2, tokString, tokString}),
         ((TestEntityImport[]) {(TestEntityImport){ .nameInd = 0, .typeInd = 1},
                        (TestEntityImport){ .nameInd = 1, .typeInd = 0}})
      ),
      createTest(
         s("Operators simple"),
         s("x = 1 + 9 / 3;"),
         (((Node[]) {
            (Node){ .tp = nodDef, .pl1 = 0, .pl2 = 7, .pl3 = 2 },
            (Node){ .tp = nodVar,      .pl1 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = nodExpr,  .pl2 = 5 },
            (Node){ .tp = tokInt, .pl2 = 1 },
            (Node){ .tp = tokInt, .pl2 = 9 },
            (Node){ .tp = tokInt, .pl2 = 3 },
            (Node){ .tp = nodCall, .pl1 = oper(opDivBy, tokInt), .pl2 = 2 },
            (Node){ .tp = nodCall, .pl1 = oper(opPlus, tokInt), .pl2 = 2 }
         })),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("Unary operator precedence"),
         s("x = `12` + $ # -3;"),
         ((Node[]) {
            (Node){ .tp = nodDef, .pl2 = 7, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl2 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = nodExpr,           .pl2 = 5 },
            (Node){ .tp = tokString },
            (Node){ .tp = tokInt, .pl1 = -1,   .pl2 = -3 },
            (Node){ .tp = nodCall, .pl1 = oper(opSize, tokInt), .pl2 = 1 },
            (Node){ .tp = nodCall, .pl1 = oper(opToString, tokInt), .pl2 = 1 },
            (Node){ .tp = nodCall, .pl1 = oper(opPlus, tokString), .pl2 = 2 }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTestWithError(
         s("Operator arity error"),
         s(errTypeNoMatchingOverload),
         s("x = 1 + 20 100;"),
         (((Node[]) {
            (Node){ .tp = nodAssignment, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl3 = assiVarAssignment  },
            (Node){ .tp = nodExpr },
            (Node){ .tp = tokInt, .pl2 = 1 },
            (Node){ .tp = tokInt, .pl2 = 20 },
            (Node){ .tp = tokInt, .pl2 = 100 },
            (Node){ .tp = nodCall, .pl1 = opPlus + O, .pl2 = 3 }
         })),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("Array accessor"),
         s("def arr = [true false true];\n"
           "x = arr[1];"
          ),
         ((Node[]) {
            (Node){ .tp = nodDef, .pl2 = 9, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl2 = 0, .pl3 = assiVarAssignment  }, // arr
            (Node){ .tp = nodExpr,  .pl1 = 1, .pl2 = 7 },
            (Node){ .tp = nodAssignment,     .pl2 = 5, .pl3 = 2 },
            (Node){ .tp = nodVar,  .pl1 = 1, .pl2 = 0, .pl3 = assiVarAssignment }, // temp for arr
            (Node){ .tp = nodDataAlloc, .pl1 = 163, .pl2 = 3, .pl3 = 3 },
            (Node){ .tp = tokBool,         .pl2 = 1 },
            (Node){ .tp = tokBool,         .pl2 = 0 },
            (Node){ .tp = tokBool,         .pl2 = 1 },
            (Node){ .tp = nodVar,   .pl1 = 1, .pl2 = 0 },

            (Node){ .tp = nodDef, .pl2 = 5, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 2, .pl2 = 0, .pl3 = assiVarAssignment }, // x
            (Node){ .tp = nodExpr,           .pl2 = 3 },
            (Node){ .tp = nodVar,     .pl1 = 0, .pl2 = 0 }, // arr
            (Node){ .tp = tokInt,            .pl2 = 1 },
            (Node){ .tp = nodCall, .pl1 = opGetElem, .pl2 = 2, .pl3 = callGetElem }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("Field accessors"),
         s("arr = [true false true];\n"
           "x = arr.len + arr.len;"
          ),
         ((Node[]) {
            (Node){ .tp = nodDef,          .pl2 = 9, .pl3 = 2 },
            (Node){ .tp = nodVar,          .pl2 = 0, .pl3 = assiVarAssignment  }, // arr
            (Node){ .tp = nodExpr,  .pl1 = 1, .pl2 = 7 },
            (Node){ .tp = nodAssignment,     .pl2 = 5, .pl3 = 2 },
            (Node){ .tp = nodVar,  .pl1 = 1, .pl2 = 0, .pl3 = assiVarAssignment }, // temp for arr
            (Node){ .tp = nodDataAlloc, .pl1 = 163, .pl2 = 3, .pl3 = 3 },
            (Node){ .tp = tokBool,         .pl2 = 1 },
            (Node){ .tp = tokBool,         .pl2 = 0 },
            (Node){ .tp = tokBool,         .pl2 = 1 },
            (Node){ .tp = nodVar,  .pl1 = 1, .pl2 = 0 },

            (Node){ .tp = nodDef,           .pl2 = 7, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 2, .pl2 = 0, .pl3 = assiVarAssignment }, // x
            (Node){ .tp = nodExpr,           .pl2 = 5 },
            (Node){ .tp = nodVar,  .pl1 = 0, .pl2 = 0 }, // arr
            (Node){ .tp = nodCall, .pl1 = 163, .pl2 = 1, .pl3 = callField }, // 1 is the Array.len
            (Node){ .tp = nodVar,  .pl1 = 0, .pl2 = 0 }, // arr
            (Node){ .tp = nodCall, .pl1 = 163, .pl2 = 1, .pl3 = callField },
            (Node){ .tp = nodCall, .pl1 = oper(opPlus, tokInt), .pl2 = 2, .pl3 = callNormal }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      )
   }));
}

//}}}
//{{{ Function tests

ParserTestSet* functionTests(Compiler* protoOvs, Arena* a) {
   return createTestSet(s("Functions test set"), a, ((ParserTest[]){
      createTestWithLocs(
         s("Simple function definition 1"),
         s("fn newFn F(Int (L Bool) ->) = f{x y -> a = x;};"),
         ((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 5 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiFnParam  },  // param x
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = assiFnParam  },  // param y
            (Node){ .tp = nodAssignment, .pl2 = 2, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 2, .pl2 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 }   // x
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {}),
         ((SourceLoc[]) {
            (SourceLoc){ .startBt = 12, .lenBts = 28 },
            (SourceLoc){ .startBt = 14, .lenBts = 1 },
            (SourceLoc){ .startBt = 21, .lenBts = 1 },
            (SourceLoc){ .startBt = 33, .lenBts = 6 },
            (SourceLoc){ .startBt = 33, .lenBts = 1 },
            (SourceLoc){ .startBt = 37, .lenBts = 1 }
          })
      ),
      createTest(
         s("Simple function definition 2"),
         s("fn newFn F(Str Double -> Str) = f{x y ->\n"
           "   a = x;\n"
           "   return a;\n"
           "};"
         ),
         ((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 7 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiFnParam }, // param x
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = assiFnParam }, // param y
            (Node){ .tp = nodAssignment,      .pl2 = 2, .pl3 = 2  },
            (Node){ .tp = nodVar, .pl1 = 2, .pl2 = 0, .pl3 = assiVarAssignment }, // local a
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0  }, // x
            (Node){ .tp = nodReturn,        .pl2 = 1  },
            (Node){ .tp = nodVar, .pl1 = 2, .pl2 = 0  }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("Simple function definition 3"),
         s("fn main = f{->\n"
           "   print `asdf`;\n"
           "};"
         ),
         ((Node[]) {
            (Node){ .tp = nodFnDef,     .pl2 = 3 },
            (Node){ .tp = nodExpr,      .pl2 = 2 },
            (Node){ .tp = tokString    },
            (Node){ .tp = nodCall, .pl1 = I - 3, .pl2 = 1 }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTestWithError(
         s("Function definition wrong return type"),
         s(errTypeWrongReturnType),
         s("fn newFn F(Double Double -> Str) = f{x y ->\n"
           "   a = x;\n"
           "   return a;\n"
           "};"
         ),
         ((Node[]) {
            (Node){ .tp = nodFnDef                  },
            (Node){ .tp = nodVar, .pl1 = 1, .pl3 = assiVarAssignment }, // param x
            (Node){ .tp = nodVar, .pl1 = 2, .pl3 = assiVarAssignment }, // param y
            (Node){ .tp = nodAssignment,      .pl2 = 2, .pl3 = 2  },
            (Node){ .tp = nodVar, .pl1 = 3, .pl3 = assiVarAssignment }, // local a
            (Node){ .tp = nodVar, .pl1 = 1,    .pl2 = 1  }, // x
            (Node){ .tp = nodReturn,                },
            (Node){ .tp = nodVar, .pl1 = 3,    .pl2 = 3  }  // a
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("Function definition with complex return"),
         s("fn newFn F(Int Double -> Str) = f{x y ->\n"
           "   return $(foo x - y);};"
         ),
         ((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 9 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiFnParam },  // param x
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = assiFnParam },  // param y
            (Node){ .tp = nodReturn,        .pl2 = 6  },
            (Node){ .tp = nodExpr,          .pl2 = 5  },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 },   // x
            (Node){ .tp = nodCall, .pl1 = I - 1, .pl2 = 1 }, // foo
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0 },   // y
            (Node){ .tp = nodCall, .pl1 = oper(opMinus, tokDouble), .pl2 = 2 },
            (Node){ .tp = nodCall, .pl1 = oper(opToString, tokDouble), .pl2 = 1 },
         }),
         ((Int[]) { 2, tokInt, tokDouble }),
         ((TestEntityImport[]) {{ .nameInd = 0, .typeInd = 0 }})
      ),
      createTest(
         s("Mutually recursive function definitions"),
         s("fn func1 F(Int Double -> Int) = f{x y ->\n"
           "   a = x;\n"
           "   return func2 y a;\n"
           "};\n"
           "fn func2 F(Double Int -> Int) = f{x y ->\n"
           "   return func1 y x;\n"
           "};"
         ),
         ((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 10  }, // func1
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiFnParam }, // param x
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = assiFnParam },  // param y
            (Node){ .tp = nodAssignment,      .pl2 = 2, .pl3 = 2  },
            (Node){ .tp = nodVar, .pl1 = 2, .pl2 = 0, .pl3 = assiVarAssignment },  // local a
            (Node){ .tp = nodVar,    .pl1 = 0, .pl2 = 0  },  // x
            (Node){ .tp = nodReturn,         .pl2 = 4  },
            (Node){ .tp = nodExpr,          .pl2 = 3  },
            (Node){ .tp = nodVar, .pl1 = 1,    .pl2 = 0  }, // y
            (Node){ .tp = nodVar, .pl1 = 2,    .pl2 = 0  }, // a
            (Node){ .tp = nodCall, .pl1 = 1,   .pl2 = 2  }, // func2 call

            (Node){ .tp = nodFnDef,  .pl1 = 1, .pl2 = 7 }, // func2
            (Node){ .tp = nodVar, .pl1 = 3, .pl2 = 0, .pl3 = assiFnParam }, // param x
            (Node){ .tp = nodVar, .pl1 = 4, .pl2 = 0, .pl3 = assiFnParam }, // param y
            (Node){ .tp = nodReturn,         .pl2 = 4   },
            (Node){ .tp = nodExpr,          .pl2 = 3   },
            (Node){ .tp = nodVar, .pl1 = 4,    .pl2 = 0   }, // y
            (Node){ .tp = nodVar, .pl1 = 3,    .pl2 = 0   },  // x
            (Node){ .tp = nodCall, .pl1 = 0,  .pl2 = 2   } // func1 call
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("Function definition with nested scope"),
         s("fn main F(Int Double->) = f{x y ->\n"
           "   {\n"
           "      a = 5;\n"
           "   }\n"
           "   a = foo x - y;\n"
           "   print $a;\n"
           "};"
         ),
         ((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 17 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiFnParam }, // param x
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = assiFnParam }, // param y

            (Node){ .tp = nodScope,         .pl2 = 3 },
            (Node){ .tp = nodAssignment,    .pl2 = 2, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 2, .pl2 = 0, .pl3 = assiVarAssignment }, // first a =
            (Node){ .tp = tokInt,           .pl2 = 5 },

            (Node){ .tp = nodAssignment,      .pl2 = 6, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 3, .pl2 = 0, .pl3 = assiVarAssignment }, // snd a =
            (Node){ .tp = nodExpr,         .pl2 = 4 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 }, // x
            (Node){ .tp = nodCall, .pl1 = I - 1, .pl2 = 1 }, // foo
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0 }, // y
            (Node){ .tp = nodCall, .pl1 = oper(opMinus, tokDouble), .pl2 = 2 },

            (Node){ .tp = nodExpr,           .pl2 = 3 },
            (Node){ .tp = nodVar, .pl1 = 3,  .pl2 = 0 }, // a
            (Node){ .tp = nodCall, .pl1 = oper(opToString, tokDouble),  .pl2 = 1 }, // $
            (Node){ .tp = nodCall, .pl1 = I - 4, .pl2 = 1 } // print String
         }),
         ((Int[]) { 2, tokInt, tokDouble }), // Int -> Double
         ((TestEntityImport[]) {(TestEntityImport){ .nameInd = 0, .typeInd = 0 }})
      ),
      createTest(
         s("Local variable being called"),
         s("fn main F(F(Int -> Str) -> Str) = f{fun ->"
           "    return fun 5;"
           "}"
         ),
         (((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 5, .pl3 = 0 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiFnParam },
            (Node){ .tp = nodReturn,        .pl2 = 3 },
            (Node){ .tp = nodExpr,          .pl2 = 2 },
            (Node){ .tp = tokInt,           .pl2 = 5 },
            (Node){ .tp = nodCall, .pl1 = 0, .pl2 = 1, .pl3 = callVar }, // foo
         })),
         ((Int[]) { 2, tokInt, tokString }),
         ((TestEntityImport[]) {(TestEntityImport){ .nameInd = 0, .typeInd = 0},
                                })
      )
   }));
}

//}}}
//{{{ If tests

ParserTestSet* ifTests(Compiler* protoOvs, Arena* a) {
   return createTestSet(s("If test set"), a, ((ParserTest[]){
      createTestWithLocs(
         s("Simple if"),
         s("fn f = f{->\n"
           "   if 5 == 5 { print `5`; }\n"
           "};"
           ),
         ((Node[]) {
            (Node){ .tp = nodFnDef,   .pl2 = 9 },

            (Node){ .tp = nodIf,      .pl2 = 8 },
            (Node){ .tp = nodIfClause, .pl2 = 7, .pl3 = 0 },

            (Node){ .tp = nodExpr,    .pl2 = 3 },
            (Node){ .tp = tokInt,     .pl2 = 5 },
            (Node){ .tp = tokInt,     .pl2 = 5},
            (Node){ .tp = nodCall, .pl1 = oper(opEquality, tokInt), .pl2 = 2 }, // ==

            (Node){ .tp = nodExpr,    .pl2 = 2 },
            (Node){ .tp = tokString },
            (Node){ .tp = nodCall, .pl1 = I - 3, .pl2 = 1 } // print
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {}),
         ((SourceLoc[]) {
            (SourceLoc){ .startBt = 8, .lenBts = 33 },
            (SourceLoc){ .startBt = 15, .lenBts = 24 },
            (SourceLoc){ .startBt = 15, .lenBts = 24 },
            (SourceLoc){ .startBt = 18, .lenBts = 7 },
            (SourceLoc){ .startBt = 18, .lenBts = 1 },
            (SourceLoc){ .startBt = 23, .lenBts = 1 },
            (SourceLoc){ .startBt = 20, .lenBts = 2 },

            (SourceLoc){ .startBt = 27, .lenBts = 10 },
            (SourceLoc){ .startBt = 33, .lenBts = 3 },
            (SourceLoc){ .startBt = 27, .lenBts = 5 }

          })
      ),
      createTest(
         s("If with else"),
         s("fn f F(->Str) = f{->\n"
           "   if 5 > 3 { `5`; } else { `=)`; }\n"
           "}"
           ),
         ((Node[]) {
            (Node){ .tp = nodFnDef, .pl2 = 9 },

            (Node){ .tp = nodIf, .pl2 = 8, .pl3 = 0 },

            (Node){ .tp = nodIfClause, .pl2 = 5 },
            (Node){ .tp = nodExpr, .pl2 = 3 },
            (Node){ .tp = tokInt, .pl2 = 5 },
            (Node){ .tp = tokInt, .pl2 = 3 },
            (Node){ .tp = nodCall, .pl1 = oper(opGreaterTh, tokInt), .pl2 = 2 },
            (Node){ .tp = tokString },

            (Node){ .tp = nodIfClause,  .pl2 = 1, .pl3 = 2 },
            (Node){ .tp = tokString },
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("If with elseif"),
         s("fn f = f{->\n"
           "   if 5 > 3 { 11; }\n"
           "   eif 5 == 3 { 4; }\n"
           "}"
           ),
         ((Node[]) {
            (Node){ .tp = nodFnDef,    .pl2 = 13 },

            (Node){ .tp = nodIf, .pl2 = 12, .pl3 = 0 },

            (Node){ .tp = nodIfClause, .pl2 = 5, .pl3 = 0 },
            (Node){ .tp = nodExpr,     .pl2 = 3 },
            (Node){ .tp = tokInt,      .pl2 = 5 },
            (Node){ .tp = tokInt,      .pl2 = 3 },
            (Node){ .tp = nodCall, .pl1 = oper(opGreaterTh, tokInt), .pl2 = 2 },
            (Node){ .tp = tokInt,      .pl2 = 11 },

            (Node){ .tp = nodIfClause, .pl2 = 5, .pl3 = 1 },
            (Node){ .tp = nodExpr,     .pl2 = 3 },
            (Node){ .tp = tokInt,      .pl2 = 5 },
            (Node){ .tp = tokInt,      .pl2 = 3 },
            (Node){ .tp = nodCall, .pl1 = oper(opEquality, tokInt), .pl2 = 2 },
            (Node){ .tp = tokInt,      .pl2 = 4 }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("If with elseif and else"),
         s("fn f = f{->\n"
           "   if 5 > 3 { print `11`; } \n"
           "   eif 5 == 3 { print `4`; } \n"
           "   else { print `100`; }\n"
           "};"
           ),
         ((Node[]) {
            (Node){ .tp = nodFnDef,    .pl2 = 21 },

            (Node){ .tp = nodIf, .pl2 = 20, .pl3 = 0 },

            (Node){ .tp = nodIfClause, .pl2 = 7, .pl3 = 0 },
            (Node){ .tp = nodExpr,     .pl2 = 3},
            (Node){ .tp = tokInt,      .pl2 = 5 },
            (Node){ .tp = tokInt,      .pl2 = 3 },
            (Node){ .tp = nodCall, .pl1 = oper(opGreaterTh, tokInt), .pl2 = 2 },
            (Node){ .tp = nodExpr,   .pl2 = 2      },
            (Node){ .tp = tokString,   },
            (Node){ .tp = nodCall, .pl1 = I - 3, .pl2 = 1 },

            (Node){ .tp = nodIfClause,     .pl2 = 7, .pl3 = 1 },
            (Node){ .tp = nodExpr,     .pl2 = 3},
            (Node){ .tp = tokInt,      .pl2 = 5 },
            (Node){ .tp = tokInt,      .pl2 = 3 },
            (Node){ .tp = nodCall, .pl1 = oper(opEquality, tokInt), .pl2 = 2 },
            (Node){ .tp = nodExpr,  .pl2 = 2      },
            (Node){ .tp = tokString,  },
            (Node){ .tp = nodCall, .pl1 = I - 3, .pl2 = 1 },

            (Node){ .tp = nodIfClause,  .pl2 = 3, .pl3 = 2 },
            (Node){ .tp = nodExpr,  .pl2 = 2      },
            (Node){ .tp = tokString },
            (Node){ .tp = nodCall, .pl1 = I - 3, .pl2 = 1 }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTestWithError(
         s("If error: must be bool"),
         s(errTypeMustBeBool),
         s("fn f = f{->\n"
           "   if 5 + 5 { print `5`; }\n"
           "};"
           ),
         ((Node[]) {
            (Node){ .tp = nodFnDef },

            (Node){ .tp = nodIf },
            (Node){ .tp = nodIfClause },
            (Node){ .tp = nodExpr },
            (Node){ .tp = tokInt, .pl2 = 5 },
            (Node){ .tp = tokInt, .pl2 = 5 },
            (Node){ .tp = nodCall, .pl1 = oper(opPlus, tokInt), .pl2 = 2 }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      )
   }));
}

//}}}
//{{{ Loop tests

ParserTestSet* forTests(Compiler* protoOvs, Arena* a) {
   return createTestSet(s("For loop test set"), a, ((ParserTest[]){
      createTest(
         s("Simple loop"),
         s("fn f = f{-> for {x' = 1; x < 101; x += 1;} { print $x; } };"),
         ((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 19, .pl3 = 0 },
            (Node){ .tp = nodFor, .pl1 = 4, .pl2 = 18, .pl3 = 13 },

            (Node){ .tp = nodAssignment, .pl1 = 0, .pl2 = 2, .pl3 = 2 }, // x' = 1
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = tokInt,           .pl2 = 1 },
            (Node){ .tp = nodExpr,          .pl2 = 3 }, // x < 101
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 },
            (Node){ .tp = tokInt,           .pl2 = 101 },
            (Node){ .tp = nodCall, .pl1 = oper(opLessTh, tokInt), .pl2 = 2 },

            (Node){ .tp = nodScope,        .pl2 = 10 },
            (Node){ .tp = nodExpr,         .pl2 = 3 }, // print $x
            (Node){ .tp = nodVar,  .pl1 = 0, .pl2 = 0 },     // x
            (Node){ .tp = nodCall, .pl1 = oper(opToString, tokInt), .pl2 = 1 }, // $
            (Node){ .tp = nodCall, .pl1 = I - 3, .pl2 = 1 }, // print
            (Node){ .tp = nodAssignment, .pl1 = 0, .pl2 = 5, .pl3 = 2 }, // x = x + 1
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiReassignment },
            (Node){ .tp = nodExpr, .pl2 = 3 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 },
            (Node){ .tp = tokInt, .pl1 = 0, .pl2 = 1 },
            (Node){ .tp = nodCall, .pl1 = oper(opPlus, tokInt), .pl2 = 2 }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("For with two complex initializers"),
         s("fn f = f{->\n"
           "   for {x' = 17; y' = x / 5; y < 101; x--; y++;}{\n"
           "      print $x;}\n"
           "}"
           ),
         ((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 25 },
            (Node){ .tp = nodFor, .pl1 = 10, .pl2 = 24, .pl3 = 19 },

            (Node){ .tp = nodAssignment,         .pl2 = 2, .pl3 = 2 }, // x' = 17
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = tokInt,               .pl2 = 17 },

            (Node){ .tp = nodAssignment, .pl2 = 5, .pl3 = 2 }, // y' = x / 5
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = nodExpr,              .pl2 = 3 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 }, // x'
            (Node){ .tp = tokInt,               .pl2 = 5 },
            (Node){ .tp = nodCall, .pl1 = oper(opDivBy, tokInt), .pl2 = 2 },

            (Node){ .tp = nodExpr, .pl2 = 3,              }, // y < 101
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0 },
            (Node){ .tp = tokInt,           .pl2 = 101 },
            (Node){ .tp = nodCall, .pl1 = oper(opLessTh, tokInt), .pl2 = 2 },

            (Node){ .tp = nodScope,             .pl2 = 10 },
            (Node){ .tp = nodExpr,              .pl2 = 3 }, // print $x
            (Node){ .tp = nodVar,   .pl1 = 0,      .pl2 = 0 }, // x
            (Node){ .tp = nodCall, .pl1 = oper(opToString, tokInt), .pl2 = 1 }, // $
            (Node){ .tp = nodCall, .pl1 = I - 3,      .pl2 = 1 }, // print

            (Node){ .tp = nodExpr,         .pl2 = 2, .pl3 = 0}, // x--
            (Node){ .tp = nodVar, .pl1 = 0,     .pl2 = 0 },
            (Node){ .tp = nodCall, .pl1 = oper(opDecrement, tokInt), .pl2 = 1 },

            (Node){ .tp = nodExpr,           .pl2 = 2, .pl3 = 0}, // y++
            (Node){ .tp = nodVar,  .pl1 = 1, .pl2 = 0 },
            (Node){ .tp = nodCall, .pl1 = oper(opIncrement, tokInt), .pl2 = 1 }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("For without initializers"),
         s("fn f = f{->\n"
           "   x = 4;\n"
           "   for {x < 101;}{ \n"
           "      print $x; } };"),
         ((Node[]) {
            (Node){ .tp = nodFnDef,       .pl2 = 13 },

            (Node){ .tp = nodAssignment, .pl2 = 2, .pl3 = 2 }, // x = 4
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = tokInt,           .pl2 = 4 },

            (Node){ .tp = nodFor, .pl1 = 1, .pl2 = 9, .pl3 = 10 },

            (Node){ .tp = nodExpr, .pl2 = 3         }, // < x 101
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 },
            (Node){ .tp = tokInt,        .pl2 = 101 },
            (Node){ .tp = nodCall, .pl1 = oper(opLessTh, tokInt), .pl2 = 2 },

            (Node){ .tp = nodScope, .pl2 = 4,        }, // print $x
            (Node){ .tp = nodExpr,       .pl2 = 3 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 },
            (Node){ .tp = nodCall, .pl1 = oper(opToString, tokInt), .pl2 = 1 },
            (Node){ .tp = nodCall, .pl1 = I - 3, .pl2 = 1 }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("For loop without body"),
         s("fn f = f{-> for {x' = 1; x < 101; x += 1;} {} }"),
         ((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 15, .pl3 = 0 },
            (Node){ .tp = nodFor, .pl1 = 4, .pl2 = 14, .pl3 = 9 },

            (Node){ .tp = nodAssignment, .pl1 = 0, .pl2 = 2, .pl3 = 2 }, // x$ = 1
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = tokInt,        .pl2 = 1 },

            (Node){ .tp = nodExpr, .pl2 = 3 }, // < x 101
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 },
            (Node){ .tp = tokInt,        .pl2 = 101 },
            (Node){ .tp = nodCall, .pl1 = oper(opLessTh, tokInt), .pl2 = 2 },

            (Node){ .tp = nodScope,           .pl2 = 6},
            (Node){ .tp = nodAssignment, .pl1 = 0, .pl2 = 5, .pl3 = 2 }, // x += 1
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiReassignment },
            (Node){ .tp = nodExpr, .pl2 = 3 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 },
            (Node){ .tp = tokInt, .pl1 = 0, .pl2 = 1 },
            (Node){ .tp = nodCall, .pl1 = oper(opPlus, tokInt), .pl2 = 2 }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("For loop with no step"),
         s("fn f = f{-> for {x' = 1; x < 101; } { print $x; } }"),
         ((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 13, .pl3 = 0 },
            (Node){ .tp = nodFor, .pl1 = 4, .pl2 = 12, .pl3 = 13 },

            (Node){ .tp = nodAssignment, .pl1 = 0, .pl2 = 2, .pl3 = 2 }, // x$ = 1
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = tokInt,        .pl2 = 1 },

            (Node){ .tp = nodExpr, .pl2 = 3 }, // x < 101
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 },
            (Node){ .tp = tokInt,        .pl2 = 101 },
            (Node){ .tp = nodCall, .pl1 = oper(opLessTh, tokInt), .pl2 = 2 },

            (Node){ .tp = nodScope,        .pl2 = 4 },
            (Node){ .tp = nodExpr,         .pl2 = 3 }, // print $x
            (Node){ .tp = nodVar,   .pl1 = 0, .pl2 = 0 },     // x
            (Node){ .tp = nodCall, .pl1 = oper(opToString, tokInt), .pl2 = 1 }, // $
            (Node){ .tp = nodCall, .pl1 = I - 3, .pl2 = 1 }, // print string
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("For with no initializers nor step"),
         s("fn f = f{-> x = 0;\n"
           " for { x < 101;}{ print $x; } }"),
         ((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 13, .pl3 = 0 },

            (Node){ .tp = nodAssignment, .pl2 = 2, .pl3 = 2 }, // x = 0
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = tokInt,           .pl2 = 0 },

            (Node){ .tp = nodFor, .pl1 = 1, .pl2 = 9, .pl3 = 10 },
            (Node){ .tp = nodExpr, .pl2 = 3 }, // < x 101
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 },
            (Node){ .tp = tokInt,        .pl2 = 101 },
            (Node){ .tp = nodCall, .pl1 = oper(opLessTh, tokInt), .pl2 = 2 },

            (Node){ .tp = nodScope,        .pl2 = 4 },
            (Node){ .tp = nodExpr,         .pl2 = 3 }, // print $x
            (Node){ .tp = nodVar,   .pl1 = 0, .pl2 = 0 },     // x
            (Node){ .tp = nodCall, .pl1 = oper(opToString, tokInt), .pl2 = 1 }, // $
            (Node){ .tp = nodCall, .pl1 = I - 3, .pl2 = 1 }, // print
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("For loop with no initalizers nor body"),
         s("fn f = f{-> x' = 7;\n"
           " for {x < 101; x += 1;}{} }"),
         ((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 15, .pl3 = 0 },
            (Node){ .tp = nodAssignment, .pl2 = 2, .pl3 = 2 }, // x = 0
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment  },
            (Node){ .tp = tokInt,           .pl2 = 7 },

            (Node){ .tp = nodFor,  .pl1 = 1, .pl2 = 11, .pl3 = 6 },
            (Node){ .tp = nodExpr, .pl2 = 3 }, // x < 101
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 },
            (Node){ .tp = tokInt,        .pl2 = 101 },
            (Node){ .tp = nodCall, .pl1 = oper(opLessTh, tokInt), .pl2 = 2 },

            (Node){ .tp = nodScope,                .pl2 = 6 },
            (Node){ .tp = nodAssignment, .pl1 = 0, .pl2 = 5, .pl3 = 2 }, // x += 1
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiReassignment  },
            (Node){ .tp = nodExpr, .pl2 = 3 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 },
            (Node){ .tp = tokInt, .pl1 = 0, .pl2 = 1 },
            (Node){ .tp = nodCall,   .pl1 = oper(opPlus, tokInt), .pl2 = 2 }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("For loop with single-token condition"),
         s("fn f = f{-> x' = true;\n"
           " for {x;} {x = not x;} }"),
         ((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 11, .pl3 = 0 },
            (Node){ .tp = nodAssignment, .pl1 = 0, .pl2 = 2, .pl3 = 2 }, // x$ = 1
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = tokBool,        .pl2 = 1 },

            (Node){ .tp = nodFor, .pl1 = 1, .pl2 = 7, .pl3 = 8 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 },

            (Node){ .tp = nodScope,        .pl2 = 5 },
            (Node){ .tp = nodAssignment, .pl1 = 0, .pl2 = 4, .pl3 = 2 }, // x = x + 1
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiReassignment },
            (Node){ .tp = nodExpr, .pl2 = 2 },
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 },
            (Node){ .tp = nodCall,   .pl1 = oper(opBoolNot, tokBool), .pl2 = 1 }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTestWithError(
         s("For loop error: neither step nor body"),
         s(errLoopEmptyStepBody),
         s("fn f = f{-> for {x$ = 1; x$ < 101;} {} }"),
         ((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 0, .pl3 = 0 },
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTestWithError(
         s("For loop error: no condition"),
         s(errLoopNoCondition),
         s("def f = {{} for {x$ = 1; x$ = x$ + 1;}{ $x$ .print; } }"),
         ((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 0, .pl3 = 0 },
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("For with break and continue"),
         s("fn f = f{->\n"
           "   for {x = 0; x < 301;} {\n"
           "      break;\n"
           "      continue;}\n"
           "}"
           ),
         ((Node[]) {
            (Node){ .tp = nodFnDef,          .pl2 = 11 },
            (Node){ .tp = nodFor, .pl1 = 4 + BIG,  .pl2 = 10, .pl3 = 11 },

            (Node){ .tp = nodAssignment, .pl2 = 2, .pl3 = 2 }, // x = 0
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = tokInt, .pl2 = 0 },

            (Node){ .tp = nodExpr, .pl2 = 3 }, // x < 301
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 },
            (Node){ .tp = tokInt,      .pl2 = 301 },
            (Node){ .tp = nodCall, .pl1 = oper(opLessTh, tokInt), .pl2 = 2 },

            (Node){ .tp = nodScope, .pl2 = 2 },
            (Node){ .tp = nodBreakCont, .pl1 = 1 },
            (Node){ .tp = nodBreakCont, .pl1 = 1,        .pl3 = 1 }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTestWithError(
         s("For with break error"),
         s(errBreakContinueInvalidDepth),
         s("fn f = f{->\n"
           "   for {x = 0; x < 101;}{\n"
           "      break 2;\n"
           "} }"
           ),
         ((Node[]) {
            (Node){ .tp = nodFnDef            },
            (Node){ .tp = nodFor, .pl1 = 4    },

            (Node){ .tp = nodAssignment,     .pl2 = 2, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 1, .pl3 = assiVarAssignment }, // x
            (Node){ .tp = tokInt,          .pl2 = 0 },

            (Node){ .tp = nodExpr,         .pl2 = 3 },
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 1 }, // x
            (Node){ .tp = tokInt,        .pl2 = 101 },
            (Node){ .tp = nodCall, .pl1 = oper(opLessTh, tokInt), .pl2 = 2 },
            (Node){ .tp = nodScope }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTest(
         s("Nested for with deep break and continue"),
         s("fn f = f{->\n"
           "   for {a = 0; a < 101;}{\n"
           "      for {b = 0; b < 201;}{\n"
           "         for {c = 0; c < 301;}{\n"
           "            break 3;}\n"
           "      }\n"
           "      for {d = 0; d < 51;}{\n"
           "         for {e = 0; e < 401;}{\n"
           "            continue 2;}\n"
           "      }\n"
           "      print $a;\n"
           "   }\n"
           "}"
           ),
         ((Node[]) {
            (Node){ .tp = nodFnDef,         .pl2 = 51 },

            (Node){ .tp = nodFor,  .pl1 = 4, .pl2 = 50, .pl3 = 51 }, // "for" #1. It's being
                                                                    // "broken" from
            (Node){ .tp = nodAssignment, .pl2 = 2, .pl3 = 2}, // a = 0
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = tokInt, .pl2 = 0 },

            (Node){ .tp = nodExpr, .pl2 = 3 }, // a < 101
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 },
            (Node){ .tp = tokInt, .pl2 = 101 },
            (Node){ .tp = nodCall, .pl1 = oper(opLessTh, tokInt), .pl2 = 2 },

            (Node){ .tp = nodScope,        .pl2 = 42 },
            (Node){ .tp = nodFor, .pl1 = 4, .pl2 = 18, .pl3 = 19 }, // "for" #2

            (Node){ .tp = nodAssignment, .pl2 = 2, .pl3 = 2 }, // b = 0
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = tokInt, .pl2 = 0 },

            (Node){ .tp = nodExpr,                .pl2 = 3 }, // b < 201
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 0 },
            (Node){ .tp = tokInt, .pl2 = 201 },
            (Node){ .tp = nodCall, .pl1 = oper(opLessTh, tokInt), .pl2 = 2 },

            (Node){ .tp = nodScope,        .pl2 = 10 },
            (Node){ .tp = nodFor, .pl1 = 4, .pl2 = 9, .pl3 = 10 }, // "for" #3, double-nested

            (Node){ .tp = nodAssignment, .pl2 = 2, .pl3 = 2 }, // c =
            (Node){ .tp = nodVar, .pl1 = 2, .pl2 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = tokInt, .pl2 = 0 },

            (Node){ .tp = nodExpr,       .pl2 = 3 },
            (Node){ .tp = nodVar, .pl1 = 2, .pl2 = 0 }, // c
            (Node){ .tp = tokInt,        .pl2 = 301 },
            (Node){ .tp = nodCall, .pl1 = oper(opLessTh, tokInt), .pl2 = 2 },

            (Node){ .tp = nodScope,       .pl2 = 1 },
            (Node){ .tp = nodBreakCont, .pl1 = 3 },

            (Node){ .tp = nodFor, .pl1 = 4 + BIG, .pl2 = 18, .pl3 = 19 }, // "for" #4. It's "continued"

            (Node){ .tp = nodAssignment,   .pl2 = 2, .pl3 = 2 }, // d = 0
            (Node){ .tp = nodVar, .pl1 = 3, .pl2 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = tokInt,         .pl2 = 0 },

            (Node){ .tp = nodExpr,        .pl2 = 3 }, // d < 51
            (Node){ .tp = nodVar, .pl1 = 3, .pl2 = 0 },
            (Node){ .tp = tokInt, .pl2 = 51 },
            (Node){ .tp = nodCall, .pl1 = oper(opLessTh, tokInt), .pl2 = 2 },

            (Node){ .tp = nodScope,      .pl2 = 10 },
            (Node){ .tp = nodFor, .pl1 = 4, .pl2 = 9, .pl3 = 10 }, // "for" #5, the last one

            (Node){ .tp = nodAssignment, .pl2 = 2, .pl3 = 2 }, // e = 0
            (Node){ .tp = nodVar, .pl1 = 4, .pl2 = 0, .pl3 = assiVarAssignment },
            (Node){ .tp = tokInt, .pl2 = 0 },

            (Node){ .tp = nodExpr,       .pl2 = 3 }, // e < 401
            (Node){ .tp = nodVar, .pl1 = 4, .pl2 = 0 },
            (Node){ .tp = tokInt,        .pl2 = 401 },
            (Node){ .tp = nodCall, .pl1 = oper(opLessTh, tokInt), .pl2 = 2 },

            (Node){ .tp = nodScope,               .pl2 = 1 },
            (Node){ .tp = nodBreakCont, .pl1 = 2,         .pl3 = 1 }, // continue

            (Node){ .tp = nodExpr,                .pl2 = 3 }, // print $a
            (Node){ .tp = nodVar, .pl1 = 0, .pl2 = 0 },  // a
            (Node){ .tp = nodCall, .pl1 = oper(opToString, tokInt), .pl2 = 1 },
            (Node){ .tp = nodCall, .pl1 = I - 3, .pl2 = 1 } // print
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      ),
      createTestWithError(
         s("For with type error"),
         s(errTypeMustBeBool),
         s("fn f = f{-> for {x' = 1; x / 101;}{ print x; } }"),
         ((Node[]) {
            (Node){ .tp = nodFnDef },
            (Node){ .tp = nodFor },

            (Node){ .tp = nodAssignment,     .pl2 = 2, .pl3 = 2 },
            (Node){ .tp = nodVar, .pl1 = 1, .pl3 = assiVarAssignment }, // x
            (Node){ .tp = tokInt,          .pl2 = 1 },

            (Node){ .tp = nodExpr,         .pl2 = 3 },
            (Node){ .tp = nodVar, .pl1 = 1, .pl2 = 1 }, // x
            (Node){ .tp = tokInt,        .pl2 = 101 },
            (Node){ .tp = nodCall, .pl1 = oper(opDivBy, tokInt), .pl2 = 2 }
         }),
         ((Int[]) {}),
         ((TestEntityImport[]) {})
      )
   }));
}

//}}}

void runATestSet(ParserTestSet* (*testGenerator)(Compiler*, Arena*),
      TestContext* ct, Compiler* protoOvs
) {
   ParserTestSet* testSet = (testGenerator)(protoOvs, ct->a);
   for (Int j = 0; j < testSet->totalTests; j++) {
      ParserTest test = testSet->tests[j];
      runTest(test, ct);
   }
}

int
main() {
   printf("----------------------------\n");
   printf("--  PARSER TEST  --\n");
   printf("----------------------------\n");
   TestContext ct = {.countTests = 0, .countPassed = 0, .a = createArena() };

   // An empty compiler that we need for the built-in overloads
   Compiler* protoOvs = createLexer(empty, true, ct.a);
   initializeParser(protoOvs, ct.a);
   createOverloads(protoOvs);

   runATestSet(&assignmentTests, &ct, protoOvs);
//~   runATestSet(&expressionTests, &ct, protoOvs);
//~   runATestSet(&functionTests, &ct, protoOvs);
//~   runATestSet(&ifTests, &ct, protoOvs);
//~   runATestSet(&forTests, &ct, protoOvs);

   if (ct.countTests == 0) {
      printf("\nThere were no tests to run!\n");
   } else if (ct.countPassed == ct.countTests) {
      if (ct.countTests > 1) {
         printf("\nPassed all %d tests!\n", ct.countTests);
      } else {
         printf("\nThe test was passed.\n");
      }

   } else {
      printf("\nFailed %d tests out of %d!\n", (ct.countTests - ct.countPassed), ct.countTests);
   }

   deleteArena(ct.a);
}
