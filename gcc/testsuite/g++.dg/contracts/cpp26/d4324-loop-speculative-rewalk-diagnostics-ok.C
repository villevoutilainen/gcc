// D4324/P2680, item 4's loop-header merge rule (oa_handle_loop): once a
// loop body's own real ("pass 1") walk resolves a contract_assert's
// condition, that AST subtree is mutated in place (the is_object_address
// call is replaced with a literal 'true'), which is exactly what makes
// oa_handle_loop's own per-reassigned-decl speculative re-walks ("pass 2"
// and its nz counterpart) idempotent on it -- they re-walk the very same
// body once per reassigned pointer/integer decl, purely to check whether
// that decl's own reassignment is provable independent of its own prior
// value, and are documented to represent no real execution at all
// (OA_RETURN_TRACKING and symbolic codegen are both explicitly suppressed
// for exactly this reason). But since the assert's condition is already
// gone by the time a speculative re-walk reaches it, that walk's own
// hypothetical env never re-derives the fact the assert established --
// so any OTHER diagnostic-emitting check reached later in the same body,
// during that same speculative walk, would see the fact as if the assert
// had never run at all.
//
// oa_diagnostics_active exists precisely to keep a speculative re-walk's
// own re-derived (and, per the above, sometimes *wrongly* re-derived)
// conclusions from ever becoming a real, user-visible diagnostic --
// oa_scan_div_mod_in_expr/oa_scan_array_bounds_in_expr/oa_scan_overflow_
// in_expr all check it already. oa_handle_call_precondition_obligation
// (a plain conveyor call's own is_object_address/ownership obligation
// check) did not, and was the one diagnostic path in the whole per-
// statement walk a speculative re-walk didn't actually silence: found via
// a real false positive in bits/fs_path.h's generic_string(), whose own
// range-for loop reassigns __add_slash (a bool) every iteration --
// exactly enough to trigger oa_handle_loop's own speculative-re-walk
// machinery at all. Reproduced here in miniature: ANY second reassigned
// decl in the same loop (accum below) is what actually matters, not the
// range-for shape specifically -- an ordinary indexed for-loop with the
// exact same two ingredients reproduces identically.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct T { int v; };

struct Iter
{
  T* p;
  const T& operator*() const noexcept { return *p; }
  Iter& operator++() { ++p; return *this; }
  bool operator!=(const Iter& o) const { return p != o.p; }
};

struct Range
{
  T* b; T* e;
  Iter begin() const { return {b}; }
  Iter end() const { return {e}; }
};

int use_val_const(const T& x) conveyor { return x.v; }

int
f(Range r)
{
  int accum = 0;
  for (auto& elem : r)
    {
      contract_assert<std::contracts::never_proven_conveyor_v>
	(std::is_object_address (&elem));
      // ACCUM's own reassignment below is what triggers oa_handle_loop's
      // speculative re-walk machinery in the first place -- without it,
      // this loop would never even reach the buggy code path.
      accum += use_val_const(elem);
    }
  return accum;
}

int
main ()
{
  T arr[3] = { {1}, {2}, {3} };
  return f({arr, arr + 3}) == 6 ? 0 : 1;
}
