#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libgccjit.h>
#include <stdint.h>
#include <setjmp.h>
#include "include/eyrc.h"
#include "eyrc.internal.h"

typedef gcc_jit_param FnParam;
typedef gcc_jit_type CgType;
typedef gcc_jit_function Fn;
typedef gcc_jit_block CodeBlock;
typedef gcc_jit_context CgContext;
typedef gcc_jit_result CgResult;
typedef gcc_jit_lvalue LValue;
typedef gcc_jit_rvalue RValue;
typedef enum gcc_jit_function_kind FnKind;
typedef enum gcc_jit_types BuiltinType;
typedef enum gcc_jit_comparison BuiltinComparison;
#define toPointer(x) gcc_jit_type_get_pointer(x)

extern jmp_buf excBuf;

//{{{ Utils
//{{{ Stack

#define DEFINE_STACK_HEADER(T) \
   typedef struct {\
      Int cap;\
      Int len;\
      Arena* arena;\
      T* cont;\
   } Stack##T;\
   private Stack ## T * createStack ## T (Int initCapacity, Arena* a);\
   private Bool hasValues ## T (Stack ## T * st);\
   private T pop ## T (Stack ## T * st);\
   private T peek ## T(Stack ## T * st);\
   private void push ## T (T newItem, Stack ## T * st);

#define DEFINE_STACK(T)\
   private Stack##T * createStack##T (int initCapacity, Arena* a) {\
      int capacity = initCapacity < 4 ? 4 : initCapacity;\
      Stack##T * result = allocate(Stack##T, a);\
      result->cap = capacity;\
      result->len = 0;\
      result->arena = a;\
      T* arr = allocateArray(capacity, T, a);\
      result->cont = arr;\
      return result;\
   }\
   private bool hasValues ## T (Stack ## T * st) {\
      return st->len > 0;\
   }\
   private T pop##T (Stack ## T * st) {\
      st->len -= 1;\
      return st->cont[st->len];\
   }\
   private T peek##T(Stack##T * st) {\
      return st->cont[st->len - 1];\
   }\
   private void push##T (T newItem, Stack ## T * st) {\
      if (st->len < st->cap) {\
         memcpy((T*)(st->cont) + (st->len), &newItem, sizeof(T));\
      } else {\
         T* newContent = allocateArray(2*(st->cap), T, st->arena);\
         memcpy(newContent, st->cont, st->len*sizeof(T));\
         memcpy((T*)(newContent) + (st->len), &newItem, sizeof(T));\
         st->cap *= 2;\
         st->cont = newContent;\
      }\
      st->len += 1;\
   }\

Int
e_(Int ind, Int len) {
   if ((Unt)ind < (Unt) len) {
      return ind;
   }
   longjmp(excBuf, 1);
}

#define lLast(lst) lst->cont + lst->len - 1

#define l(ind, lst) lst->cont[e_(ind, lst->len)]

DEFINE_STACK_HEADER(SourceLoc)
DEFINE_STACK(SourceLoc) //:createStackSourceLoc

DEFINE_STACK_HEADER(Node)
DEFINE_STACK(Node)

#ifdef TEST
private void dbgStackNode(StackNode*, Arena*);
#endif

//}}}
//}}}
//{{{ Types 

typedef struct { //:CgCall Deprecated?
    Int startInd; // or externalNameId
    Int len;      // only for native names
    Byte emit;
    uint8_t arity;
    uint8_t countArgs;
    Bool needClosingParen;
} CgCall;

DEFINE_STACK_HEADER(CgCall)
DEFINE_STACK(CgCall)

typedef struct { //:CgFrame
   Byte tp; // node type
   Int pl1; // node pl1
   Int pl3; // node pl3
   Int sentinel; // node sentinel
} CgFrame;

DEFINE_STACK_HEADER(CgFrame)
DEFINE_STACK(CgFrame)

