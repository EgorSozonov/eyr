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
#ifdef TEST
   #define private
#else
   #define private static
#endif
#define OUT // the "out" parameters and args in functions
#define NULLABLE // the marker of nullability
#define BIG 70000000
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
#define print(...) \
  printf(__VA_ARGS__);\
  printf("\n");

#define dg(...) \
  printf(__VA_ARGS__);\
  printf("\n");

typedef struct Arena Arena;
Arena* createArena();
void deleteArena(Arena* ar);

typedef struct Compiler Compiler;

#define s(lit) str(lit)

void* allocateOnArena(size_t, Arena*);
#define allocate(T, a) (T*)allocateOnArena(sizeof(T), a)
#define allocateArray(cap, T, a) (T*)allocateOnArena(cap*sizeof(T), a)
#define containerOf(ptr, Type, member) ((Type *)((char *)(ptr) - offsetof(Type, member)))
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

#define DEFINE_LIST_HEADER(T) \
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

DEFINE_LIST_HEADER(Int)
DEFINE_LIST_HEADER(Unt)
DEFINE_LIST_HEADER(Ulong)
DEFINE_LIST_HEADER(SourceLoc)
DEFINE_LIST_HEADER(Node)
DEFINE_LIST_HEADER(Var)
DEFINE_LIST_HEADER(Function)

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
libeyr_String str(const char* cent);

//}}}
//{{{ AST nodes & operators

#define tokInt          0
#define tokLong         1
#define tokDouble       2
#define tokBool         3  // pl2 = value (1 or 0)
#define tokString       4

#define tokMisc         5  // pl1 = see the misc* constants. pl2 = underscore count iff miscUscore
                           // Also stands for "Void" among the primitive types

// AST nodes
#define nodVar          7  // pl1 = index into @vars.
                           // pl2 = fnId iff pl3 = assiFnVarUse /\ assiFnVarDef
                           // pl3 >0 => it's a definition (except if pl3 = assiFnVar...) and is one
                           //     of the "assi" constants
#define nodCall         8  // pl1 =
                           //   index into @functions (after type resolution) when pl3 = callNormal,
                           //   into @monos if pl3 = callMonomorph,
                           //   into @vars if pl3 = callVar,
                           //   into @types if pl3 = callField.
                           // pl2 = arg count (or ind of field within type iff pl3 = callField).
                           // pl3 = "call" constants.
// Punctuation (inner node). pl2 = node count inside (so for [span node1 node2], span.pl2 = 2)
#define nodScope        9  // if it's the outer scope of a forNode, then pl3 = length of nodes till
                           // inner scope. See parser tests for examples
#define nodExpr        10  // pl1 = 1 iff it's a composite expression (has internal var decls)
#define nodAssignment  11  // Followed by binding or complex left side. pl3 = distance to the right
                           // side, which is always an atom, nodExpr or a nodDataAlloc
#define nodDataAlloc   12  // pl1 = name of collection type, pl3 = count of elements

#define nodAssert      13  // pl1 = 1 iff it's a debug assert
#define nodBreakCont   14  // pl1 = number of label to break or continue to, -1 if none needed.
                           // pl3 = 1 iff it's a "continue"
#define nodCatch       15  // `catch e {`
#define nodImport      16  // This is for test files only, no need to import anything in main
#define nodFnDef       17  // pl1 = index into @functions
#define nodDef         18  // pl1 = entityId, pl3 = nameId. For non-function compile-time consts
#define nodTrait       19
#define nodReturn      20
#define nodTry         21
#define nodFor         22  // pl1 = number of nodes to skip to get to the condition. Loops that get
                           // "continue"d to have pl1 += BIG.
                           // pl3: the number of nodes to skip to get to the "step" part (or 0 if
                           // there's no step)
#define nodIf          23
#define nodIfClause    24  // pl3 = "ifcl" constants
#define nodImpl        25
#define nodMatch       26  // pattern matching on sum type tag
#define countAstForms  27  // sentinel

#define countSpanForms (countAstForms - nodScope)

#define metaDoc         1  // Doc comments
#define metaDefault     2  // Default values for type arguments

#define topVerbatimType tokMisc

#define voidType            tokMisc

// Not used in types, only in overloads to mark functions with first param = type param
constexpr Int outerTypeForTypeParam = topVerbatimType + 1;

#define maxWordLength   127 // Maximum name of an identifier

typedef struct Compiler Compiler;

struct Node { // :Node
   Unt tp : 6;
   Unt pl3: 26;
   Int pl1;
   Int pl2;
};

typedef struct { //:TypeId
    Int v;
} TypeId;

#define accessPrivImm   1 // private, which for abstract classes means "protected"
#define accessPrivMut   2
#define accessPubImm    3 // public immutable
#define accessPubMut    4 // public mutable
#define accessAbstract  5 // abstract methods in an abstract class

struct Var { //:Var Local variable inside function
   TypeId typeId;
   NameId name;  // if negative, then it's a nameless local & refers to @cg.local via (-x - 1)
   Byte access;  // the "access" constants above
   Int fnId;     // only for aliases to functions, otherwise -1
};

struct SourceLoc { // :SourceLoc
   Int startBt;
   Int lenBts;
};

typedef enum {   // :EmitFn
   emitParsed,   // functions parsed from code, so not built-in
   emitAdd,
   emitSubtract,
   emitMultiply,
   emitDivide,
   emitModulo,
   emitNegate,
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
   Int genericInd; // index into @monos (get full mono type & code from arg types)
   Byte access;    // the "access" constants
   Emit emit;
};

struct StructField { //:StructField Struct field names + access are in a separate table,
   NameId name;      // while their types are in @types (to support various instantiations of a
   Byte access;      // single generic struct)
};

