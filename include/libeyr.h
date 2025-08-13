//{{{ Common code
//{{{ Basic definitions

typedef int32_t NameId;   // name index (in @stringTable)
typedef uint32_t NameLoc; // 8 bit of length, 24 bits of startBt (in @standardText)
typedef int32_t Int;
typedef uint32_t Unt;
typedef int64_t Long;
typedef uint64_t Ulong;
typedef int16_t Short;
typedef uint16_t Ushort;
typedef char Byte;
typedef bool Bool;
#define InListUlong InListuint64_t
#define InListUnt InListuint32_t
#define Arr(T) T*
#define AARG(var, T) var, sizeof(var)/sizeof(T) // For passing array args to functions with length
#define null NULL
#define VarId int32_t
#define FunctionId int32_t
#define private static
#ifdef DEBUG
   #define internal
#else
   #define internal static
#endif
#define OUT // the "out" parameters and args in functions
#define NULLABLE // the marker of nullability
#define LOWER24BITS 0x00FFFFFF
#define LOWER26BITS 0x03FFFFFF
#define LOWER16BITS 0x0000FFFF
#define LOWER32BITS 0x00000000FFFFFFFF
#define PENULTIMATE8BITS 0xFF00
#define THIRTYFIRSTBIT 0x40000000
#define MAXTOKENLEN 67108864 // 2^26
#define SIXTEENPLUSONE 65537 // 2^16 + 1
#define LEXER_INIT_SIZE 1000
#define ei else if
#define defstruct(T) typedef struct T T

#define d(...) \
  printf(__VA_ARGS__);\
  printf("\n");

//{{{Arena

typedef struct Arena Arena;
Arena* createArena();
void deleteArena(Arena* ar);
void* allocateOnArena(size_t, Arena*);
#define allocate(T, a) (T*)allocateOnArena(sizeof(T), a)
#define allocateArray(cap, T, a) (T*)allocateOnArena(cap*sizeof(T), a)

//}}}
//
#define containerOf(ptr, Type, member) ((Type *)((char *)(ptr) - offsetof(Type, member)))

typedef struct Compiler Compiler;

#define s(lit) str(lit)
#define LX Compiler* restrict lx // Compiler for lexer functions
#define CM Compiler* restrict cm // compiler during parsing

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

defstruct(SourceLoc);
defstruct(Var);
defstruct(Node);
defstruct(Function);
defstruct(StructField);

//{{{ List

#define DECLARE_LIST(T) \
   typedef struct {\
      T* c;\
      Int len;\
      Int cap;\
      Arena* arena;\
   } L##T;\
   private L ## T * createL ## T (Int initCapacity, Arena* a);\
   private T removeLast ## T (L##T * st);\
   private void add ## T (T newItem, L##T * st);

#define DEFINE_LIST(T)\
   private L##T * createL##T (int initCapacity, Arena* a) {\
      int capacity = initCapacity < 4 ? 4 : initCapacity;\
      L##T * result = allocate(L##T, a);\
      result->cap = capacity;\
      result->len = 0;\
      result->arena = a;\
      T* arr = allocateArray(capacity, T, a);\
      result->c = arr;\
      return result;\
   }\
   private T removeLast##T (L##T * st) {\
      st->len--;\
      return st->c[st->len];\
   }\
   private void add##T (T newItem, L##T * st) {\
      if (st->len < st->cap) {\
         memcpy((T*)(st->c) + (st->len), &newItem, sizeof(T));\
      } else {\
         T* newcent = allocateArray(2*(st->cap), T, st->arena);\
         memcpy(newcent, st->c, st->len*sizeof(T));\
         memcpy((T*)(newcent) + (st->len), &newItem, sizeof(T));\
         st->cap *= 2;\
         st->c = newcent;\
      }\
      st->len++;\
   }\

#define last(lst) lst->c[lst->len - 1]

DECLARE_LIST(Int)
DECLARE_LIST(Unt)
DECLARE_LIST(Ulong)
DECLARE_LIST(SourceLoc)
DECLARE_LIST(Node)
DECLARE_LIST(Var)
DECLARE_LIST(Function)

//}}}
//{{{ Slice

#define DEFINE_SLICE_HEADER(T) \
   typedef struct {\
      T* c;\
      Int len;\
   } Sli##T;\

#define sliceOf(list) ({.c = list->c, .len = list->len})

#define sliceOfInternal(list) {.c = list.c, .len = list.len}

DEFINE_SLICE_HEADER(Int)
DEFINE_SLICE_HEADER(Unt)
DEFINE_SLICE_HEADER(Ulong)
DEFINE_SLICE_HEADER(Node)
DEFINE_SLICE_HEADER(SourceLoc)
DEFINE_SLICE_HEADER(Var)
DEFINE_SLICE_HEADER(Function)
DEFINE_SLICE_HEADER(StructField)

