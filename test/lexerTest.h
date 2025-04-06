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

private String str(const char* cent);
private Bool endsWith(String a, String b);

#define s(lit) str(lit)

private void* allocateOnArena(size_t, Arena*);
#define allocate(T, a) (T*)allocateOnArena(sizeof(T), a)
#define allocateArray(cap, T, a) (T*)allocateOnArena(cap*sizeof(T), a)
#define cainerOf(ptr, Type, member) ((Type *)((char *)(ptr) - offsetof(Type, member)))
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


//}}}
//{{{ Lexer tests

typedef int32_t NameId;   // name index (in @stringTable)

String prepareInput(char const* content, Arena* a);
Compiler* lexicallyAnalyze(String input, Arena*);
void printLexer(Compiler* a);
int64_t longOfDoubleBits(double d);
void printIntArray(Int count, Arr(Int) arr);
void printIntArrayOff(Int startInd, Int count, Arr(Int) arr);
void initializeParser(Compiler* lx, Arena* a);
Compiler* createLexer(String sourceCode, Bool prependStandard, Arena* a);
NameId nameOfStandard(Int strId);
void printRawOverload(Int listInd, Compiler* cm);
void printName(Int name, Compiler* cm);

void setLexerError(String errMsg, CM);
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
