//{{{ Includes

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <setjmp.h>
#include "include/libeyr.h"
#include <libgccjit.h>

extern jmp_buf excBuf;

//}}}
//{{{ GCC types and functions

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

private RValue* //:call
call(Fn* fn, int countArgs, Arr(RValue*) args, Module* md) {
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

private FnParam* //:newParam
newParam(const char* name, CgType* tp, Module* md) {
   return gcc_jit_context_new_param(md, NULL, tp, name);
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

//}}}
//{{{ Utils

private String
stringOf(Arr(char) cString) {
   return (String){.c = cString, .len = strlen(cString) };
}

//}}}
//{{{ Forward decls & generics

#define SRC Arr(char const) const restrict source // Source text
#define CR CompResult const* const restrict cr // Compilation results
private void closeStatement(LX);
private NameId nameOfStandard(Int a);

typedef FnParam* FnParamPtr;
typedef Field* FieldPtr;
defstruct(CgFrame);

DEFINE_LIST_HEADER(FnParamPtr)
DEFINE_LIST_HEADER(FieldPtr)
DEFINE_LIST_HEADER(CgFrame)

#define add(A, X) _Generic((X),\
   LInt*: addInt,\
   LUnt*: addUnt,\
   LUlong*: addUlong,\
   LNode*: addNode,\
   LSourceLoc*: addSourceLoc,\
   LCgFrame*: addCgFrame\
)(A, X)

#define removeLast(X) _Generic((X),\
   LInt*: removeLastInt,\
   LUnt*: removeLastUnt,\
   LUlong*: removeLastUlong,\
   LNode*: removeLastNode,\
   LSourceLoc*: removeLastSourceLoc,\
   LCgFrame*: removeLastCgFrame\
)(X)


DEFINE_LIST(Int)
DEFINE_LIST(Unt)
DEFINE_LIST(Ulong)
DEFINE_LIST(Node)
DEFINE_LIST(FnParamPtr)
DEFINE_LIST(FieldPtr)
DEFINE_LIST(SourceLoc)

#if defined(DEBUG) || defined(TEST)

void printIntArray(Int count, Arr(Int) arr);
void printParser(Compiler* cm);
void dbgType0(TypeId type, CM);
#define dbgType(t) dbgType0(t, cm)
private void printLInt(LInt* st);

#endif

//}}}
//{{{ Types & constants

#define fraIf     1
#define fraElse   2
#define fraFor    3

struct CgFrame { //:CgFrame Frame for the stack of nested codegen blocks
   Byte tp; // frame type, the "fra" constants
   Int pl1; // node pl1
   Int pl3; // node pl3
   CodeBlock* block;
   CodeBlock* nextBlock; // if null, there is no next block
   Int sentinel; // node sentinel
};

DEFINE_LIST(CgFrame)

typedef struct { //:TypeRef Codegenned type and index of Eyr type (index into @Compiler.types)
   Int ind;
   CgType* cgType;
} TypeRef;

typedef struct { //:Codegen

   Int i; // current node index
   Arr(Byte) buffer;

   Module* md;

   LFnParamPtr* params; // temporary buffer for function params
   LFieldPtr* fields; // temporary buffer for struct fields
   LCgFrame* bt;

   Int countTypeRefs;
   Arr(TypeRef) typeRefs;
   
   CompResult * restrict compResult; // results of the compilation from libeyr

   Arena* a;
   Bool wasError;
} Codegen;

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

//}}}
//{{{ Generation table

typedef void (*CgFunc)(Node, Arr(Node const), Codegen* restrict);
#define CG Codegen* restrict cg
#define CG_FUN(name) private void name(Node nd, Arr(Node const) nodes, CG);

CG_FUN(writeScope) CG_FUN(writeExpr) CG_FUN(writeAssignment) CG_FUN(writeDataAlloc) CG_FUN(writeAssert)
CG_FUN(writeBreakCont) CG_FUN(writeTry) CG_FUN(writeCatch) CG_FUN(writeFnDef) CG_FUN(writeDef)
CG_FUN(writeTrait) CG_FUN(writeImpl) CG_FUN(writeReturn)
CG_FUN(writeFor) CG_FUN(writeIf) CG_FUN(writeIfClause) CG_FUN(writeMatch)

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
//{{{ Type registry