//}}}
//}}}
//{{{ Strings

typedef struct { // :String
    char const* c;
    int32_t len;
} libeyr_String;

typedef struct { // :StringBuilder
   Arr(char) c;
   Int len;
   Int cap;
} libeyr_StringBuilder;

void printStringNoLn(libeyr_String s);
void printString(libeyr_String s);

constexpr libeyr_String empty = {.c = null, .len = 0};
libeyr_String str(const char* content);

//}}}
//{{{ AST nodes & operators

#define tokInt          0
#define tokLong         1
#define tokDouble       2
#define tokBool         3  // pl2 = value (1 or 0)
#define tokString       4  // pl1 = startBt, pl2 = lenBts

#define tokMisc         5  // pl1 = see the misc* constants. pl2 = underscore count iff miscUnderscore
                           // pl2 = name of field/kwarg iff miscField.
                           // Also stands for "Void" among the primitive types

// AST nodes
#define nodVar          6  // pl1 = index into @vars.
                           // pl2 = fnId iff pl3 = assiFnVarUse /\ assiFnVarDef
                           // pl3 >0 => it's a definition (except if pl3 = assiFnVar...) and is one
                           //     of the "assi" constants
#define nodCall         7  // pl1 =
                           //   index into @functions (after type resolution) when pl3 = callNormal,
                           //   into @monos if pl3 = callMonomorph,
                           //   into @vars if pl3 = callVar,
                           //   into @types if pl3 = callField or pl3 = callGetElem.
                           // pl2 = arg count (or, iff pl3 == callField, ind of field within type).
                           // pl3 = "call" constants.
// Spans. pl2 = node count inside (so for [span node1 node2], span.pl2 = 2)
#define nodScope        8  // if it's the outer scope of a forNode, then pl3 = length of nodes till
                           // inner scope. See parser tests for examples
#define nodExpr         9  // pl1 = 1 iff it's a composite expression (has internal var decls)
#define nodAssignment  10  // Followed by nodVar or complex left side. pl3 = distance to the right
                           // side, which is always an atom, nodExpr or a nodDataLit
#define nodDataLit     11  // pl1 = concrete collection type, pl3 = count of elements.
                           // if pl2 == 0, it's an array with comp-time size but no
                           // contents;
                           // if pl2 > 0 and pl3 == BIG, it's an array with runtime-known
                           // size and no contents;
                           // if pl2 > 0 and pl3 > 0, then it has fully specified contents.
#define nodStruct      12  // Struct init.
                           // pl1 = name before typecheck, concrete typeId after. pl2 = field count
                           // Iff pl3 == 1, it's a field (temporary, during expression parsing)
                           // and pl1 = name of field
#define nodAssert      13  // pl1 = 1 iff it's a debug assert
#define nodBreakCont   14  // pl1 = number of label to break or continue to, -1 if none needed.
                           // pl3 = 1 iff it's a "continue"
#define nodCatch       15  // `catch e {`
#define nodImport      16  // This is for test files only, no need to import anything in main
#define nodToplevelFn  17  // pl1 = index into @functions
#define nodTrait       18
#define nodReturn      19
#define nodTry         20
#define nodFor         21  // pl1 = number of nodes to skip to get to the condition. Loops that get
                           // "continue"d to have pl1 += BIG.
                           // pl3: the number of nodes to skip to get to the "step" part (or 0 if
                           // there's no step)
#define nodIf          22
#define nodIfClause    23  // pl3 = "ifcl" constants
#define nodImpl        24
#define nodMatch       25  // pattern matching on sum type tag
#define countAstForms  26  // sentinel

#define countSpanForms (countAstForms - nodScope)

#define metaDoc         1  // Doc comments
#define metaDefault     2  // Default values for type arguments

#define topVerbatimType tokMisc

#define voidType        tokMisc

// Not used in types, only in overloads to mark functions with first param = type param
constexpr Int outerTypeForTypeParam = topVerbatimType + 1;

#define maxWordLength   127 // Maximum name of an identifier

typedef struct Compiler Compiler;

struct Node { // :Node
   Unt tp : 5;
   Unt pl3: 27;
   Int pl1;
   Int pl2;
};

typedef struct { //:TypeId Index into @typeHeaders
    Int v;
} TypeId;


typedef struct { //:TypeHeader
   Unt start;     // index into @types
   Unt arity : 8; // count of immediate children (struct fields, or function params + return types)
   Unt len : 24;  // number of ints in @types (integers, not nodes! type calls are >1 nodes)
   Unt tyrity : 8;   // count of type parameters
   Unt entityId : 24; // If tyrity = 0, then @concrTypes, else @typeDecls, 
                  //or all 1111's if tyrity > 0 and not a declaration
   Int name; // set only for type declarations, -1 for type calls like `[Foo Int]`
} TypeHeader;

