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

typedef struct {
  String name;
   Codegen* test;
   Int countInstructions;
   Arr(Unt) control;
} CodegenTest;

typedef struct {
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


private Int //:equalDiscrepancy
equalDiscrepancy(String a, String b) {
   Int m = (a.len < b.len) ? a.len : b.len;
   for (Int i = 0; i < m; i++) {
      if (a.cont[i] != b.cont[i]) {
         print("diff char a %d b %d @ %d", a.cont[i], b.cont[i], i);
         return i;
      }
   }
   return (a.len == b.len) ? -1 : 0;
}


void runCodegenTest(CodegenTest test, TestContext* ct) {
// Runs a single lexer test and prints err msg to stdout in case of failure. Returns error code
    ct->countTests += 1;
    String result = tech_sozonov_eyr_compile(test.input);
    Int cmpRes = equalDiscrepancy(result, test.expectedOutput);
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
      (CodegenTest){.name = s("Nested calls"),
         .input = s(
            "def f Int = {{i Int, j Int,}\n"
            "   return i + j;\n"
            "};\n"
            "def g Int = {{i Int,}\n"
            "   return i * 3;\n"
            "};\n"
            "def h Int = {{i Int,}\n"
            "   return i - 1;\n"
            "};\n"
            "def main = {{}\n"
            "   x = 5;\n"
            "   y = 10;\n"
            "   a = g (f x y + (h 7));\n"
            "};"
         ),
         .expectedOutput = s(
            "function f_71(i, j) {\n"
            "   return i + j;\n"
            "}\n"
            "function g_72(i) {\n"
            "   return i * 3;\n"
            "}\n"
            "function h_73(i) {\n"
            "   return i - 1;\n"
            "}\n"
            "function main() {\n"
            "   const x = 5;\n"
            "   const y = 10;\n"
            "   const a = g_72(f_71(x, y) + h_73(7));\n"
            "}\n"
         )
      },
      (CodegenTest){.name = s("Nested operator calls"),
         .input = s(
            "def f Int = {{i Int, j Int,}\n"
            "   return i + j;\n"
            "};\n"
            "def main = {{}\n"
            "   x = 5;\n"
            "   y = 10;\n"
            "   a = f ((x + y)*2) (x/(y - 11));\n"
            "};"
         ),
         .expectedOutput = s(
            "function f_71(i, j) {\n"
            "   return i + j;\n"
            "}\n"
            "function main() {\n"
            "   const x = 5;\n"
            "   const y = 10;\n"
            "   const a = f_71((x + y) * 2, x / (y - 11));\n"
            "}\n"
         )
      },
      (CodegenTest){.name = s("Data allocator"),
         .input = s(
            "def main = {{}\n"
            "   arr = [7 8 9];\n"
            "};"
         ),
         .expectedOutput = s(
            "function main() {\n"
            "   const _l0 = new Array(3);\n"
            "   _l0[0] = 7;\n"
            "   _l0[1] = 8;\n"
            "   _l0[2] = 9;\n"
            "   const arr = _l0;\n"
            "}\n"
         )
      },
      (CodegenTest){.name = s("Nested data allocator"),
         .input = s(
            "def main = {{}\n"
            "   arr = [[1 2] [3 4] [(5 + 6) 7 8]];\n"
            "};"
         ),
         .expectedOutput = s(
            "function main() {\n"
            "   const _l0 = new Array(2);\n"
            "   _l0[0] = 1;\n"
            "   _l0[1] = 2;\n"
            "   const _l1 = new Array(2);\n"
            "   _l1[0] = 3;\n"
            "   _l1[1] = 4;\n"
            "   const _l2 = new Array(3);\n"
            "   _l2[0] = 5 + 6;\n"
            "   _l2[1] = 7;\n"
            "   _l2[2] = 8;\n"
            "   const _l3 = new Array(3);\n"
            "   _l3[0] = _l0;\n"
            "   _l3[1] = _l1;\n"
            "   _l3[2] = _l2;\n"
            "   const arr = _l3;\n"
            "}\n"
         )
      },
      (CodegenTest){.name = s("Writing to array element"),
         .input = s(
            "def main = {{}\n"
            "   arr = [1 2];\n"
            "   arr[0 + 1] = 15;\n"
            "};"
         ),
         .expectedOutput = s(
            "function main() {\n"
            "   const _l0 = new Array(2);\n"
            "   _l0[0] = 1;\n"
            "   _l0[1] = 2;\n"
            "   const arr = _l0;\n"
            "   arr[0 + 1] = 15;\n"
            "}\n"
         )
      },
      (CodegenTest){.name = s("Fields"),
         .input = s(
            "def main = {{}\n"
            "   arr = [1 2];\n"
            "   l = arr.len;\n"
            "};"
         ),
         .expectedOutput = s(
            "function main() {\n"
            "   const _l0 = new Array(2);\n"
            "   _l0[0] = 1;\n"
            "   _l0[1] = 2;\n"
            "   const arr = _l0;\n"
            "   const l = arr.length;\n"
            "}\n"
         )
      }

   }));
}

