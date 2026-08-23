// D4324/P3400: a local_violation_label that declines (returns
// not_handled) falls through to the real, replaceable global handler
// via invoke_violation_handler, and label_base's own operator() then
// terminates (since the effective semantic here is enforce) exactly
// like every other control type in this header -- not just logs and
// continues.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contract_labels>
#include <exception>
#include <cstdlib>

namespace P3400 = std::contracts::P3400;

int declined_calls = 0;

struct declining_label_t : P3400::label_base<declining_label_t>
{
  using assertion_control_object = declining_label_t;

  constexpr P3400::violation_handled
  handle_contract_violation (const std::contracts::assertion_context&) const
  {
    ++declined_calls;
    return P3400::violation_handled::not_handled;
  }
};
inline constexpr declining_label_t declining_label{};

int f (int x) pre<declining_label>(x > 0) { return x; }

void
my_terminate ()
{
  if (declined_calls == 1)
    std::exit (0);
  std::exit (1);
}

int
main ()
{
  std::set_terminate (my_terminate);
  f (-1); // violates; local handler declines, real global handler runs,
	  // then label_base's operator() terminates
  __builtin_abort (); // should not be reached
}
