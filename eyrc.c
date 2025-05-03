//{{{ Includes

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <setjmp.h>
#include "include/libeyr.h"
#include "_target/libgccjit.h"

typedef libeyr_String String;
typedef libeyr_StringBuilder StringBuilder;
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
typedef gcc_jit_struct Struct;
typedef enum gcc_jit_function_kind FnKind;
typedef enum gcc_jit_types BuiltinType;
typedef enum gcc_jit_comparison BuiltinComparison;
#define pointerOf(x) gcc_jit_type_get_pointer(x)

#define AST Arr(Node const) const restrict ast // Source text
#define SRC Arr(char const) const restrict source // Source text
#define CR CompResult const* const restrict cr // Compilation results

typedef FnParam* FnParamPtr;
typedef Field* FieldPtr;
typedef Fn* FnPtr;
typedef libeyr_CompResult CompResult;

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

typedef struct { //:CurrBlock
   Int start; // start node ind
   Int sentinel; // end node ind, exclusive
   CodeBlock* c; // non-null during code generation
   NULLABLE CodeBlock* after;
} CurrBlock;

typedef struct { //:FutureBlock
   Int start;
   //Byte tp; // the "blo" constants above
   NULLABLE CodeBlock* c;
   CodeBlock* after;
} FutureBlock;

DEFINE_LIST_HEADER(FutureBlock)
DEFINE_LIST(FutureBlock)

typedef CodeBlock* CodeBlockPtr;
typedef RValue* RValuePtr;

