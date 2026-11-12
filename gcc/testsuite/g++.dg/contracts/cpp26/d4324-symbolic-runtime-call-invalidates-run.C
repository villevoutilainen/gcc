// D4324 (P4445 2.3.2.1): under -fcontract-symbolic-runtime-checks, a call
// that isn't conveyor and is given a pointer or reference to an object may
// change it, so the symbolic records established for the object before the
// call end with it, as the static analysis (-fcontract-symbolic-proofs)
// assumes -- but not the ones the callee's own postconditions establish,
// nor before the callee's own preconditions consult them.  This also covers
// a class whose symbolic facts come from contracts with a symbolic control
// object rather than from a symbolic predicate function, and calls that
// return a value or a class, or exit with an exception.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks -O0" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

int results[16];
int n;

struct report
{
  static constexpr bool is_symbolic (std::contracts::assertion_static_info)
  { return true; }
  void operator() (const std::contracts::assertion_context &ctx) const
  { results[n++] = ctx.check (); }
};
inline constexpr report report_v{};

struct widget
{
  int state = 0;
  __attribute__((noipa)) void produce ()
    post<report_v> (this->state >= 40 && this->state < 100)
  { state = 50; }
  __attribute__((noipa)) void consume ()
    pre<report_v> (this->state >= 20)
  { }
};

__attribute__((noipa)) void mutate (widget &w) { w.state = -7; }
__attribute__((noipa)) int peek (const widget *w) { return w->state; }
__attribute__((noipa)) void thrower (widget &w) { w.state = -7; throw 1; }
__attribute__((noipa)) int setter (widget &w) { w.state = -7; return 1; }
__attribute__((noipa)) int thrower_val (widget &w) { w.state = -7; throw 1; }
struct big { int x[8]; };
__attribute__((noipa)) big make (widget &w) { w.state = -7; return {}; }

int
main ()
{
  widget a;
  a.produce ();
  a.consume ();		// holds: produce's postcondition survives its own call
  a.consume ();		// violated: the first consume () may have changed a
  widget b;
  b.produce ();
  mutate (b);
  b.consume ();		// violated: mutate (b) may have changed b
  widget c;
  c.produce ();
  (void) peek (&c);
  c.consume ();		// violated: so may peek (&c)
  widget d;		// a call that exits with an exception
  d.produce ();
  try { thrower (d); } catch (...) { }
  d.consume ();		// violated
  widget e;		// one returning a scalar
  e.produce ();
  int r = setter (e);
  e.consume ();		// violated
  widget f;		// one returning a scalar, exiting with an exception
  f.produce ();
  try { r += thrower_val (f); } catch (...) { }
  f.consume ();		// violated
  widget g;		// one returning a class, initialized in place
  g.produce ();
  big bg = make (g);
  g.consume ();		// violated
  (void) bg;
  int expected[] = { 1, 0, 0, 0, 0, 0, 0, 0 };
  if (n != 8 || r != 1)
    __builtin_abort ();
  for (int i = 0; i < 8; ++i)
    if (results[i] != expected[i])
      __builtin_abort ();
}
