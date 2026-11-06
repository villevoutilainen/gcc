// D4324: std::contracts::constexpr_print_str, constexpr_warning_str and
// constexpr_error_str do nothing outside constant evaluation.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts" }

#include <contracts>

namespace sc = std::contracts;

int
main ()
{
  sc::constexpr_print_str ("print");
  sc::constexpr_warning_str ("tag", "warning");
  sc::constexpr_error_str ("", "error");
}
