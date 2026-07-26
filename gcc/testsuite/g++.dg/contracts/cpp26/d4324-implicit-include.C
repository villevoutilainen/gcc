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

// This test never links the runtime contracts support library
// (libstdc++exp): default_v's operator() calls these two library-only
// entry points in its non-executed (check-failed) branch, which is enough
// to need them defined at link time even though this test's checks all
// pass.  Trivial local definitions sidestep the resulting link error, the
// same established workaround used by every other test exercising
// default_v (see d4324-control-object-state.C et al.).
namespace std { namespace contracts {
void __d4324_log_violation (const char*, std::source_location) noexcept {}
[[noreturn]] void __d4324_terminate () noexcept { __builtin_trap (); }
} }

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
