// D4324: a jump out of a block (break, continue, goto) ends its automatic
// variables' objects, as reaching its end does, and a merge counts every
// path into it (P4445 BUG-23).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

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
