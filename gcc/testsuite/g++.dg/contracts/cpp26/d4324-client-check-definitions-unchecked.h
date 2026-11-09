// Shared by d4324-client-check-definitions-unchecked.C and its -aux.cc.
#include <contracts>

namespace vendor
{
  int f (int x) pre<std::contracts::default_v> (x > 0);
}
