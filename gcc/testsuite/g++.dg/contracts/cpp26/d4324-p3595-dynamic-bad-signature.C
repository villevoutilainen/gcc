// D4324, QoI: P3595R0's "dynamic" output: a declaration of the selection
// function's name must be a function std::contracts::evaluation_semantic ()
// of the configuration's linkage.  (With C linkage, a name that isn't an
// identifier is a symbol, which nothing declares.)
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"namespace":"a"},"output":{"dynamic":{"name":"mylib::contract_semantic"}}},{"match":{"namespace":"b"},"output":{"dynamic":{"name":"other","linkage":"C"}}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace mylib
{
  int contract_semantic ();
}

// C++ linkage, where the configuration says C.
std::contracts::evaluation_semantic other ();

namespace a { int f (int x) pre (x > 0); } // { dg-error "'mylib::contract_semantic', the 'dynamic' function of a matching contract configuration entry, names something other than a function 'std::contracts::evaluation_semantic \\(\\)' with C\\+\\+ linkage" }
namespace b { int f (int x) pre (x > 0); } // { dg-error "'other', the 'dynamic' function .* with C linkage" }
