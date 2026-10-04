// { dg-do run { target c++26 } }
// { dg-additional-options "-freflection" }
// The proposal's own motivating "Test that" scenarios: types with
// encapsulated data (span/string_view members, or user types with
// private data and public constructor+getters) can now be reflected
// via define_static_array/define_static_string/define_static_object
// and used at runtime, purely because span/string_view (and, for the
// user type below, the type itself) are marked structural.

#include <meta>
#include <span>
#include <string_view>
#include <vector>

// A string_view constructed at compile-time via reflection, used at
// runtime.
constexpr std::string_view sv = std::define_static_string ("hello, structural");

// A span constructed at compile-time via reflection, used at runtime.
constexpr std::span<const int> sp
  = std::define_static_array (std::vector<int> { 1, 2, 3, 4, 5 });

// A span of spans, used at runtime -- impossible before span itself
// was structural (a span's own private _M_ptr/_M_extent members
// would otherwise disqualify it as define_static_array's element
// type).
constexpr std::span<const int> part1
  = std::define_static_array (std::vector<int> { 1, 2, 3 });
constexpr std::span<const int> part2
  = std::define_static_array (std::vector<int> { 4, 5, 6 });
constexpr std::span<const std::span<const int>> span_of_spans
  = std::define_static_array (std::vector<std::span<const int>> { part1, part2 });

// A span of classes with private data (public constructor + getters),
// constructed at compile-time via reflection, used at runtime.
struct Point structural
{
private:
  int x_, y_;
public:
  constexpr Point (int x, int y) : x_ (x), y_ (y) {}
  constexpr int x () const { return x_; }
  constexpr int y () const { return y_; }
};
constexpr std::span<const Point> points
  = std::define_static_array (std::vector<Point> { Point (1, 2), Point (3, 4) });

// define_static_object with an encapsulated type: blocked before this
// change by the identical structural_type_p gate (via
// meta::reflect_constant, not reflect_constant_array) -- not a
// separate implementation, a direct consequence of the same fix.
constexpr const Point *point_obj = std::define_static_object (Point (10, 20));

int
main ()
{
  if (sv != "hello, structural")
    __builtin_abort ();

  if (sp.size () != 5 || sp[0] != 1 || sp[4] != 5)
    __builtin_abort ();

  if (span_of_spans.size () != 2)
    __builtin_abort ();
  if (span_of_spans[0].size () != 3 || span_of_spans[0][0] != 1 || span_of_spans[0][2] != 3)
    __builtin_abort ();
  if (span_of_spans[1].size () != 3 || span_of_spans[1][0] != 4 || span_of_spans[1][2] != 6)
    __builtin_abort ();

  if (points.size () != 2)
    __builtin_abort ();
  if (points[0].x () != 1 || points[0].y () != 2)
    __builtin_abort ();
  if (points[1].x () != 3 || points[1].y () != 4)
    __builtin_abort ();

  if (point_obj->x () != 10 || point_obj->y () != 20)
    __builtin_abort ();

  return 0;
}
