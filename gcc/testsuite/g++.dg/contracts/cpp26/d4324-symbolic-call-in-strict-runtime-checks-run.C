// D4324: under -fcontract-symbolic-runtime-checks, a symbolic contract
// beside a strict one checks the fact recorded for its object.  A
// postcondition may establish a fact about the function's result, here a
// string_view returned by value and given to the predicate by reference.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <string_view>
namespace sc = std::contracts;

int violations;
void handle_contract_violation (const sc::contract_violation&) { ++violations; }

struct strict_t
{
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& c) const
  { if (!c.check ()) sc::invoke_violation_handler (c.info (), sc::detection_mode::predicate_false); }
};
inline constexpr strict_t strict_v {};

struct File { int fd; };
bool is_open (const File*) symbolic;
File the_file;
File* open_file () post<sc::never_proven_symbolic_v>(r: is_open (r)) { return &the_file; }
void close_file (File* const f)
  pre<strict_v>(f != nullptr)
  pre<sc::symbolic_assert_v>(is_open (f)) {}

bool is_zero_terminated (const std::string_view&) symbolic;
std::string_view make_s () post<sc::never_proven_symbolic_v>(r: is_zero_terminated (r))
{ return "zero"; }

int
main ()
{
  close_file (open_file ());	// recorded: opened
  if (violations != 0)
    __builtin_abort ();
  File other;
  close_file (&other);		// never opened
  if (violations != 1)
    __builtin_abort ();
  close_file (nullptr);		// both, observed: neither short-circuits
  if (violations != 3)
    __builtin_abort ();
  (void) make_s ();
}
