// Axiom contracts (~/gcc-axiom-contracts.md): a function declared
// 'symbolic' has no definition and is used purely as a name inside
// contract conditions -- declaring one, with no body anywhere, and
// using it inside pre/post is well-formed.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct io_facility {
  bool is_opened (io_facility*) symbolic;
  void open () post<symbolic_ctrl_v>(is_opened (this)) {}
  void write_data () pre<symbolic_ctrl_v>(is_opened (this)) {}
};

void open (io_facility &f) { f.open (); }
void write (io_facility &f) { f.write_data (); }

int main ()
{
  io_facility f;
  open (f);
  write (f);
  return 0;
}
