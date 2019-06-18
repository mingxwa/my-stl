![avatar](https://repository-images.githubusercontent.com/150747489/96be8380-91ca-11e9-8997-b256113e3c25)

---

This is the repository of all the ISO C++ proposals I have submitted, together
with their corresponding implementation.

The organization structure of this repository is as follows:

`./doc` contains every ISO C++ proposals I have written. Most of them are
        related to concurrency and polymorphism, and some are about new core
        language features.

`./src` is the source root.

`./src/main` contains the implementation of the proposals, which are grouped
             by the related paper numbers.

`./src/test` contains the test cases that could run with the proposals. Each
             `.cc` file is a standalone compile unit that could run
             independently. To compile the `.cc` files, please use the latest
             version of GCC, MSVC or LLVM C++ compiler, and enable C++17
             features.
