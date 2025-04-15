//{{{ Includes

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <setjmp.h>
#include "include/libeyr.h"
#include "_target/libgccjit.h"

extern jmp_buf excBuf;

//}}}
//{{{ Forward decls, generics & utils

typedef gcc_jit_param FnParam;
typedef gcc_jit_type CgType;
typedef gcc_jit_function Fn;
typedef gcc_jit_field Field;
typedef gcc_jit_block CodeBlock;
typedef gcc_jit_context Module;
typedef gcc_jit_result CgResult;
typedef gcc_jit_lvalue LValue;
typedef gcc_jit_rvalue RValue;
typedef enum gcc_jit_function_kind FnKind;
typedef enum gcc_jit_types BuiltinType;
typedef enum gcc_jit_comparison BuiltinComparison;
#define pointerOf(x) gcc_jit_type_get_pointer(x)


#define AST Arr(Node const) const restrict ast // Source text
#define SRC Arr(char const) const restrict source // Source text
#define CR CompResult const* const restrict cr // Compilation results
private void closeStatement(LX);
private NameId nameOfStandard(Int a);

typedef FnParam* FnParamPtr;
typedef Field* FieldPtr;
typedef Fn* FnPtr;

DEFINE_LIST_HEADER(FnParamPtr)
DEFINE_LIST_HEADER(FieldPtr)

#define add(A, X) _Generic((X),\
   LInt*: addInt,\
   LUnt*: addUnt,\
   LUlong*: addUlong,\
   LNode*: addNode,\
   LSourceLoc*: addSourceLoc,\
   LFnParamPtr*: addFnParamPtr,\
   LRValuePtr*: addRValuePtr,\
   LFutureBlock*: addFutureBlock,\
   LBtLoop*: addBtLoop\
)(A, X)

#define removeLast(X) _Generic((X),\
   LInt*: removeLastInt,\
   LUnt*: removeLastUnt,\
   LUlong*: removeLastUlong,\
   LNode*: removeLastNode,\
   LSourceLoc*: removeLastSourceLoc,\
   LFnParamPtr*: removeLastFnParamPtr,\
   LRValuePtr*: removeLastRValuePtr,\
   LFutureBlock*: removeLastFutureBlock,\
   LBtLoop*: removeLastBtLoop\
)(X)

DEFINE_LIST(Int)
DEFINE_LIST(Unt)
DEFINE_LIST(Ulong)
DEFINE_LIST(Node)
DEFINE_LIST(FnParamPtr)
DEFINE_LIST(FieldPtr)
DEFINE_LIST(SourceLoc)

//{{{ Types & constants

#define fraIf     1
#define fraElse   2
#define fraFor    3

typedef struct { //:TypeRef Codegenned type and index of Eyr type (index into @Compiler.types)
   Int ind;
   CgType* cgType;
} TypeRef;

#define bloCommon        0 // common blocks. nextBlock = afterBlock
#define bloIf            1 // if conditions. nextBlock = next "else if"/"else"
#define bloLoopCond      2 // loop conditions. nextBlock = afterBlock
#define bloLoopBody      3 // loop bodies. nextBlock = bloLoopCond

typedef struct { //:TypedBlock
   Byte tp; // the "blo" constants above
   CodeBlock* c;
} TypedBlock;

typedef struct { //:CurrBlock
   Int start; // start node ind
   Int end; // end node ind, exclusive
   TypedBlock c;
   Fn* fn; // the function we are in
   NULLABLE TypedBlock nextBlock;
   NULLABLE TypedBlock afterBlock;
} CurrBlock;

typedef struct { //:FutureBlock
   Int start;
   TypedBlock c;
} FutureBlock;

DEFINE_LIST_HEADER(FutureBlock)
DEFINE_LIST(FutureBlock)

typedef CodeBlock* CodeBlockPtr;
typedef RValue* RValuePtr;

typedef struct { //:BtLoop
   CodeBlock* condition; // used for "continue" implementation
   Int sentinel; // node index of end, exclusive
} BtLoop;

DEFINE_LIST_HEADER(CodeBlockPtr)
DEFINE_LIST_HEADER(FnPtr)
DEFINE_LIST_HEADER(RValuePtr)
DEFINE_LIST_HEADER(BtLoop)
DEFINE_LIST(CodeBlockPtr)
DEFINE_LIST(FnPtr)
DEFINE_LIST(RValuePtr)
DEFINE_LIST(BtLoop)


typedef struct { //:Codegen
   Int i; // current node index
   CurrBlock cbl; // no relation to Carbon-Based Lifeforms
   LFutureBlock* futureBlocks;
   LBtLoop* bt;// backtrack of loop conditions, used for "continue" block linking

   Module* md;

   Int bufferLen;
   Byte buffer[maxWordLength + 1]; // temporary buffer for name writing

   Arr(Fn*) functions; // same len as @compResult.functions
   Arr(RValue*) vars; // same len as @compResult.vars

   LFnParamPtr* params; // temporary buffer for function params
   LRValuePtr* exp; // temporary buffer for expression evaluation
   LFieldPtr* fields; // temporary buffer for struct fields

   Int countTypeRefs;
   Arr(TypeRef) typeRefs; // links between Eyr types and GCC types

   CompResult compResult; // results of the compilation from libeyr

   Arena* a;
   Bool wasError;
} Codegen;

//}}}
//{{{ Utils