//}}}
//{{{ Core forms

CodegenTestSet* coreFormTests(Arena* a) {
   return createTestSet(s("Core form test set"), a, ((CodegenTest[]){
      (CodegenTest){.name = s("If"),
         .input = s(
            "def main = {{}\n"
            "   x = 11;\n"
            "   if x > 14 {\n"
            "      return;\n"
            "   }\n"
            "};"
         ),
         .expectedOutput = s(
            "function main() {\n"
            "   const x = 11;\n"
            "   if (x > 14) {\n"
            "      return;\n"
            "   }\n"
            "}\n"
         )
      },
      (CodegenTest){.name = s("If with else if"),
         .input = s(
            "def main = {{}\n"
            "   x = 11;\n"
            "   if x > 14 {\n"
            "      return;\n"
            "   } eif x > 12 {\n"
            "      return;\n"
            "   } else {\n"
            "      return;\n"
            "   }\n"
            "};"
         ),
         .expectedOutput = s(
            "function main() {\n"
            "   const x = 11;\n"
            "   if (x > 14) {\n"
            "      return;\n"
            "   } else if (x > 12) {\n"
            "      return;\n"
            "   } else {\n"
            "      return;\n"
            "   }\n"
            "}\n"
         )
      },
      (CodegenTest){.name = s("For"),
         .input = s(
            "def main = {{}\n"
            "   for { a' = 1; a < 10; a += 1; }{\n"
            "      print a;\n"
            "   }\n"
            "};"
         ),

         .expectedOutput = s(
            "function main() {\n"
            "   for (let a = 1; a < 10; a = a + 1) {\n"
            "      console.log(a);\n"
            "   }\n"
            "}\n"
         )
      },

      (CodegenTest){.name = s("For complex"),
         .input = s(
            "def main = {{}\n"
            "   for { a' = 1; b' = 0; a + b < 10; a -= 1; b += 2; }{\n"
            "      print (a + b);\n"
            "   }\n"
            "};"
         ),

         .expectedOutput = s(
            "function main() {\n"
            "   {\n"
            "      let a = 1;\n"
            "      let b = 0;\n"
            "      for (; (a + b) < 10; a = a - 1, b = b + 2) {\n"
            "         console.log(a + b);\n"
            "      }\n"
            "   }\n"
            "}\n"
         )
      },
      (CodegenTest){.name = s("Generic functions as params"),
         .input = s(
            "def gene Int = {{ operand $T, fn F $T Int, } return fn operand; };\n"
            "def actor Int = {{ operand Double, } return 17; };\n"
            "def main = {{} \n"
            "   rator F Double Int = actor;\n"
            "   a = gene 12.345 rator;\n"
            "   print $a;\n"
            "};"
         ),
         .expectedOutput = s(
            "function actor_72(operand) {\n"
            "   return 17;\n"
            "}\n"
            "function main() {\n"
            "   const a = gene_74(12.344992, actor_72);\n"
            "   console.log(a);\n"
            "}\n"
            "function gene_74(operand, fn) {\n"
            "   return fn(operand);\n"
            "}\n"
         )
      }
   }));
}

//}}}
int main(int argc, char** argv) {
    printf("----------------------------\n");
    printf("    --  CODEGEN TEST  --\n");
    printf("----------------------------\n");

    TestContext ct = (TestContext){ .countTests = 0, .countPassed = 0, .a = createArena() };

    runATestSet(&exprTests, &ct);
    runATestSet(&coreFormTests, &ct);

    if (ct.countTests == 0) {
        print("\nThere were no tests to run!");
    } else if (ct.countPassed == ct.countTests) {
        print("\nAll %d tests passed!", ct.countTests);
    } else {
        print("\nFailed %d tests out of %d!", (ct.countTests - ct.countPassed), ct.countTests);
    }

    deleteArena(ct.a);
}
