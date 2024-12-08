// { dg-options "-std=c++11"
// { dg-do compile }


namespace [[gnu::no_template_explicit_instantiation]] N 
{
  template<class T>
  class A { };
}

template class N::A<int>; // { dg-warning "explicit instantiation" }

namespace M
{
  template<class T>
  class A { };
}

template class M::A<int>;

namespace [[gnu::no_template_explicit_instantiation]] O
{
  template<class T>
  class A { };
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtemplate-explicit-instantiation"
template class O::A<int>;
#pragma GCC diagnostic pop
