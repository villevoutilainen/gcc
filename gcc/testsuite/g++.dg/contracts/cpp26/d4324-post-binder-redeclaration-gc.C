// D4324: a postcondition result name on repeated declarations of a
// function.  The result variable made for a redeclaration has that
// redeclaration as its context, which is freed when it is merged into
// the first declaration; the record of lone binder names
// (mark_single_post_binder) kept it alive, so the next collection
// marked through the freed declaration and crashed.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts --param ggc-min-expand=0 --param ggc-min-heapsize=0" }

int f (int x) post (r: r > 0);
int f (int x) post (r: r > 0);
int f (int x) post (r: r > 0) { return x > 0 ? x : 1; }
int g (int y) { return f (y) + 1; }
int h (int y) { return g (y) + f (2); }
