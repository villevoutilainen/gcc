// Axiom contracts ("the gem", ~/gcc-axiom-contracts.md):
// -fcontract-symbolic-runtime-checks, the predicate-call shape's
// proven-true case -- open()'s postcondition really does record, at
// run time, that is_opened(this) holds for f, and write_data()'s
// precondition really does consult that record and find it -- so the
// control object's operator() sees ctx.check() succeed and never
// records a failure.
// { dg-do run { target c++26 } }
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

int main ()
{
  io_facility f;
  f.open ();
  f.write_data ();
  if (!checked || failed)
    __builtin_abort ();
  return 0;
}
