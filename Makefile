#{{{ Params

.RECIPEPREFIX = /

ifndef VERBOSE
.SILENT: # Silent mode unless you run it like "make all VERBOSE=1"
endif

.PHONY: all clean help lexerTest parserTest codegenTest tests

CC=gcc --std=gnu2x
CONFIG=-g3
WARN=-Wpedantic -Wreturn-type -Wunused-variable -Wshadow -Wfatal-errors \
    -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
    -Werror=int-conversion -fstrict-flex-arrays=3
SANITIZE=-fsanitize=address # include it occasionally
INCLUDES=-iquote .
OPT=-march=native
LIBS_TEST=-lm
LIBS_EXE=-lm -lgccjit
APP=eyrc

TEST_INCLUDES = -iquote test
TEST_FLAGS = $(CONFIG) $(WARN) $(OPT) $(DEPFLAGS) $(INCLUDES) -g3 -DTEST -DSAFETY 
COMPILE_TEST = $(CC) $(TEST_FLAGS) $(TEST_INCLUDES) $(LIBS_TEST)

DEBUG_FLAGS = $(CONFIG) $(WARN) $(OPT) $(DEPFLAGS) $(INCLUDES) -g3 -DDEBUG -DSAFETY 
COMPILE_DEBUG = $(CC) $(DEBUG_FLAGS) $(LIBS_EXE)

RELEASE_FLAGS = $(CONFIG) $(WARN) $(OPT) $(DEPFLAGS) $(INCLUDES) -O2
COMPILE_RELEASE = $(CC) $(RELEASE_FLAGS) $(LIBS_EXE)

DEBUG_TGT = _target/debug
EXE=$(DEBUG_TGT)/$(APP)

#}}}
#{{{ Commands

$(DEBUG_TGT):
/ mkdir -p $(DEBUG_TGT)


all: $(DEBUG_TGT) ## Build the whole compiler
/ clear
/ $(COMPILE_DEBUG) -o $(EXE) $(APP).c
/ @echo "_________________________________________"
/ @echo "|            BUILD SUCCESS              |"
/ @echo "========================================="
/ $(EXE)


clean: ## Delete cached build results
/ test -f $(DEBUG_TGT) | rm $(DEBUG_TGT)/ *


testLexer: $(DEBUG_TGT) ## Test the lexical analyzer
/ $(COMPILE_TEST) -o $(DEBUG_TGT)/lexerTest test/lexerTest.c libeyr.c
/ $(DEBUG_TGT)/lexerTest


testParser: $(DEBUG_TGT) ## Test the parser & typechecker
/ $(COMPILE_TEST) -DDEBUG -o $(DEBUG_TGT)/parserTest test/parserTest.c libeyr.c
/ $(DEBUG_TGT)/parserTest


testCodegen: $(DEBUG_TGT) ## Test the code generator
/ $(COMPILE_TEST) -DDEBUG -o $(DEBUG_TGT)/codegenTest test/codegenTest.c libeyr.c
/ $(DEBUG_TGT)/codegenTest


tests: | testLexer testParser testCodegen ## Run all tests

#}}}
#{{{ Meta

help: ## Show this help
/ @egrep -h '\s##\s' $(MAKEFILE_LIST) | sort | awk 'BEGIN {print "-- Help --";print ""; FS = ":.*?## "}; {printf "\033[32m%-10s\033[0m %s\n", $$1, $$2}'
/ echo
# MAKEFILE_LIST lists the contents of this present file
# egrep selects only lines with the double sharp, they are then sorted
# BEGIN in AWK means an action to be executed once before the linewise
# FS means "field separator" - the separator between parts of a single line
# the printf looks so scary because of the ASCII color codes

#}}}
