// D4324/P3400: a library-only approximation of WG21 P3400 ("Controlling
// Contract-Assertion Properties") built on this branch's existing
// control-object dispatch. Basic smoke test: each standard label used
// alone, under an evaluation semantic its own allowed_semantics
// actually permits.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace P3400 = std::contracts::P3400;

int f_terminating (int x) pre<P3400::terminating>(x > 0) { return x; }
int f_review (int x) pre<P3400::review>(x > 0) { return x; }
int f_opt (int x) pre<P3400::opt>(x > 0) { return x; }
int f_audit (int x) pre<P3400::audit>(x > 0) { return x; }
int f_hardened (int x) pre<P3400::hardened>(x > 0) { return x; }
int f_always_enforce (int x) pre<P3400::always_enforce>(x > 0) { return x; }
int f_never_ignore (int x) pre<P3400::never_ignore>(x > 0) { return x; }
int f_empty (int x) pre<P3400::empty_label>(x > 0) { return x; }

int
main ()
{
  int r = f_terminating (1) + f_review (1) + f_opt (1) + f_audit (1)
	  + f_hardened (1) + f_always_enforce (1) + f_never_ignore (1)
	  + f_empty (1);
  if (r != 8)
    __builtin_abort ();
  return 0;
}
