// D4324/P3400: a second, separate TU using the same owner_notify_label
// (with its own, different instance data -- a different owner name)
// and calling into the same shared, cross-TU notify_owner handler --
// proving the custom handler is genuinely used from more than just the
// TU that defines it.

#include "d4324-p3400-notify-shared.h"

int
g (int x) pre<bob_notify>(x > 0)
{
  return x;
}
