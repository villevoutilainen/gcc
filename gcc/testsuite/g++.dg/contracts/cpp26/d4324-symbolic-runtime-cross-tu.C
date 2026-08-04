// Axiom contracts ("the gem", ~/gcc-axiom-contracts.md): the document's
// central ask -- open_it() (compiled as a separate TU,
// d4324-symbolic-runtime-cross-tu-aux.C) establishes is_opened(this) at
// run time; this TU's write_data() precondition consults that record
// and finds it, across the link boundary, via the shared runtime
// record store -- not anything computed within one translation unit.
// { dg-do run { target c++26 } }
// { dg-additional-sources "d4324-symbolic-runtime-cross-tu-aux.cc" }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

bool checked = false;
bool failed = false;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  {
    checked = true;
    if (!ctx.check ())
      failed = true;
  }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct io_facility {
  static bool is_opened (io_facility*) symbolic;
  void open () post<symbolic_ctrl_v>(is_opened (this)) {}
  void write_data () pre<symbolic_ctrl_v>(is_opened (this)) {}
};

void open_it (io_facility& f);

int main ()
{
  io_facility f;
  open_it (f);
  f.write_data ();
  if (!checked || failed)
    __builtin_abort ();
  return 0;
}
