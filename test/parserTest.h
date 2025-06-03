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

typedef libeyr_String String;
typedef libeyr_CompResult CompResult;
#define BIG 70000000

//}}}
//{{{ Lexer

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

typedef struct { // :TestEntityImport
    Int nameInd; // 0, 1 or 2. Corresponds to the "foobarinner" in standardText
    Int typeInd; // index in the intermediary array of types that is imported alongside
} TestEntityImport;

typedef struct { //:ChInterval
   Int startBt;
   Int lenBts;
} ChInterval;

#define CM Compiler* restrict cm
void printParser(Compiler* cm);
Int tryGetOper0(Int opType, Int typeId, Compiler* protoOvs);
void createOverloads(CM);
void initializeParser(Compiler* lx, Arena* a);
void setParserError(String errMsg, Compiler* restrict cm);
void updateStats(Compiler* restrict cm);
Int getBinding(Int id, Compiler* restrict cm);
void setLoc(ChInterval loc, Int j, CM);
void pushIntypes(Int v, CM);
void importTestFns(Arr(Int) types, Int countTypes,
                   Arr(TestEntityImport) imports, Int countImports, Arena* a, OUT CM);
Int equalityParser(Compiler* a, Compiler* b, Bool compareLocsToo);
void newNode(Node node, ChInterval loc, CM);

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
extern char const errFnParamList[];
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
