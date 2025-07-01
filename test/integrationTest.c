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

LByte* buildFilename(String programName, Arena* a);
void append(String s, LByte* buf);

void ensureCapacityBuf(Int neededSpace, LByte* buf);

static String
stringOf(char const* cString) {
   Int len = strlen(cString);
   return (String){.c = cString, .len = len};
}

String
readTest(String fName, Arena* a) {
   FILE *file = fopen(fName.c, "r");
   if (!file)
      { return empty; }

   // Go to the end of the file
   if (fseek(file, 0L, SEEK_END) != 0)
      { goto cleanup; }
   long fileSize = ftell(file);
   if (fileSize == -1)
      { goto cleanup; }
   // Allocate our buffer to that size, with space for the standard text in front of it
   Arr(char) result = allocateOnArena(fileSize + 1, a);

   // Go back to the start of the file
   if (fseek(file, 0L, SEEK_SET) != 0)
      { goto cleanup; }

   // Read the entire file into memory
   size_t lenSource = fread(result, 1, fileSize, file);

   Int const len = lenSource + 1; // extra 1 for the '\0'
   if (ferror(file) != 0 ) {
      longjmp(excBuf, 1);
   } else {
      result[len] = '\0'; // Just to be safe
   }
   cleanup:
   fclose(file);
   return (String){.c = result, .len = len};
   
}

String
parseExpectedResult(String programName, Arena* a) {
   LByte* filename = buildFilename(programName, a);
   LByte* expectedResult = createLByte(16, a);
   
   String testText = readTest((String){.c = filename->c, .len = filename->len}, a);
   
   Int i = 0;
   Int j = 0;
   Bool insideExpected = false;
   
   for (; i < testText.len; j++, i = j) {
      for (; j < testText.len && testText.c[j] != '\n'; j++) {
      }
      Int const len = j - i;
      if (len >= 13) { // `////EXPECTED{`
         if (memcmp(testText.c + i, opener, 13) == 0) {
            if (insideExpected) {
               longjmp(excBuf, 1);
            } else {
               insideExpected = true;
               continue;
            }
         }
      }
      
      if (len >= 5) { // `////}`
         if (memcmp(testText.c + i, closer, 5) == 0) {
            break;
         }
      }
      if (insideExpected) {
         if (len < 3 || testText.c[i] != '/' || testText.c[i + 1] != '/') {
            longjmp(excBuf, 1);
         }
         append((String){.c = testText.c + i + 2, .len = len - 1}, expectedResult);
      }
   }
   ensureCapacityBuf(1, expectedResult);
   expectedResult->c[expectedResult->len] = '\0';
   return (String){ .c = expectedResult->c, .len = expectedResult->len };
}

///{{{ Test utils

void
findLineBounds(String s, Int startInd, OUT Int* lineStart, OUT Int* lineEnd) {
   Int j;
   for (j = startInd; j < s.len && s.c[j] != '\n'; j++) {
   }
   *lineEnd = j;
   for (j = startInd; j > -1 && s.c[j] != '\n'; j--) {
   }
   *lineStart = j + 1;
}

void
printDifferingLine(String testName, String programOutput, String expectedResult) {
   Int diffInd = -1;
   Int commonLen = MIN(programOutput.len, expectedResult.len);
   if (commonLen == 0) {
      print("Empty result");
      return;
   }
   
   for (Int j = 0; j < commonLen; j++) {
      if (programOutput.c[j] != expectedResult.c[j]) {
         diffInd = j;
         break;
      }
   }
   
   Int programLineStart, programLineEnd, expectedLineStart, expectedLineEnd;
   findLineBounds(programOutput, diffInd, &programLineStart, &programLineEnd);
   findLineBounds(expectedResult, diffInd, &expectedLineStart, &expectedLineEnd);
   
   print("Diff in test %s diffInd %d", testName.c, diffInd);
   printf("Expected: ");
   fwrite(expectedResult.c + expectedLineStart, 1, expectedLineEnd - expectedLineStart, stdout);
   printf("\n");
   printf("But got: ");
   fwrite(programOutput.c + programLineStart, 1, programLineEnd - programLineStart, stdout);
   printf("\n");
}

Bool
compareResults(String programOutput, String expectedResult) {
   if (programOutput.len != expectedResult.len) {
      return false;
   } ei (programOutput.len == 0) {
      return true;
   }
   return memcmp(programOutput.c, expectedResult.c, programOutput.len) == 0;
}