typedef struct { //:BtLoop
   CodeBlock* continueBlock; // used for "continue" - it's either the steppers or condition
   CodeBlock* after; // used for "break" implementation
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

typedef struct { //:Builtins
   Fn* printer; // the "printf" function
   CgType* cString; // the zero-terminated array of chars
   CgType* sloppyInt; // the sloppy "int" type of C
   RValue* formatInt; // "%d\n"
   RValue* formatDou; // "%f\n"
   RValue* formatStr; // "%s\n"
} Builtins;

typedef struct { //:Codegen
   Int i; // current node index
   CurrBlock cbl; // no relation to Carbon-Based Lifeforms
   Fn* currFn; // the function we are in
   LFutureBlock* futureBlocks; // future block at lowest AST index is at the top
   LBtLoop* loops;// backtrack of loop conditions, used for "continue" block linking

   Module* md;

   Int bufferLen;
   Byte buffer[maxWordLength + 1]; // temporary buffer for name writing

   Arr(Fn*) functions; // same len as @compResult.functions
   Arr(LValue*) vars; // same len as @compResult.vars

   LFnParamPtr* params; // temporary buffer for function params
   LRValuePtr* exp; // temporary buffer for expression evaluation
   LFieldPtr* fields; // temporary buffer for struct fields

   Int countTypeRefs;
   Arr(TypeRef) typeRefs; // links between Eyr types and GCC types

   CompResult compResult; // results of the compilation from libeyr
   Builtins builtins;

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

void dbgFutureBlocks(CG);

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
newFn(
   const char* name, FnKind accessLevel, int countParams, Arr(FnParam*) params, CgType* returnType,
   Module* md
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

private void //:returnFromFn
returnFromFn(RValue* retValue, CodeBlock* bl) {
   gcc_jit_block_end_with_return(bl, NULL, retValue);
}

private void //:returnVoid
returnVoid(CodeBlock* bl) {
   gcc_jit_block_end_with_void_return(bl, NULL);
}

private void //:jump
jump(CodeBlock* from, NULLABLE CodeBlock* to) {
// End current block with a jump to another block (if set) or with a void return
   if (to != null) {
      gcc_jit_block_end_with_jump(from, NULL, to);
   } else {
      gcc_jit_block_end_with_void_return(from, NULL);
   }
}

private void //:conditional
conditional(CodeBlock* from, RValue* condition, CodeBlock* toIfTrue, CodeBlock* toIfFalse) {
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
localVar(NameId name, TypeId tp, CG) {
   prepareName(name, cg);
   return gcc_jit_function_new_local(cg->currFn, NULL, cgType(tp, cg), cg->buffer);
}

private FnParam* //:param
param(NameId nameId, TypeId tp, CG) {
   prepareName(nameId, cg);
   return gcc_jit_context_new_param(cg->md, NULL, cgType(tp, cg), cg->buffer);
}

private FnParam* //:paramFromChars
paramFromChars(char const* s, CgType* tp, CG) {
   return gcc_jit_context_new_param(cg->md, NULL, tp, s);
}

private CgType* //:fnPointerType
fnPointerType(Int countParams, Arr(CgType*) paramTypes, CgType* returnTp, Module* md) {
   return gcc_jit_context_new_function_ptr_type (
      md, null, returnTp, countParams, paramTypes, 0
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

private NULLABLE RValue* //:eCall
eCall(FunctionId fnId, Int countArgs, Arr(RValue*) args, CG) {
// A call within an expression.
// It returns null for void-returning functions. This is safe because the return value
// is used only in assignments, where the type checker already validated the return type.
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
   case emitPrintInt: {
      RValue* printfArgs[2];
      printfArgs[0] = cg->builtins.formatInt;
      printfArgs[1] = args[0];
      return callParsed(cg->builtins.printer, 2, printfArgs, cg->md);
   }
   case emitPrintDou: {
      RValue* printfArgs[2];
      printfArgs[0] = cg->builtins.formatDou;
      printfArgs[1] = args[0];
      return callParsed(cg->builtins.printer, 2, printfArgs, cg->md);
   }
   case emitPrintStr: {
      RValue* printfArgs[2];
      printfArgs[0] = cg->builtins.formatStr;
      printfArgs[1] = args[0];
      return callParsed(cg->builtins.printer, 2, printfArgs, cg->md);
   }
   }
   return null; // unreachable
}

Field* //:field
field(NameId nameId, CgType* tp, CG) {
   prepareName(nameId, cg);
   return gcc_jit_context_new_field(cg->md, null, tp, cg->buffer);
}

Struct* //:newStruct
newStruct(NameId nameId, Int countFields, Arr(Field*) fields, CG) {
   prepareName(nameId, cg);
   return gcc_jit_context_new_struct_type(cg->md, null, cg->buffer, countFields, fields);
}

//}}}
//{{{ Generation table

typedef void (*CgFunc)(Node, Int, Arr(Node const) const restrict, Codegen* restrict);
#define CG_FUN(fnName) static void fnName(Node nd, Int sentinel, AST, CG)

CG_FUN(writeNop); CG_FUN(writeExpr); CG_FUN(writeAssignment); CG_FUN(writeDataAlloc);
CG_FUN(writeAssert); CG_FUN(writeBreakCont); CG_FUN(writeTry); CG_FUN(writeCatch);
CG_FUN(writeFnDef); CG_FUN(writeDef);
CG_FUN(writeTrait); CG_FUN(writeImpl); CG_FUN(writeReturn);
CG_FUN(writeFor); CG_FUN(writeIf); CG_FUN(writeMatch);

private CgFunc const CODEGEN_TABLE[countSpanForms] = {
   [0]                        = &writeNop, // scopes do not affect codegen!
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
   [nodIfClause   - nodScope] = &writeNop, // not needed because will be handled by {openBlock}
   [nodMatch      - nodScope] = &writeMatch
};

//{{{ Host text

// Host strings for codegen. Must agree in order with the "host" constants below :hostText
constexpr char hostText[] = "mallocfree";
constexpr Byte
hostStringLens[] = {
    6, 4
};

// host string constants
#define hostMalloc    0
#define hostFree      1

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

   Field* stringFields[2];
   stringFields[0] = field(nameOfStandard(strLen), intType(cg), cg);
   stringFields[1] = field(nameOfStandard(strContent), cg->builtins.cString, cg);
   Struct* stringStruct = newStruct(nameOfStandard(strString), 2, stringFields, cg);
   cg->typeRefs[tokString] = (TypeRef){
      .ind = tokString, .cgType = gcc_jit_struct_as_type(stringStruct) };
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

private TypeId //:tFunctionReturnType
tFunctionReturnType(TypeId funcTypeId, CR) {
   TypeHeader hdr = libeyr_readTypeHeader(funcTypeId, cr->types.c);
   return typeOf(cr->types.c[funcTypeId.v + TYPE_PREFIX_LEN + hdr.arity - 1]);
}

//}}}
//{{{ Code generator
//{{{ Codegen utils

private RValue* //:intConst
intConst(int val, Codegen* cg) {
   return gcc_jit_context_new_rvalue_from_int(cg->md, intType(cg), val);
}


private RValue* //:stringConst
stringConst(SourceLoc loc, Codegen* cg) {
// To avoid an extra copy we perform a tactical temporary mutation: change the closing `
// of the string constant to a \0 character, let libgccjit copy it to its internals, then change
// back so the source code is unchanged.
   StringBuilder sourceCode = cg->compResult.sourceCode;
   sourceCode.c[loc.startBt + loc.lenBts - 1] = '\0';
   RValue* newConstant =
      gcc_jit_context_new_string_literal(cg->md, (char const*)(sourceCode.c + loc.startBt + 1));
   sourceCode.c[loc.startBt + loc.lenBts - 1] = '`';
   return newConstant;
}

private RValue* //:cStringConst
cStringConst(Arr(char const) val, Codegen* cg) {
   return gcc_jit_context_new_string_literal(cg->md, val);
}

private void //:prepareName Writes a name from source code to codegen buffer, zero-terminated
prepareName(NameId nameId, CG) {
   NameLoc name = cg->compResult.names.c[nameId];
   memcpy(&(cg->buffer), cg->compResult.sourceCode.c + (name & LOWER24BITS), name >> 24);
   cg->bufferLen = (name & LOWER24BITS) + 1;
   cg->buffer[name >> 24] = '\0';
}

private void //:reverseFutureBlocks
reverseFutureBlocks(FutureBlock* bl, Int count) {
   for (Int i = 0, j = count - 1; i < j; i++, j--) {
      FutureBlock tmp = bl[i];
      bl[i] = bl[j];
      bl[j] = tmp;
   }
}

//~private FutureBlock //:findFutureBlockAt
//~findFutureBlockAt(Int j, CG) {
//~   for (Int k = cg->futureBlocks->len - 1; k > -1; k--) {
//~      if (cg->futureBlocks->c[k].start == j)
//~         { return cg->futureBlocks->c[k]; }
//~   }
//~   // unreachable
//~   return (FutureBlock){};
//~}

private CodeBlock* //:splitCurrentBlock
splitCurrentBlock(Int sentinel, CG) {
// When we're about to create a sub-block within the current block, we need to check whether
// this sub-block will end the current or split it.
   if (sentinel < cg->cbl.sentinel) {
      // if the new block splits the current one into two, we need to create the tail
      return newBlock(cg->currFn);
   } else {
      return cg->cbl.after;
   }
}


//}}}

private Builtins //:createBuiltins
createBuiltins(CG) {
   CgType* constCharPtrTp = builtinType(GCC_JIT_TYPE_CONST_CHAR_PTR, cg->md);
   CgType* sloppyInt = builtinType(GCC_JIT_TYPE_INT, cg->md);
   FnParam* paramFormat = paramFromChars("format", constCharPtrTp, cg);
   Fn* printfFn = importFn(
      "printf",
      1,
      &paramFormat,
      sloppyInt,
      true,
      cg->md
   );

   return (Builtins){
      .printer = printfFn, .cString = constCharPtrTp,
      .sloppyInt = builtinType(GCC_JIT_TYPE_INT, cg->md),
      .formatInt = cStringConst("%d\n", cg),
      .formatDou = cStringConst("%f\n", cg),
      .formatStr = cStringConst("%s\n", cg)
   };
}

private Codegen* //:createCodegen
createCodegen(CR, Arena* a) {
   Codegen* cg = allocate(Codegen, a);
   Module* md = gcc_jit_context_acquire();
   (*cg) = (Codegen) {
      .i = 0,
      .cbl = (CurrBlock){
         .start = 0, .sentinel = 0, .c = null, .after = null
      },
      .currFn = null,
      .futureBlocks = createLFutureBlock(16, a),
      .loops = createLBtLoop(16, a),
      .bufferLen = 0,
      .functions = allocateArray(cr->functions.len, Fn*, a),
      .vars = allocateArray(cr->vars.len, LValue*, a),
      .params = createLFnParamPtr(16, a),
      .exp = createLRValuePtr(16, a),
      .fields = createLFieldPtr(16, a),
      .md = md,
      .compResult = *cr,
      .a = a,
      .wasError = false
   };

   Builtins builtins = createBuiltins(cg);
   cg->builtins = builtins;
   registerTypes(cg);
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

private RValue* //:expr
expr(Int start, Int sentinel, AST, CG) {
// Converts an Eyr expression into a Libgccjit one. Consumes no nodes. Returns the result of expr.
// Does NOT handle complex expressions or void-returning functions
// "start" = first node of the expression body (so, 1 past the nodExpr, if any)
// Precondition: we are looking 1 past the nodExpr/singular node.
   LRValuePtr* exp = cg->exp;
   exp->len = 0;
   for (Int j = start; j < sentinel; j++) {
      Node expNode = ast[j];
      switch (expNode.tp) {
         case tokInt: {
            Int value = expNode.pl2;
            add(intConst(value, cg), exp);
            break;
         }
         case tokString: {
            add(stringConst(cg->compResult.sourceLocs.c[j], cg), exp);
            break;
         }
         case nodVar: {
            add(rValueOf(cg->vars[expNode.pl1]), exp);
            break;
         }
         case nodCall: {
            Int countArgs = expNode.pl2;
            RValue* callResult = eCall(expNode.pl1, countArgs, exp->c + exp->len - countArgs, cg);
            exp->len -= (countArgs - 1);
            exp->c[exp->len - 1] = callResult;
            break;
         }
      }
   }
   return exp->c[0]; // the type checker guarantees that there is only one element at this point
}

private void //:writeExpr
writeExpr(Node nd, Int sentinel, AST, CG) {
   RValue* exprResult = expr(cg->i, sentinel, ast, cg);
   evalExpr(exprResult, cg->cbl.c);
   cg->i = sentinel;
}

private LValue* //:assignmentLeft
assignmentLeft(Int leftSentinel, Arr(Node const) ast, CG) {
// Creates a local variable or returns a pre-existing one for the left side of an assignment
   Node leftNd = ast[cg->i];
   if (leftNd.tp == nodVar) {
      VarId varId = leftNd.pl1;
      if (leftNd.pl3 == assiVarAssignment) {
         Var v = cg->compResult.vars.c[varId];
         cg->vars[varId] = localVar(v.name, v.typeId, cg);
      }
      return cg->vars[varId];
   } else if (leftNd.tp == nodExpr) {
      cg->i++; // CONSUME the nodExpr
      expr(cg->i, calcNodeSentinel(leftNd, cg->i - 1), ast, cg);
   }

   return null; // TODO
}

private RValue* //:assignmentRight
assignmentRight(Int rightNodeInd, Int innerExprInd, Int sentinel, Arr(Node const) ast, CG) {
// Evaluates the right side of an expression
// the "innerExprInd" here is the actual expression start (so for complex expressions, the inner
// assignments have been skipped).
   if (innerExprInd > rightNodeInd) {
//~      for(; start < sentinel && ast[start].tp == nodAssignment;
//~            start = calcNodeSentinel(ast[start], start)
//~      ) {
//~      }
   } else {
      return expr(innerExprInd, sentinel, ast, cg);
   }
   return null;
}

private void //:assignmentWorker
assignmentWorker(Node nd, Int sentinel, AST, CG) {
// Pre-condition: we are looking at the binding node, 1 past the assignment node
// Consumes the whole assignment
   if (nd.pl2 == 1 && ast[cg->i].pl3 == assiFnVarDef)
      { goto end; } // a function-typed local var - nothing to codegen here

   Int const rightNodeInd = cg->i + nd.pl3 - 1;
   
   Int innerExprInd = rightNodeInd; // for complex expressions
   for (; innerExprInd < sentinel && ast[innerExprInd].tp == nodAssignment; innerExprInd++) {
   }

   LValue* lValue = assignmentLeft(rightNodeInd, ast, cg);
   cg->i = innerExprInd;

   RValue* rValue = assignmentRight(rightNodeInd, innerExprInd, sentinel, ast, cg);

   assignment(lValue, rValue, cg->cbl.c);
   end:
   cg->i = sentinel;
}

private void //:writeAssignment
writeAssignment(Node nd, Int sentinel, Arr(Node const) ast, CG) {
// Pre-condition: we are looking at the binding node, 1 past the assignment node
   assignmentWorker(nd, sentinel, ast, cg);
}

private void //:writeDataAlloc
writeDataAlloc(Node fr, Int sentinel, Arr(Node const) ast, CG) {
   // TODO
   cg->i = sentinel; // CONSUME the whole assignment
}

private void //:writeAssert
writeAssert(Node fr, Int sentinel, Arr(Node const) ast, CG) {
   // TODO
   cg->i = sentinel; // CONSUME the whole assignment
}

private void //:writeReturn
writeReturn(Node fr, Int sentinel, AST, CG) {
   if (cg->i == sentinel)
      { return; }

   Node rightSide = ast[cg->i];
   if (rightSide.tp == nodExpr)
      { cg->i++; } // CONSUME the expr node

   RValue* returnValue = expr(cg->i, sentinel, ast, cg);
   returnFromFn(returnValue, cg->cbl.c);
   cg->i = sentinel; // CONSUME the whole "return" statement
}

private void //:writeNop
writeNop(Node nd, Int sentinel, AST, CG) {
}


private CodeBlock* //:ifCreateBlocks
ifCreateBlocks(Node nd, Int sentinel, AST, CG) {
// Create blocks for all the clauses and add them to @futureBlocks. Does not change @cbl
   CodeBlock* ifAfterBlock = splitCurrentBlock(sentinel, cg);

   // For an "if" expression, we need to create a block for every "else if" condition (but not for
   // the "if" condition - it ties into the preceding block) and a block for every branch's body
   Int const ifBlocksOrig = cg->futureBlocks->len;
   Int mbSecondBlockInd = calcNodeSentinel(ast[cg->i], cg->i);
   for (Int j = mbSecondBlockInd; j < sentinel; j = calcNodeSentinel(ast[j], j)) {
      CodeBlock* block = newBlock(cg->currFn);
      add(
         ((FutureBlock){.start = j, .c = block, .after = ifAfterBlock }), cg->futureBlocks
      );
   }

   if (ifAfterBlock != cg->cbl.after) {
      add(((FutureBlock) {
         .start = sentinel, .c = ifAfterBlock, .after = cg->cbl.after
      }), cg->futureBlocks);
   }

   Int const ifBlocksFinal = cg->futureBlocks->len;
   if (ifBlocksFinal > ifBlocksOrig) // need to reverse order of newly inserted blocks
      { reverseFutureBlocks(cg->futureBlocks->c + ifBlocksOrig, ifBlocksFinal - ifBlocksOrig); }
   return ifAfterBlock;
}

private RValue* //:ifWriteCondition
ifWriteCondition(OUT Int* startIfBody, OUT Int* sentinelIfBranch, AST, CG) {
   Node ifClause = ast[cg->i];
   *sentinelIfBranch = calcNodeSentinel(ifClause, cg->i);

   cg->i++; // CONSUME the nodIfClause
   Node cond = ast[cg->i];
   Int const startIfCond = cond.tp == nodExpr ? cg->i + 1 : cg->i;
   *startIfBody = calcNodeSentinel(cond, cg->i);

   return expr(startIfCond, *startIfBody, ast, cg);
}

private void //:ifInitialCondition
ifInitialCondition(CodeBlock* ifAfterBlock, AST, CG) {
// Emit the "if" condition and branch from the current block based on it. Consumes all the nodes.
// Precondition: we are looking at the first nodIfClause in an "if"
   Int startIfBody, sentinelIfBranch;
   RValue* ifCondition = ifWriteCondition(OUT &startIfBody, OUT &sentinelIfBranch, ast, cg);

   // Link to the next "else if" or "else", or, if none - to the block after the "if"
   FutureBlock firstAdjacent = last(cg->futureBlocks);

   CodeBlock* ifBody = newBlock(cg->currFn); // the body of the branch directly under "if"

   // close the current block with two branches, and enter the first "if" clause
   conditional(cg->cbl.c, ifCondition, ifBody, firstAdjacent.c);
   cg->cbl = (CurrBlock) {
      .start = startIfBody, .sentinel = sentinelIfBranch, .c = ifBody, .after = ifAfterBlock
   };
   cg->i = startIfBody;
}

private void //:writeIf
writeIf(Node nd, Int sentinel, AST, CG) {
// Consumes the first clause of an "if"
   CodeBlock* ifAfterBlock = ifCreateBlocks(nd, sentinel, ast, cg);
   ifInitialCondition(ifAfterBlock, ast, cg);
}

private void //:writeMatch
writeMatch(Node nd, Int sentinel, AST, CG) {
}

private void //:forWriteInitializers
forWriteInitializers(Node nd, Int sentinel, AST, CG) {
// Consumes everything up to condition
   Int const condInd = cg->i + (nd.pl1 >= BIG ? (nd.pl1 - BIG) : nd.pl1) - 1;
   for (; cg->i < condInd; ) {
      Node assign = ast[cg->i];
      Int sentinel = calcNodeSentinel(assign, cg->i);
      cg->i++;
      assignmentWorker(assign, sentinel, ast, cg);
   }
}

private void //:forBranchOnCondition
forBranchOnCondition(Node forNode, Int stepNodeInd, Int sentinel, AST, CG) {
// Creates two blocks (for the condition and the body) and adds the loop to @loops
// Precondition: we are at condInd
   CodeBlock* loopAfter = splitCurrentBlock(sentinel, cg);
   if (loopAfter != cg->cbl.after) {
      add(
         ((FutureBlock){.start = sentinel, .c = loopAfter, .after = cg->cbl.after }), cg->futureBlocks
      );
   }
   Node cond = ast[cg->i];
   Int const startLoopCond = cond.tp == nodExpr ? cg->i + 1 : cg->i;
   Int const loopBodyInd = calcNodeSentinel(cond, cg->i);

   CodeBlock* loopCondition = newBlock(cg->currFn);
   jump(cg->cbl.c, loopCondition);
   cg->cbl = (CurrBlock) {
      .start = cg->i, .sentinel = loopBodyInd, .c = loopCondition, .after = null
   };
   RValue* conditionValue = expr(startLoopCond, loopBodyInd, ast, cg);

   CodeBlock* loopBody = newBlock(cg->currFn);
   conditional(loopCondition, conditionValue, loopBody, loopAfter);

   Bool isTargetOfContinue = forNode.pl1 >= BIG;
   CodeBlock* stepper = null;
   if (isTargetOfContinue) {
      // create a separate block for steppers so "continue" can jump to it
      stepper = newBlock(cg->currFn);
      add(
         ((FutureBlock){.start = stepNodeInd, .c = stepper, .after = loopCondition }), cg->futureBlocks
      );
      add(((BtLoop)
         {.continueBlock = stepper, .after = loopAfter, .sentinel = sentinel }), cg->loops
      );
   } else {
      // there are no steppers, so "continue" will just jump back to the loop condition
      add(((BtLoop)
         {.continueBlock = loopCondition, .after = loopAfter, .sentinel = sentinel }), cg->loops
      );
   }

   cg->i = loopBodyInd;
   cg->cbl = (CurrBlock) {
      .start = loopBodyInd, .sentinel = (isTargetOfContinue ? stepNodeInd : sentinel),
      .c = loopBody, .after = (isTargetOfContinue ? stepper : loopCondition)
   };
}

private void //:writeFor
writeFor(Node nd, Int sentinel, AST, CG) {
   Int stepNodeInd = nd.pl3 < nd.pl2 ? cg->i + nd.pl3 - 1 : 0;
   forWriteInitializers(nd, sentinel, ast, cg);
   forBranchOnCondition(nd, stepNodeInd, sentinel, ast, cg);
}

private void //:writeBreakCont
writeBreakCont(Node nd, Int sentinel, Arr(Node const) ast, CG) {
   Int unwindDepth = nd.pl1;
   Bool isContinue = nd.pl3 == 1;
   BtLoop unwindTarget = cg->loops->c[cg->loops->len - unwindDepth];
   FutureBlock nextBlock = last(cg->futureBlocks);
   if (isContinue) {
      cg->cbl.after = unwindTarget.continueBlock;
   } else {
      cg->cbl.after = unwindTarget.after;
   }
   cg->i = nextBlock.start;
}

private void //:writeTry
writeTry(Node fr, Int sentinel, Arr(Node const) ast, CG) {
}

private void //:writeCatch
writeCatch(Node fr, Int sentinel, Arr(Node const) ast, CG) {
}

private void //:writeFnDef
writeFnDef(Node nd, Int sentinel, Arr(Node const) ast, CG) {
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
writeDef(Node nd, Int sentinel, Arr(Node const) ast, CG) {
// TODO
}

private void //:writeTrait
writeTrait(Node nd, Int sentinel, Arr(Node const) ast, CG) {
// TODO
}


private void //:writeImpl
writeImpl(Node nd, Int sentinel, Arr(Node const) ast, CG) {
// TODO
}

private void //:mbCloseLoops
mbCloseLoops(CG) {
   for (Int j = cg->loops->len - 1; j > -1 && cg->loops->c[j].sentinel == cg->i; j--) {
      removeLast(cg->loops);
   }
}

private void //:openBlockIfClause
openBlockIfClause(FutureBlock futureBlock, Node nd, AST, CG) {
// Handles only "else if" and "else" clauses
   CodeBlock* const ifAfterBlock = cg->cbl.after;

   Int const sentinel = calcNodeSentinel(nd, cg->i);
   if (nd.pl3 == ifclElseIf) {
      Int startIfBody, sentinelIfBranch;
      RValue* ifCondition = ifWriteCondition(OUT &startIfBody, OUT &sentinelIfBranch, ast, cg);

      // Link to the next "else if" or "else", or, if none - to the block after the "if"
      FutureBlock firstAdjacent = last(cg->futureBlocks);
      CodeBlock* ifBody = newBlock(cg->currFn); // the body of the branch directly under "if"
      conditional(futureBlock.c, ifCondition, ifBody, firstAdjacent.c);

      cg->i = startIfBody - 1; // - 1 because the main loop will increment right now
      cg->cbl = (CurrBlock) {
         .start = startIfBody, .sentinel = sentinelIfBranch, .c = ifBody, .after = ifAfterBlock
      };
   } else { // "else"
      cg->cbl = (CurrBlock) {
         .c = futureBlock.c, .after = futureBlock.after, .start = cg->i, .sentinel = sentinel
      };
   }
}

private void //:openBlockScope
openBlockScope(FutureBlock futureBlock, Node nd, AST, CG) {
   Int sentinel = calcNodeSentinel(nd, cg->i);
   cg->cbl = (CurrBlock) {
      .start = cg->i, .sentinel = sentinel, .c = futureBlock.c, .after = futureBlock.after
   };
}

private void //:openBlock
openBlock(FutureBlock futureBlock, Node nd, AST, CG) {
// Precondition: we are looking at nd
   jump(cg->cbl.c, NULLABLE cg->cbl.after);
   if (nd.tp == nodIfClause) {
      openBlockIfClause(futureBlock, nd, ast, cg);
   } else {
      openBlockScope(futureBlock, nd, ast, cg);
   }
}

private Fn* //:openFn
openFn(FunctionId toplevelId, OUT Int* arity, CR, CG) {
// Opens a new function in the codegen and installs it as the current function.
// Precondition: the function is neither imported nor generic
   Function eyrFn = cr->functions.c[toplevelId];
   TypeHeader typeHeader = libeyr_readTypeHeader(eyrFn.typeId, cr->types.c);
   *arity = typeHeader.arity - 1;
   TypeId returnType = tFunctionReturnType(eyrFn.typeId, cr);

   enum gcc_jit_function_kind accessLevel =
      eyrFn.access == accessPrivImm ? GCC_JIT_FUNCTION_INTERNAL : GCC_JIT_FUNCTION_EXPORTED;
   Fn* freshFn;
   if (*arity == 0) {
      freshFn = newFnReal(
         eyrFn.name,
         null,
         cgType(returnType, cg),
         accessLevel,
         cg
      );
   } else if (toplevelId == cr->entrypoint) {
      CgType* sloppyInt = builtinType(GCC_JIT_TYPE_INT, cg->md);
      FnParam* mainParams[2];
      mainParams[0] = paramFromChars("argc", sloppyInt, cg);
      mainParams[1] = paramFromChars("argv", pointerOf(cg->builtins.cString), cg);
      freshFn = newFn("main", GCC_JIT_FUNCTION_EXPORTED, 2, mainParams, sloppyInt, cg->md);
      *arity = 2;
   } else {
      cg->params->len = 0;
      for (Int n = 0; n < *arity; n++) {
         VarId varId = cr->ast.c[eyrFn.nodeInd + n + 1].pl1;
         Var parVar = cr->vars.c[varId];
         FnParam* newParam = param(
            parVar.name, typeOf(cr->types.c[eyrFn.typeId.v + TYPE_PREFIX_LEN + n]), cg
         );
         cg->vars[varId] = gcc_jit_param_as_lvalue(newParam);
         add(newParam, cg->params);
      }
      freshFn = newFnReal(
         eyrFn.name,
         cg->params,
         cgType(returnType, cg),
         accessLevel,
         cg
      );
   }

   cg->currFn = freshFn;
   cg->functions[toplevelId] = freshFn;
   return freshFn;
}

private void //:writeToplevelFn
writeToplevelFn(FunctionId toplevelId, CR, CG) {
   Function eyrFn = cr->functions.c[toplevelId];

   if (eyrFn.genericInd != -1 || eyrFn.tokenInd == -1) // generic or imported fn
      { return; }

   Int arity;
   Fn* newToplevel = openFn(toplevelId, OUT &arity, cr, cg);
   TypeId returnType = tFunctionReturnType(eyrFn.typeId, cr);

   CodeBlock* mainBlock = newBlock(newToplevel);
   cg->cbl = (CurrBlock){
      .start = eyrFn.nodeInd,
      .sentinel = calcNodeSentinel(cr->ast.c[eyrFn.nodeInd], eyrFn.nodeInd),
      .c = mainBlock,
      .after = null
   };
   if (returnType.v == tokMisc) { // default jump target for void-returning functions
      CodeBlock* voidReturnBlock = newBlock(newToplevel);
      returnVoid(voidReturnBlock);
      cg->cbl.after = voidReturnBlock;
   }

   Node nodeFn = cr->ast.c[eyrFn.nodeInd];
   Int const fnSentinel = calcNodeSentinel(nodeFn, eyrFn.nodeInd);

   cg->i = eyrFn.nodeInd + arity + 1; // CONSUME nodFnDef and the parameters
   if (toplevelId == cr->entrypoint) {
      // TODO temp
      cg->i -= 2;
   }
   for (; cg->i < fnSentinel;) {
      Node nd = cr->ast.c[cg->i];
      Int const sentinel = calcNodeSentinel(nd, cg->i);
      if (cg->futureBlocks->len > 0 && last(cg->futureBlocks).start == cg->i)  {
         FutureBlock newBlock = removeLast(cg->futureBlocks);
         openBlock(newBlock, nd, cr->ast.c, cg);
      }
      
      if (nd.tp < nodScope) {
         print("LOOP erroneous tp %d @%d", nd.tp - nodScope, cg->i)
      }
      
      cg->i++; // CONSUME the span node
      (CODEGEN_TABLE[nd.tp - nodScope])(nd, sentinel, cr->ast.c, cg);
      mbCloseLoops(cg);
   }
   mbCloseLoops(cg);

//~   if (toplevelId == cr->entrypoint) {
//~      gcc_jit_function_dump_to_dot(newToplevel, "cfg.dot");
//~   }

   if (returnType.v == tokMisc)
      { jump(cg->cbl.c, cg->cbl.after); }
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

//}}}
//{{{ Utils for tests & debugging

#if defined(DEBUG) || defined(TEST)

void
dbgBtLoops(Codegen* cg) {
   printf("BtLoops [");
   if (cg->loops->len == 0)
      { goto closing; }
   printf("%d ", cg->loops->c[0].sentinel);
   for (Int i = 1; i < cg->loops->len; i++) {
      printf("%d ", cg->loops->c[i].sentinel);
   }
   closing:
   printf("]\n");
}

void //:dbgTypeOuter
dbgFutureBlocks(CG) {
   printf("FutureBlocks[ ");
   if (cg->futureBlocks->len == 0)
      { goto closing; }

   Int const j = cg->futureBlocks->len - 1;
   printf("|%d %p after: %p", cg->futureBlocks->c[j].start,
      cg->futureBlocks->c[j].c, cg->futureBlocks->c[j].after);

   for (Int i = cg->futureBlocks->len - 2; i > -1; i--) {
      printf("| %d %p after: %p", cg->futureBlocks->c[i].start,
         cg->futureBlocks->c[i].c, cg->futureBlocks->c[i].after);
      if (i % 4 == 0) {
         printf("\n");
      }
   }
   closing:
   print("]");
}

#endif

//}}}
//{{{ Main

private void
displayHelp() {
   printf("Eyr compiler. Usage:\n\neyrc file.eyr\n\nor\n\neyrc directory\n");
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
//~   Codegen* cg = allocate(Codegen, a);
//~   Module* md = gcc_jit_context_acquire();
//~   (*cg) = (Codegen) {
//~      .i = 0,
//~      .md = md,
//~      .bt = createLBtLoop(16, a),
//~      .compResult = null,
//~      .a = a,
//~      .wasError = false
//~   };
//~   temp2(cg);
//~   return 0;
//}}}

   CompResult* compResult = libeyr_compileFile(str("program.eyr"));
   
   Codegen* cg;
   if (setjmp(excBuf) == 0) {
      cg = generateCode(compResult);
   } else {
      print("Codegen exception!");
   }

   if (cg->wasError) {
      print("Code generation error");
      return 1;
   }

   Module* md = cg->md;
   gcc_jit_context_compile_to_file(md, GCC_JIT_OUTPUT_KIND_EXECUTABLE, "compiledProgram");

   gcc_jit_result* result = gcc_jit_context_compile(md);
   gcc_jit_context_dump_to_file(md, "outputDump.c", 0);


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
