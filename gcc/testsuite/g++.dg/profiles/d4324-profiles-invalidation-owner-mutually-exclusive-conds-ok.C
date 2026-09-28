// 'cond' and '!cond' are mutually exclusive by construction, so
// exactly one of these two deletes ever executes -- must stay clean.
// Recognized via ip_owner_edge_refined_out's dominance-based
// predecessor exclusion in ip_compute_owner_reach_info/ip_compute_
// owner_maybe_consumed_info's own "in" loops (and ip_check_owner_
// binding's own leak-point-1 exit check): for each of the second
// if's own real predecessors, exclude the ones provably reachable
// only via the first condition's incompatible edge, rather than
// naively OR-merging all of them. This used to be an accepted,
// documented false positive (both a spurious "consumed again" and a
// spurious "never deleted or passed on") until the predecessor-
// exclusion redesign closed it -- see ip_check_owner_binding's own
// leak-point-3 comment for the full history.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f (int *p [[owner]], bool cond)
{
  if (cond) delete p;
  if (!cond) delete p;
}
