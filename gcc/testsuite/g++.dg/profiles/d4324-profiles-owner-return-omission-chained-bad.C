// Companion to d4324-profiles-owner-return-omission-new-bad.C: the same
// laundering hazard, one hop further out -- wrapper() isn't itself
// declared [[owner]], but its own return provably traces to a call to
// an [[owner]]-returning function (ip_owner_fresh_source_call_p already
// recognizes this shape as a fresh source), so it must be flagged too.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

[[owner]] int *helper ();

int *
wrapper ()
{
  return helper (); // { dg-error "freshly-allocated pointer from a function not marked" }
}
