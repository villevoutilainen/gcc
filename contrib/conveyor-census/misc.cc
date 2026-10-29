// Numerics, time, bits and text processing.
#include "types.h"
#include <numeric>
#include <bit>
#include <chrono>
#include <complex>
#include <valarray>
#include <random>
#include <ratio>
#include <regex>
#include <sstream>
#include <iomanip>
#include <limits>
#include <system_error>
#include <future>
#include <generator>
#include <spanstream>

namespace census
{
  template<class T>
  int integers ()
  {
    int n = 0;
    T a = 12, b = 18;
    n += std::gcd (a, b) + std::lcm (a, b) + std::midpoint (a, b);
    n += std::saturating_add (a, b) + std::saturating_sub (a, b)
	 + std::saturating_mul (a, b) + std::saturating_div (a, b)
	 + std::saturating_cast<signed char> (a);
    if constexpr (std::is_unsigned_v<T>)
      n += std::popcount (a) + std::countl_zero (a) + std::countr_one (a)
	   + std::bit_width (a) + std::has_single_bit (a) + std::bit_ceil (a)
	   + std::rotl (a, 1);
    n += std::cmp_less (a, b) + std::in_range<short> (a);
    return n;
  }

  std::generator<int> inner_gen ()
  {
    co_yield 2;
  }

  std::generator<int> outer_gen ()
  {
    co_yield 1;
    co_yield std::ranges::elements_of (inner_gen ());
  }

  // String and span streams over a string or buffer, and a nested
  // generator.
  int streams ()
  {
    int n = 0;
    std::string s = "1 2";
    std::stringbuf sb (s);
    std::istringstream is (s);
    std::ostringstream os (s);
    std::stringstream ss (s);
    sb.str (s);
    is.str (s);
    n += sb.str ().size () + is.str ().size () + os.str ().size ()
	 + ss.str ().size ();
    char buf[16] = {};
    std::spanstream sps (buf);
    sps << 12;
    sps.seekp (0);
    sps.seekg (0);
    n += sps.tellp () == 0;
    for (int x : outer_gen ())
      n += x;
    return n;
  }

  int all ()
  {
    int n = integers<int> () + integers<unsigned> () + integers<long long> ()
	    + integers<unsigned char> () + streams ();
    using namespace std::chrono;
    auto d = 1h + 30min + 5s;
    n += duration_cast<seconds> (d).count () + floor<minutes> (d).count ();
    auto d2 = d;
    d2 += d;
    d2 -= d;
    d2 %= d;
    n += d2.count () > 0;
    // A ratio that's neither a whole multiple nor a whole fraction.
    using thirds = std::chrono::duration<long, std::ratio<1, 3>>;
    using halves = std::chrono::duration<long, std::ratio<1, 2>>;
    n += duration_cast<halves> (thirds (5)).count ();
    auto t = system_clock::now ();
    n += time_point_cast<seconds> (t).time_since_epoch ().count () > 0;
    t += d;
    t -= d;
    year_month_day ymd (2026y, October, 26d);
    n += ymd.ok () + int (ymd.year ());
    std::complex<double> c (1, 2);
    n += std::abs (c * c) > 0;
    std::valarray<int> va = { 1, 2, 3 };
    n += (va * 2).sum ();
    std::mt19937 g (1);
    std::uniform_int_distribution<int> dist (1, 6);
    n += dist (g);
    std::regex re ("a+b");
    n += std::regex_match ("aab", re);
    std::ostringstream os;
    os << std::setw (4) << 42 << std::hex << 255;
    n += os.str ().size ();
    n += std::numeric_limits<int>::max () > 0;
    std::string what = "w";
    std::system_error se (1, std::generic_category (), what);
    std::system_error se2 (1, std::system_category (), what);
    std::system_error se3 (1, std::future_category (), what);
    std::system_error se4 (1, std::iostream_category (), what);
    n += se.code ().value () + se2.code ().value () + se3.code ().value ()
	 + se4.code ().value ();
    return n;
  }
}

int main () { return census::all () == 0; }
