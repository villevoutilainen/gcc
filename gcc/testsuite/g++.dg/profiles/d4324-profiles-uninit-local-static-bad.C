// [[uninit]] only applies to automatic-storage locals -- a
// function-local 'static' has static storage duration, same as a
// namespace-scope variable, and is rejected the same way.
// { dg-do compile { target c++11 } }

void f ()
{
  [[uninit]] static int x; // { dg-error "on declaration other than automatic variable" }
  (void) x;
}
