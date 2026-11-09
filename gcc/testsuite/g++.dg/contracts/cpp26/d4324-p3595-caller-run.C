// D4324, QoI: P3595R0's "caller" match key.  An entry with "caller" true,
// or a "caller" object describing the calling code, matches only the
// redundant caller-side checks (assertion_check_side::client of a
// non-virtual function), and makes a call get one even without
// -fcontracts-client-check; other entries match only the other checks.
// The control object sees the caller-side semantic through semantic(),
// and side() says client: default_v (a plain pre) follows it unchanged.
// Calls from code matching different "caller" objects get different
// semantics.  The vendor's definitions keep the translation unit's
// semantic, ignore.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"caller":{"namespace":"ours"},"namespace":"vendor"},"output":{"semantic":"observe"}},{"match":{"caller":true,"namespace":"vendor"},"output":{"semantic":"enforce"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

int calls;
evaluation_semantic last;

void
handle_contract_violation (const contract_violation& v)
{
  ++calls;
  last = v.semantic ();
  if (v.semantic () == evaluation_semantic::enforce)
    throw 0;
}

// Records the side and semantic of a failed check; never terminates.
int rec_fails;
assertion_check_side rec_side;
evaluation_semantic rec_sem;
struct rec_t
{
  static constexpr bool
  is_ignored (assertion_static_info i) noexcept
  { return i.semantic () == evaluation_semantic::ignore; }

  constexpr void
  operator() (const assertion_context& c) const
  {
    if (!c.check ())
      {
	++rec_fails;
	rec_side = c.static_info ().side ();
	rec_sem = c.static_info ().semantic ();
      }
  }
};
inline constexpr rec_t rec{};

namespace vendor
{
  int f (int x) pre (x > 0) { return x; }
  int r (int x) pre<rec> (x > 0) { return x; }
}

namespace ours
{
  int g (int x) pre (x > 0) { return x; }
  void h () { vendor::f (0); }
  void hr () { vendor::r (0); }
}

int (*volatile fp) (int);

int
main ()
{
  // From namespace ours: the first entry, observe.
  ours::h ();
  if (calls != 1 || last != evaluation_semantic::observe)
    __builtin_abort ();

  // From elsewhere: the second, enforce.
  try { vendor::f (0); __builtin_abort (); } catch (int) { }
  if (calls != 2 || last != evaluation_semantic::enforce)
    __builtin_abort ();

  // Our own functions: no "caller" entry matches, no
  // -fcontracts-client-check, and their definitions are ignored.
  ours::g (0);
  // Through a pointer: no caller-side check, and the vendor's definition
  // is ignored.
  fp = vendor::f;
  fp (0);
  if (calls != 2)
    __builtin_abort ();

  // What a control object sees at the caller-side check.
  ours::hr ();
  if (rec_fails != 1 || rec_side != assertion_check_side::client
      || rec_sem != evaluation_semantic::observe)
    __builtin_abort ();
  vendor::r (0);
  if (rec_fails != 2 || rec_side != assertion_check_side::client
      || rec_sem != evaluation_semantic::enforce)
    __builtin_abort ();
}
