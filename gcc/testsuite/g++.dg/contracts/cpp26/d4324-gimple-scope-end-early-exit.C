// D4324 (GIMPLE engine): a jump out of a block (break, continue, goto)
// ends its automatic variables' objects, as reaching its end does, and a
// merge counts every path into it (P4445 BUG-23).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
int use(const int& r) conveyor { return r; }

// Must be rejected: the object's scope has ended.
int early_break(bool c) {
  const int* p = nullptr;
  for (;;) { int x = 1; p = &x; if (c) break; break; }
  return use(*p);                 // { dg-error "cannot prove .is_object_address." }
}
int merged(bool c) {
  const int* p;
  if (c) { int x = 1; p = &x; } else { int z = 3; p = &z; }
  return use(*p);                 // { dg-error "cannot prove .is_object_address." }
}
int one_branch(bool c) {
  int y = 2;
  const int* p = &y;
  if (c) { int x = 1; p = &x; }
  return use(*p);                 // { dg-error "cannot prove .is_object_address." }
}
int nested_break(bool c) {
  const int* p = nullptr;
  for (;;) { { int x = 1; p = &x; if (c) break; } break; }
  return use(*p);                 // { dg-error "cannot prove .is_object_address." }
}
int switch_break(int k) {
  const int* p = nullptr;
  switch (k) { case 1: { int x = 1; p = &x; break; } default: return 0; }
  return use(*p);                 // { dg-error "cannot prove .is_object_address." }
}
int continue_then(bool c) {
  const int* p = nullptr;
  int y = 0;
  for (int i = 0; i < 2; ++i) { int x = i; p = &x; if (c) continue; p = &y; }
  return p ? use(*p) : 0;         // { dg-error "cannot prove .is_object_address." }
}
// Must be accepted.
int in_scope(bool c) {
  int s = 0;
  for (int i = 0; i < 3; ++i) { int x; x = i; const int* p = &x; s += use(*p); }
  return s;                       // OK
}
int in_scope_uninit_then_set(bool c) {
  int s = 0;
  for (int i = 0; i < 3; ++i) { int x; const int* p = &x; x = i; s += use(*p); }
  return s;                       // OK
}
int outer_alive(bool c) {
  int y = 2;
  const int* p = &y;
  for (;;) { int x = 1; if (c) break; (void) x; break; }
  return use(*p);                 // OK
}
int after_merge_outer(bool c) {
  int y = 2;
  const int* p = &y;
  if (c) { int x = 1; (void) x; } else { int z = 3; (void) z; }
  return use(*p);                 // OK
}
int goto_out(bool c) {
  const int* p = nullptr;
  { int x = 1; p = &x; if (c) goto out; }
  return 0;
 out:
  return use(*p);                 // { dg-error "cannot prove .is_object_address." }
}
int goto_ok(bool c) {
  int y = 1;
  const int* p = &y;
  { int x = 1; (void) x; if (c) goto out; }
  return 0;
 out:
  return use(*p);                 // OK
}
// A temporary made afresh in each iteration, by a call's result or by a
// constructor, is alive where it is used, though the previous
// iteration's ended: one in a loop's condition belongs to no block the
// use is in.
struct It { int *p; It () : p (nullptr) { } It (const It &o) : p (o.p) { } };
struct T { int *p; };
It make () conveyor;
T maket () conveyor;
bool same (const It &a, const It &b) conveyor { return a.p == b.p; }
bool samet (const T &a, const T &b) conveyor { return a.p == b.p; }
int while_result () { It a; int k = 0; while (!same (a, make ())) ++k; return k; }   // OK
int for_result () { It a; int k = 0; for (; !same (a, make ()); ) ++k; return k; }   // OK
int while_ctor () { It a; int k = 0; while (!same (a, It ())) ++k; return k; }       // OK
int do_result () { It a; int k = 0; do ++k; while (!same (a, make ())); return k; }  // OK
int while_trivial () { T a{}; int k = 0; while (!samet (a, maket ())) ++k; return k; } // OK
int body_trivial (int n)
{
  T a{};
  int k = 0;
  for (int i = 0; i < n; ++i)
    if (samet (a, maket ()))      // OK
      ++k;
  return k;
}
