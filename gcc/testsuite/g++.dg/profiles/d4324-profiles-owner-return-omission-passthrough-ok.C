// Negative control for d4324-profiles-owner-return-omission-new-bad.C:
// returning an unmarked parameter's own value is not a fresh owner
// source at all (ip_owner_resolve_origin's own PARM_DECL fallback,
// profiles_owning_ptr_p(var), is false for an unmarked parameter) --
// must stay clean. Confirms the fix doesn't conflate "returning an
// existing, unowned value" with "returning a fresh owner source".
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int *
f (int *p)
{
  return p;
}