#define assiVarAssignment  1 // definition of a var
#define assiTypeDefinition 2 // definition of a type
#define assiFnParam        3 // introduction of a function parameter
#define assiReassignment   4 // reassignment to a previously defined var
#define assiFnVarDef       5 // definition of a local var that is a function
#define assiFnVarUse       6 // usage (NOT an assignment) of a variable that is a function

// if clauses
#define ifclIf        0
#define ifclElseIf    1
#define ifclElse      2

// types of a nodCall
#define callNormal     0
#define callField      1 // field accessor
#define callVar        2 // a local variable referencing a function
#define callMonomorph  3 // monomorphized version of a generic function
#define callGetElem    4

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
#define opIncrement      10 // ++
#define opPlusExt        11 // +:
#define opPlus           12 // +
#define opDecrement      13 // --
#define opMinusExt       14 // -:
#define opMinus          15 // -
#define opNegate         16 // -
#define opDivByExt       17 // /:
#define opIntersect      18 // /\   type-level trait intersection ?
#define opDivBy          19 // /
#define opBitShiftL      20 // <<.
#define opComparator     21 // <=>
#define opLTZero         22 // <0   less than zero
#define opLTEQ           23 // <=
#define opLessTh         24 // <
#define opRefEquality    25 // ===
#define opEquality       26 // ==
#define opBitShiftR      27 // >>.  unsigned right bit shift
#define opGTZero         28 // >0   greater than zero
#define opGTEQ           29 // >=
#define opGreaterTh      30 // >
#define opNullCoalesce   31 // ?:   null coalescing operator
#define opQuestionMark   32 // ?   Initially nullable pointers
#define opBitwiseXor     33 // ^.   bitwise XOR
#define opBitwiseOr      34 // ||.  bitwise or
#define opBoolOr         35 // ||   logical or
#define countSignOperators 36 // sentinel of operators spelled using special signs

#define opBoolNot          36 // `not` logical negation
#define opGetElem          37 // Get list element
#define opGetElemPtr       38 // Get pointer to list element
#define countOperators     39 // sentinel

//}}}

#define sorDeclare         1 // Used for definitions of records and sum types, both generic and not
#define sorTypeCall        2 // A reference to a generic type. May be generic itself (when
                             // not all generic params are filled in)
#define sorGenericParam    3 // A generic variant. outer = de Bruijn index
#define sorMaxType         sorGenericParam

typedef struct { //:TypeHeader
   Byte sort;    // "sor" constants above
   Byte tyrity;  // "tyrity" = type arity, the number of type parameters
   Byte arity;   // for function types, equals arity + 1. For structs, number of fields
   Bool isGeneric;
   NameLoc name;
} TypeHeader;

#define TYPE_PREFIX 3 // ceil((sizeof TypeHeader)/4) + 1. Length (in ints) of the prefix in type repr

#define typeOf(x) (TypeId){.v = x}

//}}}
//{{{ Standard strings :standardStr

#define strAlias     0
#define strAssert    1
#define strBreak     2
#define strCatch     3
#define strContinue  4
#define strDef       5
#define strEach      6
#define strElseIf    7
#define strElse      8
#define strFalse     9
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
#define strInt      strFirstNonReserved // types must come first here?, see "buildPreludeTypes"
#define strLong     22
#define strDouble   23
#define strBool     24
#define strString   25
#define strVoid     26
#define strF        27 // F(unction type)
#define strL        28 // L(ist)
#define strArray    29
#define strD        30 // D(ictionary)
#define strRec      31 // Record
#define strEnum     32 // Enum
#define strTu       33 // Tu(ple)
#define strLen      34
#define strCap      35
#define strF1       36
#define strF2       37
#define strPrint    38
#define strPrintErr 39
#define strMathPi   40
#define strMathE    41
#define strTypeVarT 42
#define strTypeVarU 43
#define strLength   44
#define strAdd      45
#define strMain     46
#define strContent  47
#ifndef TEST
#define strSentinel 48
#else
#define strSentinel 51
#endif

void populateStringOffsets(Arr(Byte const) stringLens, Int start, Int len, OUT Arr(Int) offsets);
NameId nameOfStandard(Int a);

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

   Int standardTextLen; // length of @standardText
   Int firstParsedName; // the name index for the first parsed word in @names
   Int firstBuiltin;    // the name for the first built-in word in @standardStrings
} CompStats;

typedef struct { //:CompResult
   libeyr_StringBuilder sourceCode; // mutable for need to temporarily change "`" to \0 and back

   // main data produced by the compiler:
   SliNode ast;
   SliSourceLoc sourceLocs;
   SliVar vars;
   SliFunction functions;
   SliInt types;
   SliUnt names;
   SliStructField genericFields;

   SliInt publicFns; // indices into @functions
   SliInt publicConsts; // indices into @vars
   SliInt toplevels; // indices into @functions
   Int entrypoint; // index into @functions
   CompStats stats;
   Bool wasLexerError;
   Bool wasParserError;
   libeyr_String errMsg;
   Arena* a;
} libeyr_CompResult;

Int calcNodeSentinel(Node nd, Int nodeInd);
Compiler* lexicallyAnalyzeFromFile(libeyr_String sourceCode, Arena* a);
libeyr_String readSourceFile(libeyr_String fName, Arena* a);
libeyr_CompResult* getCompResult(CM);
TypeHeader libeyr_readTypeHeader(TypeId t, Arr(Int) types);
Int libeyr_getStructFieldInd(TypeId t, TypeHeader hdr, Arr(Int) types);
TypeId libeyr_typeGetGenericArg(TypeId t, TypeHeader hdr, Int ind, Arr(Int) types);
libeyr_CompResult* libeyr_compileFile(libeyr_String filename);
libeyr_CompResult* libeyr_compile(libeyr_String sourceCode);

//}}}
