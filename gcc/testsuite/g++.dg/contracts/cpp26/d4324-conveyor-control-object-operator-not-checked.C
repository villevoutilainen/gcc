// D4324: is_conveyor() == true only constrains the predicate condition
// itself; the control object's own operator() (violation handling,
// logging, terminate, etc.) is never checked against conveyor rules and
// legitimately may use constructs conveyor forbids.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  {
    // Deliberately full of constructs that would violate conveyor rules
    // if this operator() were checked -- it must not be.
    if (!ctx.check ())
      {
	int* leak = new int (42);
	(void) leak;
	throw 1;
      }
  }
};
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

// The predicate condition itself is perfectly conveyor-clean.
int f (int x) pre<conveyor_ctrl_v>(x > 0)
{
  return x;
}

int main () { return f (1) - 1; }