typedef struct { //:Codegen
   Int i; // current node index
   Int indentation;
   Int len;
   Int cap;
   Arr(Byte) buffer;
   StackCgCall calls; // temporary stack for generating expressions
    
   CgContext* cont;
   CgType* intTp;
   CgType* voidTp;
    
    

   StackCgFrame backtrack;
   String sourceCode;
   Compiler* cm;
   Int local; // counter of nameless locals, reset at function codegen start
   CgExpr cgExpr; // [aTmp] State for codegeneration for expressions
   Arena* a;
   Bool wasError;
} Codegen;

//}}}

private void
assignment(LValue* left, RValue* right, CodeBlock* block) {
   gcc_jit_block_add_assignment(block, NULL, left, right);
}

private Fn*
importFn(const char* name, int countParams, Arr(FnParam*) params, 
      CgType* returnType, Bool isVariadic, CgContext* ctx
) {
   return gcc_jit_context_new_function(
      ctx,
      NULL, // source location
      GCC_JIT_FUNCTION_IMPORTED,
      returnType,
      name,
      countParams,
      params,
      isVariadic ? 1 : 0
   );
}


private Fn*
newFn(const char* name, FnKind accessLevel, int countParams, Arr(FnParam*) params, 
      CgType* returnType, CgContext* ctx
) {
   return gcc_jit_context_new_function(
      ctx,
      NULL, // source location
      accessLevel,
      returnType,
      name,
      countParams,
      params,
      0
   );
}

private RValue*
call(Fn* fn, int countArgs, Arr(RValue*) args, CgContext* ctx) {
   return gcc_jit_context_new_call(ctx, NULL, fn, countArgs, args);
}

private void
evalExpr(RValue* rValue, CodeBlock* block) {
   gcc_jit_block_add_eval(block, NULL, rValue);
}

private CodeBlock*
newBlock(Fn* fn) {
   return gcc_jit_function_new_block(fn, NULL);
}

private void
jump(CodeBlock* from, CodeBlock* to) {
   gcc_jit_block_end_with_jump (from, NULL, to);
}

private void
conditional(CodeBlock* from, CodeBlock* toIfTrue, CodeBlock* toIfFalse, RValue* condition) {
   gcc_jit_block_end_with_conditional(from, NULL, condition, toIfTrue, toIfFalse);
}

private RValue*
comparison(RValue* a, BuiltinComparison operator, RValue* b, CgContext* ctx) {
   return gcc_jit_context_new_comparison(ctx, NULL, operator, a, b);
}


private CodeBlock*
newNamedBlock(Fn* fn, char const* name) {
   return gcc_jit_function_new_block(fn, name);
}

private RValue*
intConst(int val, Codegen* cg) {
   return gcc_jit_context_new_rvalue_from_int(cg->cont, cg->intTp, val);
}

private LValue*
arrElem(RValue* arr, RValue* index, CgContext* ctx)  {
   return gcc_jit_context_new_array_access(ctx, NULL, arr, index);
}

private RValue*
toRValue(LValue* lvalue) {
   return gcc_jit_lvalue_as_rvalue(lvalue);
}

private RValue*
ptrCast(RValue* v, CgType* tp, CgContext* ctx) {
   return gcc_jit_context_new_cast(ctx, NULL, v, tp);
}

private Fn*
createCodeInner(CgContext* ctx) {
//~     int
//~     inner(int x) {
//~        if (x > 10) {
//~           return x -10
//~        } else { 
//~           return x * 20;
//~        }
//~     }
   CgType* intTp = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT32_T);
   FnParam* paramName = gcc_jit_context_new_param(ctx, NULL, intTp, "x");
   Fn* fn = newFn(
      "inner",
      GCC_JIT_FUNCTION_INTERNAL,
      1,
      &paramName,
      intTp,
      ctx
   );
   
   CodeBlock* body = newBlock(fn);
   CodeBlock* yesBlock = newBlock(fn);
   CodeBlock* noBlock = newBlock(fn);
   RValue* testVal = gcc_jit_context_new_comparison(
      ctx, NULL, GCC_JIT_COMPARISON_GT,
      gcc_jit_param_as_rvalue(paramName), gcc_jit_context_new_rvalue_from_int(ctx, intTp, 10)
   );
      
   conditional(body, yesBlock, noBlock, testVal);
      
   RValue* yesReturnVal = gcc_jit_context_new_binary_op(ctx, NULL, GCC_JIT_BINARY_OP_MINUS, intTp, 
      gcc_jit_param_as_rvalue(paramName), gcc_jit_context_new_rvalue_from_int(ctx, intTp, 10));
   gcc_jit_block_end_with_return(yesBlock, NULL, yesReturnVal);
   
   RValue* noReturnVal = gcc_jit_context_new_binary_op(ctx, NULL, GCC_JIT_BINARY_OP_MULT, intTp, 
      gcc_jit_param_as_rvalue(paramName), gcc_jit_context_new_rvalue_from_int(ctx, intTp, 20));
   gcc_jit_block_end_with_return(noBlock, NULL, noReturnVal);
   
   return fn;
}