private String
stringOf(Arr(char) cString) {
   return (String){.c = cString, .len = strlen(cString) };
}

//}}}
#define CG Codegen* restrict cg
private void prepareName(NameId name, CG);
private CgType* cgType(TypeId tp, CG);
private CgType* intType(CG);
private CgType* voidType(CG);
private CgType* longType(CG);
private CgType* boolType(CG);
private CgType* doubleType(CG);


#if defined(DEBUG) || defined(TEST)

void printIntArray(Int count, Arr(Int) arr);
void printParser(Compiler* cm);
void dbgType0(TypeId type, CM);
#define dbgType(t) dbgType0(t, cm)
private void printLInt(LInt* st);

#endif

//}}}
//{{{ GCC wrapper functions

private void //:assignment
assignment(LValue* left, RValue* right, CodeBlock* block) {
   gcc_jit_block_add_assignment(block, NULL, left, right);
}

private Fn* //:importFn
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

private Fn* //:newFnReal
newFnReal(NameId name, NULLABLE LFnParamPtr* params, CgType* returnType, FnKind accessLevel, CG) {
   prepareName(name, cg);
   return gcc_jit_context_new_function(
      cg->md,
      NULL, // source location
      accessLevel,
      returnType,
      cg->buffer,
      params->len,
      params->c,
      0
   );
}

private Fn* //:newFn
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

private RValue* //:callParsed
callParsed(Fn* fn, int countArgs, Arr(RValue*) args, Module* md) {
   return gcc_jit_context_new_call(md, NULL, fn, countArgs, args);
}

private void //:evalExpr
evalExpr(RValue* rValue, CodeBlock* block) {
   gcc_jit_block_add_eval(block, NULL, rValue);
}

private CodeBlock* //:newBlock
newBlock(Fn* fn) {
   return gcc_jit_function_new_block(fn, NULL);
}

private void
returnFromBlock(RValue* retValue, CodeBlock* bl) {
   gcc_jit_block_end_with_return(bl, NULL, retValue);
}

private void //:returnVoid
returnVoid(CodeBlock* bl) {
   gcc_jit_block_end_with_void_return(bl, NULL);
}

private void //:jump
jump(CodeBlock* from, CodeBlock* to) {
   gcc_jit_block_end_with_jump (from, NULL, to);
}

private void //:conditional
conditional(CodeBlock* from, CodeBlock* toIfTrue, CodeBlock* toIfFalse, RValue* condition) {
   gcc_jit_block_end_with_conditional(from, NULL, condition, toIfTrue, toIfFalse);
}

private RValue* //:comparison
comparison(RValue* a, BuiltinComparison operator, RValue* b, Module* md) {
   return gcc_jit_context_new_comparison(md, NULL, operator, a, b);
}

private CodeBlock* //:newNamedBlock
newNamedBlock(Fn* fn, char const* name) {
   return gcc_jit_function_new_block(fn, name);
}

private RValue* //:strConst
strConst(char const* val, Module* md) {
    return gcc_jit_context_new_string_literal(md, val);
}

private LValue* //:arrElem
arrElem(RValue* arr, RValue* index, Module* md)  {
   return gcc_jit_context_new_array_access(md, NULL, arr, index);
}

private RValue* //:rValueOf
rValueOf(LValue* lvalue) {
   return gcc_jit_lvalue_as_rvalue(lvalue);
}

private RValue* //:ptrCast
ptrCast(RValue* v, CgType* tp, Module* md) {
   return gcc_jit_context_new_cast(md, NULL, v, tp);
}

private LValue* //:localVar
localVar(const char* name, CgType* tp, Fn* fn) {
   return gcc_jit_function_new_local(fn, NULL, tp, name);
}

private FnParam* //:param
param(NameId nameId, TypeId tp, CG) {
   prepareName(nameId, cg);
   return gcc_jit_context_new_param(cg->md, NULL, cgType(tp, cg), cg->buffer);
}

private FnParam* //:param
paramFromChars(char const* s, TypeId tp, CG) {
   return gcc_jit_context_new_param(cg->md, NULL, cgType(tp, cg), s);
}

private CgType* //:fnPointerType
fnPointerType(Int countParams, Arr(CgType*) paramTypes, CgType* returnTp, Module* md) {
   return gcc_jit_context_new_function_ptr_type (
      md,
	   null,
      returnTp,
      countParams,
      paramTypes,
	   0
   );
}

private RValue*
callFnPtr(RValue* fnPtr, Int countArgs, Arr(RValue*) args, Module* md) {
   return gcc_jit_context_new_call_through_ptr(md, null, fnPtr, countArgs, args);
}

#define builtinBinary(op, retType, arg1, arg2) gcc_jit_context_new_binary_op(\
   cg->md, null, op, retType, arg1, arg2)
#define builtinUnary(op, retType, arg1) gcc_jit_context_new_unary_op(\
   cg->md, null, op, retType, arg1)
#define builtinCompare(op, arg1, arg2) gcc_jit_context_new_comparison(\
   cg->md, null, op, arg1, arg2)

