// D4324 (P4445 2.3.2.3): under -fcontract-symbolic-runtime-checks, a
// symbolic record doesn't outlive the object it was established for, however
// the object's lifetime ends or its storage is reused: an explicit call of a
// trivial destructor, std::destroy_at, the end of an int's scope (a free
// symbolic function describes it), storage released without destructors and
// reused, storage reused for an object of another type or for implicitly
// created objects, switching a union's active member away and back, and
// storage from malloc or an aligned operator new after the previous storage
// was freed.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstdlib>
#include <memory>
#include <new>

int results[16];
int n;

struct report
{
  static constexpr bool is_symbolic (std::contracts::assertion_static_info)
  { return true; }
  void operator() (const std::contracts::assertion_context &ctx) const
  { results[n++] = ctx.check (); }
};
inline constexpr report report_v{};

struct io_facility
{
  bool is_opened (io_facility *) const symbolic;
  void open () post<report_v> (is_opened (this)) {}
  void write_data () pre<report_v> (is_opened (this)) {}
};

bool claimed (int *p) symbolic;
void claim (int *const p) post<report_v> (claimed (p)) {}
void use (int *p) pre<report_v> (claimed (p)) {}

int
main ()
{
  { // 0. the fact holds
    io_facility f; f.open (); f.write_data ();
  }
  { // 1. an explicit call of the trivial destructor
    io_facility f; f.open (); f.~io_facility (); new (&f) io_facility;
    f.write_data ();
  }
  { // 2. std::destroy_at
    io_facility f; f.open (); std::destroy_at (&f); std::construct_at (&f);
    f.write_data ();
  }
  { // 3. an int's scope ends; the next iteration's int is at the same address
    int *first = nullptr;
    for (int i = 0; i < 2; ++i)
      {
	int x = 0;
	if (i == 0) { claim (&x); first = &x; }
	else if (&x == first) use (&x);
	else results[n++] = 0;
      }
  }
  { // 4. array storage released without destructors, then reused
    void *raw = ::operator new (4 * sizeof (io_facility));
    io_facility *a = new (raw) io_facility[4];
    for (int i = 0; i < 4; ++i) a[i].open ();
    ::operator delete (raw);
    void *raw2 = ::operator new (4 * sizeof (io_facility));
    std::launder (static_cast<io_facility *> (raw2))[2].write_data ();
    ::operator delete (raw2);
  }
  { // 5. storage reused for an object of another type
    alignas (io_facility) unsigned char buf[4 * sizeof (io_facility)];
    io_facility *a = new (buf) io_facility[4];
    for (int i = 0; i < 4; ++i) a[i].open ();
    new (buf) int (5);
    std::launder (reinterpret_cast<io_facility *> (buf))[0].write_data ();
  }
  { // 6. objects created implicitly by a byte-array new-expression
    alignas (io_facility) unsigned char buf[4 * sizeof (io_facility)];
    io_facility *a = new (buf) io_facility[4];
    for (int i = 0; i < 4; ++i) a[i].open ();
    new (buf) unsigned char[sizeof buf];
    std::launder (reinterpret_cast<io_facility *> (buf))[3].write_data ();
  }
  { // 7. a union's active member switched away and back
    union U { io_facility f; int i; U () : f () {} } u;
    u.f.open ();
    u.i = 1;
    new (&u.f) io_facility;
    u.f.write_data ();
  }
  { // 8. malloc after free
    void *m = std::malloc (sizeof (io_facility));
    (new (m) io_facility)->open ();
    std::free (m);
    void *m2 = std::malloc (sizeof (io_facility));
    std::launder (static_cast<io_facility *> (m2))->write_data ();
    std::free (m2);
  }
  { // 9. an aligned operator new after the previous storage was freed
    void *raw = ::operator new (sizeof (io_facility), std::align_val_t (64));
    (new (raw) io_facility)->open ();
    ::operator delete (raw, std::align_val_t (64));
    void *raw2 = ::operator new (sizeof (io_facility), std::align_val_t (64));
    std::launder (static_cast<io_facility *> (raw2))->write_data ();
    ::operator delete (raw2, std::align_val_t (64));
  }
  if (n != 10 || !results[0])
    __builtin_abort ();
  for (int i = 1; i < 10; ++i)
    if (results[i])
      __builtin_abort ();
}
