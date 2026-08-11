// D4324: named-predicate identity resolution (oa_object_identity_decl,
// oa_env_predicate_result) now looks through a wrapper's own implicit
// conversion operator (oa_strip_conversion_operator_call), the same
// lookthrough the value-fact layer (nonzero/range/relational) already
// had. open_it()'s postcondition establishes is_opened(f) for the
// object 'ref' converts to; use_it()'s precondition consults the same
// fact, reached via ref's own conversion operator on a *separate*
// statement. Also exercises the invalidation-transparency fix
// (oa_call_is_conversion_operator_call): calling ref's own conversion
// operator to *reach* its identity must not itself be treated as an
// opaque call invalidating that same identity (oa_invalidate_symbolic_
// facts_for_call_args) -- confirmed by direct testing that without that
// guard, the fact established by open_it() was wiped out by ref's own
// conversion-operator call before use_it()'s precondition ever ran.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

struct file { bool opened = false; };

bool is_opened (file *f) conveyor { return f != nullptr; }

struct file_ref {
  file *f;
  operator file* () const { return f; }
};

void open_it (file * const f) post<conveyor_ctrl_v> (is_opened (f)) { f->opened = true; }
void use_it (file * const f) pre<conveyor_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f;
  file_ref ref{&f};
  open_it (ref);
  use_it (ref);
  return 0;
}
