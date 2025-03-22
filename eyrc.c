#include <libgccjit.h>
#include "libeyr.h"

//{{{ Types 

typedef gcc_jit_param CgParam;
typedef gcc_jit_type CgType;
typedef gcc_jit_function CgFn;
typedef gcc_jit_block CodeBlock;
typedef gcc_jit_context Module;
typedef gcc_jit_result CgResult;
typedef gcc_jit_lvalue LValue;
typedef gcc_jit_rvalue RValue;
typedef enum gcc_jit_function_kind FnKind;
typedef enum gcc_jit_types BuiltinType;
typedef enum gcc_jit_comparison BuiltinComparison;
#define toPointer(x) gcc_jit_type_get_pointer(x)

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

typedef struct { //:CgTypeRef Codegenned type and index of Eyr type (index into @Compiler.types)
   Int ind;
   CgType* cgType;
} CgTypeRef;

typedef struct { //
   CgType* intTp;
   CgType* voidTp;
} Primitives;

typedef struct { //:Codegen
   String sourceCode;
   Compiler* cm;
   
   Int i; // current node index
   Int len;
   Int cap;
   Arr(Byte) buffer;
    
   Module* md;
   Primitives primitives;
   
   StackFnParam params; // temporary buffer for function params

   StackCall calls; // temporary stack for generating expressions
   StackFrame backtrack;
   Expr Expr; // [aTmp] State for codegeneration for expressions
   
   Int countTypeRefs;
   Arr(CgTypeRef) typeRefs;
   
   Arena* a;
   Bool wasError;
} Codegen;

typedef struct { //:CgeCallSpan A function and which operands it spans (both ends inclusive)
   Int startNode;
   Int endNode;
   Int firstArgEndNode; // the last operand within the first arg, e.g.
                        // for `g` in `5 10 .f .g` it will be `10`
   Byte countArgs; // args counted during the codegen run, not parsed
   Byte arity;
   FunctionId fnId; // index into @cm.functions
   VarId varId;     // if fnId == -1, then this is used for emitting (function-typed params)
   Int n;
   Byte closerType; // "closer" constants
} CgeCallSpan;

#define closerPrefix   1
#define closerInfix    2
#define closerParen    3  // emit a closing paren for infix opers
#define closerBracket  4  // emit a closing "]" for array accessors
#define closerAccessor 5  // emit an opening "[" without spaces
#define closerField    6
#define closerNone     7

typedef struct { //:CgeCloser
   Int nodeInd; // index of node after which the closer is applied
   Byte tp;   // "closerTp" constants
   Byte arity;
   FunctionId fnId; // index into @cm.functions (for emit = emitInfix) or
                    // -1 (for emitPrefix, just adding a ")")
} CgeCloser;    // something that needs to be codegenned when exiting an operand, like adding a ")"

DEFINE_STACK_HEADER(CgeCallSpan)
DEFINE_STACK(CgeCallSpan)
DEFINE_STACK_HEADER(CgeCloser)
DEFINE_STACK(CgeCloser)

void cgExpr(Int start, Int endInclusive, Bool isComplex, Arr(Node const) ast, CG);

//}}}
//{{{ Host text

// Host strings for codegen. Must agree in order with the "host" constants below :hostText
constexpr char hostText[] = "functionelseconstletlonewArrayconsole.logpushlengthMath.abs"
                            "";
constexpr Byte
hostStringLens[] = {
    8, 4, 5, 3, 2,  // lo
    3, 5, 11, 4, 6, // length
    8
};

private Int
hostOffsets[sizeof(hostStringLens)]; // filled in by "populateStringOffsets"

