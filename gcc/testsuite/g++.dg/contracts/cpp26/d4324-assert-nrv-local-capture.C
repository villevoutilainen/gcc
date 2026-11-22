// D4324: a contract_assert whose condition uses a local that the named
// return value optimization turns into the function's result: the outlined
// predicate captures it like any other local (it used to reference the
// caller's RESULT_DECL and ICE in out-of-SSA, or in inlining at -O3).
// Found by the plain-contracts experiment on Qt (QByteArray::toBase64).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

struct cco
{
  static constexpr bool is_ignored (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool constify (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool assumable (sc::assertion_static_info) noexcept { return false; }
  constexpr void operator() (const sc::assertion_context &c) const noexcept
  {
    if (!c.check ())
      __builtin_abort ();
  }
};

struct BA
{
  char *p;
  long n;
  BA (long k) : p (new char[k]), n (k) {}
  BA (const BA &o) : p (new char[o.n]), n (o.n) {}
  ~BA () { delete[] p; }
  char *data () { return p; }
  long size () const { return n; }
};

BA
f (int options, long sz)
{
  BA tmp (sz);
  char *out = tmp.data ();
  for (long i = 0; i < sz; ++i)
    *out++ = 'a';
  contract_assert<cco{}> ((options & 1) || (out == tmp.size () + tmp.data ()));
  contract_assert ((options & 1) || (out == tmp.size () + tmp.data ()));
  return tmp;
}

int
main ()
{
  BA b = f (0, 4);
  return b.size () == 4 ? 0 : 1;
}
