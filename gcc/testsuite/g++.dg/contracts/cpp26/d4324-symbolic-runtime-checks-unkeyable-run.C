// D4324: under -fcontract-symbolic-runtime-checks, a fact about a class
// parameter passed by value, or about a function's class result, is proven
// statically only: the call may copy the object (in registers, or into the
// argument area), so no address keys it.  Nothing is recorded, and the
// check passes.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <string_view>
using namespace std::contracts;

int violations;
void handle_contract_violation (const contract_violation &) { ++violations; }

bool is_zero_terminated (const std::string_view &) symbolic;
std::string_view make_s () post<never_proven_symbolic_v>(r: is_zero_terminated (r))
{ return "zero"; }
int read_s (std::string_view sv) pre<symbolic_assert_v>(is_zero_terminated (sv))
{ return 0; }

struct Big { char buf[64]; };		// returned by invisible reference
bool good (const Big &) symbolic;
Big make_b () post<never_proven_symbolic_v>(r: good (r)) { return {}; }
int read_b (Big b) pre<symbolic_assert_v>(good (b)) { return 0; }

int
main ()
{
  read_s (make_s ());
  read_b (make_b ());
  if (violations != 0)
    __builtin_abort ();
}
