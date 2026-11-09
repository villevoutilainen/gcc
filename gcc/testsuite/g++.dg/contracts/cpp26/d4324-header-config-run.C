// D4324: contract control objects configured by data defined in a header
// (d4324-header-config-data.h) -- pure library code, with no parsing:
// each label decides its semantic per kind of assertion for its group
// when it is constructed, from the first rule that covers it
// hierarchically, and a group or kind no rule covers gets the build's
// semantic.  Compare d4324-embed-config-group-label-run.C, which reads
// the same kind of configuration from JSON with #embed.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "d4324-header-config-data.h"

using namespace std::contracts;

int reports;
evaluation_semantic last;

void
handle_contract_violation (const contract_violation& v)
{
  ++reports;
  last = v.semantic ();
  if (v.semantic () == evaluation_semantic::enforce)
    throw 0;
}

inline constexpr config::label<config::rules> lib_io {"lib.io"};
inline constexpr config::label<config::rules> lib_net {"lib.net"};
inline constexpr config::label<config::rules> app {"app"};
inline constexpr config::label<config::rules> tools {"tools"};

int read (int x) pre<lib_io> (x > 0) post<lib_io> (r: r > 1) { return x; }
int send (int x) pre<lib_net> (x > 0) post<lib_net> (r: r > 1) { return x; }
int run (int x) pre<app> (x > 0) post<app> (r: r > 1) { return x; }
void tool (int x) { contract_assert<tools> (x > 0); }

int
main ()
{
  // "lib.io": observe, for every kind.
  read (0);
  if (reports != 2 || last != evaluation_semantic::observe)
    __builtin_abort ();

  // "lib.net", covered by "lib": its precondition enforced (the handler
  // throws), its postcondition ignored.
  try { send (0); __builtin_abort (); } catch (int) { }
  if (reports != 3 || last != evaluation_semantic::enforce)
    __builtin_abort ();
  send (1);
  if (reports != 3)
    __builtin_abort ();

  // "app": its precondition observed; its postcondition is covered by no
  // rule, so the build's semantic, ignore.
  run (0);
  if (reports != 4 || last != evaluation_semantic::observe)
    __builtin_abort ();

  // "tools": no rule at all, so the build's semantic, ignore.
  tool (0);
  if (reports != 4)
    __builtin_abort ();
}
