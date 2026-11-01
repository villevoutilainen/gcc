// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -D_GLIBCXX_CONVEYOR_ASSERTIONS" }
// D4324: basic_string<char> is an extern template, so this header already
// substitutes c_str's contract-specifiers for its declaration.  The TU
// that loads the PCH must not substitute them again (it ICEd in
// register_local_specialization when that record wasn't in the PCH).
#include <string>
