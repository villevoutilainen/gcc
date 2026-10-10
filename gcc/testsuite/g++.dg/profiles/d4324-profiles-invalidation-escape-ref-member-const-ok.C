// P3446R0 Invalidation profile: a 'const T&' DATA MEMBER, read
// directly and returned, gets the same trust as the sibling escape-
// ref-member-direct-ok.C's plain 'T&' member -- deliberately NOT
// mirroring the const-ref/rvalue-ref restriction this file applies to
// a reference PARAMETER (ip_local_var_p) or a reference-bound CALL
// ARGUMENT elsewhere in this file.
//
// That restriction exists because a parameter's (or call argument's)
// const-ref/rvalue-ref binding to a caller-supplied temporary has a
// lifetime scoped to roughly THIS SAME call -- returning it directly
// could manufacture a NEW dangling reference local to this one
// activation. A member's binding, by contrast, happened in an
// entirely separate, already-finished EARLIER activation (the
// object's own construction): by the time 'get_ref()' runs, whether
// 'value' is valid was already, irrevocably settled one way or the
// other, and nothing this read does can be what newly breaks it.
// Whether a const-ref member's own too-short-lived construction-time
// binding is independently caught at ITS OWN site is a separate,
// open question (aggregate reference-member lifetime extension is one
// of the more esoteric corners of the standard) -- it does not change
// the answer to the question this read site is actually being asked.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct HandleConst
{
  const int &value;

  const int &get_ref () const
  {
    return value;
  }
};