private CgType* //:getType
getType(BuiltinType tp, Module* md) {
   return gcc_jit_context_get_type(md, tp);
}

private void //:registerTypes
registerTypes(CG) {
   // for every type in @cm.types, create an entry in @cg.typeRefs
   cg->countTypeRefs = tokMisc;
   cg->typeRefs = allocateArray(cg->countTypeRefs, TypeRef, cg->a);
   cg->typeRefs[tokInt] = (TypeRef){.ind = tokInt, .cgType = getType(GCC_JIT_TYPE_INT32_T, cg->md) };
   cg->typeRefs[tokBool] = (TypeRef){.ind = tokBool, .cgType = getType(GCC_JIT_TYPE_BOOL, cg->md) };
   cg->typeRefs[tokMisc] = (TypeRef){.ind = tokMisc, .cgType = getType(GCC_JIT_TYPE_VOID, cg->md) };
}

private CgType* //:intType
intType(CG) {
   return cg->typeRefs[0].cgType;
}

private CgType* //:voidType
voidType(CG) {
   return cg->typeRefs[tokMisc].cgType;
}

//}}}
//{{{ Code generator


private RValue* //:intConst
intConst(int val, Codegen* cg) {
   return gcc_jit_context_new_rvalue_from_int(cg->md, cg->typeRefs[0].cgType, val);
}



//~private void //:ensureBufferLength
//~ensureBufferLength(Int additionalLength, CG) {
//~// Ensures that the buffer has space for at least that many bytes plus 10 by increasing its
//~// capacity if necessary
//~    if (cg->len + additionalLength + 10 < cg->cap) {
//~        return;
//~    }
//~    Int neededLength = cg->len + additionalLength + 10;
//~    Int newCap = 2*cg->cap;
//~    while (newCap <= neededLength) {
//~        newCap *= 2;
//~    }
//~    Arr(Byte) new = allocateOnArena(newCap, cg->a);
//~    memcpy(new, cg->buffer, cg->len);
//~    cg->buffer = new;
//~    cg->cap = newCap;
//~}