void
ensureCapacityBuf(Int neededSpace, LByte* buf) {
   if (buf->len + neededSpace < buf->cap) {
      return;
   }
   Int const newCap = (2*(buf->cap) > buf->cap + neededSpace)
      ? 2*buf->cap
      : buf->cap + neededSpace;
   Arr(Byte) newContent = allocateArray(newCap, Byte, buf->arena);
   memcpy(newContent, buf->c, buf->len);
   buf->cap = newCap;
   buf->c = newContent;
}

void
appendCString(char const* const s, LByte* buf) {
   Int len = strlen(s);
   ensureCapacityBuf(len, buf);
   memcpy(buf->c + buf->len, s, len);
   buf->len += len;
}

void
append(String s, LByte* buf) {
   ensureCapacityBuf(s.len, buf);
   memcpy(buf->c + buf->len, s.c, s.len);
   buf->len += s.len;
}

LByte*
buildCommandCompile(String programName, Arena* a) {
   LByte* commandBuf = createLByte(64, a);
   appendCString("bin/eyrc ./test/integration/", commandBuf);
   append(programName, commandBuf);
   appendCString(" -o _debug/", commandBuf);
   append((String){.c = programName.c, .len = programName.len - 4}, commandBuf);
   ensureCapacityBuf(1, commandBuf);
   commandBuf->c[commandBuf->len] = '\0';
   commandBuf->len++;
   
   return commandBuf;
}

LByte*
buildCommandRun(String programName, Arena* a) {
   LByte* commandBuf = createLByte(64, a);
   appendCString("_debug/", commandBuf);
   append((String){.c = programName.c, .len = programName.len - 4}, commandBuf);
   ensureCapacityBuf(1, commandBuf);
   commandBuf->c[commandBuf->len] = '\0';
   commandBuf->len++;
   
   return commandBuf;
}

LByte*
buildFilename(String programName, Arena* a) {
   LByte* commandBuf = createLByte(64, a);
   appendCString("test/integration/", commandBuf);
   append(programName, commandBuf);
   ensureCapacityBuf(1, commandBuf);
   commandBuf->c[commandBuf->len] = '\0';
   commandBuf->len++;
   
   return commandBuf;
}

//}}}

Bool
compileTest(String programName, Arena* a) {
   LByte* command = buildCommandCompile(programName, a);
   FILE *pipe = popen(command->c, "r");
   if (pipe == NULL) {
      perror("Compiler error: popen failed");
      return 1;
   }
   
   Int exitCode = pclose(pipe);
   return exitCode == 0;
}

void
runIntegrationTest(TestContext* ctx, String programName, Arena* a) {
   if (setjmp(excBuf) == 0) {
      if (programName.len < 5) {
         print("Test name must be at least 5 symbols (it ends in `.eyr`)");
         return;
      }
      if (!compileTest(programName, a)) {
         print("Could not compile %s", programName.c);
         return;
      }

      static char buffer[BUFFER_SIZE];
      LByte* runCommand = buildCommandRun(programName, a);
      FILE *pipe = popen(runCommand->c, "r");
      if (pipe == NULL) {
         longjmp(excBuf, 1);
      }
      
      LByte* programOutput = createLByte(16, a);
      while (fgets(buffer, BUFFER_SIZE, pipe) != NULL) {
         Int len = strlen(buffer);
         ensureCapacityBuf(len, programOutput);
         append((String){.c = buffer, .len = len}, programOutput);
      }
      if (pclose(pipe) != 0) { longjmp(excBuf, 1); }
      
      String testResult = (String){.c = programOutput->c, .len = programOutput->len };
      String expectedResult = parseExpectedResult(programName, a);
      
      if (compareResults(testResult, expectedResult)) {
         ctx->countPassed++;
      } else {
         printDifferingLine(programName, testResult, expectedResult);
      }
   } else {
      printf("Exception in test ");
      printString(programName);
   }
}

char const* tests[] = {
   "blocks.eyr",
   "fibonacci.eyr",
   "fizzBuzz.eyr",
   "forLoopBreak.eyr",
   "forLoopContinue.eyr",
   "forLoopSimple.eyr",
   "eachLoop.eyr",
   "ifWithElseIf.eyr",
   "insertionSort.eyr",
   "matrixMultiplication.eyr",
   "nestedForLoops.eyr"
};

int main() {
   printf("----------------------------\n");
   printf("----  INTEGRATION TEST  ----\n");
   printf("----------------------------\n");
   
   Arena* a = createArena();
   TestContext ct = (TestContext){.countTests = 0, .countPassed = 0, .a = a };
   
   for (Int t = 0; t < sizeof(tests)/sizeof(char*); t++) {
      ct.countTests++;
      char const* testFile = tests[t];
      runIntegrationTest(&ct, stringOf(testFile), a);
   }
   
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

