//{{{ Includes

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <setjmp.h>
#include "include/libeyr.h"
#include <libgccjit.h>
//#include "_debug/libgccjit.h"

typedef libeyr_String String;
typedef libeyr_StringBuilder StringBuilder;
extern jmp_buf excBuf;

//}}}
//{{{ Forward decls, generics & utils
//{{{ Libgccjit wrapper declarations

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
typedef gcc_jit_location Loc;
typedef enum gcc_jit_function_kind FnKind;
typedef enum gcc_jit_types BuiltinType;
typedef enum gcc_jit_comparison BuiltinComparison;
#define ptrOf(x) gcc_jit_type_get_pointer(x)


//}}}
//{{{ General definitions & generics

#define BIG 70000000
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
   LFieldPtr*: addFieldPtr,\
   LFnParamPtr*: addFnParamPtr,\
   LRValuePtr*: addRValuePtr,\
   LCgTypePtr*: addCgTypePtr,\
   LFutureBlock*: addFutureBlock,\
   LBtLoop*: addBtLoop\
)(A, X)

#define removeLast(X) _Generic((X),\
   LInt*: removeLastInt,\
   LUnt*: removeLastUnt,\
   LUlong*: removeLastUlong,\
   LNode*: removeLastNode,\
   LSourceLoc*: removeLastSourceLoc,\
   LFieldPtr*: removeLastFieldPtr,\
   LFnParamPtr*: removeLastFnParamPtr,\
   LRValuePtr*: removeLastRValuePtr,\
   LCgTypePtr*: removeLastCgTypePtr,\
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

//}}}
//{{{ Types & constants

#define fraIf     1
#define fraElse   2
#define fraFor    3

typedef struct { //:TypeRef Codegenned type and index of Eyr type (index into @Compiler.types)
   Int ind;
   CgType* cgType;
} TypeRef;

typedef struct { //:CurrBlock
   Int start; // start node ind
   Int sentinel; // end node ind, exclusive
   CodeBlock* c; // non-null during code generation
   NULLABLE CodeBlock* after;
} CurrBlock;

typedef struct { //:FutureBlock
   Int start;
   Int sentinel;
   NULLABLE CodeBlock* c;
   CodeBlock* after;
} FutureBlock;

typedef struct { //:Name
   NameId nameId;
   Int suffix; // if > -1, then a "_123" with this number will be added to the name
} Name;

Name //:nameWoSuffix
nameWoSuffix(NameId nameId) { return (Name){.nameId = nameId, .suffix = -1}; }

DEFINE_LIST_HEADER(FutureBlock)
DEFINE_LIST(FutureBlock)

typedef CodeBlock* CodeBlockPtr;
typedef RValue* RValuePtr;

typedef CgType* CgTypePtr;

typedef struct { //:BtLoop
   CodeBlock* continueBlock; // used for "continue" - it's either the steppers or condition
   CodeBlock* after; // used for "break" implementation
   Int sentinel; // node index of end, exclusive
} BtLoop;

typedef struct { //:TypeInfo
   CgType* c;
   Int fieldInd; // index into @concreteFields. null for function types
} TypeInfo;

DEFINE_LIST_HEADER(CodeBlockPtr)
DEFINE_LIST_HEADER(FnPtr)
DEFINE_LIST_HEADER(RValuePtr)
DEFINE_LIST_HEADER(CgTypePtr)
DEFINE_LIST_HEADER(BtLoop)
DEFINE_LIST(CodeBlockPtr)
DEFINE_LIST(FnPtr)
DEFINE_LIST(RValuePtr)
DEFINE_LIST(CgTypePtr)
DEFINE_LIST(BtLoop)

typedef struct { //:Builtins
   Fn* printer;  // the "printf" function from libc
   Fn* memAlloc; // the "malloc" function from libc
   CgType* cString; // the zero-terminated array of chars
   CgType* sloppyInt; // the sloppy "int" type of C
   CgType* sizeT;
   Int fieldsArray;
   Int fieldsList;
   RValue* formatInt; // "%d\n"
   RValue* formatDou; // "%f\n"
   RValue* formatStr; // "%s\n"
} Builtins;

typedef struct { //:Codegen
   Int i; // current node index
   CurrBlock cbl; // no relation to Carbon-Based Lifeforms
   Fn* currFn;    // the function we are in
   LFutureBlock* futureBlocks; // future block at lowest AST index is at the top
   LBtLoop* loops;// backtrack of loop conditions, used for "continue" block linking

   Module* md;

   Byte buffer[maxWordLength + 45]; // temporary buffer for name writing

   Arr(Fn*) functions; // same len as @compResult.functions
   Arr(LValue*) vars;  // same len as @compResult.vars

   LFnParamPtr* params;       // temporary buffer for function params
   LValue* lValue;            // temporary pointer for generating complex assignment left sides
   LRValuePtr* exp;           // temporary buffer for expression evaluation

   Int countConcreteTypes;
   Arr(Int) typeRefs;   // indices into @compResult.types, len = countTypes
   Arr(TypeInfo) types; // len = countTypes. The GCC types corresponding to Eyr types via @typeRefs

   LFieldPtr concreteFields; // fields of concrete structs & unions

   CompResult compResult;    // results of the compilation from libeyr
   Builtins builtins;

   Arena* a;
   Bool wasError;
   String errMsg;
} Codegen;

#define CG Codegen* restrict cg

//}}}
//{{{ Utils

private String
stringOf(Arr(char) cString) {
   return (String){.c = cString, .len = strlen(cString) };
}

private Int //:binarySearch
binarySearch(Int key, Int start, Int end, Arr(Int) arr) {
   if (end <= start)
      { return -1; }
   Int i = start;
   Int j = end - 1;
   if (arr[start] == key) {
      return i;
   } ei (arr[j] == key) {
      return j;
   }

   while (i < j) {
      if (j - i == 1)
         { return -1; }
      Int midInd = (i + j)/2;
      Int mid = arr[midInd];
      if (mid > key) {
         j = midInd;
      } ei (mid < key) {
         i = midInd;
      } else {
         return midInd;
      }
   }
   return -1;
}

_Noreturn private void //:throwExcCodegen
throwExcCodegen0(Int errInd, Int lineNumber, CG) {
   cg->wasError = true;
#ifdef DEBUG
   printf("Internal codegen error %d at line %d\n", errInd, lineNumber);
#endif
   printString(cg->errMsg);
   longjmp(excBuf, 1);
}

#define throwExcCodegen(errInd, cm) throwExcCodegen0(errInd, __LINE__, cg)

#ifdef SAFETY
#define VALIDATEI(cond, errInd) if (!(cond)) { throwExcCodegen0(errInd, __LINE__, cg); }
#endif
#ifndef SAFETY
#define VALIDATEI(cond, errInd)
#endif

//}}}
//{{{ Forward declarations

private void prepareName(Name name, CG);
private TypeInfo cgType(TypeId tp, CG);
private CgType* longType(CG);
private CgType* boolType(CG);
private CgType* doubleType(CG);
private TypeId tFunctionReturnType(TypeId funcTypeId, CR);
private CgType* searchCgTypePartiallyFilled(TypeId tp, Int typeCounter, CG);
private RValue* allocateCgArrayKnownLength(TypeId concreteType, Int len, CG);
private RValue* expr(Node nd, Int sentinel, AST, CG);

