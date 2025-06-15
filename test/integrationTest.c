//{{{ Includes

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>

#include <unistd.h>
#include <dirent.h>

#include "include/libeyr.h"
#include "integrationTest.h"

//}}}

typedef libeyr_String String;
#define BUFFER_SIZE 1024

DEFINE_LIST_HEADER(Byte);
DEFINE_LIST(Byte);

extern jmp_buf excBuf;

constexpr char opener[] = "////EXPECTED{";
constexpr char closer[] = "////}";

static String
stringOf(char const* cString) {
   Int len = strlen(cString);
   return (String){.c = cString, .len = len};
}

String
parseExpectedResult(String testFn, Arena* a) {
   LByte* expectedResult = createLByte(16, a);
   
   String testText = readSourceFile(testFn, a);
   Int i = 0;
   Int j = 0;
   Bool insideExpected = false;

   for (; i < textText.len;) {
      for (; j < testText.len && testText.c[j] != '\n'; j++) {
      }
      if (j - i >= 13) { // `////EXPECTED{`
         if (memcmp(testText.c + i, opener, 13)) {
            if (insideExpected) {
               longjmp();
            } else {
               insideExpected = true;
            }
            
         }
      }
      
      if (j - i >= 5) { // `////}`
         if (memcmp(testText.c + i, closer, 5)) {
            insideExpected = false;
         }
      }
      
      i = j;
   }
   
}

void
findLineBounds(String s, Int startInd, OUT Int* lineStart, OUT Int* lineEnd) {
   Int j;
   for (j = startInd; j < s.len && s.c[j] != '\n'; j++) {
   }
   *lineEnd = j;
   for (j = startInd; j > -1 && s.c[j] != '\n'; j++) {
   }
   *lineStart = j + 1;
}

Bool
compareResults(String testName, String programOutput, String expectedResult) {
   if (programOutput.len != expectedResult.len) {
      return false;
   } ei (programOutput.len == 0) {
      return true;
   }
   if (memcmp(programOutput.c, expectedResult.c, programOutput.len) == 0) {
      return true;
   }
   Int diffInd = -1;
   for (Int j = 0; j < expectedResult.len; j++) {
      if (programOutput.c[j] != expectedResult.c[j]) {
         diffInd = j;
         break;
      }
   }
   Int programLineStart, programLineEnd, expectedLineStart, expectedLineEnd;
   findLineBounds(programOutput, diffInd, &programLineStart, &programLineEnd);
   findLineBounds(expectedResult, diffInd, &expectedLineStart, &expectedLineEnd);
   print("Diff in test %s", testName.c);
   printf("Expected: ", expectedResult.c);
   fwrite(expectedResult.c + expectedLineStart, 1, expectedLineEnd - expectedLineStart, stdout);
   printf("But got: ", programResult.c);
   fwrite(programOutput.c + programLineStart, 1, programLineEnd - programLineStart, stdout);
   return false;
}

Bool
compileTest() {
   FILE *pipe = popen(
      "_target/eyrc ./test/integration/fibonacci.eyr -o _target/debug/fibonacci",
      "r"
   );
   if (pipe == NULL) {
      perror("Compiler error: popen failed");
      return 1;
   }
   
   Int exitCode = pclose(pipe);
   return exitCode == 0;
}

void
runIntegrationTest(TestContext* ctx, String programName) {
   ctx->countTests++;
   static char buffer[BUFFER_SIZE];
   FILE *pipe = popen(programName.c, "r");
   if (pipe == NULL) {
      print("Compiled program error: popen failed");
   }
   
   Arena* a = createArena();
   LByte* programOutput = createLByte(16, a);


   while (fgets(buffer, BUFFER_SIZE, pipe) != NULL) {
      Int len = strlen(buffer);
      for (Int j = 0; j < len; j++) {
         addByte(buffer[j], programOutput);
      }
   }
   if (pclose(pipe) != 0) { return; }
   
   String expectedResult = parseExpectedResult(programName, a);
   if (compareResults(programName, programOutput, expectedResult)) {
      ctx->countPassed++;
   }
   
   printf("Str len %d\n", str->len);
   printf("%s\n", str->c);
   
}


int main() {
   printf("----------------------------\n");
   printf("----  INTEGRATION TEST  ----\n");
   printf("----------------------------\n");
   if (!compileTest()) {
      print("Could not compile fibonacci");
      return 0;
   }
   
   Arena* a = createArena();
   TestContext ct = (TestContext){.countTests = 0, .countPassed = 0, .a = a };
   runIntegrationTest(&ct, stringOf("_target/fibonacci"), a);
   
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

   deleteArena(a);
   return 0;
}
