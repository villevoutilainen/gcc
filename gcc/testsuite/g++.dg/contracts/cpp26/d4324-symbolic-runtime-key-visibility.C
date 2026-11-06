// D4324: a symbolic run-time record's key has default visibility under
// -fvisibility=hidden.  A hidden key made a shared library's established
// facts invisible to the program checking them (P4445 BUG-34).
// { dg-do compile { target { c++26 && fpic } } }
// { dg-require-visibility "" }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks -fvisibility=hidden -fPIC" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }
// { dg-final { scan-assembler "_ZGSyP_ZNK2io9is_openedEPS_" } }
// { dg-final { scan-assembler-not "\\.hidden\[ \t\]+_ZGSyP" } }

#include <contracts>

struct rep
{
  static constexpr bool is_symbolic (std::contracts::assertion_static_info)
  { return true; }
  void operator() (const std::contracts::assertion_context&) const {}
};

struct io
{
  bool is_opened (io*) const symbolic;
  void open () post<rep{}>(is_opened (this));
};

void io::open () {}
