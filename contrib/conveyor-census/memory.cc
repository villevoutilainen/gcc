// Smart pointers, allocators and the uninitialized-memory algorithms.
#include "types.h"
#include <memory>
#include <memory_resource>
#include <vector>

namespace census
{
  template<class T>
  int smart ()
  {
    int n = 0;
    auto u = std::make_unique<T> (1);
    std::unique_ptr<T> v (new T (2));
    n += (*u == *v) + (u < v) + (u.get () != nullptr) + bool (u);
    u.swap (v);
    v.reset ();
    T *raw = u.release ();
    delete raw;
    auto arr = std::make_unique<T[]> (3);
    n += arr[0] == arr[1];
    auto s = std::make_shared<T> (1);
    std::shared_ptr<T> t = s;
    std::weak_ptr<T> w = s;
    n += s.use_count () + (*s == *t) + !w.expired () + (w.lock () == s) + (s == t);
    auto a = std::allocate_shared<T> (std::allocator<T> (), 1);
    n += bool (a);
    std::shared_ptr<T[]> sa = std::make_shared<T[]> (2);
    n += sa[0] == sa[1];
    return n;
  }

  template<class T>
  int raw_memory ()
  {
    int n = 0;
    alignas (T) unsigned char buf[4 * sizeof (T)];
    T *p = reinterpret_cast<T *> (buf);
    T src[4] = { T (1), T (2), T (3), T (4) };
    std::uninitialized_copy (src, src + 4, p);
    std::destroy (p, p + 4);
    std::uninitialized_fill (p, p + 4, T (5));
    std::destroy_n (p, 4);
    std::uninitialized_value_construct_n (p, 4);
    std::ranges::destroy (p, p + 4);
    T *q = std::construct_at (p, 6);
    n += *q == T (6);
    std::destroy_at (q);
    std::allocator<T> al;
    T *m = std::allocator_traits<std::allocator<T>>::allocate (al, 2);
    std::allocator_traits<std::allocator<T>>::construct (al, m, 1);
    std::allocator_traits<std::allocator<T>>::destroy (al, m);
    std::allocator_traits<std::allocator<T>>::deallocate (al, m, 2);
    MinAlloc<T> ma;
    n += std::allocator_traits<MinAlloc<T>>::max_size (ma) > 0;
    n += std::addressof (src[0]) == src;
    return n;
  }

  int pmr ()
  {
    std::pmr::monotonic_buffer_resource mr;
    std::pmr::vector<int> v (&mr);
    v.push_back (1);
    std::pmr::polymorphic_allocator<Val> pa (&mr);
    Val *p = pa.new_object<Val> (1);
    int n = p->v;
    pa.delete_object (p);
    return n + v.size ();
  }

  int all ()
  {
    return smart<int> () + smart<Val> () + smart<AddrOp> ()
	   + raw_memory<int> () + raw_memory<Val> () + raw_memory<ThrowCopy> ()
	   + pmr ();
  }
}

int main () { return census::all () == 0; }
