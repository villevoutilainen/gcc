// D4324: under -fcontract-symbolic-runtime-checks, a symbolic predicate's
// by-value class argument with unique object representations is a value:
// its fact is recorded by the value's bytes at the call site, so a copy
// has it, and it ends with the innermost block containing that call.  A
// reference parameter's fact stays one about the object.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <string_view>
using namespace std::contracts;

int violations;
void handle_contract_violation (const contract_violation &) { ++violations; }

bool is_zero_terminated (std::string_view) symbolic;
std::string_view make_s () post<never_proven_symbolic_v>(r: is_zero_terminated (r))
{ return "zero"; }
int read_s (std::string_view sv) pre<symbolic_assert_v>(is_zero_terminated (sv))
{ return 0; }

std::string_view kept;
void keep () { std::string_view s = make_s (); kept = s; read_s (s); }

#define EXPECT(n, stmt) \
  do { int before = violations; stmt; \
       if (violations - before != (n)) __builtin_abort (); } while (0)

int
main ()
{
  EXPECT (0, read_s (make_s ()));
  std::string_view s = make_s ();
  EXPECT (0, read_s (s));
  std::string_view t = s;
  EXPECT (0, read_s (t));		// a copy of the value
  EXPECT (1, read_s (std::string_view ("abc", 2)));
  EXPECT (0, keep ());
  EXPECT (1, read_s (kept));		// its block has ended
}
