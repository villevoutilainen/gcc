// D4324/P3400: local_violation_label lets a label intercept a
// violation before the real global handler; returning handled skips
// it entirely, while returning not_handled (or declining to model the
// facet at all) falls through to the real, user-replaceable global
// ::handle_contract_violation via the existing invoke_violation_handler
// -- not an approximation. label_base's own operator() then decides
// termination from the *effective* semantic (after any
// compute_semantic/group-config transform), not the raw, TU-configured
// one, matching how every other control type in this header behaves.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contract_labels>

namespace P3400 = std::contracts::P3400;

int handled_calls = 0;

struct suppressing_label_t : P3400::label_base<suppressing_label_t>
{
  using assertion_control_object = suppressing_label_t;

  constexpr P3400::violation_handled
  handle_contract_violation (const std::contracts::assertion_context& ctx) const
  {
    ++handled_calls;
    if (!ctx.check ())
      return P3400::violation_handled::handled;
    return P3400::violation_handled::not_handled;
  }
};
inline constexpr suppressing_label_t suppressing_label{};

int f (int x) pre<suppressing_label>(x > 0) { return x; }

int
main ()
{
  int r = f (-1); // violates; local handler suppresses it, no termination
  if (r != -1 || handled_calls != 1)
    __builtin_abort ();
  return 0;
}
