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
typedef tech_sozonov_eyr_String String;
#define StackInt Stackint32_t
#define StackUnt Stackuint32_t
#define InListUlong InListuint64_t
#define InListUnt InListuint32_t
#define Any void
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
#define print(...) \
  printf(__VA_ARGS__);\
  printf("\n");

typedef struct Arena Arena;
typedef struct Compiler Compiler;

private void printStringNoLn(String s);
private void printString(String s);

constexpr String empty = {.c = null, .len = 0};
private String str(const char* cent);
private Bool endsWith(String a, String b);

#define s(lit) str(lit)

private void* allocateOnArena(size_t, Arena*);
#define allocate(T, a) (T*)allocateOnArena(sizeof(T), a)
#define allocateArray(cap, T, a) (T*)allocateOnArena(cap*sizeof(T), a)
#define containerOf(ptr, Type, member) ((Type *)((char *)(ptr) - offsetof(Type, member)))
#define LX Compiler* restrict lx // Compiler for lexer functions
#define CM Compiler* restrict cm // compiler during parsing

Bool equal(String a, String b);

//}}}
//{{{ Arena

typedef struct Arena Arena;


Arena* createArena(void);

void* allocateOnArena(size_t allocSize, Arena* a);

void deleteArena(Arena* ar);

void clearArena(Arena* a);

//}}}
//{{{ Common for tests

typedef struct {
    Int countTests;
    Int countPassed;
    Arena* a;
} TestContext;

typedef struct {
   Int inpLength;
   Bool wasLexerError;

   Int countNonparsedVars;
   Int countNonparsedFns;
   Int countOverloads;
   Int countOverloadedNames;
   Int countOperatorFns;
   Int toksLen;
   Int nodesLen;
   Int typesLen;
   Int loopCounter;
   Bool wasError;
   String errMsg;
   Int listType;

   Int standardTextLen; // length of standardText
   Int firstParsedName; // the name index for the first parsed word
   Int firstBuiltin;    // the name for the first built-in word in standardStrings
} CompStats;

CompStats getStats(CM);

//}}}
//{{{ Standard strings :standardStr

#define strAlias     0
#define strAssert    1
#define strBreak     2
#define strCatch     3
#define strcinue  4
#define strDo        5
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
#define strFirstNonReserved 20
#define strInt      strFirstNonReserved // types must come first here?, see "buildPreludeTypes"
#define strLong     21
#define strDouble   22
#define strBool     23
#define strString   24
#define strVoid     25
#define strF        26 // F(unction type)
#define strL        27 // L(ist)
#define strArray    28
#define strD        29 // D(ictionary)
#define strRec      30 // Record
#define strEnum     31 // Enum
#define strTu       32 // Tu(ple)
#define strPromise  33 // Promise
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
#ifndef TEST
#define strSentinel 47
#else
#define strSentinel 50
#endif

//}}}
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

#define l(ind, lst) lst->c[e_(ind, lst->len)]

DEFINE_LIST_HEADER(Ulong)


//}}}
//{{{ Lexer tests

void printLexer(LX);
void createCompiler(Compiler* lx, Arena* a);
Compiler* lexicallyAnalyze(String input, Arena*);
private Compiler* createLexer(String sourceCode, Bool prependStandard, Arena* a);

#define tokInt          0
#define tokLong         1
#define tokDouble       2
#define tokBool         3  // pl2 = value (1 or 0)
#define tokString       4
#define tokMisc         5  // pl1 = see the misc* constants. pl2 = underscore count iff miscUscore

#define voidType            tokMisc

//}}}
//{{{ Parser
//{{{ AST nodes

// AST nodes
#define nodVar          7  // pl1 = index into @vars.
                           // pl2 = iff pl3 = assiFnVarUse, assiFnVarDef then fnId
                           // pl3 >0 => it's a definition (except if pl3 = assiFnVar...) and is one
                           // of the "assi" constants
#define nodCall         8  // pl1 =
                           //   index into @functions (after type resolution) when pl3 = callNormal,
                           //   into @monos if pl3 = callMonomorph,
                           //   into @vars if pl3 = callVar
                           //     pl2 = arg count, pl3 = one of "call" constants.
                           // iff pl3 = callField, then pl1 = nameId, pl2 = 0

// Punctuation (inner node). pl2 = node count inside (so for [span node1 node2], span.pl2 = 2)
#define nodScope        9  // if it's the outer scope of a forNode, then pl3 = length of nodes till
                           // inner scope. See parser tests for examples
