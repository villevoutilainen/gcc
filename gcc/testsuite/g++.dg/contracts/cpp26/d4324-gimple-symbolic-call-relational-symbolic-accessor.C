// D4324: the built-in GIMPLE pass's own mirror of d4324-symbolic-call-
// relational-symbolic-accessor.C -- symbolic's own call-relational gate
// (oa_underlying_call_range_operand, reused as-is by contracts-gimple.cc's
// own matchers) widens to accept a symbolic-declared accessor as well as
// a conveyor one, never a plain untagged one. See .claude/plans/lazy-
// stirring-pearl.md.
//
// Two cases, exactly mirroring the AST test: (1) the new capability,
// self-trust and consult both exercised for a symbolic-declared accessor
// via cg_seed_self_trust/cg_check_call, plus the same fact run through
// cg_get_call_relational's own variable-offset fallback; (2) still
// rejected -- a plain, untagged accessor named from a symbolic contract
// is never recognized as a checkable obligation at all.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs-gimple" }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

// Case 1: the new capability -- 'size ()' is symbolic (no definition,
// never really called), named from a symbolic contract.
struct Sym {
  int size () const symbolic;
  int get (int n) const pre<symbolic_ctrl_v>(n < size ()) { return n; }
};

int get_checked (Sym& s, int i) pre<symbolic_ctrl_v>(i < s.size ())
{
  return s.get (i);
}

int get_unchecked (Sym& s, int i)
{
  return s.get (i); // { dg-warning "cannot verify that .i. satisfies" }
}

// Case 1b: the same symbolic accessor, through cg_get_call_relational's
// own fallback to cg_established_range_of for a shift amount that isn't
// itself a literal (built earlier this session) -- confirms that
// machinery has no hidden conveyor-only assumption either.
int use_sound_shift (Sym& s, int i, int k)
  pre<symbolic_ctrl_v>(i < s.size () && k <= 0)
{
  int j = i + k;
  return s.get (j);
}

int use_unsound_shift (Sym& s, int i, int k)
  pre<symbolic_ctrl_v>(i < s.size () && k >= 0 && k <= 2)
{
  int j = i + k;
  return s.get (j); // { dg-warning "cannot verify that .j. satisfies" }
}

// Case 2: still rejected -- 'plain_size ()' has a real body but no
// conveyor/symbolic tag at all; a symbolic contract naming it must not
// be treated as a trackable fact.
struct Plain {
  int plain_size () const { return 5; }
  int get (int n) const pre<symbolic_ctrl_v>(n < plain_size ()) { return n; }
};

int plain_checked (Plain& p, int i) pre<symbolic_ctrl_v>(i < p.plain_size ())
{
  return p.get (i);
  /* No warning: 'plain_size ()' is neither conveyor- nor symbolic-
     declared, so this conjunct is never recognized as a checkable
     obligation at all -- not even to report "cannot verify."  Contrast
     get_unchecked above, whose identical structure with a *symbolic*-
     declared accessor does warn.  */
}

int main ()
{
  Sym s;
  Plain p;
  return get_checked (s, 2) // { dg-warning "cannot verify that .2. satisfies" }
	 + get_unchecked (s, 9)
	 + use_sound_shift (s, 2, -1) // { dg-warning "cannot verify that .2. satisfies" }
	 + use_unsound_shift (s, 2, 1) // { dg-warning "cannot verify that .2. satisfies" }
	 + plain_checked (p, 2);
}
