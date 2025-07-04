//{{{ Common for tests

typedef struct {
    Int countTests;
    Int countPassed;
    Int singleId; // if > -1, then only test with this id will be run
    Bool ranSingle; // did we run the single test? Or we didn't encounter that id?
    Arena* a;
} TestContext;

typedef libeyr_String String;
typedef libeyr_CompResult CompResult;
#define BIG 70000000

//{{{ Errors

#define errNonAscii                     0
#define errPrematureEndOfInput          1
#define errUnrecognizedByte             2
#define errWordChunkStart               3
#define errWordCapitalizationOrder      4
#define errWordLengthExceeded           5
#define errWordMutability               6
#define errWordFreeFloatingFieldAcc     7
#define errNumericEndUnderscore         8
#define errNumericWidthExceeded         9
#define errNumericBinWidthExceeded     10
#define errNumericFloatWidthExceeded   11
#define errNumericEmpty                12
#define errNumericMultipleDots         13
#define errNumericIntWidthExceeded     14
#define errPunctuationExtraOpening     15
#define errPunctuationExtraClosing     16
#define errPunctuationCommaNotClause   17
#define errPunctuationOnlyInMultiline  18
#define errPunctuationFnNotInStmt      19
#define errPunctuationUnmatched        20
#define errPunctuationScope            21
#define errOperatorUnknown             22
#define errOperatorAssignmentPunct     23
#define errAssignmentEmptyRight        24
#define errOperatorTypeDeclPunct       25
#define errOperatorMutationInDef       26
#define errCoreNotInsideStmt           27
#define errCoreMisplacedElse           28
#define errCoreMissingParen            29
#define errBareAtom                    30
#define errImportsNonUnique            31
#define errCannotMutateImmutable       32
#define errPrematureEndOfTokens        33
#define errUnexpectedToken             34
#define errCoreFormTooShort            35
#define errCoreFormUnexpected          36
#define errCoreFormAssignment          37
#define errCoreFormInappropriate       38
#define errIfLeft                      39
#define errIfRight                     40
#define errIfEmpty                     41
#define errIfMalformed                 42
#define errIfElseMustBeLast            43
#define errFnParamList                 44
#define errFnDuplicateParams           45
#define errFnEntrypoint                46
#define errFnMissingBody               47
#define errFnOperatorOverlArity        48
#define errLoopSyntaxError             49
#define errLoopNoCondition             50
#define errLoopEmptyStepBody           51
#define errLoopWrongFormInStepper      52
#define errLoopBreakOutside            53
#define errBreakContinueTooComplex     54
#define errBreakContinueInvalidDepth   55
#define errEachLoopWrongSyntax         56
#define errEachLoopInvalidValue        57
#define errEachNotACollection          58
#define errDuplicateFunction           59
#define errExpressionError             60
#define errExpressionWrongArgCount     61
#define errExpressionCannotContain     62
#define errExpressionFunctionless      63
#define errTypeDefCountNames           64
#define errTypeDefCannotContain        65
#define errTypeExpr                    66
#define errTypeDefError                67
#define errTypeDefParamsError          68
#define errOperatorWrongArity          69
#define errUnknownBinding              70
#define errUnknownFunction             71
#define errOperatorUsedInappropriately 72
#define errAssignment                  73
#define errListDifferentEltTypes       74
#define errListUnknownEltType          75
#define errMutation                    76
#define errAssignmentShadowing         77
#define errAssignmentLeftSide          78
#define errAssignmentAccessOnToplevel  79
#define errAssignmentToFunctionVar     80
#define errFnSignature                 81
#define errFnTypeArrows                82
#define errArrowOutOfPlace             83
#define errReturn                      84
#define errScope                       85
#define errMetaOnlyInArr               86
#define errMetaArrSyntax               87
#define errTemp                        88
#define errEmptySourceCode             89
#define errUnknownType                 90
#define errUnexpectedType              91
#define errExpectedType                92
#define errUnknownTypeConstructor      93
#define errTypeUnknownFirstArg         94
#define errTypeOverloadsIntersect      95
#define errTypeOverloadsOnlyOneZero    96
#define errTypeNoMatchingOverload      97
#define errTypeWrongArgumentType       98
#define errTypeWrongReturnType         99
#define errTypeMismatch               100
#define errTypeMustBeBool             101
#define errTypeConstructorWrongArity  102
#define errTypeTooManyParameters      103
#define errTypeOfNotList              104
#define errTypeOfListIndex            105
#define errTypePolymorphicAssignment  106
#define errTypeGenericCallDoesntUnify 107
#define errTypeFieldNotFound          108

//}}}
//}}}
//{{{ Lexer

void printLexer(LX);
void createCompiler(Compiler* lx, Arena* a);
Compiler* lexicallyAnalyze(String input, Arena*);
Compiler* createLexer(String sourceCode, Bool prependStandard, Arena* a);

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
Int getOper(Int opType, Int typeId, Compiler* protoOvs);
void createOverloads(CM);
void initializeParser(Compiler* lx, Arena* a);
void setParserError(Int errId, Compiler* restrict cm);
void updateStats(Compiler* restrict cm);
Int getBinding(Int id, Compiler* restrict cm);
void setLoc(ChInterval loc, Int j, CM);
void pushIntypes(Int v, CM);
void importTestFns(Arr(Int) types, Int countTypes,
                   Arr(TestEntityImport) imports, Int countImports, Arena* a, OUT CM);
Int equalityParser(Compiler* a, Compiler* b, Bool compareLocsToo);
void newNode(Node node, ChInterval loc, CM);

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
#define assiFnVarReassign  6 // reassignment to a local var that is a function
#define assiFnVarUse       7 // usage (NOT an assignment) of a variable that is a function

void parseMain(CM, Arena* a);
Long longOfDoubleBits(double d);
NameId nameOfStandard(Int strId);

//}}}
