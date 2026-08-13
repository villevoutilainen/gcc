// Companion to d4324-control-object-redecl-match.C: the control-object
// comparison added to mismatched_contracts_p (see d4324-control-object-
// redecl-mismatch.C) folds each side down to its actual constant value
// (cp_fully_fold_init + cp_tree_equal), not object identity -- so a
// *temporary* CCO named directly in pre<...>, one fresh temporary
// object per occurrence, at a forward declaration, a redeclaration, and
// the definition, still counts as "the same control object" as long as
// each is constructed from the same constant arguments, even though no
// two of the three are ever the same object (each temporary's own
// lifetime ends at the end of its own declaration). The diagnostic
// message a violation actually produces must also come out identical,
// confirming this isn't merely "no error was reported" but "the check
// genuinely treated them as equivalent."
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <string>

namespace sc = std::contracts;

struct annotated {
  const char* note;
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.check ())
      return;
    seen = std::string (ctx.comment ()) + ": " + note;	// returns -> continue
  }
  static std::string seen;
};
std::string annotated::seen;

// Forward declaration, redeclaration, and definition -- three distinct
// annotated temporaries, same type and same constant argument.
void f (int x) pre<annotated{"stay positive"}>(x >= 0);
void f (int x) pre<annotated{"stay positive"}>(x >= 0);
void f (int x) pre<annotated{"stay positive"}>(x >= 0) { }

int main ()
{
  f (-1);
  if (annotated::seen != "x >= 0: stay positive")
    __builtin_abort ();
  return 0;
}