private RValue* //:eCall
eCall(FunctionId fnId, Int countArgs, Arr(RValue*) args, CG) {
// Handles all calls in expressions. Does NOT change the expression stack
   Function fn = cg->compResult.functions.c[fnId];
   Int eyrRetType = cg->compResult.types.c[fn.typeId.v + TYPE_PREFIX_LEN + countArgs];
   CgType* retType = cgType(typeOf(eyrRetType), cg);

   switch (fn.emit) {
   case emitParsed: {
      return callParsed(cg->functions[fnId], countArgs, args, cg->md);
   }
   case emitAdd: {
      return builtinBinary(GCC_JIT_BINARY_OP_PLUS, retType, args[0], args[1]);
   }
   case emitSubtract: {
      return builtinBinary(GCC_JIT_BINARY_OP_MINUS, retType, args[0], args[1]);
   }
   case emitMultiply: {
      return builtinBinary(GCC_JIT_BINARY_OP_MULT, retType, args[0], args[1]);
   }
   case emitDivide: {
      return builtinBinary(GCC_JIT_BINARY_OP_DIVIDE, retType, args[0], args[1]);
   }
   case emitModulo: {
      return builtinBinary(GCC_JIT_BINARY_OP_MODULO, retType, args[0], args[1]);
   }
   case emitNegate: {
      return builtinUnary(GCC_JIT_UNARY_OP_MINUS, retType, args[0]);
   }
   case emitAbsolute: {
      return builtinUnary(GCC_JIT_UNARY_OP_ABS, retType, args[0]);
   }
   case emitLogicAnd: {
      return builtinBinary(GCC_JIT_BINARY_OP_LOGICAL_AND, boolType(cg), args[0], args[1]);
   }
   case emitLogicOr: {
      return builtinBinary(GCC_JIT_BINARY_OP_LOGICAL_OR, boolType(cg), args[0], args[1]);
   }
   case emitLogicNegate: {
      return builtinUnary(GCC_JIT_UNARY_OP_LOGICAL_NEGATE, retType, args[0]);
   }
   case emitBitAnd: {
      return builtinBinary(GCC_JIT_BINARY_OP_BITWISE_AND, retType, args[0], args[1]);
   }
   case emitBitOr: {
      return builtinBinary(GCC_JIT_BINARY_OP_BITWISE_OR, retType, args[0], args[1]);
   }
   case emitBitXor: {
      return builtinBinary(GCC_JIT_BINARY_OP_BITWISE_XOR, retType, args[0], args[1]);
   }
   case emitBitNegate: {
      return builtinUnary(GCC_JIT_UNARY_OP_BITWISE_NEGATE, retType, args[0]);
   }
   case emitBitLeftShift: {
      return builtinBinary(GCC_JIT_BINARY_OP_LSHIFT, retType, args[0], args[1]);
   }
   case emitBitRightShift: {
      return builtinBinary(GCC_JIT_BINARY_OP_RSHIFT, retType, args[0], args[1]);
   }
   case emitEq: {
      return builtinCompare(GCC_JIT_COMPARISON_EQ, args[0], args[1]);
   }
   case emitNotEq: {
      return builtinCompare(GCC_JIT_COMPARISON_NE, args[0], args[1]);
   }
   case emitLessThanOrEq: {
      return builtinCompare(GCC_JIT_COMPARISON_LE, args[0], args[1]);
   }
   case emitLessThan: {
      return builtinCompare(GCC_JIT_COMPARISON_LT, args[0], args[1]);
   }
   case emitGreaterThan: {
      return builtinCompare(GCC_JIT_COMPARISON_GT, args[0], args[1]);
   }
   case emitGreaterThanEq: {
      return builtinCompare(GCC_JIT_COMPARISON_GE, args[0], args[1]);
   }
   case emitPrint: {
      return builtinUnary(GCC_JIT_UNARY_OP_BITWISE_NEGATE, retType, args[0]); // TODO
   }
   }
   return null; // unreachable
}

//}}}
//{{{ Generation table

typedef void (*CgFunc)(Node, Arr(Node const) const restrict, Codegen* restrict);
#define CG_FUN(fnName) static void fnName(Node nd, AST, CG)

// host string constants
#define hostFunction  0
#define hostElse      1
#define hostConst     2
#define hostLet       3
#define hostLo        4
#define hostNew       5
#define hostArray     6
#define hostPrint     7
#define hostAdd       8
#define hostLength    9
#define hostAbs      10


CG_FUN(writeScope); CG_FUN(writeExpr); CG_FUN(writeAssignment); CG_FUN(writeDataAlloc);
CG_FUN(writeAssert); CG_FUN(writeBreakCont); CG_FUN(writeTry); CG_FUN(writeCatch);
CG_FUN(writeFnDef); CG_FUN(writeDef);
CG_FUN(writeTrait); CG_FUN(writeImpl); CG_FUN(writeReturn);
CG_FUN(writeFor); CG_FUN(writeIf); CG_FUN(writeIfClause); CG_FUN(writeMatch);

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

//}}}
//}}}
//{{{ Type registry

private CgType* //:builtinType
builtinType(BuiltinType tp, Module* md) {
   return gcc_jit_context_get_type(md, tp);
}

private CgType* //:cgType
cgType(TypeId tp, CG) {
   return cg->typeRefs[tp.v].cgType;
}

