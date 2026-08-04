// Axiom contracts ("the gem"): -fcontract-symbolic-runtime-checks, the
// predicate-call shape's opposite-polarity case -- close()'s
// postcondition records !is_opened(this), the exact opposite of what
// write_data()'s precondition requires for the same object; the
// control object's operator() must see ctx.check() fail.
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
  void close () post<symbolic_ctrl_v>(!is_opened (this)) {}
  void write_data () pre<symbolic_ctrl_v>(is_opened (this)) {}
};

int main ()
{
  io_facility f;
  f.open ();
  f.close ();
  f.write_data ();
  if (!checked || !failed)
    __builtin_abort ();
  return 0;
}
