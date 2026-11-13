// D4324 (P4425): a function declared 'symbolic' names a fact; it has no
// definition and is never called.  Only a symbolic contract's condition --
// ghost code, never evaluated -- may name it.  So a control object is not
// both strict and symbolic (a strict condition and a symbolic fact are two
// assertions), and a symbolic function has no address.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct File { int fd; };
bool is_open (const File *) symbolic;

struct both_t
{
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context &) const {}
};

void both (const File *f) pre<both_t{}> (f != nullptr);              // { dg-error "cannot be both strict and symbolic" }
template <class T> void dep (const File *f) pre<T{}> (f != nullptr) {} // { dg-error "cannot be both strict and symbolic" }
void use_dep (File *f) { dep<both_t> (f); }

// Where it is written, also on a declaration and in a class.
void strict_pre (const File *f) pre<sc::conveyor_assert_v> (is_open (f)); // { dg-error "may only be used in the condition of a symbolic contract" }
struct M { void m (const File *f) pre (is_open (f)); };              // { dg-error "may only be used in the condition of a symbolic contract" }
void strict_assert (const File *f)
{ contract_assert<sc::conveyor_assert_v> (is_open (f)); }            // { dg-error "may only be used in the condition of a symbolic contract" }

// Two assertions instead: fine.
void close_file (const File *f)
  pre<sc::conveyor_assert_v> (f != nullptr)
  pre<sc::symbolic_assert_v> (is_open (f));

// No address, nor a reference to it.
auto p = &is_open;                                                   // { dg-error "has no address" }
bool (*q) (const File *) = is_open;                                  // { dg-error "has no address" }
bool (&r) (const File *) = is_open;                                  // { dg-error "has no address" }
static_assert (sizeof (&is_open) == sizeof (void (*) ()));           // unevaluated: fine
// { dg-prune-output "declared here" }
