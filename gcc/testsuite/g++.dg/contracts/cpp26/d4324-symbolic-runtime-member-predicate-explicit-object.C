// D4324: -fcontract-symbolic-runtime-checks with a non-static member
// symbolic predicate given its object explicitly as well, 'is_opened
// (this)': a precondition with no earlier open() fails, one after it
// holds.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

int checks = 0;
int failures = 0;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  {
    ++checks;
    if (!ctx.check ())
      ++failures;
  }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct io_facility {
  bool is_opened (io_facility*) symbolic;
  void open () post<symbolic_ctrl_v>(is_opened (this)) {}
  void write_data () pre<symbolic_ctrl_v>(is_opened (this)) {}
  ~io_facility () {}
};

int main ()
{
  io_facility f;
  f.write_data ();
  if (checks != 1 || failures != 1)
    __builtin_abort ();
  f.open ();
  f.write_data ();
  if (failures != 1)
    __builtin_abort ();
  return 0;
}