extern void
populateStringOffsets(Arr(Byte const) stringLens, Int start, Int len, OUT Arr(Int) offsets) {

//}}}
//{{{ Generation table

typedef void (*CgFunc)(Node, Arr(Node const), Codegen* restrict);
#define CG Codegen* restrict cg
#define CG_FUN(name) private void name(Node nd, Arr(Node const) nodes, CG);
CG_FUN(writeScope) CG_FUN(writeExpr) CG_FUN(cgAssignment) CG_FUN(writeDataAlloc) CG_FUN(writeAssert)
CG_FUN(writeBreakCont) CG_FUN(writeTry) CG_FUN(writeCatch) CG_FUN(writeFnDef) CG_FUN(writeDef)
CG_FUN(writeTrait) CG_FUN(writeImpl) CG_FUN(cgReturn)
CG_FUN(writeFor) CG_FUN(cgIf) CG_FUN(cgIfClause) CG_FUN(writeMatch)

private CgFunc const CODEGEN_TABLE[countSpanForms] = {
   [0]                        = &writeScope,
   [nodExpr       - nodScope] = &writeExpr,
   [nodAssignment - nodScope] = &writeAssignment,
   [nodDataAlloc  - nodScope] = &writeDataAlloc,
   [nodAssert     - nodScope] = &writeAssert,
   [nodBreakCont  - nodScope] = &writeBreakCont,
   [nodTry        - nodScope] = &writeTry,
   [nodCatch      - nodScope] = &writeCatch,
   [nodFnDef      - nodScope] = &writeFnDef,
   [nodDef        - nodScope] = &writeDef,
   [nodTrait      - nodScope] = &writeTrait,
   [nodImpl       - nodScope] = &writeImpl,
   [nodReturn     - nodScope] = &writeReturn,
   [nodFor        - nodScope] = &writeFor,
   [nodIf         - nodScope] = &writeIf,
   [nodIfClause   - nodScope] = &writeIfClause,
   [nodMatch      - nodScope] = &writeMatch
};

//}}}
//{{{ Code generator

private void
assignment(LValue* left, RValue* right, CodeBlock* block) {
   gcc_jit_block_add_assignment(block, NULL, left, right);
}

private Fn*
importFn(const char* name, int countParams, Arr(FnParam*) params, 
      CgType* returnType, Bool isVariadic, Module* md
) {
   return gcc_jit_context_new_function(
      md,
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
      CgType* returnType, Module* md
) {
   return gcc_jit_context_new_function(
      md,
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
call(Fn* fn, int countArgs, Arr(RValue*) args, Module* md) {
   return gcc_jit_context_new_call(md, NULL, fn, countArgs, args);
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
comparison(RValue* a, BuiltinComparison operator, RValue* b, Module* md) {
   return gcc_jit_context_new_comparison(md, NULL, operator, a, b);
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
arrElem(RValue* arr, RValue* index, Module* md)  {
   return gcc_jit_context_new_array_access(md, NULL, arr, index);
}

private RValue*
toRValue(LValue* lvalue) {
   return gcc_jit_lvalue_as_rvalue(lvalue);
}

private RValue*
ptrCast(RValue* v, CgType* tp, Module* md) {
   return gcc_jit_context_new_cast(md, NULL, v, tp);
}


LValue* localVar(const char* name, CgType* tp, Fn* fn) {
   return gcc_jit_function_new_local(fn, NULL, tp, name);
}

CgType* getType(BuiltinType tp, Module* md) {
   return gcc_jit_context_get_type(md, tp);
}

static void
addArrayPrint(CodeBlock* block, Fn* fn, Fn* printfFn, Codegen* cg) {
//~     Arr(Int) arr = malloc(4*sizeof(Int));
//~     for (int i = 0; i < 4; i++) {
//~        printf("%d\n", arr[i]);
//~     }
   Module* md = cg->cont;
   CgType* intPtrTp = toPointer(cg->intTp);
   CgType* voidPtrTp = getType(GCC_JIT_TYPE_VOID_PTR, md);
   FnParam* paramSz = gcc_jit_context_new_param(md, NULL, cg->intTp, "sz");
   Fn* mallocFn = importFn(
      "malloc",
      1,
      &paramSz,
      voidPtrTp,
      false,
      md
   );
   LValue* arr = localVar("arr", intPtrTp, fn);
   RValue* arrR = toRValue(arr);
   RValue* sixteen = intConst(16, cg);
   assignment(arr, ptrCast(call(mallocFn, 1, &sixteen, md), intPtrTp, md), block);
   // arr[0] = ..., arr[1] = ...
   assignment(arrElem(arrR, intConst(0, cg), md), intConst(17, cg), block);
   assignment(arrElem(arrR, intConst(1, cg), md), intConst(-27, cg), block);
   assignment(arrElem(arrR, intConst(2, cg), md), intConst(37, cg), block);
   assignment(arrElem(arrR, intConst(3, cg), md), intConst(107, cg), block);
   
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
      comparison(toRValue(iVar), GCC_JIT_COMPARISON_LT, intConst(4, cg), md)
   );
         
   // body
   int const countArgs = 2;
   RValue* args[countArgs];
   args[0] = gcc_jit_context_new_string_literal(md, "%d\n");
   LValue* arrVal = arrElem(toRValue(arr), toRValue(iVar), md); // arr[i]
   args[1] = toRValue(arrVal);
   evalExpr(call(printfFn, countArgs, args, md), loopBody);
   
   RValue* nextI = gcc_jit_context_new_binary_op(
      md, NULL, GCC_JIT_BINARY_OP_PLUS, cg->intTp, 
      toRValue(iVar), intConst(1, cg));
   assignment(iVar, nextI, loopBody); //i++
   jump(loopBody, loopCond);
   
   // after loop
   gcc_jit_block_end_with_void_return(loopAfter, NULL);
}

static void
createCodeGreet(Codegen* cg) {
   Module* md = cg->cont;
   CgType* constCharPtrTp = getType(GCC_JIT_TYPE_CONST_CHAR_PTR, md);
   FnParam* paramName = gcc_jit_context_new_param(md, NULL, constCharPtrTp, "name");
   
   Fn* innerFn = createCodeInner(md);
   
   Fn* greet = newFn(
      "greet",
      GCC_JIT_FUNCTION_EXPORTED,
      1,
      &paramName,
      cg->voidTp,
      md
   );
   
   FnParam* paramFormat = gcc_jit_context_new_param(md, NULL, constCharPtrTp, "format");
   Fn* printfFn = importFn(
      "printf",
      1,
      &paramFormat,
      cg->intTp,
      true,
      md
   );
   RValue* fifteen = intConst(15, cg);
   RValue* resultOfEvalInner = call(innerFn, 1, &fifteen, md);
   
   const int countArgs = 3;
   RValue* args[countArgs];
   args[0] = gcc_jit_context_new_string_literal(md, "hello %s value is %d\n");
   args[1] = gcc_jit_param_as_rvalue(paramName);
   args[2] = resultOfEvalInner;

   CodeBlock* block = gcc_jit_function_new_block(greet, NULL);

   evalExpr(call(printfFn, countArgs, args, md), block);
   
   RValue* five = intConst(5, cg);
   RValue* resultOfEvalInner2 = call(innerFn, 1, &five, md);
   
   RValue* args2[countArgs];
   args2[0] = gcc_jit_context_new_string_literal(md, "hello %s value is %d\n");
   args2[1] = gcc_jit_param_as_rvalue(paramName);
   args2[2] = resultOfEvalInner2;
   evalExpr(call(printfFn, countArgs, args2, md), block);
   
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
   Module* md = gcc_jit_context_acquire();
   Codegen* cg = malloc(sizeof(Codegen));
   cg->cont = md;
   cg->voidTp = getType(GCC_JIT_TYPE_VOID, md);
   cg->intTp = getType(GCC_JIT_TYPE_INT32_T, md);
   if (!md) {
      fprintf(stderr, "NULL mdt");
      exit (1);
   }

   /* Set some options on the context.
     Let's see the code being generated, in assembler form.  */
   gcc_jit_context_set_bool_option(md, GCC_JIT_BOOL_OPTION_DUMP_GENERATED_CODE, 0);

   createCodeGreet(cg);

   // Compile the code
   gcc_jit_result* result = gcc_jit_context_compile(md);
   
   gcc_jit_context_dump_to_file(md, "outp.c", 0); 
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

   gcc_jit_context_release(md);
   gcc_jit_result_release(result);
   return 0;
}

//}}}
//{{{ Codegen proper

private Codegen* //:createCodegen
createCodegen(CM, Arena* a) {
   Codegen* cg = allocate(Codegen, a);
   Expr expr = (Expr){
      .callSpans = createStackCgeCallSpan(4, a),
      .sortedCallSpans = createStackCgeCallSpan(4, a),
      .closers = createStackCgeCloser(4, a)
   };
   Module* md = gcc_jit_context_acquire();
   Primitives primitives = {
      .intTp = getType(GCC_JIT_TYPE_VOID, md),
      .voidTp = getType(GCC_JIT_TYPE_INT32_T, md),
   };
   (*cg) = (Codegen) {
      .sourceCode = cm->sourceCode, .cm = cm,
      .i = 0, .len = 0, .cap = 64, .buffer = allocateOnArena(64, a),
      .md = md, .primitives = primitives,

      .calls = *createStackCall(16, a),
      .backtrack = *createStackCgFrame(16, a),
      .expr = expr,
      .a = a,
      .wasError = false
   };
   
   return cg;
}

void
init() {
   populateStringOffsets(hostStringLens, 0, sizeof(hostStringLens),
                         OUT hostOffsets);
}

//~private void //:writeHostConstant
//~writeHostConstant(Int indConst, Bool addSpace, CG) {
//~   Int const len = hostStringLens[indConst] + (addSpace ? 1 : 0);
//~   cgEnsureBufferLength(len, cg);
//~   memcpy(cg->buffer + cg->len, hostText + hostOffsets[indConst], len);
//~   cg->len += len;
//~   if (addSpace) {
//~      cg->buffer[cg->len - 1] = 32;
//~   }
//~}

private void //:writeStr
writeStr(String str, CG) {
   cgEnsureBufferLength(str.len + 2, cg); // +2 for the quotation marks
   cg->buffer[cg->len] = aQuote;
   memcpy(cg->buffer + cg->len + 1, str.cont, str.len);
   cg->len += (str.len + 2);
   cg->buffer[cg->len - 1] = aQuote;
}

private void //:writeBytesFromSource
writeBytesFromSource(SourceLoc loc, CG) {
   writeBytes(cg->sourceCode.cont + loc.startBt, loc.lenBts, cg);
}

//{{{ CgExpr - code generation for expressions

private void //:writeVar
writeVar(Node nd, CG) {
   Var v = cg->cm->vars.cont[nd.pl1];
   if (v.fnId == -1) {
      writeName(v.name, cg);
   } else {
      FunctionId fnId = nd.pl2;
      writeName(cg->cm->functions.cont[fnId].name, cg);
      writeChar(aUnderscore, cg);
      writeInt(fnId, cg);
   }
}

private void //:writeExprOperand
writeExprOperand(Node n, SourceLoc loc, CG) {
   if (n.tp == nodVar) {
      writeVar(n, cg);
   } else if (n.tp == nodDataAlloc && n.pl2 == 0) {
      writeChars(((Byte[]){ aBracketLeft, aBracketRight}), cg);
   } else if (n.tp == tokInt) {
      Ulong upper = n.pl1;
      Ulong lower = n.pl2;
      Ulong total = (upper << 32) + lower;
      Long signedTotal = (Long)total;
      cgEnsureBufferLength(45, cg);
      Int lenWritten = sprintf(cg->buffer + cg->len, "%d", signedTotal);
      cg->len += lenWritten;
   } else if (n.tp == tokString) {
      writeBytesFromSource(loc, cg);
   } else if (n.tp == tokDouble) {
      Ulong upper = n.pl1;
      Ulong lower = n.pl2;
      Long total = (Long)((upper << 32) + lower);
      double floating = doubleOfLongBits(total);
      cgEnsureBufferLength(45, cg);
      Int lenWritten = sprintf(cg->buffer + cg->len, "%f", floating);
      cg->len += lenWritten;
   } else if (n.tp == tokBool) {
      writeBytesFromSource(loc, cg);
   }
}

void //:cgeSortCodeSpans
cgeSortCodeSpans(StackCgeCallSpan* sortedCallSpans) {
// Spans are sorted by startNode DESC, the sort is stable. Then they will be consumed backwards
   Int const len = sortedCallSpans->len;

   for (Int i = 0; i < len; i += 1) {
      Int maxInd = i;
      Int maxStartNode = sortedCallSpans->cont[maxInd].startNode;

      Int j = i + 1;
      for (j = len - 1; j > i; j -= 1) {
         if (sortedCallSpans->cont[j].startNode > maxStartNode) {
            maxInd = j;
            maxStartNode = sortedCallSpans->cont[j].startNode;
         }
      }
      if (maxInd != i) {
         CgeCallSpan tmp = sortedCallSpans->cont[i];
         sortedCallSpans->cont[i] = sortedCallSpans->cont[maxInd];
         sortedCallSpans->cont[maxInd] = tmp;
      }
   }
}

void //:cgeResolveCallSpans
cgeResolveCallSpans(AST, CgExpr cge, CM) {
// For every call in an expression, resolves the node ids of the operands it spans
// Ex: for [id1 id2 literal .call(2)] resolves that the binary `.call` spans `.id2` and `literal`
   cge.callSpans->len = 0;
   cge.sortedCallSpans->len = 0;
   for(Int j = cge.endInclusive; j >= cge.start; j--) {
      Node nd = ast[j];
      if (nd.tp == nodCall) {
      
         Byte closerType = closerPrefix;
         Int fnId = -1;
         switch (nd.pl3) {
         case callNormal:
            fnId = nd.pl1;
            Function fn = cm->functions.cont[fnId];
            if (fn.emit == emitInfix || fn.emit == emitField)
               { closerType = closerInfix; }
            break;
         case callField:
            continue;
         case callMonomorph:
            fnId = cm->monos->cont[nd.pl1].fnId; break;
         case callVar:
            fnId = cm->vars.cont[nd.pl1].fnId; break;
         case callGetElem:
            closerType = closerAccessor; break;
            
         }
         Int varId = fnId != -1 ? -1 : nd.pl1; // only for function-typed function params
         push(((CgeCallSpan){
                .arity = nd.pl2,
                .fnId = fnId,
                .varId = varId,
                .countArgs = 0,
                .n = j,
                .closerType = closerType
              }), cge.callSpans
         );
      } else {
         CgeCallSpan* top = cge.callSpans->cont + cge.callSpans->len - 1;
         for (CgeCallSpan* p = top; p >= cge.callSpans->cont && p->countArgs == 0; p--) {
            p->endNode = j;
         }

         top->countArgs++;
         if (top->countArgs == top->arity) {
            top->startNode = j;
            top->firstArgEndNode = j;
            CgeCallSpan full = pop(cge.callSpans); // this function's arity has been saturated
            push(full, cge.sortedCallSpans);
            for(;
               hasValues(cge.callSpans);
               full = pop(cge.callSpans), push(full, cge.sortedCallSpans)
            ){
               top--;
               top->countArgs++;
               if (top->countArgs < top->arity)
                  { break; }
               top->startNode = j;
               top->firstArgEndNode = full.endNode;
            }
         }
      }
   }

   if (false) {
      print("resolved code spans:")
      printf("[");
      for (Int j = 0; j < cge.sortedCallSpans->len; j++) {
         auto sp = cge.sortedCallSpans->cont[j];
         printf(" (j %d | %d %d faen %d)", sp.n,
            sp.startNode, sp.endNode, sp.firstArgEndNode);
      }
      printf("]\n");
   }

   cgeSortCodeSpans(cge.sortedCallSpans);

   if (false) {
      print("sorted code spans:")
      printf("[");
      for (Int j = 0; j < cge.sortedCallSpans->len; j += 1) {
         auto sp = cge.sortedCallSpans->cont[j];
         printf(" (j [sort %d] %d: %d %d)", sp.closerType, sp.n,
                sp.startNode, sp.endNode);
      }
      printf("]\n");
   }
}

void //:cgeProcessFnCallSpan
cgeProcessFnCallSpan(CgeCallSpan sp, CgExpr cge, CG) {
   Function fn = cg->cm->functions.cont[sp.fnId];
   if (fn.emit == emitPrefix) {
      writeName(fn.name, cg);
      writeChar(aUnderscore, cg);
      writeInt(sp.fnId, cg);
      writeChar(aParenLeft, cg);
      push(((CgeCloser){  // closing paren for a prefix function
               .nodeInd = sp.endNode,
               .tp = closerPrefix,
               .arity = tGetFnArity(fn.typeId, cg->cm)
           }),
           cge.closers
      );
   } ei (fn.emit == emitInfix) {
      push(((CgeCloser) { // for infix operators, scheduling the operator itself
               .nodeInd = sp.firstArgEndNode,
               .tp = closerInfix,
               .arity = 2,
               .fnId = sp.fnId
           }),
           cge.closers
      );

      if (cge.closers->len > 1) {
         Byte prevCloserType = cge.closers->cont[cge.closers->len - 2].tp;
         if (prevCloserType != closerPrefix && prevCloserType != closerBracket) {
            writeChar(aParenLeft, cg);
            push(((CgeCloser) { // ... and maybe its closing paren - only if inside infix
                     .nodeInd = sp.endNode,
                     .tp = closerParen,
                     .fnId = sp.fnId
                 }),
                 cge.closers
            );
         } else {
            push(((CgeCloser){ // This empty closer signifies that we're inside an infix span
                     .nodeInd = sp.endNode,
                     .tp = closerNone
                 }),
                 cge.closers
            );
         }
      }
   } ei (fn.emit == emitHostPrefix) {
      writeHostConstant(fn.hostName, false, cg);
      writeChar(aParenLeft, cg);
      push(((CgeCloser){  // closing paren for a prefix function
               .nodeInd = sp.endNode,
               .tp = closerPrefix,
               .arity = tGetFnArity(fn.typeId, cg->cm)
           }),
           cge.closers
      );
   } ei (fn.emit == emitField)  {
      push(((CgeCloser){
               .nodeInd = sp.endNode,
               .tp = closerField,
               .fnId = sp.fnId,
               .arity = 1
           }),
           cge.closers
      );
   } ei (fn.emit == emitHostInfix) {
      push(((CgeCloser) { // for infix operators, schedule the operator itself
               .nodeInd = sp.firstArgEndNode,
               .tp = closerInfix,
               .arity = 2,
               .fnId = sp.fnId
           }),
           cge.closers
      );
      push(((CgeCloser){  // closing paren for a prefix function
               .nodeInd = sp.endNode,
               .tp = closerPrefix,
               .arity = tGetFnArity(fn.typeId, cg->cm)
           }),
           cge.closers
      );

   }
}

void //:cgeProcessCallSpans
cgeProcessCallSpans(CgeCallSpan sp, CgExpr cge, CG) {
// Process call spans associated with an operand in an expression
   if (sp.closerType == closerAccessor) {
      push(((CgeCloser){  // closing paren for a prefix function
               .nodeInd = sp.firstArgEndNode,
               .tp = closerAccessor,
               .arity = 2
           }),
           cge.closers
      );
      push(((CgeCloser) { // ... and maybe its closing paren - only if inside infix
               .nodeInd = sp.endNode,
               .tp = closerBracket,
           }),
           cge.closers
      );
      return;
   } ei (sp.fnId != -1) {
      cgeProcessFnCallSpan(sp, cge, cg);
   } else { // variable which is a parameter with fn type. We just print it by its var name
      Var v = cg->cm->vars.cont[sp.varId];
      writeName(v.name, cg);
      writeChar(aParenLeft, cg);
      push(((CgeCloser){  // closing paren for a prefix function
               .nodeInd = sp.endNode,
               .tp = closerPrefix,
               .arity = sp.closerType == closerField ? 1 : tGetFnArity(v.typeId, cg->cm)
           }),
           cge.closers
      );
   }
}

void //:cgeSortClosers
cgeSortClosers(StackCgeCloser* closers) {
// Sorts the active closers ascending by nodeId so they are applied to corresponding operands
   for (Int i = 0; i < closers->len; i += 1)  {
      Int maxInd = i;
      Int maxVal = closers->cont[i].nodeInd;
      for (Int j = i + 1; j < closers->len; j++) {
         if (closers->cont[j].nodeInd > maxVal) {
            maxInd = j;
            maxVal = closers->cont[j].nodeInd;
         }
      }
      if (maxInd != i) {
         CgeCloser tmp = closers->cont[i];
         closers->cont[i] = closers->cont[maxInd];
         closers->cont[maxInd] = tmp;
      }
   }
}

void //:cgeApplyClosers
cgeApplyClosers(Int operandInd, CgExpr cge, CG) {
// Apply the closers (infix operators, closing parens, brackets) after an operand
   Int clInd = cge.closers->len - 1;
   Bool infixWasInserted = false;
   for (; clInd > -1 && operandInd == cge.closers->cont[clInd].nodeInd; clInd -= 1 ) {
      CgeCloser closer = cge.closers->cont[clInd];
      if (closer.tp == closerPrefix || closer.tp == closerParen) {
         writeChars(((Byte[]){ aParenRight }), cg);
      } else if (closer.tp == closerAccessor)  {
         writeChar(aBracketLeft, cg);
      } else if (closer.tp == closerBracket)  {
         writeChar(aBracketRight, cg);
      } else if (closer.tp == closerInfix)  {
         Function fn = cg->cm->functions.cont[closer.fnId];
         if (closer.fnId < cg->cm->stats.countOperatorFns) {
            NameLoc nameLoc = fn.name;
            Bool isIncrement =
               nameLoc == OPERATORS[opIncrement].name || nameLoc == OPERATORS[opDecrement].name;
            if (!isIncrement)
               { writeChar(aSpace, cg); }
            writeBytes(cg->sourceCode.cont + (nameLoc & LOWER24BITS), (nameLoc >> 24), cg);
            if (!isIncrement)
               { writeChar(aSpace, cg); }
         } else {
            writeChar(aDot, cg);
            if (fn.emit == emitInfix) {
               writeName(fn.name, cg);
            } else if (fn.emit == emitHostInfix) {
               writeHostConstant(fn.hostName, false, cg);
            }
            writeChar(aParenLeft, cg);
         }

         infixWasInserted = true;
      } ei (closer.tp == closerField) {
         writeChar(aDot, cg);
         writeHostConstant(cg->cm->functions.cont[closer.fnId].hostName, false, cg);
      } else if (closer.tp != closerNone) {
         writeChar(aSpace, cg);
         Function fn = cg->cm->functions.cont[closer.fnId];
         writeBytesFromSource(
            (SourceLoc){.startBt = fn.name & LOWER24BITS, .lenBts = (fn.name >> 24)}, cg
         );
         writeChar(aSpace, cg);
         infixWasInserted = true;
      }
   }

   cge.closers->len = clInd + 1;

   // comma separator for prefix calls
   if (clInd > -1 && !infixWasInserted) {
      CgeCloser outerCloser = cge.closers->cont[clInd];
      if (outerCloser.arity > 1 && outerCloser.tp == closerPrefix)
         { writeChars(((Byte[]){aComma, aSpace}), cg); }
   }
}

void //:cgeGenerate
cgeGenerate(CgExpr cge, Arr(Node const) ast, CG) {
// The super-complex expression codegeneration
   Int spInd = cge.sortedCallSpans->len - 1;
   CgeCallSpan sp = cge.sortedCallSpans->cont[spInd];
   Compiler const* cm = cg->cm;
   cge.closers->len = 0;
   for (Int j = cge.start; j <= cge.endInclusive; j += 1) {
      Node nd = ast[j];
      if (nd.tp == nodCall) {
         if (nd.pl3 == callField) {
            writeChar(aDot, cg);
            writeName(nd.pl1, cg);
         } else // calls will be applied to respective operands, this is what CgeCallSpans are for
            { continue; }
      }

      for (; j == sp.startNode && spInd > -1; spInd--, sp = cge.sortedCallSpans->cont[spInd]) {
         cgeProcessCallSpans(sp, cge, cg);
      }
      cgeSortClosers(cge.closers);
      writeExprOperand(nd, cm->sourceLocs->cont[j], cg);
      cgeApplyClosers(j, cge, cg);
   }
}

Int //:cgeComplexSubexpressions
cgeComplexSubexpressions(Node exprNode, Int exprNodeInd, Arr(Node const) ast, CG) {
// Consumes no nodes. Prints out all inner assignments of a complex expression
   Int j = exprNodeInd + 1;
   Compiler* cm = cg->cm;
   // loop over the subexpressions
   for (Node nd = ast[j];
        nd.tp == nodAssignment;
        j = calcNodeSentinel(nd, j), nd = ast[j], cg->local++
   ) {
      Node varNd = ast[j + 1];
      Node allocNd = ast[j + 2];
#ifdef SAFETY
   VALIDATEI(nd.pl3 == 2 && varNd.tp == nodVar, iErrorComplexExpression);
#endif

      Int const countElts = allocNd.pl3;
      // writes a nameless local var like `const _l123 = new Array(4);`
      writeNewline(cg);
      writeHostConstant(hostConst, true, cg);

      cm->vars.cont[varNd.pl1].name = -cg->local - 1;
      Int const localNameStart = cg->len;
      writeName(-cg->local - 1, cg);
      Int const localNameLen = cg->len - localNameStart;
      writeChars(((Byte[]){aSpace, aEqual, aSpace}), cg);

      writeHostConstant(hostNew, true, cg); // new Array(len)
      writeHostConstant(hostArray, false, cg);
      writeChar(aParenLeft, cg);
      writeInt(countElts, cg);
      writeChars(((Byte[]){ aParenRight, aSemicolon }), cg);

      // loop over the elements of a list
      Int t = j + 3; // skipping the nodAss, nodVar and nodDataAlloc
      for (Int e = 0; e < countElts; e++) {
         writeNewline(cg);
         writeCopy(localNameStart, localNameLen, cg); // local var name
         writeChars(((Byte[]){ aBracketLeft }), cg);
         writeInt(e, cg);
         writeChars(((Byte[]){ aBracketRight, aSpace, aEqual, aSpace }), cg);
         Node eltNd = ast[t];
         if (eltNd.tp != nodExpr) {
            writeExprOperand(eltNd, cm->sourceLocs->cont[t], cg);
            t++;
         } else {
            Int exprLen = eltNd.pl2;
            cgExpr(t + 1, t + exprLen, false, ast, cg);
            t += exprLen + 1;
         }
         writeChars(((Byte[]){ aSemicolon }), cg);
      }
   }
   return j;
}

void //:writeExpr
writeExpr(Int start, Int endInclusive, Bool isComplex, AST, CG) {
// Code generate for a complex expression. Start is the ind of first node after nodExpr.
// isComplex = it has internal assignments before the main expression.
   CgExpr cge = cg->cgExpr;
   if (isComplex) {
      cgeComplexSubexpressions(ast[start - 1], start, ast, cg);
      if (cg->i == endInclusive) // a complex expr where main part is just 1 node, like `[1 2]`
         { return; }
   }
   cge.start = start;
   cge.endInclusive = endInclusive;
   if (start == endInclusive) {
      Node nd = ast[start];
      if (nd.tp == nodCall) {
         Function fn = cg->cm->functions.cont[nd.pl1];
         if (fn.emit == emitPrefix) {
            writeName(fn.name, cg);
         } else if (fn.emit == emitHostPrefix) {
            writeHostConstant(fn.hostName, false, cg);
         }
         writeChar(aParenLeft, cg);
         writeChar(aParenRight, cg);
      }
   }
   cgeResolveCallSpans(ast, cge, cg->cm);
   cgeGenerate(cge, ast, cg);
}

//}}}

private void //:writeExprProcessFirstArg
writeExprProcessFirstArg(Call* top, CG) {
   if (top->countArgs != 1)
      { return; }
   switch (top->emit) {
   case emitField:
      writeChar(aDot, cg);
      writeConstant(top->startInd, cg); return;
   case emitInfix:
      writeChar(aSpace, cg);
      writeBytes(cg->sourceCode.cont + top->startInd, top->len, cg);
      writeChar(aSpace, cg); return;
   case emitHostInfix:
      writeChar(aSpace, cg);
      writeConstant(top->startInd, cg);
      writeChar(aSpace, cg); return;
   }
}

private void //:writeExprInternal
writeExprInternal(Node nd, Int sentinel, Arr(Node const) ast, CG) {
// Consumes no nodes
// Precondition: we are looking 1 past the nodExpr/singular node. Consumes all nodes of the expr
   if (nd.tp <= topVerbatimType) {
      SourceLoc loc = cg->cm->sourceLocs->cont[cg->i - 1];
      writeBytes(cg->sourceCode.cont + loc.startBt, loc.lenBts, cg);
   } else if (nd.tp == nodVar) {
      writeVar(nd, cg);
   } else {
      cgExpr(cg->i, sentinel - 1, nd.pl1 > 0, ast, cg);
   }
}

private void //:writeExpr
writeExpr(Node nd, Arr(Node const) ast, CG) {
   writeNewline(cg);
   Int const sentinel = calcNodeSentinel(nd, cg->i - 1);
   writeExprInternal(nd, sentinel, ast, cg);
   writeChars(((Byte[]){ aSemicolon }), cg);
   cg->i = sentinel;
}

private void //:openFrame
openFrame(Node nd, CG) {
   pushFrame(
      ((Frame){
         .tp = nd.tp, .pl1 = nd.pl1, .pl3 = nd.pl3, .sentinel = calcNodeSentinel(nd, cg->i - 1)}
      ),
      &cg->backtrack
   );
}

private void //:openFrameWithSentinel
openFrameWithSentinel(Node nd, Int sentinel, CG) {
   pushFrame(
      ((Frame){ .tp = nd.tp, .pl1 = nd.pl1, .pl3 = nd.pl3, .sentinel = sentinel}),
      &cg->backtrack
   );
}


private void //:writeDummy
writeDummy(Node fr, Bool isEntry, Arr(Node const) ast, CG) {

}

private void //:writeVarNode
writeVarNode(CM, CG) {
// Write a node being pointed to. The node must be a nodVar
   Node varNd = cm->ast.cont[cg->i];
   Var theVar = cm->vars.cont[varNd.pl1];
   if (varNd.pl3 == assiVarAssignment) {
      Int class = theVar.class;
      if (class == classImm) {
         writeHostConstant(hostConst, true, cg);
      } else {
         writeHostConstant(hostLet, true, cg);
      }
   }

   SourceLoc loc = cm->sourceLocs->cont[cg->i];
   writeBytes(cg->sourceCode.cont + loc.startBt, loc.lenBts, cg);
}

private void //:assignmentLeft
assignmentLeft(Int leftSentinel, Arr(Node const) ast, CG) {
// Writes the left side & equals sign
   Node leftNd = ast[cg->i];
   if (leftNd.tp == nodVar) {
      writeVarNode(cg->cm, cg);
   } else if (leftNd.tp == nodExpr) {
      cg->i += 1; // CONSUME the nodExpr
      writeExprInternal(leftNd, calcNodeSentinel(leftNd, cg->i - 1), ast, cg);
   }
   writeChars(((Byte[]){aSpace, aEqual, aSpace}), cg);
}

private void //:assignmentRight
assignmentRight(Node rightNode, Int sentinel, Bool needSemicolon, Bool isComplex,
                  Arr(Node const) ast, CG) {
   if (cg->i == sentinel) {
      writeExprOperand(rightNode, cg->cm->sourceLocs->cont[cg->i - 1], cg);
   } else {
      // the "start" here is the actual expression start (so for complex expressions, the inner
      // assignments have been skipped). Correspondingly, we pass "isComplex = false" here:
      // the inner assignments have already been emitted.
      Int start = cg->i;
      if (isComplex) {
         for(; start < sentinel && ast[start].tp == nodAssignment;
               start = calcNodeSentinel(ast[start], start)
         ) {}
      }
      if (start == 29 && sentinel - 1 == 121) {
         print("HOROO");
      }
      cgExpr(start, sentinel - 1, false, ast, cg);
   }
   if (needSemicolon)
      { writeChar(aSemicolon, cg); }
}

private void //:assignmentWorker
assignmentWorker(Bool onNewLine, Bool needSemicolon, Node nd, Arr(Node const) ast, CG) {
// Pre-condition: we are looking at the binding node, 1 past the assignment node
// Consumes the whole assignment
   if (nd.pl2 == 0)
      { return; }
   Int const sentinel = cg->i + nd.pl2;
   Int const rightNodeInd = cg->i + nd.pl3 - 1;
   Node const rightNode = ast[rightNodeInd];
   if (nd.pl2 == 1 && ast[cg->i].pl3 == assiFnVarDef)
      { goto end; }
   Int innerExprInd = rightNodeInd + 1; // for complex expressions, will skip the inner assigns

   // if the right side is a complex expression, we need to write out its assignment sub-parts now
   if (rightNode.tp == nodExpr && rightNode.pl1 > 0)
      { innerExprInd = cgeComplexSubexpressions(rightNode, rightNodeInd, ast, cg); }
   if (onNewLine)
      { writeNewline(cg); }

   cgAssignmentLeft(rightNodeInd, ast, cg);
   cg->i = innerExprInd;
   if (sentinel == 122) {
      print("i %d pl2 %d sent %d", cg->i, nd.pl2, sentinel);
   }
   cgAssignmentRight(ast[rightNodeInd], sentinel, needSemicolon, false, ast, cg);
   end:
   cg->i = sentinel;
}

private void //:writeAssignment
writeAssignment(Node nd, Arr(Node const) ast, CG) {
// Pre-condition: we are looking at the binding node, 1 past the assignment node
   cgAssignmentWorker(true, true, nd, ast, cg);
}

private void //:writeDataAlloc
writeDataAlloc(Node fr, Arr(Node const) ast, CG) {
   // TODO
   Int const sentinel = calcNodeSentinel(fr, cg->i - 1);
   cg->i = sentinel; // CONSUME the whole assignment
}

private void //:writeAssert
writeAssert(Node fr, Arr(Node const) ast, CG) {
   // TODO
   Int const sentinel = calcNodeSentinel(fr, cg->i - 1);
   cg->i = sentinel; // CONSUME the whole assignment
}

private void //:cgReturn
writeReturn(Node fr, Arr(Node const) ast, CG) {
   Int sentinel = cg->i + fr.pl2;

   writeNewline(cg);
   if (cg->i == sentinel)  {
      writeConstant(strReturn, cg);
      writeChar(aSemicolon, cg);
      return;
   }

   writeConstantWithSpace(strReturn, cg);
   Node rightSide = ast[cg->i];
   cg->i += 1; // CONSUME the expr node

   writeExprInternal(rightSide, sentinel, ast, cg);

   writeChar(aSemicolon, cg);
   cg->i = sentinel; // CONSUME the whole "return" statement
}

private void //:writeScope
writeScope(Node nd, Arr(Node const) ast, CG) {
}

private void //:writeIfClause
writeIfClause(Node nd, Arr(Node const) ast, CG) {
   openFrame(nd, cg);

   writeChar(aSpace, cg);

   if (nd.pl3 == ifclElse) {
      writeHostConstant(hostElse, true, cg);
   } else {
      if (nd.pl3 == ifclElseIf) {
         writeHostConstant(hostElse, true, cg);
         writeConstantWithSpace(strIf, cg);
      }
      writeChar(aParenLeft, cg);

      Node expression = ast[cg->i];
      Int exprSentinel = calcNodeSentinel(expression, cg->i);
      cg->i += 1; // CONSUME the nodExpr
      writeExprInternal(expression, calcNodeSentinel(expression, cg->i - 1), ast, cg);
      cg->i = exprSentinel; // CONSUME the if of "else if" condition
   }

   if (nd.pl3 == ifclElse) {
      writeChar(aCurlyLeft, cg);
   } else {
      writeChars(((Byte[]){ aParenRight, aSpace, aCurlyLeft }), cg);
   }

}

private void //:writeIf
writeIf(Node nd, Arr(Node const) ast, CG) {
   openFrame(nd, cg);
   writeNewline(cg);
   writeConstant(strIf, cg);
}

private void //:writeMatch
writeMatch(Node nd, Arr(Node const) ast, CG) {
}

private void //:writeLoopLabel
writeLoopLabel(Int labelId, CG) {
   writeConstant(hostLo, cg);
   cgEnsureBufferLength(14, cg);
   Int lenWritten = sprintf(cg->buffer + cg->len, "%d", labelId);
   cg->len += lenWritten;
}

void //:preambleFor
preambleFor(
   Int sentinel, Int skipToBody, Arr(Node const) ast, CG,
   OUT Int* initCount, OUT Int* condInd, OUT Int* stepCount, OUT Int* stepInd, OUT Int* bodyInd
) {
// Precondition: 1 past the nodFor

   Int j = cg->i;
   *bodyInd = cg->i + skipToBody - 1;
   for (; ast[j].tp != nodExpr; j = calcNodeSentinel(ast[j], j)) {
      (*initCount)++;
   }
   *condInd = j;
   j = calcNodeSentinel(ast[j], j);
   if (j < *bodyInd) {
      *stepInd = j;
      for (; j < *bodyInd; j = calcNodeSentinel(ast[j], j)) {
         (*stepCount)++;
      }
   }
}

private void //:writeFor
writeFor(Node nd, Arr(Node const) ast, CG) {
   Int const sentinel = calcNodeSentinel(nd, cg->i - 1);
   Int initCount = 0;
   Int condInd = 0;
   Int stepCount = 0;
   Int stepInd = 0;
   Int bodyInd = 0;
   cgPreambleFor(sentinel, nd.pl3, ast, cg,
                 OUT &initCount, OUT &condInd, OUT &stepCount, OUT &stepInd, OUT &bodyInd);
   if (initCount > 1) { // create a special scope that the loop will be nested in
      writeNewline(cg);
      writeChar(aCurlyLeft, cg);
      openFrameWithSentinel(((Node){.tp = nodScope, .pl2 = nd.pl2 - 1}), sentinel, cg);

      for (; cg->i < condInd;) {
         Node initNd = ast[cg->i];
         cg->i++; // CONSUME the nodAssignment
         cgAssignmentWorker(true, true, initNd, ast, cg);
      }
   }
   writeNewline(cg);
   writeConstant(strFor, cg);

   writeChars(((Byte[]){ aSpace, aParenLeft }), cg);
   if (initCount > 1) {
      writeChars(((Byte[]){ aSemicolon, aSpace }), cg);
   }

   openFrameWithSentinel(nd, sentinel, cg);
   if (initCount == 1) {
      Node initNd = ast[cg->i];
      cg->i++; // CONSUME the nodAssignment
      cgAssignmentWorker(false, true, initNd, ast, cg);
      writeChar(aSpace, cg);
   }
   cg->i = condInd + 1;

   // loop condition
   Node exprNd = ast[condInd];
   writeExprInternal(exprNd, calcNodeSentinel(exprNd, condInd), ast, cg);
   writeChar(aSemicolon, cg);
   writeChar(aSpace, cg);

   // loop steps
   if (stepCount > 0) {
      for ( cg->i = stepInd + 1; cg->i < bodyInd; ) {
         Node currNd = ast[cg->i - 1];
         if (currNd.tp == nodAssignment) {
            cgAssignmentWorker(false, false, currNd, ast, cg);
         } ei (currNd.tp == nodExpr)  {
            Int exprSentinel = calcNodeSentinel(currNd, cg->i - 1);
            writeExprInternal(currNd, exprSentinel, ast, cg);
            cg->i = exprSentinel;
         } else { // TODO assert
            cg->i = calcNodeSentinel(currNd, cg->i - 1) + 1;
         }
         if (stepCount > 1)
            { writeChars(((Byte[]){ aComma, aSpace }), cg); }
         stepCount--;
      }
   }
   writeChars(((Byte[]){ aParenRight, aSpace, aCurlyLeft }), cg);
   cg->i = MIN(bodyInd + 1, sentinel); // CONSUME everything till the body, and the opening scope
}

private void //:writeBreakCont
writeBreakCont(Node fr, Arr(Node const) ast, CG) {
   writeNewline(cg);
   if (fr.pl1 == -1) {
      writeConstant(strBreak, cg);
   } else if (fr.pl1 == BIG - 1)     {
      writeConstant(strContinue, cg);
   } else {
      Int loopInd = fr.pl1;
      if (loopInd >= BIG) {
         writeConstantWithSpace(strContinue, cg);
         loopInd -= BIG;
      } else {
         writeConstantWithSpace(strBreak, cg);
      }
      writeLoopLabel(loopInd, cg);
   }
   writeChar(aSemicolon, cg);
   writeChar(aNewline, cg);
}

private void //:writeTry
writeTry(Node fr, Arr(Node const) ast, CG) {
   writeNewline(cg);
   writeConstantWithSpace(strTry, cg);
   writeChar(aCurlyLeft, cg);
   writeChar(aNewline, cg);
}

private void //:writeCatch
writeCatch(Node fr, Arr(Node const) ast, CG) {
   writeNewline(cg);
   writeConstantWithSpace(strCatch, cg);
   writeChar(aCurlyLeft, cg);
   writeChar(aNewline, cg);
}

private void //:writeFnDef
writeFnDef(Node nd, Arr(Node const) ast, CG) {
//~   Compiler const* restrict cm = cg->cm;
//~   SourceLoc loc = cm->sourceLocs->cont[cg->i - 1];
//~   openFrame(nd, cg);
//~   writeNewline(cg);
//~   Function fnEnt = cg->cm->functions.cont[nd.pl1];
//~
//~   writeBytesFromSource(loc, cg);
//~   if (fnEnt.emit == emitPrefix)
//~      { writeChar(aUnderscore, cg); }
//~
//~   writeChar(aParenLeft, cg);
//~   Int sentinel = cg->i + nd.pl2;
//~   Int j = cg->i + 2; // +2 to skip the function binding node and nodScope
//~   // first param
//~   if (nodes[j].tp == nodVar) {
//~      SourceLoc bindingLoc = cm->sourceLocs->cont[j];
//~      writeBytes(cg->sourceCode.cont + bindingLoc.startBt, bindingLoc.lenBts, cg);
//~      ++j;
//~   }
//~
//~   // function params
//~   print("params j %d", j)
//~   while (j < sentinel && nodes[j].tp == nodVar && nodes[j].pl3 == assiFnParam) {
//~      writeChar(aComma, cg);
//~      writeChar(aSpace, cg);
//~      SourceLoc bindingLoc = cm->sourceLocs->cont[j];
//~      writeBytes(cg->sourceCode.cont + bindingLoc.startBt, bindingLoc.lenBts, cg);
//~      j += 1;
//~   }
//~   cg->i = j;
//~   writeChars(((Byte[]){aParenRight, aSpace, aCurlyLeft, aNewline}), cg);
}


private void //:writeDef
writeDef(Node nd, Arr(Node const) ast, CG) {
// TODO
}

private void //:writeTrait
writeTrait(Node nd, Arr(Node const) ast, CG) {
// TODO
}


private void //:writeImpl
writeImpl(Node nd, Arr(Node const) ast, CG) {
// TODO
}

private void //:maybeCloseFrames
maybeCloseFrames(CG) {
   for (Int j = cg->backtrack.len - 1; j > -1 && cg->backtrack.cont[j].sentinel == cg->i; j -= 1) {
      Frame fr = popFrame(&cg->backtrack);
      if (fr.tp == nodIf) {
         continue;
      }
      writeNewline(cg);
      writeChar(aCurlyRight, cg);
   }
}

private void //:writeToplevelFn
writeToplevelFn(FunctionId toplevelId, CM, CG) {
   Function fn = cm->functions.cont[toplevelId];
   if (fn.genericInd != -1 || fn.tokenInd == -1) // generic or imported fn
      { return; }

   TypeHeader typeHdr = typeReadHeader(fn.typeId, cm);
   Arr(Node const) ast = cm->ast.cont;
   Int countParams = typeHdr.arity - 1; 
   for (Int t = fn.typeId.v + TYPE_PREFIX_LEN; t < fn.typeId.v + TYPE_PREFIX_LEN + arity; t++) {
      add(cm->types.cont[t], cg->params);
   }
   
   
   // create all the params 
   Fn* newToplevel = newFn(
      fn.name,
      GCC_JIT_FUNCTION_EXPORTED,
      countParams,
      &cg->params->cont,
      cg->primitives.voidTp,
      cg->ctx
   );
   

   
   
   TypeHeader hdr = typeReadHeader(fn.typeId, cm);
   if (hdr.arity != 2 || cm->types.cont[fn.typeId.v + TYPE_PREFIX_LEN] != voidType) {
      writeChar(aUnderscore, cg);
      writeInt(toplevelId, cg);
   }

   Node nodeFn = ast[fn.nodeInd];
   Int const sentinel = calcNodeSentinel(nodeFn, fn.nodeInd);
   pushFrame(
      ((Frame){ .tp = nodFnDef, .pl1 = nodeFn.pl1, .sentinel = sentinel}),
      &cg->backtrack
   );
   cg->local = 0;

   cg->i = fn.nodeInd + 1;

   // first param
   Node paramNd = ast[cg->i];
   if (paramNd.tp == nodVar && paramNd.pl3 == assiFnParam) {
      writeVarNode(cm, cg);
      cg->i += 1;
      paramNd = ast[cg->i];
   }

   // function params
   for ( ;
         cg->i < sentinel && paramNd.tp == nodVar && paramNd.pl3 == assiFnParam;
         cg->i += 1, paramNd = ast[cg->i]
   ) {
      writeChars(((Byte[]){ aComma, aSpace }), cg);
      writeVarNode(cm, cg);
   }
   writeChars(((Byte[]){ aParenRight, aSpace, aCurlyLeft }), cg);

   for (; cg->i < sentinel;) {
      Node nd = cm->ast.cont[cg->i];
      cg->i += 1; // CONSUME the span node
      (CODEGEN_TABLE[nd.tp - nodScope])(nd, cm->ast.cont, cg);
      cgMaybeCloseFrames(cg);
   }
   cgMaybeCloseFrames(cg);

   writeChar(aNewline, cg);
}

private void //:generateMainCode
generateMainCode(CG) {
   Compiler* cm = cg->cm;
   for (int j = 0; j < cm->toplevels.len; j++) {
      toplevelFn(cm->toplevels.cont[j], cm, cg);
   }
}

private Codegen* //:generateCode
generateCode(Compiler* cm, Arena* a) {
#ifdef TRACE
   printParser(cm);
#endif

   if (cm->stats.wasError)
      { return NULL; }

   Codegen* cg = createCodegen(cm, a);
   if (setjmp(excBuf) == 0) {
      generateMainCode(cg);
   } else {
#ifndef TEST
      print("Codegen Exception!");
      cg->wasError = true;
#endif
   }
   return cg;
}

//}}}
//{{{ Debug & test utils

void
dbgCgFrames(Codegen* cg) {
   printf("CgFrames [");
   if (cg->backtrack.len == 0) {
      goto closing;
   }
   printf("%d ", cg->backtrack.cont[0].tp);
   for (Int i = 1; i < cg->backtrack.len; i++) {
      printf("%d ", cg->backtrack.cont[i].tp);
   }
   closing:
   printf("]\n");
}

//}}}
//{{{ Main

void //:saveCompiledCode
saveCompiledCode(String output, String sourceFName, Arena* a) {
   Arr(char) outFNameBuf = allocateArray(sourceFName.len + 2, char, a);

   memcpy(outFNameBuf, sourceFName.cont, sourceFName.len - 3);
   outFNameBuf[sourceFName.len - 3] = 'h';
   outFNameBuf[sourceFName.len - 2] = 't';
   outFNameBuf[sourceFName.len - 1] = 'm';
   outFNameBuf[sourceFName.len    ] = 'l';
   outFNameBuf[sourceFName.len + 1] = '\0';
   FILE* outF = fopen(outFNameBuf, "wt");
   if (!outF) {
      printf("Error writing to file %s", outFNameBuf);
      return;
   }
   fprintf(outF, TEMPLATE_HTML_OPEN);
   fprintf(outF, output.cont);
   fprintf(outF, TEMPLATE_HTML_CLOSE);
   fclose(outF);
}


Int //:tech_sozonov_eyr_compileFile
tech_sozonov_eyr_compileFile(String filename) {
   if (filename.len == 0)
      { return 1; }
   initCompiler();

   Arena* a = createArena();
   String sourceCode = readSourceFile(filename, a);
   Compiler* cm = lexicallyAnalyzeFromFile(sourceCode, a);
   if (cm->stats.wasLexerError) {
      print("lexer error");
      printString(cm->stats.errMsg);
      return 1;
   }
   cm = parse(cm, a);
   if (cm->stats.wasError) {
      print("parse error");
      printString(cm->stats.errMsg);
      return 1;
   }
   Codegen* cg = generateCode(cm, a);
   if (cg->wasError) {
      print("codegen error");
      return 1;
   }
   String output = (String){.len = cg->len, .cont = cg->buffer};
   saveCompiledCode(output, filename, a);
   deleteArena(a);
   return 0;
}

String //:tech_sozonov_eyr_compile
tech_sozonov_eyr_compile(String sourceCode) {
   if (sourceCode.len == 0)
      { return empty; }

   initCompiler();
   Arena* a = createArena();
   Compiler* cm = lexicallyAnalyze(sourceCode, a);
   if (cm->stats.wasLexerError) {
#if defined(DEBUG)
      printString(cm->stats.errMsg);
#endif
      return str("lexer error");
   }

   cm = parse(cm, a);
   if (cm->stats.wasError) {

#if defined(DEBUG)
   printString(cm->stats.errMsg);
#endif
      return str("parse error");
   }
   Codegen* cg = generateCode(cm, a);
   return (String){.len = cg->len, .cont = cg->buffer};
}


private void
displayHelp() {
   printf("Eyr compiler. Usage:\n\neyrc file.eyr\n\nor\n\neyrc folder\n");
}

Int //:main
main(int argc, char** argv) {
   initCompiler();

//~   Arena* a = createArena();
//~   Compiler* cm = createLexer(empty, false, a);
//~   initializeParser(cm, a);
//~
//~   scopesNewLexicalScope(cm);
//~   addBinding(1, 1, cm);
//~   addBinding(2, 2, cm);
//~   scopesNewLexicalScope(cm);
//~   addBinding(3, 3, cm);
//~   addBinding(4, 4, cm);
//~   addBinding(5, 5, cm);
//~   addBinding(6, 6, cm);
//~
//~   rewindLexicalScope(cm);
//~   rewindLexicalScope(cm);
//~
//~   printIntArray(6, cm->scopes.currChunk->cont);
//~   dbgScopes(cm);
//~   return 0;


   if (argc == 1) {
      displayHelp();
      return 0;
   }
   for (Int i = 1; i < argc; i++) {
      String fName = stringOf(argv[i]);
      if (tech_sozonov_eyr_compileFile(fName) == 0) {
         print("compiled file: ");
         printString (fName);
      }
   }

   cleanup:


   return 0;
}

//}}}
