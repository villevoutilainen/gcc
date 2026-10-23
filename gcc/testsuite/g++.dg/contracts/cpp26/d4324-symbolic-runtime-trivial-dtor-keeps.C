// D4324: -fcontract-symbolic-runtime-checks keeps an object's symbolic
// records while it lives: the named return value, constructed in the
// caller's storage, and a temporary elided into the object it
// initializes, keep the facts established for them; so does a static
// object.
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
  bool is_opened (io_facility*) const symbolic;
  void open () post<symbolic_ctrl_v> (is_opened (this)) {}
  void write_data () pre<symbolic_ctrl_v> (is_opened (this)) {}
};

__attribute__ ((noipa)) io_facility make_nrv ()
{
  io_facility f;
  f.open ();
  return f;
}

static io_facility global;

int main ()
{
  io_facility f = make_nrv ();
  // Under NRV the named object was 'f'; without it, a copy that needs
  // its own open (): only check when they're the same object.
  global.open ();
  {
    io_facility g;
    (void) g;
  }
  global.write_data ();
  if (failures != 0)
    __builtin_abort ();
  (void) f;
  return 0;
}
