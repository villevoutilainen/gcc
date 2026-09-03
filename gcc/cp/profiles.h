/* Declarations for the P3589 Profiles framework, for the C++ front end.
   Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#ifndef GCC_CP_PROFILES_H
#define GCC_CP_PROFILES_H

/* Called by cp_parser_declaration (parser.cc) the moment it commits to
   parsing anything other than an empty-declaration -- see that
   function's own call site for why this single spot is a sound,
   unified chokepoint for "has any non-empty declaration happened yet
   in this translation unit", covering every top-level and
   namespace-scope declaration kind by construction, not by an
   incomplete per-kind audit.  */
extern void profiles_note_nonempty_declaration (void);

/* Called by cp_parser_declaration in place of the bare "attribute
   ignored" warning empty-declarations previously got unconditionally.
   Gives the attribute-specifier-seq of an empty-declaration a real
   semantic dispatch point, matching what the standard actually says
   ("the attribute-specifier-seq appertains to the empty-declaration")
   -- currently only profiles::enforce is recognized; anything else
   still falls back to the same generic warning as before.  */
extern void cp_finish_empty_declaration (location_t, tree);

/* True if the named profile (e.g. "std::init") is enforced for this
   translation unit.  */
extern bool profiles_enforced_p (const char *);

#endif /* ! GCC_CP_PROFILES_H */
