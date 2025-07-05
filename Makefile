#{{{ Params

.RECIPEPREFIX = /

ifndef VERBOSE
.SILENT: # Silent mode unless you run it like "make all VERBOSE=1"
endif

.PHONY: all library debug clean help testLexer testParser testIntegration test

CC=gcc --std=gnu2x
WARN=-Werror=return-type -Wunused-variable -Wshadow -Wfatal-errors \
    -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
    -Wno-discarded-qualifiers \
    -Werror=int-conversion -fstrict-flex-arrays=3
SANITIZE=-fsanitize=address # include it occasionally
INCLUDES=-iquote .
OPT=-march=native
LIBS_LIB=-lm
LIBS_EXE=-lm -lgccjit
LDFLAGS=-Wl,--exclude-libs=ALL

APP=eyrc
LIB_NAME=libeyr

TEST_INCLUDES = -iquote test
TEST_FLAGS = $(WARN) $(OPT) $(INCLUDES) -g3 -DDEBUG
COMPILE_TEST = $(CC) $(TEST_FLAGS) $(TEST_INCLUDES) $(LIBS_LIB)

DEBUG_FLAGS = $(WARN) $(OPT) $(INCLUDES) -g3 -DDEBUG -DVERBOSE
COMPILE_DEBUG = $(CC) $(DEBUG_FLAGS) $(LIBS_EXE)

RELEASE_FLAGS = $(WARN) $(OPT) $(INCLUDES) -O2
COMPILE_RELEASE = $(CC) $(RELEASE_FLAGS) $(LIBS_EXE)
COMPILE_RELEASE_LIB = $(CC) $(RELEASE_FLAGS) $(LIBS_LIB) $(LDFLAGS)

BIN=bin
DEBUG_TGT=_debug
EXE=$(BIN)/$(APP)
LIB_OUTPUT=$(BIN)/$(LIB_NAME).o
SHARED_LIB_OUTPUT=$(BIN)/$(LIB_NAME).so

GCC_PATH=~/repos/build/gcc

#}}}
#{{{ Commands

$(DEBUG_TGT):
/ mkdir -p $(DEBUG_TGT)


$(BIN):
/ mkdir -p $(BIN)


all: | $(BIN) ## Build the whole compiler
/ clear
/ $(COMPILE_RELEASE) -o $(EXE) $(LIB_NAME).c $(APP).c #-Wl,--verbose
/ @echo "_________________________________________"
/ @echo "|            BUILD SUCCESS              |"
/ @echo "========================================="


library: | $(BIN) ## Build the whole compiler
/ clear
/ $(COMPILE_RELEASE_LIB) -c -o $(LIB_OUTPUT) $(LIB_NAME).c
/ $(COMPILE_RELEASE_LIB) -c -fpic -shared -o $(SHARED_LIB_OUTPUT) $(LIB_NAME).c
/ @echo "_________________________________________"
/ @echo "|       LIBRARY BUILD SUCCESS            |"
/ @echo "========================================="

debug: | $(DEBUG_TGT) ## Debug build
/ clear
/ $(COMPILE_DEBUG) -DVERBOSE -o $(DEBUG_TGT)/$(APP) #(LIB_NAME).c $(APP).c
/ @echo "_________________________________________"
/ @echo "|         DEBUG BUILD SUCCESS            |"
/ @echo "========================================="
#/ cd $(DEBUG_TGT) && LD_LIBRARY_PATH=$(GCC_PATH):$(LD_LIBRARY_PATH) \
#  PATH=$(GCC_PATH):$(PATH) \
#  LIBRARY_PATH=$(GCC_PATH):$(LIBRARY_PATH) \
#  ./$(APP)
/ cd $(DEBUG_TGT) && ./$(APP) each.eyr


clean: ## Delete cached build results
/ test -f $(DEBUG_TGT) | rm $(DEBUG_TGT)/ *


testLexer: | $(DEBUG_TGT) ## Test the lexical analyzer. Pass TEST=12 to run single test
/ $(COMPILE_TEST) -o $(DEBUG_TGT)/lexerTest test/lexerTest.c $(LIB_NAME).c
/ $(DEBUG_TGT)/lexerTest $(TEST)


testParser: | $(DEBUG_TGT) ## Test the parser & typechecker. Pass TEST=12 to run single test
/ $(COMPILE_TEST) -DDEBUG -o $(DEBUG_TGT)/parserTest test/parserTest.c $(LIB_NAME).c
/ $(DEBUG_TGT)/parserTest $(TEST)


testIntegration: all ## Test the full compilation and program execution
/ $(COMPILE_TEST) -o $(DEBUG_TGT)/integrationTest test/integrationTest.c $(LIB_NAME).c
/ $(DEBUG_TGT)/integrationTest

test: | testLexer testParser testIntegration ## Run all tests

#}}}
#{{{ Meta

help: ## Show this help
/ @grep -E -h '\s##\s' $(MAKEFILE_LIST) | sort | awk 'BEGIN {print "[Help]";print ""; FS = ":.*?## "}; {printf "\033[32m%-10s\033[0m %s\n", $$1, $$2}'
/ echo
# MAKEFILE_LIST lists the contents of this present file
# egrep selects only lines with the double sharp, they are then sorted
# BEGIN in AWK means an action to be executed once before the linewise
# FS means "field separator" - the separator between parts of a single line
# the printf looks so scary because of the ASCII color codes

#}}}
