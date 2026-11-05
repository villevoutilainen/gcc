// D4324: under -fcontract-evaluation-semantic=quick_enforce, a failed default_v
// check terminates at once, without writing a diagnostic first, as a plain
// contract does.  The terminate handler writes the first output; any
// diagnostic before it would make the output not start with it.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=quick_enforce" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstdio>
#include <cstdlib>
#include <exception>

[[noreturn]] void
on_terminate ()
{
  std::fputs ("terminate\n", stderr);
  std::_Exit (0);
}

int f (int x) pre<std::contracts::default_v>(x > 0) { return x; }

int main ()
{
  std::set_terminate (on_terminate);
  f (-1);
  return 1;			// not reached: the failed check terminates
}

// { dg-output "^terminate\n" }