#if defined(DEBUG) || defined(TEST)

void printIntArray(Int count, Arr(Int) arr);
void printParser(Compiler* cm);
void dbgType0(TypeId type, CM);
#define dbgType(t) dbgType0(t, cm)
private void printLInt(LInt* st);

void dbgFutureBlocks(CG);

#endif

//}}}
//}}}
//{{{ GCC wrapper functions

private void //:assign
assign(LValue* left, RValue* right, CodeBlock* block) {
   gcc_jit_block_add_assignment(block, NULL, left, right);
}

private Loc* //:locOf
locOf(SourceLoc loc, CG) {
   return gcc_jit_context_new_location(cg->md, "a", loc.startLine, loc.startChar);
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
newFnReal(Name name, LFnParamPtr params, CgType* returnType, FnKind accessLevel, CG) {
   prepareName(name, cg);
   return gcc_jit_context_new_function(
      cg->md,
      NULL, // source location
      accessLevel,
      returnType,
      cg->buffer,
      params.len,
      params.c,
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
callParsed(Fn* fn, int countArgs, Arr(RValue*) args, Loc* loc, Module* md) {
   return gcc_jit_context_new_call(md, loc, fn, countArgs, args);
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
localVar(NameId nameId, CgType* tp, CG) {
   prepareName(nameWoSuffix(nameId), cg);
   return gcc_jit_function_new_local(cg->currFn, NULL, tp, cg->buffer);
}

private LValue* //:localTempVar
localTempVar(CgType* tp, CG) {
   return gcc_jit_function_new_temp(cg->currFn, null, tp);
}

private FnParam* //:param
param(NameId nameId, TypeId tp, CG) {
   prepareName(nameWoSuffix(nameId), cg);
   return gcc_jit_context_new_param(cg->md, NULL, cgType(tp, cg).c, cg->buffer);
}

private FnParam* //:paramFromChars
paramFromChars(char const* s, CgType* tp, CG) {
   return gcc_jit_context_new_param(cg->md, NULL, tp, s);
}

private CgType* //:createFnType
createFnType(Int tp, TypeHeader hdr, Int typeCounter, LCgTypePtr* buffer, CG) {
// Creates a new type for codegen for an Eyr function type. Precondition: tp is a fn type
   Int countParams = hdr.arity - 1;
   CompResult* cr = &(cg->compResult);
   TypeId returnType = tFunctionReturnType(typeOf(tp), cr);
   buffer->len = 0;

   if (countParams == 0 && cr->types.c[tp + TYPE_PREFIX] == voidType) { // nullary functions
      return gcc_jit_context_new_function_ptr_type(
         cg->md, null, searchCgTypePartiallyFilled(returnType, typeCounter, cg), 0, null, 0
      );
   } else {
      for (Int j = tp + TYPE_PREFIX; j < tp + TYPE_PREFIX + countParams; j++) {
         TypeId paramType = typeOf(cr->types.c[j]);
         add(searchCgTypePartiallyFilled(paramType, typeCounter, cg), buffer);
      }
      return gcc_jit_context_new_function_ptr_type(
         cg->md, null, searchCgTypePartiallyFilled(returnType, typeCounter, cg), countParams,
         buffer->c, 0
      );
   }
}

private RValue* //:callFnPtr
callFnPtr(RValue* fnPtr, Int countArgs, Arr(RValue*) args, CG) {
   return gcc_jit_context_new_call_through_ptr(cg->md, null, fnPtr, countArgs, args);
}

private RValue* //:fieldAccess
fieldAccess(RValue* val, Field* fld, CG) {
   return gcc_jit_rvalue_access_field(val, null, fld);
}

private LValue* //:fieldAccessLeft
fieldAccessLeft(LValue* val, Field* fld, CG) {
   return gcc_jit_lvalue_access_field(val, null, fld);
}

#define builtinBinary(op, retType, arg1, arg2) gcc_jit_context_new_binary_op(\
   cg->md, null, op, retType, arg1, arg2)
#define builtinUnary(op, retType, arg1) gcc_jit_context_new_unary_op(\
   cg->md, null, op, retType, arg1)
#define builtinCompare(op, arg1, arg2) gcc_jit_context_new_comparison(\
   cg->md, null, op, arg1, arg2)


private RValue* //:eCall
eCall(FunctionId fnId, Int countArgs, Arr(RValue*) args, CG) {
// A call within an expression.
// It returns null for void-returning functions. This is safe because the return value
// is used only in assignments, where the type checker already validated the return type.
   Function fn = cg->compResult.functions.c[fnId];
   Int eyrRetType = cg->compResult.types.c[fn.typeId.v + TYPE_PREFIX + countArgs];
   CgType* retType = cgType(typeOf(eyrRetType), cg).c;

   switch (fn.emit) {
   case emitParsed: {
      return callParsed(cg->functions[fnId], countArgs, args, null, cg->md);
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
   case emitIncrement: {
      return builtinUnary(GCC_JIT_UNARY_OP_MINUS, retType, args[0]);
   }
   case emitDecrement: {
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
   case emitArrayLen: {
      return fieldAccess(args[0], cg->concreteFields.c[cg->builtins.fieldsArray], cg);
   }
   case emitListLen: {
      return fieldAccess(args[0], cg->concreteFields.c[cg->builtins.fieldsList], cg);
   }
   case emitPrintInt: {
      RValue* printfArgs[2];
      printfArgs[0] = cg->builtins.formatInt;
      printfArgs[1] = args[0];
      return callParsed(cg->builtins.printer, 2, printfArgs, null, cg->md);
   }
   case emitPrintDou: {
      RValue* printfArgs[2];
      printfArgs[0] = cg->builtins.formatDou;
      printfArgs[1] = args[0];
      return callParsed(cg->builtins.printer, 2, printfArgs, null, cg->md);
   }
   case emitPrintStr: {
      RValue* printfArgs[2];
      printfArgs[0] = cg->builtins.formatStr;
      printfArgs[1] = fieldAccess(args[0], cg->concreteFields.c[0], cg);
      return callParsed(cg->builtins.printer, 2, printfArgs, null, cg->md);
   }
   }
   longjmp(excBuf, 1); // unreachable
}

private Field* //:field
field(NameId nameId, CgType* tp, CG) {
   prepareName(nameWoSuffix(nameId), cg);
   return gcc_jit_context_new_field(cg->md, null, tp, cg->buffer);
}

private Struct* //:newStruct
newStruct(Name name, Int countFields, Arr(Field*) fields, CG) {
   prepareName(name, cg);
   return gcc_jit_context_new_struct_type(cg->md, null, cg->buffer, countFields, fields);
}

private RValue* //:initStruct
initStruct(TypeId t, LRValuePtr values, CG) {
// The order and types of values must correspond to the fields in @concreteFields
   TypeInfo ti = cgType(t, cg);

   RValue* r = gcc_jit_context_new_struct_constructor(
      cg->md, null, ti.c, values.len, cg->concreteFields.c + ti.fieldInd, values.c
   );
   return r;
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
   [nodDataLit    - nodScope] = &writeDataAlloc,
   [nodAssert     - nodScope] = &writeAssert,
   [nodBreakCont  - nodScope] = &writeBreakCont,
   [nodTry        - nodScope] = &writeTry,
   [nodCatch      - nodScope] = &writeCatch,
   [nodToplevelFn - nodScope] = &writeFnDef,
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
//~constexpr char hostText[] = "mallocfree";
//~constexpr Byte
//~hostStringLens[] = {
//~    6, 4
//~};
//~
//~// host string constants
//~#define hostMalloc    0
//~#define hostFree      1
//~
//~private Int
//~hostOffsets[sizeof(hostStringLens)]; // filled in by "populateStringOffsets"

//}}}
//{{{ Errors

#define iErrorEyrTypeNotFound            1 // Illegal index (not found in @typeRefs)
#define iErrorTypeNotRegisteredInCodegen 2 // null in @types

//}}}
//}}}
//{{{ Type registry

private CgType* //:builtinType
builtinType(BuiltinType tp, Module* md) {
   return gcc_jit_context_get_type(md, tp);
}

private CgType* //:searchCgTypePartiallyFilled
searchCgTypePartiallyFilled(TypeId tp, Int typeCounter, CG) {
// Search for a type in a partially filled @typeRefs
   Int ind = binarySearch(tp.v, 0, typeCounter, cg->typeRefs);
   VALIDATEI(ind > -1, iErrorEyrTypeNotFound);
   CgType* res = cg->types[ind].c;
   VALIDATEI(res != null, iErrorTypeNotRegisteredInCodegen);
   return res;
}

private TypeInfo //:cgType
cgType(TypeId tp, CG) {
   Int ind = binarySearch(tp.v, 0, cg->countConcreteTypes, cg->typeRefs);
   if (ind == -1) {
      print("couldn't find type %d", tp.v)
   }
   VALIDATEI(ind > -1, iErrorEyrTypeNotFound);
   TypeInfo res = cg->types[ind];

   VALIDATEI(res.c != null, iErrorTypeNotRegisteredInCodegen);
   return res;
}

private Int //:countTheConcreteTypes
countTheConcreteTypes(OUT Int* countFields, CR) {
   // +1 for outerTypeForTypeParam, +1 because it's the array length, not index
   Int res = topVerbatimType + 2;
   *countFields = 2; // 2 for the Str type which is created in {registerPrimitiveTypes}
   for (Int j = outerTypeForTypeParam + 1; j < cr->types.len; j += (cr->types.c[j] + 1)) {
      TypeHeader hdr = libeyr_readTypeHeader(typeOf(j), cr->types.c);
      if (hdr.isGeneric)
         { continue; }
      if (hdr.name != nameOfStd(strF))
         { (*countFields) += hdr.arity; }
      res++;
   }
   return res;
}

private TypeInfo //:nonStructTypeInfo
nonStructTypeInfo(CgType* t) {
   return (TypeInfo){.c = t, .fieldInd = -1 };
}

private TypeInfo //:registerType
registerType(TypeId t, TypeHeader hdr, Int typeCounter, LCgTypePtr* buffer, CG) {
// searches in @typeRefs interval [0; typeCounter). Adds to @concreteFields
   if (hdr.name == nameOfStd(strF)) { // functions
      return nonStructTypeInfo(createFnType(t.v, hdr, typeCounter, buffer, cg));
   } else if (hdr.name == nameOfStd(strArr)) {
      CompResult* cr = &(cg->compResult);
      Int const fieldInd = cg->concreteFields.len;
      char c[2] = {'c', '\0'};

      TypeId eltType = libeyr_typeGetGenericArg(t, hdr, 0, cr->types.c);
      cg->concreteFields.c[fieldInd] = gcc_jit_context_new_field(
         cg->md, null,
         ptrOf(cgType(eltType, cg).c),
         c
      );
      cg->concreteFields.c[fieldInd + 1] = field(nameOfStd(strLen), cg->types[tokInt].c, cg);
      cg->concreteFields.len += 2;

      Struct* s = newStruct(
         ((Name){.nameId = hdr.name, .suffix = t.v}), 2, cg->concreteFields.c + fieldInd,
         cg
      );

      return (TypeInfo){ .c = gcc_jit_struct_as_type(s), .fieldInd = fieldInd };
   } else if (hdr.name == nameOfStd(strL)) {
      CompResult* cr = &(cg->compResult);
      Int const fieldInd = cg->concreteFields.len;
      char c[2] = {'c', '\0'};
      cg->concreteFields.c[fieldInd] = gcc_jit_context_new_field(
         cg->md, null,
         ptrOf(cgType(libeyr_typeGetGenericArg(t, hdr, 0, cr->types.c), cg).c),
         c
      );
      cg->concreteFields.c[fieldInd + 1] =
         field(cr->genericFields.c[fieldInd + 1].name, cg->types[tokInt].c, cg);
      cg->concreteFields.c[fieldInd + 2] =
         field(cr->genericFields.c[fieldInd + 2].name, cg->types[tokInt].c, cg);
      cg->concreteFields.len += 3;

      Struct* s = newStruct(
         ((Name){.nameId = hdr.name, .suffix = t.v}), 3, cg->concreteFields.c + fieldInd, cg
      );
      return (TypeInfo){.c = gcc_jit_struct_as_type(s), .fieldInd = fieldInd};
   } else {
      CompResult* cr = &(cg->compResult);
      Int const genericFieldInd = libeyr_getStructFieldInd(t, hdr, cr->types.c); // in compResult
      Int const concreteFieldInd = cg->concreteFields.len; // in codegen
      Int j = genericFieldInd;
      Int k = t.v + TYPE_PREFIX;
      Int l = concreteFieldInd;
      for (; j < genericFieldInd + hdr.arity; j++, k++, l++) {
         NameId fldName = cr->genericFields.c[j].name;
         Int fldType = cr->types.c[k];
         cg->concreteFields.c[l] = field(fldName, cgType(typeOf(fldType), cg).c, cg);
      }
      cg->concreteFields.c[cg->concreteFields.len + 1] =
         field(nameOfStd(strLen), cg->types[tokInt].c, cg);
      cg->concreteFields.len += 2;

      Struct* s = newStruct(
          ((Name){.nameId = hdr.name, .suffix = t.v}),
          hdr.arity,
          cg->concreteFields.c + concreteFieldInd, cg
      );
      return (TypeInfo){ .c = gcc_jit_struct_as_type(s), .fieldInd = concreteFieldInd };
   }
   return nonStructTypeInfo(null);
}

private void //:registerPrimitiveTypes
registerPrimitiveTypes(CG) {
   for (Int j = 0; j <= tokMisc; j++) {
      cg->typeRefs[j] = j;
   }
   cg->types[tokInt] = nonStructTypeInfo(builtinType(GCC_JIT_TYPE_INT32_T, cg->md));
   cg->types[tokLong] = nonStructTypeInfo(builtinType(GCC_JIT_TYPE_INT64_T, cg->md));
   cg->types[tokBool] = nonStructTypeInfo(builtinType(GCC_JIT_TYPE_BOOL, cg->md));
   cg->types[tokDouble] = nonStructTypeInfo(builtinType(GCC_JIT_TYPE_DOUBLE, cg->md));
   cg->types[tokMisc] = nonStructTypeInfo(builtinType(GCC_JIT_TYPE_VOID, cg->md));

   // String type
   Field* stringFields[2];
   char c[2] = {'c', '\0'};
   stringFields[0] = gcc_jit_context_new_field(
      cg->md, null, gcc_jit_type_get_const(builtinType(GCC_JIT_TYPE_CONST_CHAR_PTR, cg->md)), c
   );
   stringFields[1] = field(nameOfStd(strLen), cg->types[tokInt].c, cg);
   Struct* stringStruct = newStruct(nameWoSuffix(nameOfStd(strString)), 2, stringFields, cg);
   cg->typeRefs[tokString] = tokString;
   cg->types[tokString] = (TypeInfo){
      .c = gcc_jit_struct_as_type(stringStruct), .fieldInd = cg->concreteFields.len
   };
   add(stringFields[0], &(cg->concreteFields));
   add(stringFields[1], &(cg->concreteFields));

   cg->typeRefs[topVerbatimType + 1] = topVerbatimType + 1;
   cg->types[topVerbatimType + 1] = nonStructTypeInfo(null); // never to be used, it's a placeholder!
}

private void //:registerCompositeTypes
registerCompositeTypes(CG) {
   Int typeCounter = outerTypeForTypeParam + 1;
   LCgTypePtr* buffer = createLCgTypePtr(16, cg->a);
   CompResult* cr = &(cg->compResult);
   for (Int j = outerTypeForTypeParam + 1; j < cr->types.len; j += (cr->types.c[j] + 1)) {

      TypeHeader hdr = libeyr_readTypeHeader(typeOf(j), cr->types.c);
      if (hdr.isGeneric)
         { continue; }
      cg->typeRefs[typeCounter] = j;
      cg->types[typeCounter] = registerType(typeOf(j), hdr, typeCounter, buffer, cg);

      typeCounter++;
   }
}

private void //:registerTypes
registerTypes(CG) {
// for every non-generic type in @cm.types, create an entry in @cg.typeRefs
   Int countFields;
   cg->countConcreteTypes = countTheConcreteTypes(OUT &countFields, &(cg->compResult));
   cg->typeRefs = allocateArray(cg->countConcreteTypes, Int, cg->a);
   cg->types = allocateArray(cg->countConcreteTypes, TypeInfo, cg->a);
   LFieldPtr* fields = createLFieldPtr(countFields, cg->a);
   cg->concreteFields = *fields;
   registerPrimitiveTypes(cg);
   registerCompositeTypes(cg);
}

private CgType* //:longType
longType(CG) {
   return cg->types[tokLong].c;
}

private CgType* //:boolType
boolType(CG) {
   return cg->types[tokBool].c;
}

private CgType* //:doubleType
doubleType(CG) {
   return cg->types[tokDouble].c;
}

private TypeId //:tFunctionReturnType
tFunctionReturnType(TypeId funcTypeId, CR) {
   TypeHeader hdr = libeyr_readTypeHeader(funcTypeId, cr->types.c);
   return typeOf(cr->types.c[funcTypeId.v + TYPE_PREFIX + hdr.arity - 1]);
}

//}}}
//{{{ Code generator
//{{{ Codegen utils

private RValue* //:intConst
intConst(int val, Codegen* cg) {
   return gcc_jit_context_new_rvalue_from_int(cg->md, cg->types[tokInt].c, val);
}

private RValue* //:intConst
boolConst(int val, Codegen* cg) {
   return gcc_jit_context_new_cast(
      cg->md, null,
      gcc_jit_context_new_rvalue_from_int(cg->md, cg->types[tokInt].c, val), cg->types[tokBool].c
   );
}

private RValue* //:sizeTConst
sizeTConst(int val, Codegen* cg) {
   return gcc_jit_context_new_rvalue_from_int(
      cg->md,
      cg->builtins.sizeT,
      val
   );
}

private RValue* //:stringConst
stringConst(Int startBt, Int lenBts, Codegen* cg) {
// To avoid an extra copy we perform a tactical temporary mutation: change the closing `
// of the string constant to a \0 character, let libgccjit copy it to its internals, then change
// back so the source code is unchanged.
   StringBuilder sourceCode = cg->compResult.sourceCode;
   sourceCode.c[startBt + lenBts - 1] = '\0';
   RValue* newCString =
      gcc_jit_context_new_string_literal(cg->md, (char const*)(sourceCode.c + startBt + 1));
   sourceCode.c[startBt + lenBts - 1] = '`';

   return initStruct(
      typeOf(tokString),
      (((LRValuePtr){.c = (RValue*[]){newCString, intConst(lenBts, cg)}, .len = 2})),
      cg
   );
}

private RValue* //:cStringConst
cStringConst(Arr(char const) val, Codegen* cg) {
   return gcc_jit_context_new_string_literal(cg->md, val);
}

private void //:prepareName
prepareName(Name name, CG) {
// Write a name, possibly with a suffix, to the codegen's buffer, to be consumed by GCC
   NameLoc nameLoc = cg->compResult.names.c[name.nameId];
   Int lenName = nameLoc >> 24;
   memcpy(&(cg->buffer), cg->compResult.sourceCode.c + (nameLoc & LOWER24BITS), lenName);
   if (name.suffix > -1) {
      Int suffixWritten = snprintf(cg->buffer + lenName, 50, "_%d", name.suffix);
      cg->buffer[lenName + suffixWritten] = '\0';
   } else {
      cg->buffer[lenName] = '\0';
   }
}

private void //:reverseFutureBlocks
reverseFutureBlocks(FutureBlock* bl, Int count) {
   for (Int i = 0, j = count - 1; i < j; i++, j--) {
      FutureBlock tmp = bl[i];
      bl[i] = bl[j];
      bl[j] = tmp;
   }
}

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

   CgType* voidPtr = builtinType(GCC_JIT_TYPE_VOID_PTR, cg->md);
   //CgType* sloppyUnt = builtinType(GCC_JIT_TYPE_SIZE_T, cg->md);

   CgType* sizeT = gcc_jit_context_get_type(cg->md, GCC_JIT_TYPE_SIZE_T);
   FnParam* paramAllocSize = paramFromChars("p", builtinType(GCC_JIT_TYPE_INT32_T, cg->md), cg);

   Fn* memAlloc = importFn(
      "malloc",
      1,
      &paramAllocSize,
      voidPtr,
      false,
      cg->md
   );

   Int array = cg->compResult.stats.arrayType;
   TypeHeader arrayHdr = libeyr_readTypeHeader(typeOf(array), cg->compResult.types.c);
   Int list = cg->compResult.stats.listType;
   TypeHeader listHdr = libeyr_readTypeHeader(typeOf(list), cg->compResult.types.c);

   return (Builtins){
      .printer = printfFn, .memAlloc = memAlloc,
      .cString = constCharPtrTp,
      .sloppyInt = builtinType(GCC_JIT_TYPE_INT, cg->md), .sizeT = sizeT,
      .fieldsArray = cg->compResult.types.c[array + TYPE_PREFIX + arrayHdr.arity],
      .fieldsList = cg->compResult.types.c[list + TYPE_PREFIX + listHdr.arity],
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
      .functions = allocateArray(cr->functions.len, Fn*, a),
      .vars = allocateArray(cr->vars.len, LValue*, a),
      .params = createLFnParamPtr(16, a),
      .lValue = null,
      .exp = createLRValuePtr(16, a),
      // @types, @concreteFields and @typeRefs will be filled in by {registerTypes}
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

void //:init
init() {
   //populateStringOffsets(hostStringLens, 0, sizeof(hostStringLens), OUT hostOffsets);
}

private void //:mbRegisterNewVar
mbRegisterNewVar(Node varNode, CG) {
   if (varNode.pl3 == assiVarAssignment) {
      Int const varId = varNode.pl1;
      Var v = cg->compResult.vars.c[varId];
      CgType* t = cgType(v.typeId, cg).c;
      if (v.name > -1) {
         cg->vars[varId] = localVar(v.name, t, cg);
      } else {
         cg->vars[varId] = localTempVar(t, cg);
      }

   }
}

private RValue* //:simpleExprAtom
simpleExprAtom(Node nd, CG) {
   switch (nd.tp) {
   case tokInt: {
      Int value = nd.pl2;
      return intConst(value, cg);
   }
   case tokBool: {
      Int value = nd.pl2;
      return boolConst(value, cg);
   }
   case tokString: {
      return stringConst(nd.pl1, nd.pl2, cg);
   }
   case nodDataLit: { // array literal with compile-time-known length
      return allocateCgArrayKnownLength(typeOf(nd.pl1), nd.pl3, cg);
   }
   default: { //  nodVar
      return rValueOf(cg->vars[nd.pl1]);
   }
   }
}

private void //:simpleExprReduce
simpleExprReduce(Int start, Int sentinel, Bool rightMode, AST, CG) {
// Converts a simple (no internal assignments or data allocations) Eyr expression into a Libgccjit
// one. Consumes no nodes. Returns the result of evaluation of the expr.
// "start" = first node of the expression body (so, 1 past the nodExpr, if any)
// Precondition: we are looking 1 past the nodExpr.
// rightMode: if true, just normal RValue processin', otherwise the complex left side of assignment
// mode (it sets and keeps track of @lValue)
   LRValuePtr* exp = cg->exp;
   exp->len = 0;

   if (!rightMode)
      { cg->lValue = cg->vars[ast[start].pl1]; }
   Int realStart = rightMode ? start : start + 1; // skipping the lvalue we just read
   for (Int j = realStart; j < sentinel; j++) {
      Node expNode = ast[j];
      switch (expNode.tp) {
      case tokInt:
      case tokString:
      case tokBool:
      case nodDataLit:
      case nodVar: {
         add(simpleExprAtom(expNode, cg), exp);
         break;
      }
      case nodCall: {
         switch (expNode.pl3) {
         case callNormal: {
            Int countArgs = expNode.pl2;
            RValue* callResult = eCall(expNode.pl1, countArgs, exp->c + exp->len - countArgs, cg);
            exp->len -= (countArgs - 1);
            exp->c[exp->len - 1] = callResult;
            break;
         }
         case callVar: {
            Int varId = expNode.pl1;
            Int countArgs = expNode.pl2;
            LValue* funcVar = cg->vars[varId];
            RValue* callResult =
               callFnPtr(rValueOf(funcVar), countArgs, exp->c + exp->len - countArgs, cg);
            exp->len -= (countArgs - 1);
            exp->c[exp->len - 1] = callResult;
            break;
         }
         case callField: {
            TypeInfo concreteColl = cgType(typeOf(expNode.pl1), cg);
            Int indField = concreteColl.fieldInd + expNode.pl2;

            if (!rightMode && exp->len == 0) {
               cg->lValue = fieldAccessLeft(cg->lValue, cg->concreteFields.c[indField], cg);
            } else {
               RValue* callResult = fieldAccess(
                  exp->c[exp->len - 1], cg->concreteFields.c[indField], cg
               );
               exp->c[exp->len - 1] = callResult;
            }
            break;
         }
         case callGetElem: {
            TypeInfo concreteColl = cgType(typeOf(expNode.pl1), cg);
            Int indField = concreteColl.fieldInd; // "c" field is first in Array, List & String

            if (exp->len > 1) {
               RValue* rawArr = fieldAccess(exp->c[exp->len - 2], cg->concreteFields.c[indField], cg);
               RValue* elemResult = rValueOf(arrElem(rawArr, exp->c[exp->len - 1], cg->md));
               exp->c[exp->len - 2] = elemResult;
               exp->len--;
            } else {
               LValue* rawArr = fieldAccessLeft(cg->lValue, cg->concreteFields.c[indField], cg);
               cg->lValue = arrElem(rValueOf(rawArr), exp->c[exp->len - 1], cg->md);
               exp->len--;
            }
            break;
         }
         }
      }
      }
   }
}

private LValue* //:simpleExprLeft
simpleExprLeft(Int start, Int sentinel, AST, CG) {
// Converts a left-side (no assignments or data allocations) Eyr expression into a Libgccjit
// l-value. Consumes no nodes. Returns the l-value being assigned to.
// "start" = first node of the expression body (so, 1 past the nodExpr, if any)
   if (start == sentinel - 1) {
      Node varNode = ast[start];
      mbRegisterNewVar(varNode, cg);
      return cg->vars[varNode.pl1];
   } else {
      simpleExprReduce(start, sentinel, false, ast, cg);
      return cg->lValue;
   }
}

private RValue* //:exprSingleNode
exprSingleNode(Node nd, AST, CG) {
   if (nd.tp == nodCall) { // `(call)`
      return eCall(nd.pl1, 0, null, cg);
   } else {
      return simpleExprAtom(nd, cg);
   }
}

private RValue* //:simpleExpr
simpleExpr(Int start, Int sentinel, AST, CG) {
// Converts a simple (no internal assignments or data allocations) Eyr expression into a Libgccjit
// one. Consumes no nodes. Returns the result of evaluation of the expr.
// "start" = first node of the expression body (so, 1 past the nodExpr, if any)
// Precondition: we are looking 1 past the nodExpr.
   if (start == sentinel - 1) {
      return exprSingleNode(ast[start], ast, cg);
   } else {
      simpleExprReduce(start, sentinel, true, ast, cg);
      return cg->exp->c[0]; // the type checker guarantees that there is only one element at this point
   }
}

private RValue* //:allocateArray
allocateCgArray(TypeId concreteType, RValue* length, Loc* loc, CG) {
   TypeHeader hdr = libeyr_readTypeHeader(concreteType, cg->compResult.types.c);
   TypeId eltType = libeyr_typeGetGenericArg(concreteType, hdr, 0, cg->compResult.types.c);
   CgType* eltTypeCg = cgType(eltType, cg).c;
   RValue* mallocArg = builtinBinary( // len * sizeof(Elt)
      GCC_JIT_BINARY_OP_MULT,
      cg->types[tokInt].c,
      intConst(libeyr_sizeOfType(eltType, cg->compResult.types.c), cg),
      length
   );

   RValue* vals[2];
   vals[0] = ptrCast(
      callParsed(cg->builtins.memAlloc, 1, &mallocArg, null, cg->md), ptrOf(eltTypeCg), cg->md
   );
   vals[1] = length;
   // Array{ .c = malloc(...), .len = ... };
   return initStruct(concreteType, (((LRValuePtr){.c = vals, .len = 2})), cg);
}

private RValue* //:allocateArray
allocateCgArrayKnownLength(TypeId concreteType, Int len, CG) {
   RValue* length = intConst(len, cg);
   if (length > 0) {
      return allocateCgArray(concreteType, length, null, cg);
   } else {
      TypeHeader hdr = libeyr_readTypeHeader(concreteType, cg->compResult.types.c);
      TypeId eltType = libeyr_typeGetGenericArg(concreteType, hdr, 0, cg->compResult.types.c);
      CgType* eltTypeCg = cgType(eltType, cg).c;
      RValue* vals[2];
      vals[0] = gcc_jit_context_new_rvalue_from_ptr(cg->md, ptrOf(eltTypeCg), null);
      vals[1] = length;
      // Array{ .c = malloc(...), .len = ... };
      return initStruct(concreteType, (((LRValuePtr){.c = vals, .len = 2})), cg);
   }
}

private RValue* //:dataLitAssignment
dataLitAssignment(LValue* lValue, Node nd, Int sentinel, Loc* loc, AST, CG) {
// Precondition: we are 1 past the nodAssignment
   TypeId concreteType = typeOf(nd.pl1);
   Bool knowElements = nd.pl3 < BIG;
   RValue* lenR;
   Int len;
   if (knowElements) {
      len = nd.pl3;
      lenR = intConst(len, cg);
   } else {
      len = 0;
      lenR = expr(ast[cg->i + 1], sentinel, ast, cg);
   }
   RValue* arr = allocateCgArray(concreteType, lenR, loc, cg);
   assign(lValue, arr, cg->cbl.c);

   if (knowElements) { // loop over the atoms or simpleExprs, setting the array elements
      Int fieldInd = cgType(concreteType, cg).fieldInd;
      RValue* rawArr = rValueOf(fieldAccessLeft(lValue, cg->concreteFields.c[fieldInd], cg));

      for (Int j = 0; j < len; j++) {
         Node elt = ast[cg->i];
         Int eltSentinel = calcNodeSentinel(elt, cg->i);
         RValue* eltValue = simpleExpr(cg->i, eltSentinel, ast, cg);
         assign(arrElem(rawArr, intConst(j, cg), cg->md), eltValue, cg->cbl.c);
         cg->i = eltSentinel;
      }
   }
   return arr;
}

private void //:simpleAssignment
simpleAssignment(Node nd, Int sentinel, AST, CG) {
// The left side is a single var, right side is a simpleExpr or a data alloc
// Consumes the whole assignment
   Node varNode = ast[cg->i];
   Int varId = varNode.pl1;
   Node rightSide = ast[cg->i + 1];
   SourceLoc loc = cg->compResult.sourceLocs.c[cg->i + 1];
   cg->i += 2;

   mbRegisterNewVar(varNode, cg);
   LValue* lValue = cg->vars[varId];
   if (rightSide.tp == nodDataLit) {
      dataLitAssignment(lValue, rightSide, sentinel, locOf(loc, cg), ast, cg);
   } else {
      assign(lValue, simpleExpr(cg->i - 1, sentinel, ast, cg), cg->cbl.c);
   }
   cg->i = sentinel;
}

private RValue* //:expr
expr(Node nd, Int sentinel, AST, CG) {
// Evaluates the right side of a complex expression: a simpleExpr, or a data allocation,
// or a series of simpleAssignments followed by a simpleExpr.
// Consumes all nodes
   if (cg->i == sentinel)
      { return exprSingleNode(nd, ast, cg); } // single atom instead of an expression
   for (; cg->i < sentinel && ast[cg->i].tp == nodAssignment; ) {
      Int assignSentinel = calcNodeSentinel(ast[cg->i], cg->i);
      cg->i++;
      simpleAssignment(ast[cg->i], assignSentinel, ast, cg);
   }
   RValue* result = simpleExpr(cg->i, sentinel, ast, cg);
   cg->i = sentinel;
   return result;
}

private void //:assignFnToVar
assignFnToVar(Node nd, Int sentinel, CG) {
// assiFnVarDef
   Int varId = nd.pl1;
   Int fnId = nd.pl2;
   Var var = cg->compResult.vars.c[varId];
   Function fn = cg->compResult.functions.c[fnId];
   LValue* lValue = localVar(var.name, cgType(fn.typeId, cg).c, cg);
   cg->vars[varId] = lValue;
   assign(lValue, gcc_jit_function_get_address(cg->functions[fnId], NULL), cg->cbl.c);
   cg->i = sentinel;
}

private void //:reassignFnToVar
reassignFnToVar(Node nd, Int sentinel, CG) {
// assiFnVarReassign
   Int varId = nd.pl1;
   Int fnId = nd.pl2;
   LValue* lValue = cg->vars[varId];
   assign(lValue, gcc_jit_function_get_address(cg->functions[fnId], NULL), cg->cbl.c);
   cg->i = sentinel;
}

private void //:assignment
assignment(Node nd, Int sentinel, AST, CG) {
// Pre-condition: we are looking at the binding node, 1 past the assignment node
// Consumes the whole assignment
   if (nd.pl2 == 1) {
      Int pl3 = ast[cg->i].pl3;
      if (pl3 == assiFnVarDef) {
         assignFnToVar(ast[cg->i], sentinel, cg);
         return;
      } ei (pl3 == assiFnVarReassign)  {
         reassignFnToVar(ast[cg->i], sentinel, cg);
         return;
      }
   }

   Int const rightNodeInd = cg->i + nd.pl3 - 1;
   LValue* lValue = simpleExprLeft(
      ast[cg->i].tp == nodExpr ? cg->i + 1 : cg->i, rightNodeInd, ast, cg
   );

   cg->i = rightNodeInd + 1;
   RValue* rValue = expr(ast[rightNodeInd], sentinel, ast, cg);
   assign(lValue, rValue, cg->cbl.c);
   end:
   cg->i = sentinel;
}

private void //:writeExpr
writeExpr(Node nd, Int sentinel, AST, CG) {
   RValue* exprResult = expr(nd, sentinel, ast, cg);
   evalExpr(exprResult, cg->cbl.c);
}

private void //:writeAssignment
writeAssignment(Node nd, Int sentinel, Arr(Node const) ast, CG) {
// Pre-condition: we are looking at the binding node, 1 past the assignment node
   assignment(nd, sentinel, ast, cg);
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

   RValue* returnValue = expr(rightSide, sentinel, ast, cg);
   returnFromFn(returnValue, cg->cbl.c);
   cg->cbl.after = null;
   cg->i = cg->cbl.sentinel; // CONSUME the whole current block because we've returned from it
}

private void //:writeNop
writeNop(Node nd, Int sentinel, AST, CG) {
}


private CodeBlock* //:ifCreateBlocks
ifCreateBlocks(Node nd, Int sentinel, AST, CG) {
// Create blocks for all the clauses and add them to @futureBlocks. Does not change @cbl
// For an "if" expression, we need to create a block for every "else if" condition (but not for
// the "if" condition - it ties into the preceding block) and a block for every branch's body
   CodeBlock* ifAfterBlock = splitCurrentBlock(sentinel, cg);
   Int const ifBlocksOrig = cg->futureBlocks->len;
   Int mbSecondBlockInd = calcNodeSentinel(ast[cg->i], cg->i);
   for (Int j = mbSecondBlockInd; j < sentinel; j = calcNodeSentinel(ast[j], j)) {
      CodeBlock* block = newBlock(cg->currFn);
      add(((FutureBlock){
            .start = j, .sentinel = cg->cbl.sentinel, .c = block, .after = ifAfterBlock }
         ),
         cg->futureBlocks
      );
   }

   if (ifAfterBlock != cg->cbl.after) {
      add(((FutureBlock) {
         .start = sentinel, .sentinel = cg->cbl.sentinel, .c = ifAfterBlock, .after = cg->cbl.after
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

   return expr(ast[startIfCond], *startIfBody, ast, cg);
}

private void //:ifInitialCondition
ifInitialCondition(CodeBlock* ifAfterBlock, AST, CG) {
// Emit the "if" condition and branch from the current block based on it. Consumes all the nodes.
// Precondition: we are looking at the first nodIfClause in an "if"
   Int startIfBody, sentinelIfBranch;
   RValue* ifCondition = ifWriteCondition(OUT &startIfBody, OUT &sentinelIfBranch, ast, cg);

   // Link to the next "else if" or "else", or, if none - to the block after the "if"
   CodeBlock* nextClauseOrIfAfter =
      cg->futureBlocks->len > 0 ? last(cg->futureBlocks).c : cg->cbl.after;
   CodeBlock* ifBody = newBlock(cg->currFn); // the body of the branch directly under "if"

   // close the current block with two branches, and enter the first "if" clause
   conditional(cg->cbl.c, ifCondition, ifBody, nextClauseOrIfAfter);
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
      Int assignSent = calcNodeSentinel(assign, cg->i);
      cg->i++;
      assignment(assign, assignSent, ast, cg);
   }
}

private void //:forBranchOnCondition
forBranchOnCondition(Node forNode, Int stepNodeInd, Int sentinel, AST, CG) {
// Creates two blocks (for the condition and the body) and adds the loop to @loops
// Precondition: we are at condInd
   CodeBlock* loopAfter = splitCurrentBlock(sentinel, cg);
   if (loopAfter != cg->cbl.after) {
      add(((FutureBlock){
            .start = sentinel, .sentinel = cg->cbl.sentinel, .c = loopAfter, .after = cg->cbl.after
         }),
         cg->futureBlocks
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
   cg->i++; // CONSUME the first node of the condition
   RValue* conditionValue = expr(ast[startLoopCond], loopBodyInd, ast, cg);

   CodeBlock* loopBody = newBlock(cg->currFn);
   conditional(loopCondition, conditionValue, loopBody, loopAfter);

   Bool isTargetOfContinue = forNode.pl1 >= BIG;
   CodeBlock* stepper = null;
   if (isTargetOfContinue) {
      // create a separate block for steppers so "continue" can jump to it
      stepper = newBlock(cg->currFn);
      add(((FutureBlock){
            .start = stepNodeInd, .sentinel = sentinel, .c = stepper, .after = loopCondition
         }),
         cg->futureBlocks
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
   CodeBlock* const nextClauseOrIfAfter =
      cg->futureBlocks->len > 0 ? last(cg->futureBlocks).c : cg->cbl.after;

   Int const sentinel = calcNodeSentinel(nd, cg->i);
   if (nd.pl3 == ifclElseIf) {
      Int startIfBody, sentinelIfBranch;
      RValue* ifCondition = ifWriteCondition(OUT &startIfBody, OUT &sentinelIfBranch, ast, cg);

      // Link to the next "else if" or "else", or, if none - to the block after the "if"
      CodeBlock* ifBody = newBlock(cg->currFn); // the body of the branch directly under "if"
      conditional(futureBlock.c, ifCondition, ifBody, nextClauseOrIfAfter);

      cg->i = startIfBody - 1; // - 1 because the main loop will increment right now
      cg->cbl = (CurrBlock) {
         .start = startIfBody, .sentinel = sentinelIfBranch, .c = ifBody, .after = cg->cbl.after
      };
   } else { // "else"
      cg->cbl = (CurrBlock) {
         .c = futureBlock.c, .after = futureBlock.after, .start = cg->i, .sentinel = sentinel
      };
   }
}

private void //:openBlockScope
openBlockScope(FutureBlock futureBlock, Node nd, AST, CG) {
   cg->cbl = (CurrBlock) {
      .start = cg->i, .sentinel = futureBlock.sentinel,
      .c = futureBlock.c, .after = futureBlock.after
   };
}

private void //:openBlock
openBlock(FutureBlock futureBlock, Node nd, AST, CG) {
// Precondition: we are looking at nd
   if (cg->cbl.after) {
      // cbl.after can be null iff the current block has been returned from
      jump(cg->cbl.c, NULLABLE cg->cbl.after);
   }
   if (nd.tp == nodIfClause) {
      openBlockIfClause(futureBlock, nd, ast, cg);
   } else {
      openBlockScope(futureBlock, nd, ast, cg);
   }
}

private Name //:nameOfFn
nameOfFn(Function eyrFn, Int fnId) {
   return (Name){ .nameId = eyrFn.name, .suffix = (eyrFn.needsMangling ? fnId : -1) };
}

private void //:registerFn
registerFn(FunctionId toplevelId, CR, CG) {
// Registers a new function in the codegen.
// Precondition: the function is neither imported nor generic
   Function eyrFn = cr->functions.c[toplevelId];
   Name name = nameOfFn(eyrFn, toplevelId);

   TypeHeader typeHeader = libeyr_readTypeHeader(eyrFn.typeId, cr->types.c);
   Int arity = typeHeader.arity - 1;
   TypeId returnType = tFunctionReturnType(eyrFn.typeId, cr);

   enum gcc_jit_function_kind accessLevel =
      eyrFn.access == accessPrivImm ? GCC_JIT_FUNCTION_INTERNAL : GCC_JIT_FUNCTION_EXPORTED;

   Fn* freshFn;
   if (toplevelId == cr->entrypoint) {
      CgType* sloppyInt = builtinType(GCC_JIT_TYPE_INT, cg->md);

      FnParam* mainParams[2];
      mainParams[0] = paramFromChars("argc", sloppyInt, cg);
      mainParams[1] = paramFromChars("argv", ptrOf(cg->builtins.cString), cg);
      freshFn = newFn("main", GCC_JIT_FUNCTION_EXPORTED, 2, mainParams, sloppyInt, cg->md);
   } ei (arity == 0) {
      freshFn = newFnReal(
         name, ((LFnParamPtr){.c = null, .len = 0}), cgType(returnType, cg).c, accessLevel, cg
      );

   } else {
      cg->params->len = 0;
      for (Int n = 0; n < arity; n++) {
         VarId varId = cr->ast.c[eyrFn.nodeInd + n + 1].pl1;
         Var parVar = cr->vars.c[varId];
         FnParam* newParam = param(
            parVar.name, typeOf(cr->types.c[eyrFn.typeId.v + TYPE_PREFIX + n]), cg
         );
         cg->vars[varId] = gcc_jit_param_as_lvalue(newParam);
         add(newParam, cg->params);
      }
      freshFn = newFnReal(name, *(cg->params), cgType(returnType, cg).c, accessLevel, cg);
   }
   cg->functions[toplevelId] = freshFn;
}

private void //:writeToplevelFn
writeToplevelFn(FunctionId toplevelId, CR, CG) {
   Function eyrFn = cr->functions.c[toplevelId];

   if (eyrFn.genericInd != -1 || eyrFn.tokenInd == -1) // generic or imported fn
      { return; }
   cg->currFn = cg->functions[toplevelId];
   TypeId returnType = tFunctionReturnType(eyrFn.typeId, cr);
   TypeHeader hdr = libeyr_readTypeHeader(eyrFn.typeId, cr->types.c);
   Int arity = hdr.arity - 1;

   CodeBlock* mainBlock = newBlock(cg->currFn);
   cg->cbl = (CurrBlock){
      .start = eyrFn.nodeInd,
      .sentinel = calcNodeSentinel(cr->ast.c[eyrFn.nodeInd], eyrFn.nodeInd),
      .c = mainBlock,
      .after = null
   };
   if (returnType.v == tokMisc) { // default jump target for void-returning functions
      CodeBlock* voidReturnBlock = newBlock(cg->currFn);
      returnVoid(voidReturnBlock);
      cg->cbl.after = voidReturnBlock;
   }

   Node nodeFn = cr->ast.c[eyrFn.nodeInd];
   Int const fnSentinel = calcNodeSentinel(nodeFn, eyrFn.nodeInd);

   cg->i = eyrFn.nodeInd + arity + 1; // CONSUME nodToplevelFn and the parameters

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

//    dot -Tpng cfg.dot -o outfile.png
//~   if (toplevelId == cr->entrypoint) {
//~      gcc_jit_function_dump_to_dot(cg->currFn, "cfg.dot");
//~   }

   if (returnType.v == tokMisc) // void-returning function needs an implicit return
      { jump(cg->cbl.c, cg->cbl.after); }
}

private void //:generateMainCode
generateMainCode(CG) {
   CompResult* cr = &cg->compResult;
   for (int j = 0; j < cr->toplevels.len; j++) {
      Int toplevelId = cr->toplevels.c[j];
      Function eyrFn = cr->functions.c[cr->toplevels.c[j]];
      if (eyrFn.genericInd == -1 && eyrFn.tokenInd != -1) // not a generic or imported fn
         { registerFn(toplevelId, cr, cg); }
   }
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

private void //:printHelp
printHelp() {
   print("Eyr compiler. Usage:\n\n> eyrc source.eyr -o program\n\nOther options:\n"
         "-v    print version\n"
         "-h    print this help\n"
   );
}

private void //:printVersion
printVersion() {
   print("Eyr compiler version: 0.2");
}


#define whatToDoPrintHelp     0
#define whatToDoBuildExe      1
#define whatToDoPrintAst      2
#define whatToDoPrintVersion  3

typedef struct { //:TaskDescription
   String inputFilename;
   String outputFilename;
   Byte whatToDo;
   String errMsg;
} TaskDescription;

private TaskDescription //:getCommandParams
getCommandParams(int argc, char** argv, Arena* a) {
   if (argc == 1) { // no arguments to the compiler
      return (TaskDescription){
            .inputFilename = empty,
            .outputFilename = "", .errMsg = empty, .whatToDo = whatToDoPrintHelp
         };
   }
   String inputFilename = empty;
   String outputFilename = empty;
   String errMsg = empty;
   Byte whatToDo = whatToDoBuildExe;
   for (Int j = 1; j < argc; j++) {
      char* argCString = argv[j];
      String arg = stringOf(argCString);
      if (arg.c[0] == '-') {
         if (arg.len == 1) {
            errMsg = stringOf("Empty option");
            goto finish;
         }
         if (arg.len == 2) {
            if (arg.c[1] == 'v') {
               whatToDo = whatToDoPrintVersion;
               goto finish;
            } ei (arg.c[1] == 'h')    {
               whatToDo = whatToDoPrintHelp;
               goto finish;
            } ei (arg.c[1] == 'o') {
               if (j == argc - 1) {
                  errMsg = stringOf("Option -o requires a value after it!");
                  goto finish;
               } ei (outputFilename.len > 0) {
                  errMsg = stringOf("Output filename already set!");
                  goto finish;
               }
               outputFilename = stringOf(argv[j + 1]);
               j++;
            } else {
               errMsg = stringOf("Unknown option");
               goto finish;
            }
         }
      } else {
         if (inputFilename.len == 0) {
            inputFilename = arg;
            if (arg.len < 5 || arg.c[arg.len - 4] != '.' || arg.c[arg.len - 3] != 'e'
                            || arg.c[arg.len - 2] != 'y' || arg.c[arg.len - 1] != 'r'
            ) {
               errMsg = stringOf("Source file names should end with `.eyr`");
               goto finish;
            }
         } else {
            errMsg = stringOf("Only 1 input file is allowed in this early version of the compiler");
            goto finish;
         }
      }
   }
   if (inputFilename.len == 0) {
      errMsg = stringOf("No input file specified");
   } ei (outputFilename.len == 0) {
      char* outputBuffer = allocateOnArena(inputFilename.len - 3, a);
      memcpy(outputBuffer, inputFilename.c, inputFilename.len - 4);
      outputBuffer[inputFilename.len - 4] = '\0';
      outputFilename = (String){.c = outputBuffer, .len = inputFilename.len - 4};
   }
finish:
   return (TaskDescription){
      .inputFilename = inputFilename,
      .outputFilename = outputFilename, .errMsg = errMsg, .whatToDo = whatToDo
   };
}

Int //:main
main(int argc, char** argv) {
   Arena* a = createArena();

   TaskDescription task = getCommandParams(argc, argv, a);
   if (task.errMsg.len > 0) {
      print("Erroneous task description!");
      printString(task.errMsg);
      return 0;
   } ei (task.whatToDo == whatToDoPrintHelp) {
      printHelp();
      return 0;
   } ei (task.whatToDo == whatToDoPrintVersion) {
      printVersion();
      return 0;
   }

   CompResult* compResult = libeyr_compileFile(task.inputFilename);
   if (compResult->errMsg.len > 0) {
      printString(compResult->errMsg);
      return 1;
   }

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

//~   gcc_jit_context_add_command_line_option(md, "-freport-bug");
//~   gcc_jit_context_add_command_line_option(md, "-g3");
//~
//~   gcc_jit_context_set_logfile(md, stderr, 0, 0);
   gcc_jit_context_compile_to_file(md, GCC_JIT_OUTPUT_KIND_EXECUTABLE, task.outputFilename.c);

   //gcc_jit_result* result = gcc_jit_context_compile(md);
   gcc_jit_context_compile(md);
   gcc_jit_context_dump_to_file(md, "outputDump.c", 1);


   cleanup:
   deleteArena(a);
   return 0;
}

//}}}
