// D4324: -fcontract-symbolic-runtime-checks: an object begins its lifetime
// with no symbolic records, wherever it's created: storage reused without
// its previous object's lifetime visibly ending -- a pool's placement new,
// a std::vector's element reused after clear () -- doesn't pass them on.
// memcpy, memmove and memset create the objects they write anew, too.
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
  bool is_opened (io_facility*) symbolic;
  void open () post<symbolic_ctrl_v> (is_opened (this)) {}
  void write_data () pre<symbolic_ctrl_v> (is_opened (this)) {}
};

#include <cstring>
#include <new>
#include <vector>

alignas (io_facility) unsigned char pool[sizeof (io_facility)];

int main ()
{
  io_facility* p = new (pool) io_facility;
  p->open ();
  p->write_data ();
  io_facility* q = new (pool) io_facility;
  q->write_data ();
  if (failures != 1)
    __builtin_abort ();

  q->open ();
  std::memset (q, 0, sizeof *q);
  q->write_data ();
  if (failures != 2)
    __builtin_abort ();

  io_facility other;
  q->open ();
  std::memcpy (q, &other, sizeof *q);
  q->write_data ();
  if (failures != 3)
    __builtin_abort ();

  q->open ();
  std::memmove (q, &other, sizeof *q);
  q->write_data ();
  if (failures != 4)
    __builtin_abort ();

  std::vector<io_facility> v;
  v.emplace_back ();
  v[0].open ();
  io_facility* first = &v[0];
  v.clear ();
  v.emplace_back ();
  if (&v[0] != first)
    __builtin_abort ();
  v[0].write_data ();
  if (failures != 5)
    __builtin_abort ();
  return 0;
}
