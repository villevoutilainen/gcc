// D4324/Stage 4a: the array-write analogue of d4324-symbolic-proof-
// field-write-invalidates-predicate-unknown.C -- this session's own
// Stage 2b array-write branch had the identical gap (predicate_fact_
// invalidate missing for the array's own whole-object identity),
// fixed for consistency even though the array-write branch itself is
// new this session, not a pre-existing bug the way the field case is.
// A predicate can be declared over the array's own decayed-to-pointer
// identity directly ('is_opened (const file_ptr *arr)'); a later
// constant-index write to that same array must invalidate it, exactly
// like the whole-object reassignment branch already does for a plain
// decl. Note ARR must be a genuine local array, not a pointer
// parameter that merely looks array-like via decay -- oa_array_slot_
// base deliberately excludes pointer-decayed subscripting (a plain
// pointer parameter's own [i] belongs to Increment E2's own, separate
// range-tracking model), confirmed by direct testing this exact
// distinction while writing this test. See .claude/plans/well-we-
// last-discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct file { bool opened = false; };
typedef file *file_ptr;
bool is_opened (const file_ptr *arr) symbolic;

void open_it (file_ptr * const arr) post<symbolic_ctrl_v> (is_opened (arr)) { }
void use_it (file_ptr * const arr) pre<symbolic_ctrl_v> (is_opened (arr)) { }

void g (file *p)
{
  file_ptr arr[1];
  arr[0] = p;
  open_it (arr);
  arr[0] = nullptr; // constant-index write to the same local array
  use_it (arr); // { dg-warning "cannot verify" }
}

int main ()
{
  file f;
  g (&f);
  return 0;
}
