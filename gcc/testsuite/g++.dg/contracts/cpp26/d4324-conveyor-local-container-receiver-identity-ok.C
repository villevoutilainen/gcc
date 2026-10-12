// D4324 bounds-proving demo: a real false positive found via a user-
// reported Godbolt link (https://godbolt.org/z/nxE9xKdnj), distinct from
// (and found right after) the sibling d4324-conveyor-vector-index-
// margin.C's own coverage -- every function there takes its container as
// a REFERENCE parameter ('std::vector<int>& v'). This file specifically
// covers a LOCALLY-DECLARED or BY-VALUE container object instead.
//
// Root cause #1 (receiver identity): a dot-call's own receiver
// ('test.size ()') is captured as the bare VAR_DECL 'test', but
// operator[]'s own declared precondition ('this->size ()') gets its
// 'this' substituted at the call site as 'ADDR_EXPR (test)' ('&test') --
// since operator[]'s own 'this' is a pointer, unlike a reference
// parameter (already a pointer at the GIMPLE level, so no ADDR_EXPR ever
// enters the picture on either side, which is exactly why the reference-
// parameter-only sibling test never exercised this). Fixed by
// oa_strip_to_receiver_identity (contracts.cc), which strips a top-level
// ADDR_EXPR of a VAR_DECL/PARM_DECL down to that decl specifically for
// receiver-identity ("same_accessor") comparisons -- confirmed via gdb
// this asymmetry, not reasoned from inspection alone.
//
// Root cause #2 (local index establishment): once root cause #1 is
// fixed, a SECOND, independent gap surfaced for use_local_idx_from_call
// below -- oa_match_comparison_against_call's own PARAM side only ever
// accepted a bare PARM_DECL (via oa_underlying_param_operand's own
// ALLOW_VAR_DECL defaulting to false), so establishing a fact from an
// ordinary if-condition whose own subject is a LOCAL variable (not a
// parameter) silently failed. Fixed by passing allow_var_decl=true
// specifically from oa_refine_single_comparison's own within-function,
// if-condition establishment call (never from any caller parsing a
// callee's own declared contract TEXT for substitution at a call site,
// where only a real parameter is meaningful) -- confirmed this is
// genuinely a separate bug, not the same one: it reproduces even once
// root cause #1 alone is fixed, with a different diagnostic ("no fact
// relating this value", not "a different object or accessor").
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS" }

#include <vector>

int get_index ();

// The original repro: a local, value-typed container, literal-shaped
// guard. Receiver identity (root cause #1) is the only thing needed
// here, since 'x' is already a bare parameter.
int use_local_container_ok (int x)
{
  std::vector<int> test{1, 2, 3};
  if (x >= 0 && x < test.size ())
    return test[x]; // proven safe, no diagnostic
  return 0;
}

// The index itself is a local variable, assigned from an opaque call's
// return value -- exercises root cause #2 specifically (root cause #1
// alone is not enough to verify this).
int use_local_idx_from_call_ok ()
{
  std::vector<int> test{1, 2, 3};
  int idx = get_index ();
  if (idx >= 0 && idx < test.size ())
    return test[idx]; // proven safe, no diagnostic
  return 0;
}

// A by-value vector parameter -- like a local variable, a non-reference,
// directly-addressable object requiring '&v' to form 'this'; exercises
// the identical receiver-identity shape as use_local_container_ok, just
// sourced from a parameter instead of a local declaration.
int use_byvalue_param_ok (std::vector<int> v, int x)
{
  if (x >= 0 && x < v.size ())
    return v[x]; // proven safe, no diagnostic
  return 0;
}

// Negative control: an unguarded local-variable container must still
// correctly warn -- confirms neither fix introduces a false negative.
int use_local_container_unguarded_bad ()
{
  std::vector<int> test{1, 2, 3};
  int idx = get_index ();
  return test[idx]; // { dg-warning {cannot verify that [^\n]* satisfies the precondition} }
}

int main ()
{
  return use_local_container_ok (0) + use_local_idx_from_call_ok ()
	 + use_byvalue_param_ok (std::vector<int>{1, 2, 3}, 0)
	 + use_local_container_unguarded_bad ();
}
