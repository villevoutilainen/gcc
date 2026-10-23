// D4324: a non-static member symbolic predicate given its object
// explicitly as well, 'is_opened (this)' -- the implicit object argument
// and the explicit one name the same object, so it's a fact about that
// object, checked like the static and implicit-object forms.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs -fcontract-analysis-engine=gimple" }

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
  void open () post<symbolic_ctrl_v>(is_opened (this));
  void close () post<symbolic_ctrl_v>(!is_opened (this));
  void write_data () pre<symbolic_ctrl_v>(is_opened (this));
};

void good ()
{
  io_facility f;
  f.open ();
  f.write_data ();
}

void never_opened ()
{
  io_facility f;
  f.write_data (); // { dg-warning "cannot verify that 'bool io_facility::is_opened\\(io_facility\\*\\)'" }
}

void closed ()
{
  io_facility f;
  f.open ();
  f.close ();
  f.write_data (); // { dg-error "provably violates the precondition" }
}
