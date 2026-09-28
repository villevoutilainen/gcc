// Negative control for the mutually-exclusive-conds fix: the SAME
// condition (not inverted) tested twice does NOT make the second
// delete safe -- when cond is true, both deletes execute, a genuine
// double-free. Confirms ip_owner_edge_refined_out's own invert_tree_
// comparison check correctly requires an actual inversion, not merely
// a dominating condition on the same operand.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f (int *p [[owner]], bool cond) // { dg-error "never deleted or passed on" }
{
  if (cond) delete p;
  if (cond) delete p; // { dg-error "consumed again here, after possibly already being consumed" }
}