#define nodExpr        10  // pl1 = 1 iff it's a composite expression (has internal var decls)
#define nodAssignment  11  // Followed by binding or complex left side. pl3 = distance to the inner
                           // right side, which is always an atom, nodExpr or a nodDataAlloc
#define nodDataAlloc   12  // pl1 = name of collection type, pl3 = count of elements

#define nodAssert      13  // pl1 = 1 iff it's a debug assert
#define nodBreakCont   14  // pl1 = number of label to break or cinue to, -1 if none needed
                           // It's a cinue iff it's >= BIG
#define nodCatch       15  // `catch e {`
#define nodImport      16  // This is for test files only, no need to import anything in main
#define nodFnDef       17  // pl1 = index into @functions
#define nodDef         18  // pl1 = entityId, pl3 = nameId. For non-function compile-time consts
#define nodTrait       19
#define nodReturn      20
#define nodTry         21
#define nodFor         22  // pl1 = id of loop (unique within a function) if it needs to
                           // have a label in codegen; pl3 = number of nodes to skip to get to body

#define nodIf          23
#define nodIfClause    24  // pl3 = "ifcl" constants
#define nodImpl        25
#define nodMatch       26  // pattern matching on sum type tag
#define countAstForms  27  // sentinel

#define countSpanForms (countAstForms - nodScope)

#define metaDoc         1  // Doc comments
#define metaDefault     2  // Default values for type arguments


// :OperatorType
// Values must exactly agree in order with the operatorSymbols array in the tl.c file.
// The order is defined by ASCII. Operator is bitwise <=> it ends with dot
#define opBitwiseNeg      0 // !. bitwise negation
#define opNotEqual        1 // !=
#define opBoolNeg         2 // !
#define opSize            3 // #
#define opToString        4 // $
#define opRemainder       5 // %
#define opBitwiseAnd      6 // &&. bitwise "and"
#define opBoolAnd         7 // &&  logical "and"
#define opRef             8 // '  References
#define opTimesExt        9 // *:
#define opTimes          10 // * Multiplication and nullable pointers
#define opIncrement      11 // ++
#define opPlusExt        12 // +:
#define opPlus           13 // +
#define opDecrement      14 // --
#define opMinusExt       15 // -:
#define opMinus          16 // -
#define opNegate         17 // -
#define opDivByExt       18 // /:
#define opIntersect      19 // /\   type-level trait intersection ?
#define opDivBy          20 // /
#define opBitShiftL      21 // <<.
#define opComparator     22 // <=>
#define opLTZero         23 // <0   less than zero
#define opLTEQ           24 // <=
#define opLessTh         25 // <
#define opRefEquality    26 // ===
#define opEquality       27 // ==
#define opBitShiftR      28 // >>.  unsigned right bit shift
#define opGTZero         29 // >0   greater than zero
#define opGTEQ           30 // >=
#define opGreaterTh      31 // >
#define opNullCoalesce   32 // ?:   null coalescing operator
#define opQuestionMark   33 // ?   Initially nullable pointers
#define opAwait          34 // @
#define opBitwiseXor     35 // ^.   bitwise XOR
#define opBitwiseOr      36 // ||.  bitwise or
#define opBoolOr         37 // ||   logical or
#define opGetElem        38 // Get list element
#define opGetElemPtr     39 // Get pointer to list element
#define countOperators   40 // sentinel

constexpr Int countRealOperators = countOperators - 2; // The "unreal" ones are `a[..]`

typedef struct Compiler Compiler;

typedef struct { // :Node
   Unt tp : 6;
   Unt pl3: 26;
   Int pl1;
   Int pl2;
} Node;

//}}}

typedef struct { // :TestEntityImport
    Int nameInd; // 0, 1 or 2. Corresponds to the "foobarinner" in standardText
    Int typeInd; // index in the intermediary array of types that is imported alongside
} TestEntityImport;

typedef struct {
   Int startBt;
   Int lenBts;
} SourceLoc;

