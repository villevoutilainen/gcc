// Documents a known, accepted residual imprecision of the diamond-
// shaped leak-point-3/4 fix: 'cond' and '!cond' are mutually exclusive
// by construction, so exactly one of these two deletes ever executes
// -- this is always safe. But split across two SEPARATE if-statements,
// plain CFG-reachability dataflow (unlike Rule #0/#1's own dedicated
// PHI-of-constants recognizer for a single compound &&/|| condition)
// has no way to correlate the two conditions as complements, so the
// second delete is (over-)flagged as a possible double-consume. A
// false positive, not a false negative -- consistent with this
// profile's own "must never accept an actually-unsafe program" bar,
// which allows erring toward extra diagnostics on rarer, more
// contrived shapes like this one. Not fixed here; see ip_check_owner_
// binding's own leak-point-3 comment. The same cond/!cond correlation
// blindness also produces a companion leak-point-1 false positive
// below (the "may still be unconsumed" fact reaching function exit
// can't see that every actual execution does delete p exactly once).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f (int *p [[owner]], bool cond) // { dg-error "never deleted or passed on" }
{
  if (cond) delete p;
  if (!cond) delete p; // { dg-error "consumed again here, after possibly already being consumed" }
}
