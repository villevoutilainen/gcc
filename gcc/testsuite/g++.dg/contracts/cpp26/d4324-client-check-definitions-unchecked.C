// D4324 (P4445 2.1.3.5): with definition-side checks off and caller-side
// checks on, a TU's own definitions are unchecked and its calls are
// checked, including calls of a function defined in another translation
// unit (the -aux.cc file).  default_v needs no knowledge of sides: the
// caller-side check has this TU's semantic.  Each violating call is
// reported once; a call through a function pointer gets no caller-side
// check and, the definitions being unchecked, none at all.
// { dg-do run { target c++26 } }
// { dg-additional-sources "d4324-client-check-definitions-unchecked-aux.cc" }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontracts-definition-check=off -fcontracts-client-check=pre -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "d4324-client-check-definitions-unchecked.h"

int violations;
void handle_contract_violation (const std::contracts::contract_violation &)
{ ++violations; }

namespace local
{
  int g (int x) pre<std::contracts::default_v> (x > 0);
  int g (int x) pre<std::contracts::default_v> (x > 0) { return x; }
}

int (*volatile fp) (int);

int
main ()
{
  vendor::f (1);
  local::g (1);
  if (violations != 0)
    __builtin_abort ();

  vendor::f (0);
  if (violations != 1)
    __builtin_abort ();
  local::g (0);
  if (violations != 2)
    __builtin_abort ();

  fp = vendor::f;
  fp (0);
  fp = local::g;
  fp (0);
  if (violations != 2)
    __builtin_abort ();
}