private Codegen* //:createCodegen
createCodegen(CR, Arena* a) {
   Codegen* cg = allocate(Codegen, a);
   Module* md = gcc_jit_context_acquire();
   (*cg) = (Codegen) {
      .i = 0, .buffer = allocateOnArena(64, a),
      .md = md, 
      .bt = createLCgFrame(16, a),
      .compResult = cr,
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

//~private void //:writeExprProcessFirstArg
//~writeExprProcessFirstArg(Call* top, CG) {
//~   if (top->countArgs != 1)
//~      { return; }
//~   switch (top->emit) {
//~   case emitField:
//~      writeChar(aDot, cg);
//~      writeConstant(top->startInd, cg); return;
//~   case emitInfix:
//~      writeChar(aSpace, cg);
//~      writeBytes(cg->sourceCode.cont + top->startInd, top->len, cg);
//~      writeChar(aSpace, cg); return;
//~   case emitHostInfix:
//~      writeChar(aSpace, cg);
//~      writeConstant(top->startInd, cg);
//~      writeChar(aSpace, cg); return;
//~   }
//~}

private void //:writeExprInternal
writeExprInternal(Node nd, Int sentinel, Arr(Node const) ast, CG) {
// Consumes no nodes
// Precondition: we are looking 1 past the nodExpr/singular node. Consumes all nodes of the expr
}

private void //:writeExpr
writeExpr(Node nd, Arr(Node const) ast, CG) {
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
   Node const rightNode = ast[rightNodeInd];
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
writeScope(Node nd, Arr(Node const) ast, CG) {
}

private void //:writeIfClause
writeIfClause(Node nd, Arr(Node const) ast, CG) {
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
writeIf(Node nd, Arr(Node const) ast, CG) {
   openFrame(nd, cg);
}

private void //:writeMatch
writeMatch(Node nd, Arr(Node const) ast, CG) {
}

private void //:writeLoopLabel
writeLoopLabel(Int labelId, CG) {
   //ensureBufferLength(14, cg);
   //Int lenWritten = sprintf(cg->buffer + cg->len, "%d", labelId);
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

private void //:maybeCloseFrames
maybeCloseFrames(CG) {
   for (Int j = cg->bt->len - 1; j > -1 && cg->bt->c[j].sentinel == cg->i; j--) {
      CgFrame fr = removeLast(cg->bt);
      if (fr.tp == nodIf) {
         continue;
      }
   }
}

private void //:writeToplevelFn
writeToplevelFn(FunctionId toplevelId, CR, CG) {
//~   Function fn = cr->functions.c[toplevelId];
//~   if (fn.genericInd != -1 || fn.tokenInd == -1) // generic or imported fn
//~      { return; }
//~
//~   TypeHeader typeHdr = typeReadHeader(fn.typeId, cm);
//~   Arr(Node const) ast = cr->ast.c;
//~   Int countParams = typeHdr.arity - 1;
//~   for (Int t = fn.typeId.v + TYPE_PREFIX_LEN; t < fn.typeId.v + TYPE_PREFIX_LEN + arity; t++) {
//~      add(cr->types.c[t], cg->params);
//~   }
//~
//~
//~   // create all the params
//~   Fn* newToplevel = newFn(
//~      fn.name,
//~      GCC_JIT_FUNCTION_EXPORTED,
//~      countParams,
//~      &cg->params->c,
//~      voidType(cg),
//~      cg->md
//~   );
//~
//~   TypeHeader hdr = typeReadHeader(fn.typeId, cm);
//~   if (hdr.arity != 2 || cr->types.cont[fn.typeId.v + TYPE_PREFIX_LEN] != voidType) {
//~      writeChar(aUnderscore, cg);
//~      writeInt(toplevelId, cg);
//~   }
//~
//~   Node nodeFn = ast[fn.nodeInd];
//~   Int const sentinel = calcNodeSentinel(nodeFn, fn.nodeInd);
//~   pushFrame(
//~      ((Frame){ .tp = nodFnDef, .pl1 = nodeFn.pl1, .sentinel = sentinel}),
//~      &cg->bt
//~   );
//~   cg->local = 0;
//~
//~   cg->i = fn.nodeInd + 1;
//~
//~   // first param
//~   Node paramNd = ast[cg->i];
//~   if (paramNd.tp == nodVar && paramNd.pl3 == assiFnParam) {
//~      writeVarNode(cm, cg);
//~      cg->i++;
//~      paramNd = ast[cg->i];
//~   }
//~
//~   // function params
//~   for ( ;
//~         cg->i < sentinel && paramNd.tp == nodVar && paramNd.pl3 == assiFnParam;
//~         cg->i++, paramNd = ast[cg->i]
//~   ) {
//~      writeChars(((Byte[]){ aComma, aSpace }), cg);
//~      writeVarNode(cm, cg);
//~   }
//~   writeChars(((Byte[]){ aParenRight, aSpace, aCurlyLeft }), cg);
//~
//~   for (; cg->i < sentinel;) {
//~      Node nd = cr->ast.cont[cg->i];
//~      cg->i++; // CONSUME the span node
//~      (CODEGEN_TABLE[nd.tp - nodScope])(nd, cr->ast.cont, cg);
//~      cgMaybeCloseFrames(cg);
//~   }
//~   cgMaybeCloseFrames(cg);
}


void temp(CG);

private void //:generateMainCode
generateMainCode(CG) {
   CompResult* cr = cg->compResult;
   for (int j = 0; j < cr->toplevels.len; j++) {
      //toplevelFn(cr.toplevels.c[j], cr, cg);
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
dbgCgFrames(Codegen* cg) {
   printf("CgFrames [");
   if (cg->bt->len == 0) {
      goto closing;
   }
   printf("%d ", cg->bt->c[0].tp);
   for (Int i = 1; i < cg->bt->len; i++) {
      printf("%d ", cg->bt->c[i].tp);
   }
   closing:
   printf("]\n");
}

//}}}
//{{{ Temp

void //:temp
temp(CG) {
   Module* md = cg->md;
   registerTypes(cg);
   
   CgType* constCharPtrTp = getType(GCC_JIT_TYPE_CONST_CHAR_PTR, md);
   CgType* const intTp = intType(cg);
   CgType* const voidTp = voidType(cg);
   FnParam* paramFormat = newParam("format", constCharPtrTp, md);
   Fn* printfFn = importFn("printf", 1, &paramFormat, intTp, true, md);
   
   Fn* fn1 = newFn("fn1", GCC_JIT_FUNCTION_EXPORTED, 0, null, voidTp, md);
   CodeBlock* bl1 = newBlock(fn1);
   Fn* fn2 = newFn("fn2", GCC_JIT_FUNCTION_EXPORTED, 0, null, voidTp, md);
   CodeBlock* bl2 = newBlock(fn2);
   
   RValue* zero = intConst(0, cg);
   RValue* one = intConst(1, cg);
   RValue* fifteen = intConst(15, cg);
   RValue* hundred = intConst(100, cg);
   RValue* hwArgs[2];
   hwArgs[0] = strConst("HW from f1 %d\n", md);
   hwArgs[1] = fifteen;
   evalExpr(call(printfFn, 2, hwArgs, md), bl1);
   returnVoid(bl1);
   
   hwArgs[0] = strConst("HW from f2 %d\n", md);
   hwArgs[1] = hundred;
   evalExpr(call(printfFn, 2, hwArgs, md), bl2);
   returnVoid(bl2);
   
   
//~   evalExpr(call(fn1, 0, null, md), mainBlock);
//~   evalExpr(call(fn2, 0, null, md), mainBlock);
   CgType* fnTp = fnPointerType(0, null, voidTp, md);
   CgType* fTableTp = gcc_jit_context_new_array_type(md, null, fnTp, 2);
   LValue* fTable = gcc_jit_context_new_global(
      md, null, GCC_JIT_GLOBAL_INTERNAL, fTableTp, "FTABLE"
   );
   RValue* fns[2];
   fns[0] = gcc_jit_function_get_address(fn1, null);
   fns[1] = gcc_jit_function_get_address(fn2, null);
   fTable = gcc_jit_global_set_initializer_rvalue(
      fTable, 
      gcc_jit_context_new_array_constructor(md, null, fTableTp, 1, fns)
   );
   
   FnParam* mainParams[2];
   mainParams[0] = newParam("argc", intTp, md);
   mainParams[1] = newParam("argv", pointerOf(constCharPtrTp), md);
   Fn* mainFn = newFn("main", GCC_JIT_FUNCTION_EXPORTED, 2, mainParams, intTp, md);
   
   CodeBlock* mainBlock = newBlock(mainFn);
   
   evalExpr(callFnPtr(rValueOf(arrElem(rValueOf(fTable), zero, md)), 0, null, md), mainBlock);
   evalExpr(callFnPtr(rValueOf(arrElem(rValueOf(fTable), one, md)), 0, null, md), mainBlock);
   
   returnFromBlock(intConst(0, cg), mainBlock);
   
   //gcc_jit_type *gcc_jit_context_new_function_ptr_type(gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_type *return_type, int num_params, gcc_jit_type **param_types, int is_variadic)


   //gcc_jit_lvalue *gcc_jit_context_new_global(gcc_jit_context *ctxt, gcc_jit_location *loc, GCC_JIT_GLOBAL_INTERNAL, gcc_jit_type *type, const char *name)
   // gcc_jit_lvalue *gcc_jit_global_set_initializer_rvalue(gcc_jit_lvalue *global, gcc_jit_rvalue *init_value)
   //gcc_jit_rvalue *gcc_jit_context_new_array_constructor(gcc_jit_context *ctxt, gcc_jit_location *loc, gcc_jit_type *type, size_t num_values, gcc_jit_rvalue **values)
   
   
   
   
   gcc_jit_context_compile_to_file(md, GCC_JIT_OUTPUT_KIND_EXECUTABLE, "_target/program");
   
   gcc_jit_result* result = gcc_jit_context_compile(md);
   gcc_jit_context_dump_to_file(md, "_target/outputDump.c", 0);
}

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
      .i = 0, .buffer = allocateOnArena(64, a),
      .md = md, 
      .bt = createLCgFrame(16, a),
      .compResult = null,
      .a = a,
      .wasError = false
   };
   temp(cg);
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
