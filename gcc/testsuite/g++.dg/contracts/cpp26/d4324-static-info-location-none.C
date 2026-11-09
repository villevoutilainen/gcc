// D4324: under -fcontract-violation-information=none no source location is
// embedded, and assertion_static_info::location() is empty too.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-violation-information=none" }

#include <contracts>
using namespace std::contracts;

struct check_t
{
  static constexpr bool
  is_ignored (assertion_static_info i)
  {
    if (i.kind () != assertion_kind::pre || i.location ().line () != 0)
      throw 0;	// Not a constant expression: a hard error.
    return false;
  }
  constexpr void operator() (const assertion_context&) const { }
};
inline constexpr check_t chk{};

int f (int x) pre<chk> (x > 0) { return x; }
int g () { return f (1); }
