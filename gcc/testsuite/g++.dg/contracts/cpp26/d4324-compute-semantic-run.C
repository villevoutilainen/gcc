// D4324 (experimental): a control object's optional compute_semantic
// query gives each contract assertion its semantic, from what the
// compiler tells it about the assertion; the result is the semantic its
// other queries (is_ignored) and its operator() see.  Here: preconditions
// observed in the definition, postconditions ignored, and preconditions
// checked on the caller's side reported as enforce (the handler throws).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore -fcontracts-client-check=pre" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <exception>
using namespace std::contracts;

int reports;
evaluation_semantic last;

void
handle_contract_violation (const contract_violation& v)
{
  ++reports;
  last = v.semantic ();
  if (v.semantic () == evaluation_semantic::enforce)
    throw 0;
}

struct by_kind_t
{
  static constexpr evaluation_semantic
  compute_semantic (assertion_static_info i)
  {
    if (i.kind () == assertion_kind::post)
      return evaluation_semantic::ignore;
    if (i.side () == assertion_check_side::client)
      return evaluation_semantic::enforce;
    return evaluation_semantic::observe;
  }
  static constexpr bool
  is_ignored (assertion_static_info i) noexcept
  { return i.semantic () == evaluation_semantic::ignore; }
  constexpr void
  operator() (const assertion_context& c) const
  {
    if (c.check ())
      return;
    invoke_violation_handler (c.info (), detection_mode::predicate_false);
    if (c.semantic () != evaluation_semantic::observe)
      std::terminate ();
  }
};
inline constexpr by_kind_t by_kind{};

int f (int x) pre<by_kind> (x > 0) post<by_kind> (r: r > 5) { return x; }
int (*volatile fp) (int) = f;

int
main ()
{
  // The caller-side check: enforce.
  try { f (0); __builtin_abort (); } catch (int) { }
  if (reports != 1 || last != evaluation_semantic::enforce)
    __builtin_abort ();
  // Through a pointer, only the definition's check: observe; the failing
  // postcondition is ignored.
  fp (0);
  if (reports != 2 || last != evaluation_semantic::observe)
    __builtin_abort ();
}
