// D4324: <contracts>' control objects print a violated assertion's
// operands as the library's own do, simply, without std::format: a
// class, even one with a formatter, as '?' (std::contracts::
// format_operands_to, in <format>, uses its formatter).  A message given
// to the control object comes first; the comment stays the condition's
// text.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <format>
#include <string>
#include <cstring>

int calls;
char comment[512];
char message[512];

void
handle_contract_violation (const std::contracts::contract_violation& v)
{
  ++calls;
  std::strcpy (comment, v.comment ());
  std::strcpy (message, v.message () ? v.message () : "(null)");
}

struct P { int a; };	// formattable, but not printed simply
template<>
struct std::formatter<P> : std::formatter<int>
{
  template<typename _Ctx>
    auto format (P p, _Ctx& c) const
    { return std::formatter<int>::format (p.a * 100, c); }
};
struct Q { int a; };	// not formattable
bool ok (P) { return false; }
bool ok (Q) { return false; }

using namespace std::contracts;

int d (int x, double v, const std::string& s)
  pre<default_v>(x > 0 && v < 0.1 && s == "") { return 0; }
int r (P p, Q q) pre<review_v>(ok (p) && ok (q)) { return 0; }
int m (int x) pre<default_control ("lib"group, "x must be positive")>(x > 0) { return 0; }

static void
expect (const char *c, const char *msg)
{
  if (std::strcmp (comment, c) || std::strcmp (message, msg))
    __builtin_abort ();
}

int
main ()
{
  d (-1, 0.1, "str");
  expect ("x > 0 && v < 0.1 && s == \"\"", "-1 > 0 && 0.10000000000000001 < 0.1 && \"str\" == \"\"");
  r (P{7}, Q{8});
  expect ("ok (p) && ok (q)", "ok (?) && ok (?)");
  m (-2);
  expect ("x > 0", "x must be positive: -2 > 0");
  if (calls != 3)
    __builtin_abort ();
}