LValue* localVar(const char* name, CgType* tp, Fn* fn) {
   return gcc_jit_function_new_local(fn, NULL, tp, name);
}

CgType* getType(BuiltinType tp, CgContext* ctx) {
   return gcc_jit_context_get_type(ctx, tp);
}

static void
addArrayPrint(CodeBlock* block, Fn* fn, Fn* printfFn, Codegen* cg) {
//~     Arr(Int) arr = malloc(4*sizeof(Int));
//~     for (int i = 0; i < 4; i++) {
//~        printf("%d\n", arr[i]);
//~     }
   CgContext* ctx = cg->cont;
   CgType* intPtrTp = toPointer(cg->intTp);
   CgType* voidPtrTp = getType(GCC_JIT_TYPE_VOID_PTR, ctx);
   FnParam* paramSz = gcc_jit_context_new_param(ctx, NULL, cg->intTp, "sz");
   Fn* mallocFn = importFn(
      "malloc",
      1,
      &paramSz,
      voidPtrTp,
      false,
      ctx
   );
   LValue* arr = localVar("arr", intPtrTp, fn);
   RValue* arrR = toRValue(arr);
   RValue* sixteen = intConst(16, cg);
   assignment(arr, ptrCast(call(mallocFn, 1, &sixteen, ctx), intPtrTp, ctx), block);
   // arr[0] = ..., arr[1] = ...
   assignment(arrElem(arrR, intConst(0, cg), ctx), intConst(17, cg), block);
   assignment(arrElem(arrR, intConst(1, cg), ctx), intConst(-27, cg), block);
   assignment(arrElem(arrR, intConst(2, cg), ctx), intConst(37, cg), block);
   assignment(arrElem(arrR, intConst(3, cg), ctx), intConst(107, cg), block);
   
   CodeBlock* loopInit = newNamedBlock(fn, "loopInit");
   CodeBlock* loopCond = newNamedBlock(fn, "loopCond");
   CodeBlock* loopBody = newNamedBlock(fn, "loopBody");
   CodeBlock* loopAfter = newNamedBlock(fn, "loopAfter");
   
   jump(block, loopInit);
   
   // init
   LValue* iVar = localVar("i", cg->intTp, fn);
   assignment(iVar, intConst(0, cg), loopInit);
   jump(loopInit, loopCond);
   
   // conditional 
   conditional(
      loopCond, loopBody, loopAfter,
      comparison(toRValue(iVar), GCC_JIT_COMPARISON_LT, intConst(4, cg), ctx)
   );
         
   // body
   int const countArgs = 2;
   RValue* args[countArgs];
   args[0] = gcc_jit_context_new_string_literal(ctx, "%d\n");
   LValue* arrVal = arrElem(toRValue(arr), toRValue(iVar), ctx); // arr[i]
   args[1] = toRValue(arrVal);
   evalExpr(call(printfFn, countArgs, args, ctx), loopBody);
   
   RValue* nextI = gcc_jit_context_new_binary_op(
      ctx, NULL, GCC_JIT_BINARY_OP_PLUS, cg->intTp, 
      toRValue(iVar), intConst(1, cg));
   assignment(iVar, nextI, loopBody); //i++
   jump(loopBody, loopCond);
   
   // after loop
   gcc_jit_block_end_with_void_return(loopAfter, NULL);
}

