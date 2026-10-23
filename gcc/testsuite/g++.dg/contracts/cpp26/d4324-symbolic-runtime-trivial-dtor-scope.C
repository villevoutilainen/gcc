// D4324: -fcontract-symbolic-runtime-checks clears an object's symbolic
// records when its lifetime ends even when its destructor is trivial and
// so never called: a later object at the same address -- here, two
// sequential automatic objects at the same stack offset -- doesn't inherit
// them.  On every exit from the scope: a normal one, an early return, a
// goto out of it, an exception.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

int failures = 0;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) ++failures; }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

// No destructor of its own: a trivial one, never called.
struct io_facility {
  bool is_opened (io_facility*) symbolic;
  void open () post<symbolic_ctrl_v> (is_opened (this)) {}
  void write_data () pre<symbolic_ctrl_v> (is_opened (this)) {}
};

static_assert (__is_trivially_destructible (io_facility));

__attribute__ ((noipa)) void opens () { io_facility f; f.open (); f.write_data (); }
__attribute__ ((noipa)) void doesnt_open () { io_facility f; f.write_data (); }

__attribute__ ((noipa)) int opens_early_return (int n)
{
  io_facility f;
  f.open ();
  if (n > 0)
    return n;
  f.write_data ();
  return 0;
}

__attribute__ ((noipa)) void opens_goto ()
{
  {
    io_facility f;
    f.open ();
    goto out;
  }
 out:;
}

__attribute__ ((noipa)) void opens_throws ()
{
  io_facility f;
  f.open ();
  throw 1;
}

int main ()
{
  opens ();
  if (failures != 0)
    __builtin_abort ();
  doesnt_open ();
  if (failures != 1)
    __builtin_abort ();

  opens_early_return (1);
  doesnt_open ();
  if (failures != 2)
    __builtin_abort ();

  opens_goto ();
  doesnt_open ();
  if (failures != 3)
    __builtin_abort ();

  try { opens_throws (); } catch (int) {}
  doesnt_open ();
  if (failures != 4)
    __builtin_abort ();
  return 0;
}
