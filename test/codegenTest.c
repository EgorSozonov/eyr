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

DEFINE_LIST_HEADER(Ulong)
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
createTest0(String name, String sourceCode, Arr(Unt) instrs, Int countInstrs, Arena* a) {
   Compiler* cm = lexicallyAnalyze(sourceCode, a);

   initializeParser(cm, a);
   updateStats(cm);
   LUlong* test =  generateBytecode(sourceCode, a);
   
   
   //importTestFns(types, countTypes, imports, countImports, a, OUT test);
   return (CodegenTest){ .name = name, .test = test, .control = instrs, .countInstrs = countInstrs };
}

//:createTest
#define createTest(name, sourceCode, instrs) \
   createTest0((name), (sourceCode), (instrs), sizeof(instrs)/sizeof(Ulong), a)


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
        printf("]\nExpected len = %d: \n", test.expectedOutput.len);
        printString(test.expectedOutput);
        printf("\nBut got: len = %d Discrepancy at %d\n", result.len, cmpRes);
        printString(result);
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
