// Types the census instantiates the library with: the plain ones, and the
// awkward ones a library template must also cope with.
#ifndef CONVEYOR_CENSUS_TYPES_H
#define CONVEYOR_CENSUS_TYPES_H

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <ext/extptr_allocator.h>
#include <testsuite_iterators.h>

namespace census
{
  // A class with user-provided special members and an ordering.
  struct Val
  {
    int v = 0;
    Val () { }
    Val (int i) : v (i) { }
    Val (const Val &o) : v (o.v) { }
    Val &operator= (const Val &o) { v = o.v; return *this; }
    ~Val () { }
    friend bool operator== (const Val &a, const Val &b) { return a.v == b.v; }
    friend auto operator<=> (const Val &a, const Val &b) { return a.v <=> b.v; }
  };

  struct MoveOnly
  {
    int v = 0;
    MoveOnly () = default;
    MoveOnly (int i) : v (i) { }
    MoveOnly (MoveOnly &&) = default;
    MoveOnly &operator= (MoveOnly &&) = default;
    friend bool operator== (const MoveOnly &, const MoveOnly &) = default;
    friend auto operator<=> (const MoveOnly &, const MoveOnly &) = default;
  };

  // Copies may throw.
  struct ThrowCopy
  {
    int v = 0;
    ThrowCopy () = default;
    ThrowCopy (int i) : v (i) { }
    ThrowCopy (const ThrowCopy &o) noexcept (false) : v (o.v) { }
    ThrowCopy &operator= (const ThrowCopy &o) noexcept (false)
    { v = o.v; return *this; }
    friend bool operator== (const ThrowCopy &, const ThrowCopy &) = default;
    friend auto operator<=> (const ThrowCopy &, const ThrowCopy &) = default;
  };

  // 'operator&' isn't the address.
  struct AddrOp
  {
    int v = 0;
    AddrOp () = default;
    AddrOp (int i) : v (i) { }
    void operator& () const = delete;
    friend bool operator== (const AddrOp &, const AddrOp &) = default;
    friend auto operator<=> (const AddrOp &, const AddrOp &) = default;
  };

  // The least an allocator needs.
  template<class T>
  struct MinAlloc
  {
    using value_type = T;
    MinAlloc () = default;
    template<class U> MinAlloc (const MinAlloc<U> &) { }
    T *allocate (std::size_t n) { return std::allocator<T> ().allocate (n); }
    void deallocate (T *p, std::size_t n) { std::allocator<T> ().deallocate (p, n); }
    friend bool operator== (const MinAlloc &, const MinAlloc &) { return true; }
  };

  // A stateful allocator that propagates on copy, move and swap.
  template<class T>
  struct PropAlloc
  {
    using value_type = T;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;
    int id = 0;
    PropAlloc () = default;
    PropAlloc (int i) : id (i) { }
    template<class U> PropAlloc (const PropAlloc<U> &o) : id (o.id) { }
    T *allocate (std::size_t n) { return std::allocator<T> ().allocate (n); }
    void deallocate (T *p, std::size_t n) { std::allocator<T> ().deallocate (p, n); }
    friend bool operator== (const PropAlloc &a, const PropAlloc &b)
    { return a.id == b.id; }
  };

  // Fancy pointers.
  template<class T> using ExtAlloc = __gnu_cxx::_ExtPtr_allocator<T>;

  // Iterator categories over an array.
  template<class T, template<class> class It>
  using range = __gnu_test::test_container<T, It>;
  // The same, as a std::ranges range.
  template<class T, template<class> class It>
  using rrange = __gnu_test::test_range<T, It>;
  using __gnu_test::input_iterator_wrapper;
  using __gnu_test::forward_iterator_wrapper;
  using __gnu_test::bidirectional_iterator_wrapper;
  using __gnu_test::random_access_iterator_wrapper;

  template<class T> T make (int i) { return T (i); }
}

#endif
