#include <contracts>
inline int gmf_fails;
struct gmf_count_t
{
  static constexpr bool
  is_ignored (std::contracts::assertion_static_info) noexcept { return false; }
  constexpr void
  operator() (const std::contracts::assertion_context& c) const
  { if (!c.check ()) ++gmf_fails; }
};
inline constexpr gmf_count_t gmf_counter{};
// Implicitly inline: defined in their classes outside any named module.
struct Q { Q (int p) : v (p) { contract_assert<gmf_counter> (v > 0); } int v; };
template <class T> struct R
{ R (T p) : v (p) { contract_assert<gmf_counter> (v > 0); } T v; };
inline R<int> make_r (int x) { return R<int> (x); }
