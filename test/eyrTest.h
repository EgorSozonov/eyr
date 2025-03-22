#ifndef EYR_TEST_H
#define EYR_TEST_H

//{{{ Common

Arena* createArena();
void* allocateOnArena(size_t allocSize, Arena* ar);
void deleteArena(Arena* ar);

Bool equal(String a, String b);

typedef int32_t NameId;   // name index (in @stringTable)
typedef struct {
    Int countTests;
    Int countPassed;
    Arena* a;
} TestContext;

String prepareInput(char const* content, Arena* a);
Compiler* lexicallyAnalyze(String input, Arena*);
void printLexer(Compiler* a);
int64_t longOfDoubleBits(double d);
void printIntArray(Int count, Arr(Int) arr);
void printIntArrayOff(Int startInd, Int count, Arr(Int) arr);
void initializeParser(Compiler* lx, Arena* a);
void newNode(Node node, SourceLoc loc, Compiler* cm);
Compiler* createLexer(String sourceCode, Bool prependStandard, Arena* a);
void parseMain(Compiler* restrict lx, Arena* a);
StandardText getStandardTextLength();
void typePrint(Int, Compiler*);
NameId nameOfStandard(Int strId);
void printRawOverload(Int listInd, Compiler* cm);
void printName(Int name, Compiler* cm);

//}}}
//{{{ Parser

typedef struct { // :TestEntityImport
    Int nameInd; // 0, 1 or 2. Corresponds to the "foobarinner" in standardText
    Int typeInd; // index in the intermediary array of types that is imported alongside
} TestEntityImport;

#define CM Compiler* restrict cm
void createCompiler(Compiler* lx, Arena* a);
void printParser(Compiler* cm);
Int tryGetOper0(Int opType, Int typeId, Compiler* protoOvs);
void createOverloads(CM);
CompStats getStats(Compiler* restrict cm);
void setLexerError(String errMsg, Compiler* restrict cm);
void setParserError(String errMsg, Compiler* restrict cm);
void updateStats(Compiler* restrict cm);
Int getBinding(Int id, Compiler* restrict cm);
void setLoc(SourceLoc loc, Int j, CM);
void pushIntypes(Int v, CM);
void importTestFns(Arr(Int) types, Int countTypes,
                   Arr(TestEntityImport) imports, Int countImports, Arena* a, OUT CM);
Int equalityParser(Compiler* a, Compiler* b, Bool compareLocsToo);

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


//}}}
//{{{ Utils


void sortPairsDisjoint(Int startInd, Int endInd, Arr(Int) arr);
void sortPairs(Int startInd, Int endInd, Arr(Int) arr);
bool verifyUniquenessPairsDisjoint(Int startInd, Int endInd, Arr(Int) arr);
bool makeSureOverloadsUnique(Int startInd, Int endInd, Arr(Int) overloads);

//}}}
//{{{ Codegen


//}}}

#endif
