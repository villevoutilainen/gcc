// D4324: a symbolic precondition that finds no record of its fact
// (-fcontract-symbolic-runtime-checks) reports the violation to the
// replaceable violation handler with the configured semantic, as a plain
// contract does: under observe the handler is called and the program
// continues; under enforce it then terminates (P4445: symbolic_assert_v
// used to abort with libstdc++'s assertion message, bypassing the handler,
// and reported enforce whatever the build selected).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstdlib>
#include <exception>

namespace sc = std::contracts;

int calls;
sc::evaluation_semantic seen;

void
handle_contract_violation (const sc::contract_violation &v)
{
  ++calls;
  seen = v.semantic ();
}

struct io_facility
{
  bool is_opened (io_facility *) const symbolic;
  void open () post<sc::symbolic_assert_v>(is_opened (this)) {}
  void write () pre<sc::symbolic_assert_v>(is_opened (this)) {}
};

[[noreturn]] void
on_terminate ()
{
  std::_Exit (calls == 1 && seen == sc::evaluation_semantic::enforce ? 0 : 2);
}

int
main ()
{
  std::set_terminate (on_terminate);
  io_facility f;
  f.open ();
  f.write ();			// established: no violation
  if (calls != 0)
    __builtin_abort ();
  io_facility g;
  g.write ();			// never opened
  if (calls != 1 || seen != sc::evaluation_semantic::observe)
    __builtin_abort ();
}
