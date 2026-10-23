#pragma GCC system_header
#include <contracts>

struct H { int* p; H (int* q) : p (q) {} ~H () {} };

inline void kill (H* p) { p->~H (); }

struct S
{
  int buf[2];
  H h;
  int* get () { return buf; }
  S () : h (get ()) {}
};