#define sorFn      1
#define sorStruct  2
#define sorSumType 3

typedef struct {  //:ConcrType All primitive types, concrete structs and monomorphic function types
   Byte sort;     // "sor" constants above
   TypeId typeId; // points to @typeHeaders to `[Foo Int Str]` where `Foo` is a generic struct.
                  // Or, if this is a concrete struct, just is the struct's typeId 
   Unt body;      // points to @types where there's a list of types comprising the body (i.e. 
                  // fields for a struct, or params and return for a function, variant types 
                  // for a sum type)
   Unt fieldsInd;  // index into @fieldNames, or for a function, -1 
   Int size;      // size of type in bytes
} ConcrType;

#define accessPrivImm   1 // private, which for abstract classes means "protected"
#define accessPrivMut   2
#define accessPubImm    3 // public immutable
#define accessPubMut    4 // public mutable
#define accessAbstract  5 // abstract methods in an abstract class

struct Var { //:Var Local variable inside function
   TypeId typeId;
   NameId name;  // if negative, then it's a nameless local
   Byte access;  // the "access" constants above
   Int fnId;     // only for aliases to functions, otherwise -1
};

struct SourceLoc { // :SourceLoc
   Int startLine;
   Int startChar;
   Int endLine;
   Int endChar;
};

typedef enum {   // :EmitFn
   emitParsed,   // functions parsed from code, so not built-in
   emitAdd,
   emitSubtract,
   emitMultiply,
   emitDivide,
   emitModulo,
   emitNegate,
   emitIncrement,
   emitDecrement,
   emitAbsolute,
   emitLogicAnd,
   emitLogicOr,
   emitLogicNegate,
   emitBitAnd,
   emitBitOr,
   emitBitXor,
   emitBitNegate,
   emitBitLeftShift,
   emitBitRightShift,
   emitEq,
   emitNotEq,
   emitLessThanOrEq,
   emitLessThan,
   emitGreaterThan,
   emitGreaterThanEq,
   emitArrayLen,
   emitListLen,
   emitPrintInt,
   emitPrintDou,
   emitPrintStr
} Emit;

struct Function { //:Function Parsed or built-in function
   TypeId typeId;
   NameId name;
   Int tokenInd;   // Index into @tokens
   Int nodeInd;    // Index into @ast
   Int genericInd; // index into @functionMonos (get full mono type & code from arg types)
   Emit emit;
   Byte access;    // the "access" constants
   Bool needsMangling; // do we need to add "_123" to this function's name when generating code?
};

typedef struct { //:FieldName Struct field names + access are in a separate table,
   NameId name;  // while their types are in @types (to support various instantiations of a
   Byte access;  // single generic struct). Also used for functions with > 3 params, for
                 // param names
} FieldName;


// nodVar.pl3. It's 0 for uses of ordinary var usage, and one of the following for other uses
#define assiVarAssignment  1 // definition of a var
#define assiTypeDefinition 2 // definition of a type
#define assiFnParam        3 // introduction of a function parameter
#define assiReassignment   4 // reassignment to a previously defined var
#define assiFnVarDef       5 // definition of a local var that is a function
#define assiFnVarReassign  6 // reassignment to a local var that is a function
#define assiFnVarUse       7 // usage (NOT an assignment) of a variable that is a function

// if clauses
#define ifclIf        0
#define ifclElseIf    1
#define ifclElse      2

// types of a nodCall
#define callNormal     0
#define callField      1 // field accessor
#define callVar        2 // a local variable referencing a function
#define callGetElem    3

//{{{ Operators header

// :OperatorType
// Values must exactly agree in order with the operatorSymbols array in the tl.c file.
// The order is defined by ASCII. Operator is bitwise <=> it ends with dot
#define opBitwiseNeg      0 // !. bitwise negation
#define opNotEqual        1 // !=
#define opSize            2 // #
#define opToString        3 // $
#define opRemainder       4 // %
#define opBitwiseAnd      5 // &&. bitwise "and"
#define opBoolAnd         6 // &&  logical "and"
#define opRef             7 // '  References
#define opTimesExt        8 // *:
#define opTimes           9 // * Multiplication and nullable pointers
#define opPlusExt        10 // +:
#define opPlus           11 // +
#define opMinusExt       12 // -:
#define opMinus          13 // -
#define opNegate         14 // -
#define opDivByExt       15 // /:
#define opIntersect      16 // /\   type-level trait intersection ?
#define opDivBy          17 // /
#define opBitShiftL      18 // <<.
#define opComparator     19 // <=>
#define opLTZero         20 // <0   less than zero
#define opLTEQ           21 // <=
#define opLessTh         22 // <
#define opRefEquality    23 // ===
#define opEquality       24 // ==
#define opBitShiftR      25 // >>.  unsigned right bit shift
#define opGTZero         26 // >0   greater than zero
#define opGTEQ           27 // >=
#define opGreaterTh      28 // >
#define opNullCoalesce   29 // ?:   null coalescing operator
#define opQuestionMark   30 // ?   Initially nullable pointers
#define opBitwiseXor     31 // ^.   bitwise XOR
#define opBitwiseOr      32 // ||.  bitwise or
#define opBoolOr         33 // ||   logical or
#define countSignOperators 34 // sentinel of operators spelled using special signs

