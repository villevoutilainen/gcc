// { dg-do run { target c++23 } }

#include <vector>
#include <span>
#include <testsuite_hooks.h>
#include <testsuite_iterators.h>
#include <testsuite_allocator.h>

template<typename Range, typename Alloc>
constexpr void
do_test()
{
  using T = std::ranges::range_value_t<Range>;
  T a[]{1,1,0,1,0,0,1,0,0};

  auto eq = [](const std::vector<bool, Alloc>& l, std::span<T> r) {
    if (l.size() != r.size())
      return false;
    for (auto i = 0u; i < l.size(); ++i)
      if (l[i] != r[i])
	return false;
    return true;
  };

  Range r4(a, a+4);
  Range r5(a+4, a+9);

  std::vector<bool, Alloc> v;
  v.append_range(r4);
  VERIFY( eq(v, {a, 4}) );
  v.append_range(r5); // larger than v.capacity()
  VERIFY( eq(v, a) );
  v.append_range(Range(a, a));
  VERIFY( eq(v, a) );
  v.clear();
  v.append_range(Range(a, a));
  VERIFY( v.empty() );
}

template<typename Range>
void
do_test_a()
{
  do_test<Range, std::allocator<bool>>();
  do_test<Range, __gnu_test::SimpleAllocator<bool>>();
}

bool
test_ranges()
{
  using namespace __gnu_test;

  do_test_a<test_forward_range<bool>>();
  do_test_a<test_forward_sized_range<bool>>();
  do_test_a<test_sized_range_sized_sent<bool, forward_iterator_wrapper>>();

  do_test_a<test_input_range<bool>>();
  do_test_a<test_input_sized_range<bool>>();
  do_test_a<test_sized_range_sized_sent<bool, input_iterator_wrapper>>();

  do_test_a<test_range<bool, input_iterator_wrapper_nocopy>>();
  do_test_a<test_sized_range<bool, input_iterator_wrapper_nocopy>>();
  do_test_a<test_sized_range_sized_sent<bool, input_iterator_wrapper_nocopy>>();

  do_test_a<test_forward_range<short>>();
  do_test_a<test_input_range<short>>();

  // Not lvalue-convertible to bool
  struct C {
    C(bool v) : val(v) { }
    operator bool() && { return val; }
    bool operator==(bool b) const { return b == val; }
    bool val;
  };
  using rvalue_input_range = test_range<C, input_iterator_wrapper_rval>;
  do_test<rvalue_input_range, std::allocator<bool>>();

  return true;
}

constexpr bool
test_constexpr()
{
  // XXX: this doesn't test the non-forward_range code paths are constexpr.
  do_test<std::span<short>, std::allocator<bool>>();
  return true;
}

int main()
{
  test_ranges();
  static_assert( test_constexpr() );
}
