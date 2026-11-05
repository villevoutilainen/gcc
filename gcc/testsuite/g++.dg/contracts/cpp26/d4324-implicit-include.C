// D4324: -fcontracts-implicit-include silently #includes <contracts> before
// the translation unit -- as if it were the very first line of this file --
// so both the standard library's std::contracts:: names and a bare
// pre(x >= 0) (which needs std::contracts::default_v to be declared) work
// with no explicit #include at all.  This test deliberately has no
// #include of any kind.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts-implicit-include" }

// Proves the library declarations are visible with no explicit include:
// naming std::contracts::evaluation_semantic here would fail to compile if
// <contracts> hadn't already been implicitly included.
using sem_t = std::contracts::evaluation_semantic;
static_assert (sem_t::enforce != sem_t::ignore);

// The option also makes the driver link the runtime contracts support
// library (libstdc++exp), as -fcontracts does: default_v's operator()
// needs it, so this test doesn't link without it (P4445 BUG-38).

// A bare pre, naming no control object at all -- relies on
// -fcontracts-implicit-include enabling -fcontract-control-objects (so
// this resolves to std::contracts::default_v) as well as making
// std::contracts::default_v visible in the first place.
int f (int x) pre (x >= 0) { return x; }

int main ()
{
  if (f (3) != 3)
    __builtin_abort ();
  return 0;
}