#define opBoolNot          34 // `not` logical negation
#define opGetElem          35 // Get list element
#define countOperators     36 // sentinel

//}}}

#define typeOf(x) (TypeId){.v = x}

//}}}
//{{{ Standard strings :standardStr

#define strAlias     0
#define strAssert    1
#define strBreak     2
#define strCatch     3
#define strContinue  4
#define strEach      5
#define strElseIf    6
#define strElse      7
#define strFalse     8
#define strFn        9 // `fn`
#define strFor      10
#define strIf       11
#define strImpl     12
#define strImport   13
#define strMatch    14
#define strPub      15
#define strReturn   16
#define strTrait    17
#define strTrue     18
#define strTry      19
#define strNot      20
#define strFirstNonReserved 21
#define strSkip     strFirstNonReserved // types must come first here?, see "buildPreludeTypes"
#define strStep     22
#define strBalk     23
#define strInt      24 // these types must be together and in same order as in tokInt, tokLong etc
#define strLong     25
#define strDouble   26
#define strBool     27
#define strString   28
#define strVoid     29
#define strF        30 // F(unction type)
#define strL        31 // L(ist)
#define strArr      32
#define strD        33 // D(ictionary)
#define strStruct   34
#define strEnum     35 // Enum
#define strTu       36 // Tu(ple)
#define strLen      37
#define strCap      38
#define strF1       39
#define strF2       40
#define strPrint    41
#define strPrintErr 42
#define strMathPi   43
#define strMathE    44
#define strTypeVarT 45
#define strTypeVarU 46
#define strLength   47
#define strAdd      48
#define strMain     49
#define strContent  50
#ifndef DEBUG
#define strSentinel 51
#else
#define strSentinel 54
#endif

void populateStringOffsets(Arr(Byte const) stringLens, Int start, Int len, OUT Arr(Int) offsets);
NameId nameOfStd(Int a);

//}}}
//}}}
//{{{ libeyr interface

typedef struct { //:CompStats
   Int inpLength;

   Int countNonparsedVars;
   Int countNonparsedFns;
   Int countOverloads;
   Int countOverloadedNames;
   Int countOperatorFns;
   Int toksLen;
   Int astLen;
   Int typesLen;
   Int listType;
   Int arrayType;
   Int voidToVoidType;

   Int standardTextLen; // length of @standardText
   Int firstParsedName; // the name index for the first parsed word in @names
   Int firstParsedType; // the length of @types just before parsing
   Int firstBuiltin;    // the name for the first built-in word in @standardStrings
} CompStats;

typedef struct libeyr_CompilationErrors libeyr_CompilationErrors;

typedef struct { //:CompResult
   libeyr_StringBuilder sourceCode; // mutable for need to temporarily change "`" to \0 and back

   // main data produced by the compiler:
   SliNode ast;
   SliSourceLoc sourceLocs;
   SliVar vars;
   SliFunction functions;
   SliInt types;
   SliUnt names;
   SliStructField genericFields; // "name" field is an index into @names
   SliUnt concreteFields; // indices into @types

   SliInt publicFns; // indices into @functions
   SliInt publicConsts; // indices into @vars
   SliInt toplevels; // indices into @functions
   Int entrypoint; // index into @functions
   CompStats stats;
   Bool wasLexerError;
   Bool wasParserError;
   libeyr_CompilationErrors* errors;
   Arena* a;
} libeyr_CompResult;

Int calcNodeSentinel(Node nd, Int nodeInd);
Compiler* lexicallyAnalyzeFromFile(libeyr_String sourceCode, Arena* a);

libeyr_CompResult* getCompResult(CM);
TypeHeader libeyr_readTypeHeader(TypeId t, Arr(Int) types);
Int libeyr_sizeOfType(TypeId t, Arr(Int) types);
Int libeyr_getStructFieldInd(TypeId t, TypeHeader hdr, Arr(Int) types);
TypeId libeyr_typeGetGenericArg(TypeId t, TypeHeader hdr, Int ind, Arr(Int) types);
void libeyr_printErrors(libeyr_CompResult*);
Int libeyr_getFirstErrorId(libeyr_CompResult*);
libeyr_CompResult* libeyr_compileFile(libeyr_String filename);
libeyr_CompResult* libeyr_compile(libeyr_String sourceCode);

//}}}
