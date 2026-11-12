// D4324 (P4445 2.3.2.3): under -fcontract-symbolic-runtime-checks, the
// record is reentrant: recording a fact allocates, and a replacement operator
// new built with the flag, whose own allocation clears records -- and whose
// local of a type with a symbolic predicate clears them at its scope exit --
// doesn't deadlock.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstdlib>
#include <new>

struct io_facility
{
  bool is_opened (io_facility *) const symbolic;
  void open () post<std::contracts::symbolic_assert_v> (is_opened (this)) {}
  void write_data () pre<std::contracts::symbolic_assert_v> (is_opened (this)) {}
};

void *
operator new (std::size_t size)
{
  io_facility scratch;
  (void) scratch;
  if (void *p = std::malloc (size ? size : 1))
    return p;
  throw std::bad_alloc ();
}

void operator delete (void *p) noexcept { std::free (p); }
void operator delete (void *p, std::size_t) noexcept { std::free (p); }

int
main ()
{
  io_facility f;
  for (int i = 0; i < 100; ++i)
    f.open ();
  f.write_data ();
}
