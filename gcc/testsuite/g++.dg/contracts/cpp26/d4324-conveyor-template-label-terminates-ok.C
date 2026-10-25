// D4324: a control type that is a class template's specialization
// ('Label<true>') has an operator() nothing has instantiated yet when
// the analysis asks whether a failed check stops execution.  With no body
// the answer was "may return", so its contract established nothing.  A
// constexpr one is now instantiated then, as constant evaluation does.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
#include <contract_labels>

[[noreturn]] void stop ();

template <bool C>
struct Label : std::contracts::P3400::label_base<Label<C> >
{
  using assertion_control_object = Label;
  const char *level;
  constexpr Label (const char *l) conveyor : level (l) {}
  static constexpr bool is_conveyor (std::contracts::assertion_static_info) noexcept
  { return C; }
  static constexpr bool constify (std::contracts::assertion_static_info) noexcept
  { return false; }
  constexpr void operator() (const std::contracts::assertion_context &c) const
  {
    if (c.check ())
      return;
    stop ();
  }
};

int f (int a) conveyor
{
  contract_assert<Label<true>{"x"}> (0 < a);
  return a - 1;
}