private void //:registerTypes
registerTypes(CG) {
   // for every type in @cm.types, create an entry in @cg.typeRefs
   cg->countTypeRefs = tokMisc;
   cg->typeRefs = allocateArray(cg->countTypeRefs, TypeRef, cg->a);
   cg->typeRefs[tokInt] = (TypeRef){.ind = tokInt,
      .cgType = builtinType(GCC_JIT_TYPE_INT32_T, cg->md) };
   cg->typeRefs[tokLong] = (TypeRef){.ind = tokLong,
      .cgType = builtinType(GCC_JIT_TYPE_INT64_T, cg->md) };
   cg->typeRefs[tokBool] = (TypeRef){.ind = tokBool,
      .cgType = builtinType(GCC_JIT_TYPE_BOOL, cg->md) };
   cg->typeRefs[tokDouble] = (TypeRef){.ind = tokDouble,
      .cgType = builtinType(GCC_JIT_TYPE_DOUBLE, cg->md) };
   cg->typeRefs[tokMisc] = (TypeRef){.ind = tokMisc,
      .cgType = builtinType(GCC_JIT_TYPE_VOID, cg->md) };
}

private CgType* //:intType
intType(CG) {
   return cg->typeRefs[0].cgType;
}

private CgType* //:longType
longType(CG) {
   return cg->typeRefs[tokLong].cgType;
}

private CgType* //:boolType
boolType(CG) {
   return cg->typeRefs[tokBool].cgType;
}

private CgType* //:doubleType
doubleType(CG) {
   return cg->typeRefs[tokDouble].cgType;
}

//}}}
//{{{ Code generator
//{{{ Codegen utils

private RValue* //:intConst
intConst(int val, Codegen* cg) {
   return gcc_jit_context_new_rvalue_from_int(cg->md, cg->typeRefs[0].cgType, val);
}


private void //:intConst Writes a name from source code to codegen buffer, zero-terminated
prepareName(NameId nameId, CG) {
   NameLoc name = cg->compResult.names.c[name];
   memcpy(cg->buffer, cg->compResult.sourceCode.c + (name & LOWER24BITS), name >> 24);
   cg->bufferLen = (name & LOWER24BITS) + 1;
   cg->buffer[name >> 24] = '\0';
}

//}}}

private Codegen* //:createCodegen
createCodegen(CR, Arena* a) {
   Codegen* cg = allocate(Codegen, a);
   Module* md = gcc_jit_context_acquire();
   (*cg) = (Codegen) {
      .i = 0,
      .md = md,
      .bt = createLBtLoop(16, a),
      .compResult = *cr,
      .vars = allocateArray(cr->vars.len, RValue*, a),
      .a = a,
      .wasError = false
   };

   return cg;
}

