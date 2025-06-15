# Eyr
### A new mid-level programming language (under construction)


### Using it

- Build it with `make all` and install the latest libgccjit

- The `test/examples` folder contains example programs

- Compile a file like `eyrc docs/examples/fizzBuzz.eyr`

- The output will be in an adjacent file

- Run it


### Releases

0.1: Primitive types & lists, a static type system with function overloads generic functions

0.2: Native codegen via Libgccjit, strings and better syntax, a CLI interface


### Source code

Source code is just 3 files (eyrc.c, include/libeyr.h, libeyr.c). Navigate them using code folds. For example, 
in Neovim, use "za" to toggle a fold, and "zm" to close them en masse.

The architecture is core library (files "libeyr.c" and "include/libeyr.h") and the executable ("eyrc.c").
The library contains the lexer + parser + typechecker and is useful for writing code analyzers, linters, formatters,
LSPs and so on. The executable uses libeyr as an ordinary library consumer, and contains the code generator
using the libgccjit library.

The tests can be run with `make testLexer` and `make testParser`.


### Future releases roadmap


#### version 0.3
* more developed static type system (structs, sum types)
* "for each" loops
* integration tests
* better error reporting from the compiler