#define CM Compiler* restrict cm
void printParser(Compiler* cm);
Int tryGetOper0(Int opType, Int typeId, Compiler* protoOvs);
void createOverloads(CM);
void initializeParser(Compiler* lx, Arena* a);
void setParserError(String errMsg, Compiler* restrict cm);
void updateStats(Compiler* restrict cm);
Int getBinding(Int id, Compiler* restrict cm);
void setLoc(SourceLoc loc, Int j, CM);
void pushIntypes(Int v, CM);
void importTestFns(Arr(Int) types, Int countTypes,
                   Arr(TestEntityImport) imports, Int countImports, Arena* a, OUT CM);
Int equalityParser(Compiler* a, Compiler* b, Bool compareLocsToo);
void newNode(Node node, SourceLoc loc, CM);

extern char const errBareAtom[];
extern char const errImportsNonUnique[];
extern char const errCannotMutateImmutable[];
extern char const errPrematureEndOfTokens[];
extern char const errUnexpectedToken[];
extern char const errInconsistentSpan[];
extern char const errCoreFormTooShort[];
extern char const errCoreFormUnexpected[];
extern char const errCoreFormAssignment[];
extern char const errCoreFormInappropriate[];
extern char const errIfLeft[];
extern char const errIfRight[];
extern char const errIfEmpty[];
extern char const errIfMalformed[];
extern char const errIfElseMustBeLast[];
extern char const errTypeDefCountNames[];
extern char const errFnNameAndParams[];
extern char const errFnDuplicateParams[];
extern char const errFnMissingBody[];
extern char const errLoopSyntaxError[];
extern char const errLoopNoCondition[];
extern char const errLoopWrongFormInStepper[];
extern char const errLoopEmptyStepBody[];
extern char const errLoopBreakOutside[];
extern char const errBreakContinueTooComplex[];
extern char const errBreakContinueInvalidDepth[];
extern char const errDuplicateFunction[];
extern char const errExpressionError[];
extern char const errExpressionWrongArgCount[];
extern char const errExpressionCannotContain[];
extern char const errExpressionFunctionless[];
extern char const errExpressionHeadFormOperators[];
extern char const errTypeDefCannotContain[];
extern char const errTypeDefError[];
extern char const errUnknownType[];
extern char const errUnknownTypeFunction[];
extern char const errOperatorWrongArity[];
extern char const errUnknownBinding[];
extern char const errUnknownFunction[];
extern char const errIncorrectPrefixSequence[];
extern char const errOperatorUsedInappropriately[];
extern char const errAssignment[];
extern char const errListDifferentEltTypes[];
extern char const errAssignmentShadowing[];
extern char const errAssignmentToplevelFn[];
extern char const errAssignmentLeftSide[];
extern char const errMutation[];
extern char const errReturn[];
extern char const errScope[];
extern char const errTemp[];
extern char const errTypeUnknownFirstArg[];
extern char const errExpectedType[];
extern char const errUnexpectedType[];
extern char const errTypeZeroArityOverload[];
extern char const errTypeNoMatchingOverload[];
extern char const errTypeWrongArgumentType[];
extern char const errTypeWrongReturnType[];
extern char const errTypeMismatch[];
extern char const errTypeMustBeBool[];
extern char const errTypeTooManyParameters[];
extern char const errAssignmentAccessOnToplevel[];
extern char const errAssignmentToFunctionVar[];
extern char const errTypeOfNotList[];
extern char const errTypeOfListIndex[];
extern char const errTypePolymorphicAssignment[];
extern char const errTypeGenericCallDoesntUnify[];
extern char const errTypeFieldNotFound[];

#define S   70000000 // A constant larger than the largest allowed file size.
extern char const errTypeOfNotList[];
extern char const errTypeOfListIndex[];


#define S   70000000 // A constant larger than the largest allowed file size.
                // Separates parsed entities from others
#define I  140000000 // The base index for imported entities/overloads
#define S2 210000000 // A constant larger than the largest allowed file size.
                //  Separates parsed entities from others
#define O  280000000 // The base index for operators



#define assiVarAssignment  1 // definition of a var
#define assiTypeDefinition 2 // definition of a type
#define assiFnParam        3 // introduction of a function parameter
#define assiReassignment   4 // reassignment to a previously defined var
#define assiFnVarDef       5 // definition of a local var that is a function
#define assiFnVarUse       6 // usage (NOT an assignment) of a variable that is a function


void parseMain(CM, Arena* a);
Long longOfDoubleBits(double d);
NameId nameOfStandard(Int strId);

//}}}
//{{{ Codegen tests

LUlong* generateBytecode(String sourceCode, Arena* a); // return value is nullable!

//}}}
