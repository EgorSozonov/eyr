#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <setjmp.h>
#include "../include/libeyr.h"
#include "codegenTest.h"

//{{{ Definitions

typedef struct Codegen Codegen;

DEFINE_LIST(Ulong)

typedef struct { //:CodegenTest
   String name;
   LUlong* test;
   LUlong* control;
} CodegenTest;

typedef struct { //:CodegenTestSet
    String name;
    Int totalTests;
    Arr(CodegenTest) tests;
} CodegenTestSet;

//}}}
//{{{ Utils

#define S   70000000 // A constant larger than the largest allowed file size. Separates parsed
                     // names from others

//~#define add(A, X) _Generic((X),\
//~   LBtToken*: addBtToken,\
//~   LParseFrame*: addParseFrame,\
//~   LExprFrame*: addExprFrame,\
//~   LTypeFrame*: addTypeFrame,\
//~   LMonomorphization*: addMonomorphization,\
//~   LTypeLoc*: addTypeLoc,\
//~   LInt*: addInt,\
//~   LUnt*: addUnt,\
//~   LUlong*: addUlong,\
//~   LNode*: addNode,\
//~   LSourceLoc*: addSourceLoc,\
//~   LBtInstr*: addBtInstr\
//~)(A, X)
//~
//~#define removeLast(X) _Generic((X),\
//~   LBtToken*: removeLastBtToken,\
//~   LParseFrame*: removeLastParseFrame,\
//~   LExprFrame*: removeLastExprFrame,\
//~   LTypeFrame*: removeLastTypeFrame,\
//~   LInt*: removeLastInt,\
//~   LUnt*: removeLastUnt,\
//~   LUlong: removeLastUlong,\
//~   LNode*: removeLastNode,\
//~   LSourceLoc*: removeLastSourceLoc,\
//~   LBtInstr*: removeLastBtInstr\
//~)(X)



private CodegenTestSet* createTestSet0(String name, Arena *a, int count, Arr(CodegenTest) tests) {
    CodegenTestSet* result = allocateOnArena(sizeof(CodegenTestSet), a);
    result->name = name;
    result->totalTests = count;
    result->tests = allocateOnArena(count*sizeof(CodegenTest), a);
    if (result->tests == NULL) return result;
    for (int i = 0; i < count; i++) {
        result->tests[i] = tests[i];
    }
    return result;
}

#define createTestSet(n, a, tests) createTestSet0(n, a, sizeof(tests)/sizeof(CodegenTest), tests)

typedef struct Codegen Codegen;

private CodegenTest
createTest0(String name, String sourceCode, Arr(Ulong) instrs, Int countInstrs, Arena* a) {
   Compiler* cm = lexicallyAnalyze(sourceCode, a);

   initializeParser(cm, a);
   updateStats(cm);
   LUlong* test =  generateBytecode(sourceCode, a);
   LUlong* control = createLUlong(countInstrs, a);
   memcpy(control->c, instrs, countInstrs*8);
   
   //importTestFns(types, countTypes, imports, countImports, a, OUT test);
   return (CodegenTest){ .name = name, .test = test, .control = control };
}

//:createTest
#define createTest(name, sourceCode, instrs) \
   createTest0((name), (sourceCode), (instrs), sizeof(instrs)/sizeof(Ulong), a)
   
Int
compare(CodegenTest test) {
   return 0;
}


void runCodegenTest(CodegenTest test, TestContext* ct) {
// Runs a single lexer test and prints err msg to stdout in case of failure. Returns error code
    ct->countTests += 1;
    Int cmpRes = compare(test);
    if (cmpRes == -1) {
        ct->countPassed += 1;
    } else {
        print("------------- -----")
        printf("\n\nERROR IN [");
        printStringNoLn(test.name);
        printf("]\n @ %d: \n", cmpRes);
        print("------------- -----")
    }
}


void runATestSet(CodegenTestSet* (*testGenerator)(Arena*), TestContext* ct) {
    CodegenTestSet* testSet = (testGenerator)(ct->a);
    for (int j = 0; j < testSet->totalTests; j++) {
        CodegenTest test = testSet->tests[j];
        runCodegenTest(test, ct);
    }
}

//}}}
//{{{ Expressions

CodegenTestSet* exprTests(Arena* a) {
   return createTestSet(s("Expression test set"), a, ((CodegenTest[]){
      createTest(
         s("Simple function"),
         s(
            "def main = {{}\n"
            "   x = 5;\n"
            "   y = 10;\n"
            "   a = x + y;\n"
            "};"
         ),
         (Ulong[]){
            1,
            2,
            3
         }
      )
   });
}

//}}}

int main(int argc, char** argv) {
    printf("----------------------------\n");
    printf("    --  CODEGEN TEST  --\n");
    printf("----------------------------\n");

    TestContext ct = (TestContext){ .countTests = 0, .countPassed = 0, .a = createArena() };

    runATestSet(&exprTests, &ct);

    if (ct.countTests == 0) {
        print("\nThere were no tests to run!");
    } else if (ct.countPassed == ct.countTests) {
        print("\nAll %d tests passed!", ct.countTests);
    } else {
        print("\nFailed %d tests out of %d!", (ct.countTests - ct.countPassed), ct.countTests);
    }

    deleteArena(ct.a);
}
