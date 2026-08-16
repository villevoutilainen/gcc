// D4324/P3400: the *one* real definition of the custom, strongly-typed
// handler declared in d4324-p3400-notify-shared.h -- called from
// d4324-p3400-notify-multi-tu.C (this test's own main TU) and from
// d4324-p3400-notify-multi-tu-user2.cc (a second, separate TU),
// exercising ordinary one-definition-rule linkage across three
// translation units total.

#include "d4324-p3400-notify-shared.h"

int notify_owner_calls = 0;
const char* notify_owner_names[notify_owner_max_calls] = {};
const char* notify_owner_comments[notify_owner_max_calls] = {};

void
notify_owner (const OwnerNotification& n)
{
  if (notify_owner_calls < notify_owner_max_calls)
    {
      notify_owner_names[notify_owner_calls] = n.owner_name;
      notify_owner_comments[notify_owner_calls] = n.comment;
    }
  ++notify_owner_calls;
}
