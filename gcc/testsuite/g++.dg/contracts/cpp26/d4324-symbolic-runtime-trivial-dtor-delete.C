// D4324: -fcontract-symbolic-runtime-checks clears an object's symbolic
// records when a delete-expression frees it, whether or not its destructor
// is trivial: a later allocation reusing the storage doesn't inherit them.
// For delete[], every element's; with a virtual destructor, the dynamic
// type's whole object's.
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

// Storage reused deterministically: every allocation gets the same buffer.
alignas (16) unsigned char buffer[256];
struct arena
{
  static void* operator new (__SIZE_TYPE__) { return buffer; }
  static void* operator new[] (__SIZE_TYPE__) { return buffer; }
  static void operator delete (void*) {}
  static void operator delete[] (void*) {}
};

struct facility : arena, io_facility { };
static_assert (__is_trivially_destructible (facility));

struct poly_base : arena { virtual ~poly_base () {} };
struct poly : poly_base { int i; io_facility f; };

int main ()
{
  facility* p = new facility;
  p->open ();
  p->write_data ();
  delete p;
  facility* q = new facility;
  if (q != p)
    __builtin_abort ();
  q->write_data ();
  if (failures != 1)
    __builtin_abort ();
  delete q;

  facility* a = new facility[4];
  a[3].open ();
  delete[] a;
  facility* b = new facility[4];
  if (b != a)
    __builtin_abort ();
  b[3].write_data ();
  if (failures != 2)
    __builtin_abort ();
  delete[] b;

  poly_base* pb = new poly;
  static_cast<poly*> (pb)->f.open ();
  delete pb;
  poly_base* pc = new poly;
  if (pc != pb)
    __builtin_abort ();
  static_cast<poly*> (pc)->f.write_data ();
  if (failures != 3)
    __builtin_abort ();
  delete pc;
  return 0;
}
