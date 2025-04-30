# Eyr
### A new programming language (under construction)



### Using it

- Build it with `make all`

- Compile a file like `eyrc test/examples/fizzBuzz.eyr`

- The output will be in an adjacent .html file

- Open it in a browser and check the Dev Console for output and code


### Source code

Source code is just 3 files (eyrc.c, include/libeyr.h, libeyr.c). Navigate them using code folds. For example, 
in Neovim, use "za" to toggle a fold, and "zm" to close them en masse.

The library (files "libeyr.c" and "include/libeyr.h") are the lexer+parser+typechecker that is useful
for writing code analyzers, linters, formatters, LSPs and so on.

The compiler executable (file "eyrc.c") uses libeyr as a library, too, and contains the code generator
using the libgccjit library.


### Releases

0.1: Primitive types & lists, a static type system with function overloads generic functions


### Future releases roadmap


#### version 0.2
* more developed static type system (structs, sum types)
* a different codegen (bye-bye, Javascript!)

