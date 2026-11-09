// Read the compact binary form of a contract configuration (written by
// d4324-embed-config-to-compact.py) into cfg::rules_t at compile time.
// Layout, all integers little-endian:
//   "P3595c\x01"  magic and version
//   u8 count
//   per rule: u8 kind, u8 caller, u8 semantic, u8 nlines,
//             nlines x (u32 lo, u32 hi),
//             u8 len + bytes for namespace, function, file.
#ifndef D4324_EMBED_CONFIG_COMPACT_H
#define D4324_EMBED_CONFIG_COMPACT_H

#include "d4324-embed-config-common.h"

namespace cfg {

template <std::size_t N>
consteval rules_t
parse_compact (const unsigned char (&d)[N])
{
  std::size_t i = 0;
  auto u8 = [&] () -> unsigned
    {
      if (i == N)
	throw "compact contract configuration: truncated";
      return d[i++];
    };
  auto u32 = [&] ()
    {
      std::uint32_t v = 0;
      for (int b = 0; b < 4; ++b)
	v |= std::uint32_t (u8 ()) << (8 * b);
      return v;
    };
  auto s = [&] (fstr& out)
    {
      unsigned n = u8 ();
      char buf[sizeof out.s] = {};
      if (n >= sizeof buf)
	throw "compact contract configuration: string too long";
      for (unsigned k = 0; k < n; ++k)
	buf[k] = char (u8 ());
      out.assign ({buf, n});
    };
  for (char c : std::string_view ("P3595c\x01", 7))
    if (u8 () != (unsigned char) c)
      throw "compact contract configuration: bad magic";
  rules_t rs;
  rs.n = u8 ();
  if (rs.n > 64)
    throw "compact contract configuration: too many entries";
  for (unsigned k = 0; k < rs.n; ++k)
    {
      rule& r = rs.r[k];
      r.kind = u8 ();
      r.caller = u8 ();
      r.semantic = u8 ();
      r.nlines = u8 ();
      if (r.nlines > 8)
	throw "compact contract configuration: too many line ranges";
      for (unsigned j = 0; j < 2u * r.nlines; ++j)
	r.lines[j] = u32 ();
      s (r.ns);
      s (r.function);
      s (r.file);
    }
  if (i != N)
    throw "compact contract configuration: trailing data";
  rs.index ();
  return rs;
}

} // namespace cfg

#endif
