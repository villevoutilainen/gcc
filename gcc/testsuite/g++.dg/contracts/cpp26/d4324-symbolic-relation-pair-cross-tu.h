#include <contracts>
#include <utility>
#include <vector>

using It = std::vector<int>::iterator;

// last is reachable from first: both point into the same sequence.
bool reachable_from (It first, It last) symbolic;

// The whole of v, as a pair of iterators.
std::pair<It, It> whole (std::vector<int>& v)
  post<std::contracts::never_proven_symbolic_v>(r: reachable_from (r.first, r.second));

long sum (It first, It last)
  pre<std::contracts::symbolic_assert_v>(reachable_from (first, last));