void
init() {
   populateStringOffsets(hostStringLens, 0, sizeof(hostStringLens), OUT hostOffsets);
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

//~private void //:writeStr
//~writeStr(String str, CG) {
//~   cgEnsureBufferLength(str.len + 2, cg); // +2 for the quotation marks
//~   cg->buffer[cg->len] = aQuote;
//~   memcpy(cg->buffer + cg->len + 1, str.cont, str.len);
//~   cg->len += (str.len + 2);
//~   cg->buffer[cg->len - 1] = aQuote;
//~}

//~private void //:writeBytesFromSource
//~writeBytesFromSource(SourceLoc loc, CG) {
//~   writeBytes(cg->sourceCode.cont + loc.startBt, loc.lenBts, cg);
//~}

private void //:writeExprInternal
writeExprInternal(Node nd, Int sentinel, AST, CG) {
// Consumes no nodes
// Precondition: we are looking 1 past the nodExpr/singular node. Consumes all nodes of the expr
   LRValuePtr* exp = cg->exp;
   exp->len = 0;
   for (Int j = cg->i; j < sentinel; j++) {
      Node expNode = ast[j];
      switch (expNode.tp) {
         case tokInt: {
            Int value = expNode.pl2;
            add(intConst(value, cg), exp);
            break;
         }
         case nodVar: {
            add(cg->vars[expNode.pl1], exp);
            break;
         }
         case nodCall: {
            Int countArgs = expNode.pl2;
            //Int callTp = nd.pl3;
            RValue* callResult = eCall(expNode.pl1, countArgs, exp->c + exp->len - countArgs, cg);
            exp->len -= (countArgs - 1);
            exp->c[exp->len - 1] = callResult;
            break;
         }
      }
   }
}

private void //:writeExpr
writeExpr(Node nd, AST, CG) {
   Int const sentinel = calcNodeSentinel(nd, cg->i - 1);
   writeExprInternal(nd, sentinel, ast, cg);
   cg->i = sentinel;
}

private void //:openFrame
openFrame(Node nd, CG) {
}

private void //:openFrameWithSentinel
openFrameWithSentinel(Node nd, Int sentinel, CG) {
}


private void //:writeDummy
writeDummy(Node fr, Bool isEntry, Arr(Node const) ast, CG) {

}

private void //:writeVarNode
writeVarNode(CR, CG) {
// Write a node being pointed to. The node must be a nodVar
//~   Node varNd = cr->ast.c[cg->i];
//~   Var theVar = cr->vars.c[varNd.pl1];
//~   if (varNd.pl3 == assiVarAssignment) {
//~      Int class = theVar.class;
//~   }
//~
//~   SourceLoc loc = cr->sourceLocs.c[cg->i];
}

private void //:assignmentLeft
assignmentLeft(Int leftSentinel, Arr(Node const) ast, CG) {
// Writes the left side & equals sign
   Node leftNd = ast[cg->i];
   if (leftNd.tp == nodVar) {
   } else if (leftNd.tp == nodExpr) {
      cg->i++; // CONSUME the nodExpr
      writeExprInternal(leftNd, calcNodeSentinel(leftNd, cg->i - 1), ast, cg);
   }
}

private void //:assignmentRight
assignmentRight(Node rightNode, Int sentinel, Bool isComplex,
                  Arr(Node const) ast, CG) {
   if (cg->i == sentinel) {
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
   }
}

private void //:assignmentWorker
assignmentWorker(Node nd, Arr(Node const) ast, CG) {
// Pre-condition: we are looking at the binding node, 1 past the assignment node
// Consumes the whole assignment
   if (nd.pl2 == 0)
      { return; }
   Int const sentinel = cg->i + nd.pl2;
   Int const rightNodeInd = cg->i + nd.pl3 - 1;
   //Node const rightNode = ast[rightNodeInd];
   if (nd.pl2 == 1 && ast[cg->i].pl3 == assiFnVarDef)
      { goto end; }
   Int innerExprInd = rightNodeInd + 1; // for complex expressions, will skip the inner assigns

   assignmentLeft(rightNodeInd, ast, cg);
   cg->i = innerExprInd;

   assignmentRight(ast[rightNodeInd], sentinel, false, ast, cg);
   end:
   cg->i = sentinel;
}

private void //:writeAssignment
writeAssignment(Node nd, Arr(Node const) ast, CG) {
// Pre-condition: we are looking at the binding node, 1 past the assignment node
   assignmentWorker(nd, ast, cg);
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

   if (cg->i == sentinel)
      { return; }

   Node rightSide = ast[cg->i];
   cg->i++; // CONSUME the expr node

   writeExprInternal(rightSide, sentinel, ast, cg);

   cg->i = sentinel; // CONSUME the whole "return" statement
}

private void //:writeScope
writeScope(Node nd, AST, CG) {
}

private void //:writeIfClause
writeIfClause(Node nd, AST, CG) {
   openFrame(nd, cg);

   if (nd.pl3 == ifclElse) {
   } else {
      if (nd.pl3 == ifclElseIf) {
      }

      Node expression = ast[cg->i];
      Int exprSentinel = calcNodeSentinel(expression, cg->i);
      cg->i++; // CONSUME the nodExpr
      writeExprInternal(expression, calcNodeSentinel(expression, cg->i - 1), ast, cg);
      cg->i = exprSentinel; // CONSUME the if of "else if" condition
   }
}

private void //:writeIf
writeIf(Node nd, AST, CG) {
   openFrame(nd, cg);
}

private void //:writeMatch
writeMatch(Node nd, AST, CG) {
}

private void //:writeLoopLabel
writeLoopLabel(Int labelId, CG) {
   //ensureBufferLength(14, cg);
   //Int lenWritten = sprintf(cg->buffer + cg->len, "%d", labelId);
}

void //:preambleFor
preambleFor(
   Int sentinel, Int skipToBody, AST, CG,
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
writeFor(Node nd, AST, CG) {
   Int const sentinel = calcNodeSentinel(nd, cg->i - 1);
   Int initCount = 0;
   Int condInd = 0;
   Int stepCount = 0;
   Int stepInd = 0;
   Int bodyInd = 0;
   preambleFor(sentinel, nd.pl3, ast, cg,
                 OUT &initCount, OUT &condInd, OUT &stepCount, OUT &stepInd, OUT &bodyInd);
   if (initCount > 1) { // create a special scope that the loop will be nested in
      openFrameWithSentinel(((Node){.tp = nodScope, .pl2 = nd.pl2 - 1}), sentinel, cg);

      for (; cg->i < condInd;) {
         Node initNd = ast[cg->i];
         cg->i++; // CONSUME the nodAssignment
         assignmentWorker(initNd, ast, cg);
      }
   }

   openFrameWithSentinel(nd, sentinel, cg);
   if (initCount == 1) {
      Node initNd = ast[cg->i];
      cg->i++; // CONSUME the nodAssignment
      assignmentWorker(initNd, ast, cg);
   }
   cg->i = condInd + 1;

   // loop condition
   Node exprNd = ast[condInd];
   writeExprInternal(exprNd, calcNodeSentinel(exprNd, condInd), ast, cg);

   // loop steps
   if (stepCount > 0) {
      for ( cg->i = stepInd + 1; cg->i < bodyInd; ) {
         Node currNd = ast[cg->i - 1];
         if (currNd.tp == nodAssignment) {
            assignmentWorker(currNd, ast, cg);
         } ei (currNd.tp == nodExpr)  {
            Int exprSentinel = calcNodeSentinel(currNd, cg->i - 1);
            writeExprInternal(currNd, exprSentinel, ast, cg);
            cg->i = exprSentinel;
         } else { // TODO assert
            cg->i = calcNodeSentinel(currNd, cg->i - 1) + 1;
         }
         stepCount--;
      }
   }
   cg->i = MIN(bodyInd + 1, sentinel); // CONSUME everything till the body, and the opening scope
}

private void //:writeBreakCont
writeBreakCont(Node fr, Arr(Node const) ast, CG) {
   if (fr.pl1 == -1) {
   } else if (fr.pl1 == BIG - 1)     {
   } else {
      Int loopInd = fr.pl1;
      if (loopInd >= BIG) {
         loopInd -= BIG;
      } else {
      }
      writeLoopLabel(loopInd, cg);
   }
}

private void //:writeTry
writeTry(Node fr, Arr(Node const) ast, CG) {
}

private void //:writeCatch
writeCatch(Node fr, Arr(Node const) ast, CG) {
}

private void //:writeFnDef
writeFnDef(Node nd, Arr(Node const) ast, CG) {
//~   Compiler const* restrict cm = cg->cm;
//~   SourceLoc loc = cr->sourceLocs->c[cg->i - 1];
//~   openFrame(nd, cg);
//~   writeNewline(cg);
//~   Function fnEnt = cg->cr->functions.cont[nd.pl1];
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
//~      SourceLoc bindingLoc = cr->sourceLocs->c[j];
//~      writeBytes(cg->sourceCode.cont + bindingLoc.startBt, bindingLoc.lenBts, cg);
//~      ++j;
//~   }
//~
//~   // function params
//~   print("params j %d", j)
//~   while (j < sentinel && nodes[j].tp == nodVar && nodes[j].pl3 == assiFnParam) {
//~      writeChar(aComma, cg);
//~      writeChar(aSpace, cg);
//~      SourceLoc bindingLoc = cr->sourceLocs->c[j];
//~      writeBytes(cg->sourceCode.cont + bindingLoc.startBt, bindingLoc.lenBts, cg);
//~      j++;
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

private void //:mbCloseLoops
mbCloseLoops(CG) {
   for (Int j = cg->bt->len - 1; j > -1 && cg->bt->c[j].sentinel == cg->i; j--) {
      BtLoop loop = removeLast(cg->bt);
   }
}

private void //:openBlock
openBlock(FutureBlock futureBlock, Node nd, CG) {
   Int newEnd = calcNodeSentinel(nd, cg->i);
   CodeBlock* afterBlock;
   if (newEnd < cg->cbl.end) {
      // if the new block splits the current one into two, we need to schedule the
      afterBlock = newBlock(cg->cbl.fn);
      FutureBlock afterSplit = ;
      add(afterSplit, cg->futureBlocks);
   } else {

   }
   switch (futureBlock.c.tp) {
   case (bloCommon): {

   }
   case (bloIf): {

   }
   case (bloLoopCond): {

   }
   case (bloLoopBody): {

   }
   }
   CurrBlock newBlock;
   cg->cbl = (CurrBlock){.start = newBlock.start, .end = calcNodeSentinel(nd, cg->i),
      .c = newBlock.c, .nextBlock = futureBlock.c.c, .afterBlock = futureBlock.c.c
   };
}

private void //:writeToplevelFn
writeToplevelFn(FunctionId toplevelId, CR, CG) {
   Function eyrFn = cr->functions.c[toplevelId];
   if (eyrFn.genericInd != -1 || eyrFn.tokenInd == -1) // generic or imported fn
      { return; }
   TypeHeader typeHeader = tech_sozonov_eyr_readTypeHeader(eyrFn.typeId, cr->types.c);
   Int const arity = typeHeader.arity - 1;
   TypeId returnType = typeOf(cr->types.c[eyrFn.typeId.v + TYPE_PREFIX_LEN + arity]);

   Fn* newToplevel;
   if (arity == 0) {
      newToplevel = newFnReal(
         eyrFn.name,
         null,
         cgType(returnType, cg),
         GCC_JIT_FUNCTION_EXPORTED,
         cg
      );
   } else {
      cg->params->len = 0;
      for (Int n = eyrFn.nodeInd + 1; n < eyrFn.nodeInd + 1 + arity; n++) {
         NameId parName = cr->vars.c[cr->ast.c[n].pl1].name;
         FnParam* newParam = param(
            parName, typeOf(cr->types.c[eyrFn.typeId.v + TYPE_PREFIX_LEN + n]), cg
         );
         add(newParam, cg->params);
      }
      newToplevel = newFnReal(
         eyrFn.name,
         cg->params,
         cgType(returnType, cg),
         GCC_JIT_FUNCTION_EXPORTED,
         cg
      );
   }
   cg->functions[toplevelId] = newToplevel;

   CodeBlock* mainBlock = newBlock(newToplevel);
   cg->cbl = (CurrBlock){
      .fn = newToplevel,
      .start = eyrFn.nodeInd,
      .end = calcNodeSentinel(cr->ast.c[eyrFn.nodeInd], eyrFn.nodeInd),
      .c = { .tp = bloCommon, .c = mainBlock },
      .nextBlock = null,
      .afterBlock = null
   };

   Node nodeFn = cr->ast.c[eyrFn.nodeInd];
   Int const sentinel = calcNodeSentinel(nodeFn, eyrFn.nodeInd);

   cg->i = eyrFn.nodeInd + 1;
   for (; cg->i < sentinel;) {
      Node nd = cr->ast.c[cg->i];
      if (cg->futureBlocks->len > 0 && last(cg->futureBlocks).start == cg->i)  {
         FutureBlock newBlock = removeLast(cg->futureBlocks);
         openBlock(newBlock, nd, cg);
         cg->i++; // CONSUME the span node
      } else {
         cg->i++; // CONSUME the span node
         (CODEGEN_TABLE[nd.tp - nodScope])(nd, cr->ast.c, cg);
      }
      mbCloseLoops(cg);
   }
   mbCloseLoops(cg);
}

void temp(CG);

private void //:generateMainCode
generateMainCode(CG) {
   CompResult* cr = &cg->compResult;
   for (int j = 0; j < cr->toplevels.len; j++) {
      writeToplevelFn(cr->toplevels.c[j], cr, cg);
   }
}

private Codegen* //:generateCode
generateCode(CR) {
   Codegen* cg = createCodegen(cr, cr->a);
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
dbgBtLoops(Codegen* cg) {
   printf("BtLoops [");
   if (cg->bt->len == 0)
      { goto closing; }
   printf("%d ", cg->bt->c[0].sentinel);
   for (Int i = 1; i < cg->bt->len; i++) {
      printf("%d ", cg->bt->c[i].sentinel);
   }
   closing:
   printf("]\n");
}

//}}}
//{{{ Temp


struct B_glb;
struct A_glb {
  struct B_glb *b;
};
struct B_glb {
  struct A_glb *a;
};


void
temp2(CG) {
   Module* ctxt = cg->md;


  gcc_jit_type *int_type = gcc_jit_context_get_type (ctxt,
    GCC_JIT_TYPE_INT);
    /* fn1 = () -> 10 */
    gcc_jit_function* fn1 = gcc_jit_context_new_function(
       ctxt,
       NULL,
       GCC_JIT_FUNCTION_EXPORTED,
       int_type,
       "fn1",
       0,
       NULL,
       0
    );
    gcc_jit_block *block1 = gcc_jit_function_new_block (fn1, "fn1");
    gcc_jit_rvalue* ten = gcc_jit_context_new_rvalue_from_int(ctxt, int_type, 10);
    gcc_jit_block_end_with_return(block1, NULL, ten);

    /* fn2 = () -> 2000 */
    gcc_jit_function* fn2 = gcc_jit_context_new_function(
       ctxt,
       NULL,
       GCC_JIT_FUNCTION_EXPORTED,
       int_type,
       "fn2",
       0,
       NULL,
       0
    );
    gcc_jit_block *block2 = gcc_jit_function_new_block (fn2, "fn2");
    gcc_jit_rvalue* twoThousand = gcc_jit_context_new_rvalue_from_int(ctxt, int_type, 2000);
    gcc_jit_block_end_with_return(block2, NULL, twoThousand);

    gcc_jit_type* fn_type =
       gcc_jit_context_new_function_ptr_type(ctxt, NULL, int_type, 0, NULL, 0);

    /* F_TABLE = {&fn1, &fn2}; */
    gcc_jit_type* f_table_type = gcc_jit_context_new_array_type(ctxt, NULL, fn_type, 2);
    gcc_jit_lvalue* f_table = gcc_jit_context_new_global(
      ctxt, NULL, GCC_JIT_GLOBAL_EXPORTED, f_table_type, "F_TABLE"
    );
    gcc_jit_rvalue* fns[2];
    fns[0] = gcc_jit_function_get_address(fn1, NULL);
    fns[1] = gcc_jit_function_get_address(fn2, NULL);

    f_table = gcc_jit_global_set_initializer_rvalue(
        f_table,
        gcc_jit_context_new_array_constructor(ctxt, NULL, f_table_type, 2, fns)
    );

    gcc_jit_result* result = gcc_jit_context_compile(ctxt);

    typedef int (*intToVoid)(void);
    intToVoid *compiledFns = gcc_jit_result_get_global (result, "F_TABLE");
   print("aaa %p %p", compiledFns[0], compiledFns[1]);
    if (compiledFns[0]() != 10) {
       print("first fun not 10");
    } else if (compiledFns[1]() != 2000) {
       print("second fun not 2000");
    } else {
       print("OK %d %d", compiledFns[0](), compiledFns[1]());
    }
}

//~void //:temp
//~temp(CG) {
//~   Module* md = cg->md;
//~   registerTypes(cg);
//~
//~   CgType* constCharPtrTp = builtinType(GCC_JIT_TYPE_CONST_CHAR_PTR, md);
//~   CgType* const intTp = intType(cg);
//~   CgType* const voidTp = voidType(cg);
//~   FnParam* paramFormat = paramFromChars("format", constCharPtrTp, md);
//~   Fn* printfFn = importFn("printf", 1, &paramFormat, intTp, true, md);
//~
//~
//~   FnParam* paramInt1 = paramFrom("i", intTp, md);
//~   Fn* fn1 = newFn("fn1", GCC_JIT_FUNCTION_EXPORTED, 0, null, voidTp, md);
//~   CodeBlock* bl1 = newBlock(fn1);
//~   FnParam* paramInt2 = newParam("i", intTp, md);
//~   Fn* fn2 = newFn("fn2", GCC_JIT_FUNCTION_EXPORTED, 0, null, voidTp, md);
//~   CodeBlock* bl2 = newBlock(fn2);
//~
//~   RValue* zero = intConst(0, cg);
//~   RValue* one = intConst(1, cg);
//~   RValue* fifteen = intConst(15, cg);
//~   RValue* hundred = intConst(100, cg);
//~   RValue* hwArgs[2];
//~   hwArgs[0] = strConst("HW from f1 %d\n", md);
//~   hwArgs[1] = fifteen;
//~   evalExpr(call(printfFn, 2, hwArgs, md), bl1);
//~   returnVoid(bl1);
//~
//~   hwArgs[0] = strConst("HW from f2 %d\n", md);
//~   hwArgs[1] = hundred;
//~   evalExpr(call(printfFn, 2, hwArgs, md), bl2);
//~   returnVoid(bl2);
//~
//~   CgType* fnTp = fnPointerType(0, null, voidTp, md);
//~   CgType* fTableTp = gcc_jit_context_new_array_type(md, null, fnTp, 2);
//~   LValue* fTable = gcc_jit_context_new_global(
//~      md, null, GCC_JIT_GLOBAL_INTERNAL, fTableTp, "FTABLE"
//~   );
//~   RValue* fns[2];
//~   fns[0] = gcc_jit_function_get_address(fn1, null);
//~   fns[1] = gcc_jit_function_get_address(fn2, null);
//~
//~   gcc_jit_function_type* validatingT1 = gcc_jit_type_dyncast_function_ptr_type(gcc_jit_rvalue_get_type(fns[0]));
//~   gcc_jit_function_type* validatingT2 = gcc_jit_type_dyncast_function_ptr_type(gcc_jit_rvalue_get_type(fns[1]));
//~   print("function pointer types %p and %p", validatingT1, validatingT2);
//~
//~   fTable = gcc_jit_global_set_initializer_rvalue(
//~      fTable,
//~      gcc_jit_context_new_array_constructor(md, null, fTableTp, 2, fns)
//~   );
//~
//~   FnParam* mainParams[2];
//~   mainParams[0] = paramFromChars("argc", typeOf(tokInt), cg);
//~   mainParams[1] = paramFromChars("argv", pointerOf(constCharPtrTp), cg);
//~   Fn* mainFn = newFn("main", GCC_JIT_FUNCTION_EXPORTED, 2, mainParams, intTp, md);
//~
//~   CodeBlock* mainBlock = newBlock(mainFn);
//~
//~   evalExpr(callFnPtr(rValueOf(arrElem(rValueOf(fTable), zero, md)), 0, null, md), mainBlock);
//~   evalExpr(callFnPtr(rValueOf(arrElem(rValueOf(fTable), one, md)), 0, null, md), mainBlock);
//~
//~   returnFromBlock(intConst(0, cg), mainBlock);
//~
//~   //gcc_jit_type *gcc_jit_context_new_function_ptr_type(gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_type *return_type, int num_params, gcc_jit_type **param_types, int is_variadic)
//~
//~
//~   //gcc_jit_lvalue *gcc_jit_context_new_global(gcc_jit_context *ctxt, gcc_jit_location *loc, GCC_JIT_GLOBAL_INTERNAL, gcc_jit_type *type, const char *name)
//~   // gcc_jit_lvalue *gcc_jit_global_set_initializer_rvalue(gcc_jit_lvalue *global, gcc_jit_rvalue *init_value)
//~   //gcc_jit_rvalue *gcc_jit_context_new_array_constructor(gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_type *type, size_t num_values, gcc_jit_rvalue **values)
//~
//~
//~
//~
//~   gcc_jit_context_compile_to_file(md, GCC_JIT_OUTPUT_KIND_EXECUTABLE, "_target/program");
//~
//~   gcc_jit_result* result = gcc_jit_context_compile(md);
//~   gcc_jit_context_dump_to_file(md, "_target/outputDump.c", 0);
//~}

//}}}
//{{{ Main

private void
displayHelp() {
   printf("Eyr compiler. Usage:\n\neyrc file.eyr\n\nor\n\neyrc folder\n");
}


#define whatToDoDisplayHelp  0
#define whatToDoBuildExe     1
#define whatToDoPrintAst     2

typedef struct {
   String inputFilename;
   String outputFilename;
   Byte whatToDo;
   String errMsg;
} TaskDescription;

private TaskDescription
getCommandParams(int argc, char** argv) {
   Byte whatToDo = argc == 1 ? whatToDoDisplayHelp : whatToDoBuildExe;
   return (TaskDescription){
      .inputFilename = stringOf("testFile.eyr"),
      .outputFilename = "testFile", .errMsg = empty, .whatToDo = whatToDo
   };
}

Int //:main
main(int argc, char** argv) {
//{{{ TEMP CODE
   Arena* a = createArena();
   Codegen* cg = allocate(Codegen, a);
   Module* md = gcc_jit_context_acquire();
   (*cg) = (Codegen) {
      .i = 0,
      .md = md,
      .bt = createLBtLoop(16, a),
      .compResult = null,
      .a = a,
      .wasError = false
   };
   temp2(cg);
   return 0;
//}}}


//~   TaskDescription task = getCommandParams(argc, argv);
//~   if (task.errMsg.len > 0) {
//~      print("Erroneous task description!");
//~      printString(task.errMsg);
//~      return 0;
//~   } else if (task.whatToDo == whatToDoDisplayHelp) {
//~      displayHelp();
//~      return 0;
//~   }
//~   CompResult* compResult = tech_sozonov_eyr_compileFile(task.inputFilename);
//~   if (compResult->wasLexerError || compResult->wasParserError) {
//~      print("Compilation error");
//~      printString(compResult->errMsg);
//~      return 1;
//~   }
//~   Codegen* cg = generateCode(compResult);
//~   if (cg->wasError) {
//~      print("Code generation error");
//~      return 1;
//~   }
//~
//~   Module* md = cg->md;
//~   gcc_jit_context_compile_to_file(md, GCC_JIT_OUTPUT_KIND_EXECUTABLE, task.outputFilename.c);
//~
//~   gcc_jit_result* result = gcc_jit_context_compile(md);
//~   gcc_jit_context_dump_to_file(md, "outputDump.c", 0);


   cleanup:


   return 0;
}

//}}}
