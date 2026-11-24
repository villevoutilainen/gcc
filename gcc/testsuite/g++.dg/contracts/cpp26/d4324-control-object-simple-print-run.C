// D4324: the library's own control objects (in <bits/contracts.h>, no
// <format>) print a violated assertion's operands simply into the
// violation's message: integers, char, bool, enumerations, pointers,
// nullptr, C strings, std::string, std::string_view (strings and char
// quoted and escaped), floating-point values (round-trip precision);
// anything else as '?'.  The comment
// stays the condition's text.  Shown through "name"group, a core control
// object, observed by the configuration.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"group":"obs"},"output":{"semantic":"observe"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <string>
#include <string_view>
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

enum E { e0, e3 = 3 };
enum class F : unsigned char { f7 = 7 };
struct S { int i; };
bool odd (S) { return false; }

using namespace std::contracts::controls;

int ints (int i, unsigned u, long long ll, short sh)
  pre<"obs"group>(i > 0 && u < 2 && ll > 0 && sh > 0) { return 0; }
int misc (bool b, char c, E e, F f)
  pre<"obs"group>(b && c == 'q' && e == e0 && f != F::f7) { return 0; }
int ptrs (const char *s, int *p, const char *n)
  pre<"obs"group>(s == nullptr && p != nullptr && n != nullptr) { return 0; }
int strs (const std::string& s, std::string_view v)
  pre<"obs"group>(s == "" && v == "") { return 0; }
int flts (double d, float f, long double ld)
  pre<"obs"group>(d < 0.1 && f < 1 && ld < 2) { return 0; }
int other (S s) pre<"obs"group>(odd (s)) { return 0; }

static void
expect (const char *c, const char *m)
{
  if (std::strcmp (comment, c) || std::strcmp (message, m))
    __builtin_abort ();
}

int
main ()
{
  ints (-1, 7, -9, -2);
  expect ("i > 0 && u < 2 && ll > 0 && sh > 0", "-1 > 0 && 7 < 2 && -9 > 0 && -2 > 0");
  misc (false, 'z', e3, F::f7);
  expect ("b && c == 'q' && e == e0 && f != F::f7", "false && 'z' == 'q' && 3 == e0 && 7 != F::f7");
  ptrs ("hi", nullptr, nullptr);
  expect ("s == nullptr && p != nullptr && n != nullptr", "\"hi\" == nullptr && 0x0 != nullptr && nullptr != nullptr");
  strs ("text", "view");
  expect ("s == \"\" && v == \"\"", "\"text\" == \"\" && \"view\" == \"\"");
  flts (0.1, 2.5f, 3.0L);
  expect ("d < 0.1 && f < 1 && ld < 2", "0.10000000000000001 < 0.1 && 2.5 < 1 && 3 < 2");
  other (S{1});
  expect ("odd (s)", "odd (?)");
  if (calls != 6)
    __builtin_abort ();
}
