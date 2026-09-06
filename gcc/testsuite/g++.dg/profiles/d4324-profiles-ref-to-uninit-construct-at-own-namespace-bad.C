// P4222 Initialization profile: construct_at's special, lax treatment
// is keyed on decl_in_std_namespace_p, not just the bare name --
// a DIFFERENT function that happens to also be named 'construct_at',
// declared outside namespace std, must NOT get it: its argument is
// still checked by the ordinary bidirectional flavor-consistency rule
// like any other, unrecognized callee.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

namespace not_std
{
  template<class T>
  T* construct_at (T* p, int v) { *p = v; return p; }
}

int main ()
{
  int x [[uninit]];
  int* p [[ref_to_uninit]] = &x;
  not_std::construct_at (p, 5); // { dg-error "refers to \[^\n\]*uninit\[^\n\]* memory but its parameter is not marked" }
}
