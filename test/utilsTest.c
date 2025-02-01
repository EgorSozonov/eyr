#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <setjmp.h>
#include "../eyr.internal.h"
#include "eyrTest.h"

extern jmp_buf excBuf;
//{{{ Utils
#define add(K, V, X) _Generic((X), \
    IntMap*: addIntMap \
    )(K, V, X)

#define hasKey(K, X) _Generic((X), \
    IntMap*: hasKeyIntMap \
    )(K, X)

#define get(A, V, X) _Generic((X), \
    IntMap*: getIntMap \
    )(A, V, X)

#define getUnsafe(A, X) _Generic((X), \
    IntMap*: getUnsafeIntMap \
    )(A, X)

#define validate(cond) if(!(cond)) {++(*countFailed); return;}

void printStack(StackInt* st) {
    if (st->length == 0) {
        print("[]");
        return;
    }
    printf("[ %d", st->content[0]);
    for (int i = 1; i < st->length; i++) {
        printf(", %d", st->content[i]);
    }
    print(" ]");
}

void printMultiList(Int listInd, MultiList* ml) {

}

//}}}

void testIntMap(Arena* a) {
    IntMap* hm = createIntMap(150, a);
    add(1, 1000, hm);
    add(2, 2000, hm);
    printf("Find key 1? %d Find key 3? %d\n", hasKey(1, hm), hasKey(3, hm));
}

//{{{ Sortings

void validateList(StackInt* test, StackInt* control, Int* countFailed) {
    validate(test->length == control->length)
    for (Int j = 0; j < test->length; j++) {
        validate(test->content[j] == control->content[j])
    }
}
void testSortPairsDisjoint(Int* countFailed, Arena* a) {
    StackInt* st = createStackint32_t(64, a);
    push(3, st);
    push(1, st);
    push(10, st);
    push(5, st);
    push(1, st);
    push(2, st);
    push(3, st);
    sortPairsDisjoint(1, 7, st->content);

    StackInt* control1 = createStackint32_t(64, a);
    push(3, control1);
    push(1, control1);
    push(5, control1);
    push(10, control1);
    push(1, control1);
    push(3, control1);
    push(2, control1);
    validateList(st, control1, countFailed);

    st = createStackint32_t(64, a);
    push(1, st);
    push(100, st);
    push(1, st);
    sortPairsDisjoint(1, 3, st->content);
    StackInt* control2 = createStackint32_t(64, a);
    push(1, control2);
    push(100, control2);
    push(1, control2);
    validateList(st, control2, countFailed);
}

void testSortPairs(Int* countFailed, Arena* a) {
    StackInt* st = createStackint32_t(64, a);
    push(3, st);
    push(1, st);
    push(10, st);
    push(5, st);
    push(1, st);
    push(2, st);
    push(3, st);
    sortPairs(1, 7, st->content);

    StackInt* control1 = createStackint32_t(64, a);
    push(3, control1);
    push(1, control1);
    push(10, control1);
    push(2, control1);
    push(3, control1);
    push(5, control1);
    push(1, control1);
    validateList(st, control1, countFailed);

    st = createStackint32_t(64, a);
    push(1, st);
    push(100, st);
    push(1, st);
    sortPairs(1, 3, st->content);
    StackInt* control2 = createStackint32_t(64, a);
    push(1, control2);
    push(100, control2);
    push(1, control2);
    validateList(st, control2, countFailed);
}


private void testUniquePairs(Arena* a) {
    StackInt* st = createStackint32_t(64, a);
    push(1, st);
    push(1, st);
    push(3, st);
    bool areUnique = verifyUniquenessPairsDisjoint(0, 2, st->content);
    if (!areUnique) {
        print("Overload uniqueness error");
    }

    st = createStackint32_t(64, a);
    push(3, st);
    push(1, st);
    push(2, st);
    push(3, st);
    push(10, st);
    push(10, st);
    push(10, st);
    areUnique = verifyUniquenessPairsDisjoint(0, 6, st->content);
    if (!areUnique) {
        print("Overload uniqueness error");
    }

    st = createStackint32_t(64, a);
    push(3, st);
    push(2, st);
    push(2, st);
    push(3, st);
    push(10, st);
    push(10, st);
    push(10, st);
    areUnique = verifyUniquenessPairsDisjoint(0, 6, st->content);
    if (areUnique) {
        print("Overload uniqueness error");
    }
}

//}}}


void multiListTest(Int* countFailed, Arena* a) {
    MultiList* ml = createMultiList(a);
    Int listInd = listAddMultiList(1, 10, ml);

    Int newInd = addMultiList(2, 20, listInd, ml);
    validate(newInd == -1);

    addMultiList(3, 30, listInd, ml);

    newInd = addMultiList(4, 40, listInd, ml);
    validate(newInd == 10);

    printIntArrayOff(listInd, ml->content[listInd + 1] + 2, ml->content);
    printIntArrayOff(newInd, ml->content[newInd + 1] + 2, ml->content);

    Int secondList = listAddMultiList(10, 5, ml);
    validate(secondList == 0);  // allocated the new list from the free list!
    newInd = addMultiList(20, 10, listInd, ml);
    printIntArrayOff(secondList, ml->content[secondList + 1] + 2, ml->content);
    print("freeL %d", ml->freeList)
}

int main() {
    printf("----------------------------\n");
    printf("Utils test\n");
    printf("----------------------------\n");
    Arena *a = mkArena();
    Int countFailed = 0;
    if (setjmp(excBuf) == 0) {
        //testStringMap(a);

        //testScopeStack(a);

        testSortPairsDisjoint(&countFailed, a);
        testSortPairs(&countFailed, a);
        testUniqueKeys(&countFailed, a);
//~        multiListTest(&countFailed, a);

    } else {
        printf("An exception was thrown in the tests!\n");
    }
    if (countFailed > 0) {
        print("")
        print("-------------------------")
        print("Count of failed tests %d", countFailed);
    }

    deleteArena(a);
}
