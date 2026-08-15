// D4324/P3400: _P3400_FAKE_GROUP_CONFIG emulates group-based
// build-configuration (disclosed as a macro-based stand-in, since this
// branch has no real command-line/build-tool hook to plug into): a
// group-name-to-semantic override, consulted at compile time by any
// label modeling identification_label, genuinely resolved before
// generating any check at all -- forcing an assertion in the "opt"
// group to be ignored here, regardless of the TU's own
// -fcontract-evaluation-semantic=enforce.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce -D_P3400_FAKE_GROUP_CONFIG=\"opt=ignore\"" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace P3400 = std::contracts::P3400;

int f (int x) pre<P3400::opt>(x > 0) { return x; }

int
main ()
{
  // Would violate (and, under plain -fcontract-evaluation-semantic=
  // enforce with no group override, would terminate) if actually
  // checked; the group config above forces this specific group to
  // ignore instead.
  int r = f (-1);
  if (r != -1)
    __builtin_abort ();
  return 0;
}