static void
createCodeGreet(Codegen* cg) {
   CgContext* ctx = cg->cont;
   CgType* constCharPtrTp = getType(GCC_JIT_TYPE_CONST_CHAR_PTR, ctx);
   FnParam* paramName = gcc_jit_context_new_param(ctx, NULL, constCharPtrTp, "name");
   
   Fn* innerFn = createCodeInner(ctx);
   
   Fn* greet = newFn(
      "greet",
      GCC_JIT_FUNCTION_EXPORTED,
      1,
      &paramName,
      cg->voidTp,
      ctx
   );
   
   FnParam* paramFormat = gcc_jit_context_new_param(ctx, NULL, constCharPtrTp, "format");
   Fn* printfFn = importFn(
      "printf",
      1,
      &paramFormat,
      cg->intTp,
      true,
      ctx
   );
   RValue* fifteen = intConst(15, cg);
   RValue* resultOfEvalInner = call(innerFn, 1, &fifteen, ctx);
   
   const int countArgs = 3;
   RValue* args[countArgs];
   args[0] = gcc_jit_context_new_string_literal(ctx, "hello %s value is %d\n");
   args[1] = gcc_jit_param_as_rvalue(paramName);
   args[2] = resultOfEvalInner;

   CodeBlock* block = gcc_jit_function_new_block(greet, NULL);

   evalExpr(call(printfFn, countArgs, args, ctx), block);
   
   RValue* five = intConst(5, cg);
   RValue* resultOfEvalInner2 = call(innerFn, 1, &five, ctx);
   
   RValue* args2[countArgs];
   args2[0] = gcc_jit_context_new_string_literal(ctx, "hello %s value is %d\n");
   args2[1] = gcc_jit_param_as_rvalue(paramName);
   args2[2] = resultOfEvalInner2;
   evalExpr(call(printfFn, countArgs, args2, ctx), block);
   
   addArrayPrint(block, greet, printfFn, cg);
}


int main(int argc, char** argv) {
   printf("HW\n");
   
  /* Let's try to inject the equivalent of:
     int
     inner(int x) {
        if (x > 10) {
           return x -10
        } else { 
           return x * 20;
        }
     }
     
     void
     greet(const char *name) {
        printf("hello %s value = %d\n", name, inner(5));
        printf("hello %s value = %d\n", name, inner(15));
        Arr(Int) arr = malloc(4*sizeof(Int));
        for (int i = 0; i < 4; i++) {
           printf("%d\n", arr[i]);
        }
     }
  */
//~         def newFn Str = {{x Str, y Double, }
//~            a = x;
//~            return a;
//~         };
// def newFn F[Str Double -> Str] = {x y ->
//    a = x;
//    return a;
// }
   CgContext* ctx = gcc_jit_context_acquire();
   Codegen* cg = malloc(sizeof(Codegen));
   cg->cont = ctx;
   cg->voidTp = getType(GCC_JIT_TYPE_VOID, ctx);
   cg->intTp = getType(GCC_JIT_TYPE_INT32_T, ctx);
   if (!ctx) {
      fprintf(stderr, "NULL ctxt");
      exit (1);
   }

   /* Set some options on the context.
     Let's see the code being generated, in assembler form.  */
   gcc_jit_context_set_bool_option(ctx, GCC_JIT_BOOL_OPTION_DUMP_GENERATED_CODE, 0);

   createCodeGreet(cg);

   // Compile the code
   gcc_jit_result* result = gcc_jit_context_compile(ctx);
   
   gcc_jit_context_dump_to_file(ctx, "outp.c", 0); 
   if (!result) {
      fprintf (stderr, "NULL result");
      exit(1);
   }

   // Extract the generated code from "result"
   typedef void (*FnType) (const char *);
   FnType greet = (FnType)gcc_jit_result_get_code(result, "greet");
   if (!greet) {
      fprintf(stderr, "NULL greet");
      exit(1);
   }

   greet("world");
   fflush(stdout);

   gcc_jit_context_release(ctx);
   gcc_jit_result_release(result);
   return 0;
}

