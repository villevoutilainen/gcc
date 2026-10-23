// D4324: -fcontract-symbolic-runtime-checks clears the symbolic records of
// an object's subobjects, at any depth, when its lifetime ends: a member,
// an array element, a base -- of a temporary, too.
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

struct outer { int i; io_facility f; };
struct derived : io_facility { int i; };

__attribute__ ((noipa)) void member_opens () { outer o; o.f.open (); }
__attribute__ ((noipa)) void member_doesnt () { outer o; o.f.write_data (); }

__attribute__ ((noipa)) void array_opens () { io_facility a[3]; a[2].open (); }
__attribute__ ((noipa)) void array_doesnt () { io_facility a[3]; a[2].write_data (); }

__attribute__ ((noipa)) void base_opens () { derived d; d.open (); }
__attribute__ ((noipa)) void base_doesnt () { derived d; d.write_data (); }

__attribute__ ((noipa)) void temporary_opens () { outer{}.f.open (); }
__attribute__ ((noipa)) void temporary_doesnt () { outer{}.f.write_data (); }

int main ()
{
  member_opens ();
  member_doesnt ();
  if (failures != 1)
    __builtin_abort ();
  array_opens ();
  array_doesnt ();
  if (failures != 2)
    __builtin_abort ();
  base_opens ();
  base_doesnt ();
  if (failures != 3)
    __builtin_abort ();
  temporary_opens ();
  temporary_doesnt ();
  if (failures != 4)
    __builtin_abort ();
  return 0;
}
