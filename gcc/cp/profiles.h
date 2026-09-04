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

/* Register the profile-checking GIMPLE passes (currently just the
   P4222 Initialization profile's) -- called once, early, the same way
   init_contracts (contracts.cc) registers D4324's own experimental
   GIMPLE engine.  Unlike that engine, there is no command-line flag
   to gate registration on: a profile is enabled from source
   (profiles::enforce), not the command line, so registration itself
   is unconditional and each pass's own gate () is what makes it a
   no-op when its profile isn't enforced.  */
extern void init_profiles (void);

/* True if DECL (a PARM_DECL or VAR_DECL of pointer type) is flavored
   "points to [[uninit]] memory" -- carries [[ref_to_uninit]] directly,
   or [[must_init]], which implies it (P4222 S9.3: "[[must_init]]
   implies [[ref_to_uninit]]").  Shared between the front-end
   declaration-time check (decl.cc) and the GIMPLE-level call-site/
   dominance checker (init-profile-gimple.cc), so both sides agree on
   exactly one definition of "uninit-flavored".  */
extern bool profiles_uninit_pointee_p (tree decl);

/* True if FNDECL's parameter at 1-based POSITION carries
   [[ref_to_uninit]] or [[must_init]] -- consults the synthesized
   function-level "profiles_uninit_flavor" marker (grokfndecl, decl.cc)
   rather than the PARM_DECL directly, so this still answers correctly
   even when FNDECL is only declared, never defined, in this
   translation unit (see that marker's own comment for why a direct
   PARM_DECL lookup can't be trusted to survive that case).  If
   MUST_INIT_ONLY, only a [[must_init]] parameter at that position
   counts -- used by the caller-side postcondition check ("does this
   call establish the argument as now-initialized"), which a plain
   [[ref_to_uninit]] parameter does not.  */
extern bool profiles_uninit_flavor_at_position_p (tree fndecl,
						   unsigned position,
						   bool must_init_only);

/* P3589, Phase 5: true if LOC's own file was reached via an #include
   the translation unit has exempted from PROFILE_NAME (e.g.
   "std::init") with a matching angle/quote-ness --
   '[[profiles::exempt(profile, angle_header: "NAME")]]' /
   quote_header:.  Every diagnostic site across this project's own
   profile checkers (decl.cc, tree.cc's attribute handlers, init.cc,
   init-profile-gimple.cc) is expected to consult this before actually
   emitting, the same "always check, never skip" discipline
   profiles_enforced_p itself already has -- consult profiles.cc's own
   profiles_handle_exempt_attribute for why exemptions are only
   resolvable via libcpp's cpp_get_include_spelling, not from
   line_map_ordinary alone.  */
extern bool profiles_header_exempt_p (location_t loc, const char *profile_name);

#endif /* ! GCC_CP_PROFILES_H */
