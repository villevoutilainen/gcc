// D4324: under -fcontract-evaluation-semantic=quick_enforce, a failed check
// with a <contract_labels> label terminates at once, without calling the
// violation handler, as a plain contract and default_v do (P4445 BUG-37).
// The terminate handler writes the first output; a handler call before it
// would make the output not start with it.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=quick_enforce" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contract_labels>
#include <cstdio>
#include <cstdlib>
#include <exception>

void
handle_contract_violation (const std::contracts::contract_violation&)
{
  std::fputs ("handler\n", stderr);
}

[[noreturn]] void
on_terminate ()
{
  std::fputs ("terminate\n", stderr);
  std::_Exit (0);
}

int f (int x) pre<std::contracts::P3400::terminating>(x > 0) { return x; }

int main ()
{
  std::set_terminate (on_terminate);
  f (0);
  return 1;			// not reached: the failed check terminates
}

// { dg-output "^terminate\n" }
