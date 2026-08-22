// Exception classes for <stdexcept>  -*- C++ -*-

// Copyright (C) 2001-2026 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.

// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.

/** @file bits/stdexcept_except.h
 *  This is an internal header file, included by other library headers.
 *  Do not attempt to use it directly. @headername{stdexcept}  */

#ifndef _STDEXCEPT_EXCEPT_H
#define _STDEXCEPT_EXCEPT_H 1

#include <exception>
#include <string>

namespace std _GLIBCXX_VISIBILITY(default)
{
_GLIBCXX_BEGIN_NAMESPACE_VERSION

#if _GLIBCXX_USE_DUAL_ABI
#if _GLIBCXX_USE_CXX11_ABI
#if __cpp_lib_constexpr_exceptions >= 202502L
  struct __cow_constexpr_string;
  namespace __detail
  {
    extern "C"
    {
      void _ZNSt12__cow_stringC2EPKcm(__cow_constexpr_string*, const char*,
				      unsigned long);
      void _ZNSt12__cow_stringC2EPKcj(__cow_constexpr_string*, const char*,
				      unsigned int);
      void _ZNSt12__cow_stringC2EPKcy(__cow_constexpr_string*, const char*,
				      unsigned long long);
      void _ZNSt12__cow_stringC2EPKc(__cow_constexpr_string*, const char*);
      void _ZNSt12__cow_stringC2ERKS_(__cow_constexpr_string*,
				      const __cow_constexpr_string&) noexcept;
      void _ZNSt12__cow_stringC2EOS_(__cow_constexpr_string*,
				     __cow_constexpr_string&&) noexcept;
      void _ZNSt12__cow_stringD2Ev(__cow_constexpr_string*);
      __cow_constexpr_string&
      _ZNSt12__cow_stringaSERKS_(__cow_constexpr_string*,
				 const __cow_constexpr_string&) noexcept;
      __cow_constexpr_string&
      _ZNSt12__cow_stringaSEOS_(__cow_constexpr_string*,
				__cow_constexpr_string&&) noexcept;
      const char*
      _ZNKSt12__cow_string5c_strEv(const __cow_constexpr_string*) noexcept;
    }
  } // namespace __detail

  // Emulates an old COW string when the new std::string is in use,
  // but in addition is constexpr and uses the __cow_string out of
  // line cdtors/methods unless manifestly constant evaluated.
  struct __cow_constexpr_string
  {
    union {
      const char* _M_p;
      char _M_bytes[sizeof(const char*)];
      string* _M_str;
    };

    [[__gnu__::__always_inline__]] constexpr
    __cow_constexpr_string(const string& __o)
#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
    pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__o))
#endif
    {
      if consteval {
	_M_str = new string(__o);
      } else {
	__cow_constexpr_string_ctor(__o.c_str(), __o.length());
      }
    }

    [[__gnu__::__always_inline__]] inline void
    __cow_constexpr_string_ctor(const char *__s, unsigned long __l)
    {
      __detail::_ZNSt12__cow_stringC2EPKcm(this, __s, __l);
    }

    [[__gnu__::__always_inline__]] inline void
    __cow_constexpr_string_ctor(const char *__s, unsigned int __l)
    {
      __detail::_ZNSt12__cow_stringC2EPKcj(this, __s, __l);
    }

    [[__gnu__::__always_inline__]] inline void
    __cow_constexpr_string_ctor(const char *__s, unsigned long long __l)
    {
      __detail::_ZNSt12__cow_stringC2EPKcy(this, __s, __l);
    }

    [[__gnu__::__always_inline__]] constexpr
    __cow_constexpr_string(const char* __o)
    {
      if consteval {
	_M_str = new string(__o);
      } else {
	__detail::_ZNSt12__cow_stringC2EPKc(this, __o);
      }
    }

    [[__gnu__::__always_inline__]] constexpr
    __cow_constexpr_string(const __cow_constexpr_string& __o) noexcept
#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
    pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__o))
#endif
    {
      if consteval {
#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
	// D4324/P2680: '_M_str' is a raw pointer field inside a union
	// with '_M_p'/'_M_bytes' -- is_object_address(&__o) alone says
	// nothing about whether the union's ACTIVE member is '_M_str',
	// let alone whether it holds a valid string*. This class's own
	// invariant guarantees it does whenever '_M_str' is read from a
	// manifestly-constant-evaluated context (the only context these
	// consteval branches ever run in): it's set via 'new string(...)'
	// by this same class's own constexpr constructors, never left
	// uninitialized or holding a stale '_M_p'/'_M_bytes' value there.
	// Not derivable from anything else -- trusted (never_proven),
	// same as any other class-invariant-only fact in this file.
	// Copied into a plain, non-union local first: an is_object_
	// address precondition/assert naming the raw union member
	// expression directly ('__o._M_str') is not yet a recognized
	// shape for this check (an anonymous-union member access, one
	// COMPONENT_REF hop deeper than an ordinary named field) --
	// confirmed via a minimal, non-library repro -- whereas the same
	// fact about an ordinary local variable already works.
	string* const __p = __o._M_str;
	contract_assert<std::contracts::never_proven_conveyor_v>
	  (std::is_object_address (__p));
	_M_str = new string(*__p);
#else
	_M_str = new string(*__o._M_str);
#endif
      } else {
	__detail::_ZNSt12__cow_stringC2ERKS_(this, __o);
      }
    }

    [[__gnu__::__always_inline__]] constexpr __cow_constexpr_string&
    operator=(const __cow_constexpr_string& __o) noexcept
#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
    pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (this))
    pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__o))
#endif
    {
      if consteval {
	string* __p = _M_str;
#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
	// See the copy constructor's own comment just above for why
	// '__o._M_str'/'_M_str' are each copied into a plain local first
	// before their own is_object_address is asserted -- needed for
	// '__p' too now, not just '__op': the D4324/P2680 ctor/dtor
	// soundness fix gives 'delete __p;' below a real caller-side
	// obligation on '__p' itself, which a raw union-member read has
	// never satisfied on its own (same reasoning as '__op').
	contract_assert<std::contracts::never_proven_conveyor_v>
	  (std::is_object_address (__p));
	string* const __op = __o._M_str;
	contract_assert<std::contracts::never_proven_conveyor_v>
	  (std::is_object_address (__op));
	_M_str = new string(*__op);
#else
	_M_str = new string(*__o._M_str);
#endif
	delete __p;
	return *this;
      } else {
	return __detail::_ZNSt12__cow_stringaSERKS_(this, __o);
      }
    }

    [[__gnu__::__always_inline__]] constexpr
    ~__cow_constexpr_string()
    {
      if consteval {
#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
	// See the copy constructor's own comment above for why '_M_str'
	// is copied into a plain local first before its own is_object_
	// address is asserted -- the D4324/P2680 ctor/dtor soundness fix
	// gives 'delete' a real caller-side obligation on its own operand.
	string* const __p = _M_str;
	contract_assert<std::contracts::never_proven_conveyor_v>
	  (std::is_object_address (__p));
	delete __p;
#else
	delete _M_str;
#endif
      } else {
	__detail::_ZNSt12__cow_stringD2Ev(this);
      }
    }

    [[__gnu__::__always_inline__]] constexpr
    __cow_constexpr_string(__cow_constexpr_string&& __o) noexcept
#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
    pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__o))
#endif
    {
      if consteval {
#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
	// See the copy constructor's own comment for why '__o._M_str' is
	// copied into a plain local first -- needed here for an
	// additional reason too: std::move's own Q2 ownership check
	// requires its argument to be a parameter/local the CALLING
	// function itself received or created, which a raw union-member
	// dereference ('*__o._M_str') is not, but a local copy is.
	string* const __p = __o._M_str;
	contract_assert<std::contracts::never_proven_conveyor_v>
	  (std::is_object_address (__p));
	_M_str = new string(std::move(*__p));
#else
	_M_str = new string(std::move(*__o._M_str));
#endif
      } else {
	__detail::_ZNSt12__cow_stringC2EOS_(this, std::move(__o));
      }
    }

    [[__gnu__::__always_inline__]] constexpr __cow_constexpr_string&
    operator=(__cow_constexpr_string&& __o) noexcept
#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
    pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (this))
    pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__o))
#endif
    {
      if consteval {
	string* __p = _M_str;
#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
	// See the move constructor's own comment just above, and the
	// copy-assignment operator's own identical addition, for why
	// '__p' (not just '__op') needs its own is_object_address
	// asserted before the 'delete __p;' below.
	contract_assert<std::contracts::never_proven_conveyor_v>
	  (std::is_object_address (__p));
	string* const __op = __o._M_str;
	contract_assert<std::contracts::never_proven_conveyor_v>
	  (std::is_object_address (__op));
	_M_str = new string(std::move(*__op));
#else
	_M_str = new string(std::move(*__o._M_str));
#endif
	delete __p;
	return *this;
      } else {
	return __detail::_ZNSt12__cow_stringaSEOS_(this, std::move(__o));
      }
    }

    [[__gnu__::__always_inline__]] constexpr const char*
    c_str() const noexcept
#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
    pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (this))
#endif
    {
      if consteval {
#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
	// See the copy constructor's own comment for why '_M_str' is
	// copied into a plain local first.
	string* const __p = _M_str;
	contract_assert<std::contracts::never_proven_conveyor_v>
	  (std::is_object_address (__p));
	return __p->c_str();
#else
	return _M_str->c_str();
#endif
      } else {
	return __detail::_ZNKSt12__cow_string5c_strEv(this);
      }
    }
  };

  typedef __cow_constexpr_string __cow_string;
#else
  // Emulates an old COW string when the new std::string is in use.
  struct __cow_string
  {
    union {
      const char* _M_p;
      char _M_bytes[sizeof(const char*)];
    };

    __cow_string();
    __cow_string(const std::string&);
    __cow_string(const char*, size_t);
    __cow_string(const __cow_string&) _GLIBCXX_NOTHROW;
    __cow_string& operator=(const __cow_string&) _GLIBCXX_NOTHROW;
    ~__cow_string();
#if __cplusplus >= 201103L
    __cow_string(__cow_string&&) noexcept;
    __cow_string& operator=(__cow_string&&) noexcept;
#endif
  };
#endif

  typedef basic_string<char> __sso_string;
#else // _GLIBCXX_USE_CXX11_ABI
  typedef basic_string<char> __cow_string;

  // Emulates a new SSO string when the old std::string is in use.
  struct __sso_string
  {
    struct __str
    {
      const char* _M_p;
      size_t _M_string_length;
      char _M_local_buf[16];
    };

    union {
      __str _M_s;
      char _M_bytes[sizeof(__str)];
    };

    __sso_string() _GLIBCXX_NOTHROW;
    __sso_string(const std::string&);
    __sso_string(const char*, size_t);
    __sso_string(const __sso_string&);
    __sso_string& operator=(const __sso_string&);
    ~__sso_string();
#if __cplusplus >= 201103L
    __sso_string(__sso_string&&) noexcept;
    __sso_string& operator=(__sso_string&&) noexcept;
#endif
  };
#endif // _GLIBCXX_USE_CXX11_ABI
#else  // _GLIBCXX_USE_DUAL_ABI
  typedef basic_string<char> __sso_string;
  typedef basic_string<char> __cow_string;
#endif

  /**
   * @addtogroup exceptions
   * @{
   */

  /** Logic errors represent problems in the internal logic of a program;
   *  in theory, these are preventable, and even detectable before the
   *  program runs (e.g., violations of class invariants).
   *  @brief One of two subclasses of exception.
   */
  class logic_error : public exception
  {
    __cow_string _M_msg;

  public:
#if __cpp_lib_constexpr_exceptions >= 202502L
    [[__gnu__::__gnu_inline__]]
    constexpr inline explicit
    logic_error(const string& __arg) _GLIBCXX_TXN_SAFE
#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
    pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__arg))
#endif
    : _M_msg(__arg) {}

    [[__gnu__::__gnu_inline__]]
    constexpr inline explicit
    logic_error(const char* __arg) _GLIBCXX_TXN_SAFE
    : _M_msg(__arg) {}

#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
    // D4324/P2680: this branch's implementation has no engine support
    // for a precondition on a '=default' special member (confirmed: an
    // internal compiler error, maybe_apply_function_contracts's own
    // MUST_NOT_THROW_EXPR assumption about a user-written body doesn't
    // hold for a compiler-synthesized one) -- an explicit body doing
    // exactly what the implicit one would (delegate to the base class's
    // own copy/move, then copy/move '_M_msg') sidesteps that, and
    // changes nothing observable: 'logic_error' is already non-trivial
    // for every copy/move operation regardless, since it has a virtual
    // destructor (is_trivially_copyable et al. require no virtual
    // functions/bases at all, independent of whether a special member
    // is '=default' or hand-written) -- see std::exception's own
    // identical copy/move members for why the base delegation below
    // needs no precondition of its own (no data members to prove
    // anything about).
    constexpr logic_error(logic_error&& __arg) noexcept
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__arg))
      : exception(std::move(__arg)), _M_msg(std::move(__arg._M_msg)) { }
    constexpr logic_error& operator=(logic_error&& __arg) noexcept
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (this))
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__arg))
    {
      exception::operator=(std::move(__arg));
      _M_msg = std::move(__arg._M_msg);
      return *this;
    }
    constexpr logic_error(const logic_error& __arg) noexcept
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__arg))
      : exception(__arg), _M_msg(__arg._M_msg) { }
    constexpr logic_error& operator=(const logic_error& __arg) noexcept
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (this))
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__arg))
    {
      exception::operator=(__arg);
      _M_msg = __arg._M_msg;
      return *this;
    }
#else
    constexpr logic_error(logic_error&& __arg) noexcept = default;
    constexpr logic_error& operator=(logic_error&& __arg) noexcept = default;
    constexpr logic_error(const logic_error&) noexcept = default;
    constexpr logic_error& operator=(const logic_error&) noexcept = default;
#endif

    [[__gnu__::__gnu_inline__]]
    constexpr inline virtual ~logic_error() _GLIBCXX_TXN_SAFE_DYN noexcept { }

    [[__gnu__::__gnu_inline__]]
    constexpr inline virtual const char*
    what() const _GLIBCXX_TXN_SAFE_DYN noexcept
#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
    pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (this))
#endif
    {
      return _M_msg.c_str();
    }
#else
    /** Takes a character string describing the error.  */
    explicit
    logic_error(const string& __arg) _GLIBCXX_TXN_SAFE;

#if __cplusplus >= 201103L
    explicit
    logic_error(const char*) _GLIBCXX_TXN_SAFE;

    logic_error(logic_error&&) noexcept;
    logic_error& operator=(logic_error&&) noexcept;
#endif

#if _GLIBCXX_USE_CXX11_ABI || _GLIBCXX_DEFINE_STDEXCEPT_COPY_OPS
    logic_error(const logic_error&) _GLIBCXX_NOTHROW;
    logic_error& operator=(const logic_error&) _GLIBCXX_NOTHROW;
#elif __cplusplus >= 201103L
    logic_error(const logic_error&) = default;
    logic_error& operator=(const logic_error&) = default;
#endif

    virtual ~logic_error() _GLIBCXX_TXN_SAFE_DYN _GLIBCXX_NOTHROW;

    /** Returns a C-style character string describing the general cause of
     *  the current error (the same string passed to the ctor).  */
    virtual const char*
    what() const _GLIBCXX_TXN_SAFE_DYN _GLIBCXX_NOTHROW;
#endif

# ifdef _GLIBCXX_TM_TS_INTERNAL
    friend void*
    ::_txnal_logic_error_get_msg(void* e);
# endif
  };

  /** Thrown by the library, or by you, to report domain errors (domain in
   *  the mathematical sense).  */
  class domain_error : public logic_error
  {
  public:
#if __cpp_lib_constexpr_exceptions >= 202502L
    [[__gnu__::__gnu_inline__]]
    constexpr inline explicit domain_error(const string& __arg)
      _GLIBCXX_TXN_SAFE

#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__arg))
#endif
    : logic_error(__arg) { }
    [[__gnu__::__gnu_inline__]]
    constexpr inline explicit domain_error(const char* __arg) _GLIBCXX_TXN_SAFE
    : logic_error(__arg) { }
    constexpr domain_error(const domain_error&) = default;
    constexpr domain_error& operator=(const domain_error&) = default;
    constexpr domain_error(domain_error&&) = default;
    constexpr domain_error& operator=(domain_error&&) = default;
    [[__gnu__::__gnu_inline__]]
    constexpr inline virtual ~domain_error() noexcept { }
#else
    explicit domain_error(const string& __arg) _GLIBCXX_TXN_SAFE;
#if __cplusplus >= 201103L
    explicit domain_error(const char*) _GLIBCXX_TXN_SAFE;
    domain_error(const domain_error&) = default;
    domain_error& operator=(const domain_error&) = default;
    domain_error(domain_error&&) = default;
    domain_error& operator=(domain_error&&) = default;
#endif
    virtual ~domain_error() _GLIBCXX_NOTHROW;
#endif
  };

  /** Thrown to report invalid arguments to functions.  */
  class invalid_argument : public logic_error
  {
  public:
#if __cpp_lib_constexpr_exceptions >= 202502L
    [[__gnu__::__gnu_inline__]]
    constexpr inline explicit invalid_argument(const string& __arg)
      _GLIBCXX_TXN_SAFE

#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__arg))
#endif
    : logic_error(__arg) { }
    [[__gnu__::__gnu_inline__]]
    constexpr inline explicit invalid_argument(const char* __arg)
      _GLIBCXX_TXN_SAFE
    : logic_error(__arg) { }
    constexpr invalid_argument(const invalid_argument&) = default;
    constexpr invalid_argument& operator=(const invalid_argument&) = default;
    constexpr invalid_argument(invalid_argument&&) = default;
    constexpr invalid_argument& operator=(invalid_argument&&) = default;
    [[__gnu__::__gnu_inline__]]
    constexpr inline virtual ~invalid_argument() noexcept { }
#else
    explicit invalid_argument(const string& __arg) _GLIBCXX_TXN_SAFE;
#if __cplusplus >= 201103L
    explicit invalid_argument(const char*) _GLIBCXX_TXN_SAFE;
    invalid_argument(const invalid_argument&) = default;
    invalid_argument& operator=(const invalid_argument&) = default;
    invalid_argument(invalid_argument&&) = default;
    invalid_argument& operator=(invalid_argument&&) = default;
#endif
    virtual ~invalid_argument() _GLIBCXX_NOTHROW;
#endif
  };

  /** Thrown when an object is constructed that would exceed its maximum
   *  permitted size (e.g., a basic_string instance).  */
  class length_error : public logic_error
  {
  public:
#if __cpp_lib_constexpr_exceptions >= 202502L
    [[__gnu__::__gnu_inline__]]
    constexpr inline explicit length_error(const string& __arg)
      _GLIBCXX_TXN_SAFE

#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__arg))
#endif
    : logic_error(__arg) { }
    [[__gnu__::__gnu_inline__]]
    constexpr inline explicit length_error(const char* __arg) _GLIBCXX_TXN_SAFE
    : logic_error(__arg) { }
    constexpr length_error(const length_error&) = default;
    constexpr length_error& operator=(const length_error&) = default;
    constexpr length_error(length_error&&) = default;
    constexpr length_error& operator=(length_error&&) = default;
    [[__gnu__::__gnu_inline__]]
    constexpr inline virtual ~length_error() noexcept { }
#else
    explicit length_error(const string& __arg) _GLIBCXX_TXN_SAFE;
#if __cplusplus >= 201103L
    explicit length_error(const char*) _GLIBCXX_TXN_SAFE;
    length_error(const length_error&) = default;
    length_error& operator=(const length_error&) = default;
    length_error(length_error&&) = default;
    length_error& operator=(length_error&&) = default;
#endif
    virtual ~length_error() _GLIBCXX_NOTHROW;
#endif
  };

  /** This represents an argument whose value is not within the expected
   *  range (e.g., boundary checks in basic_string).  */
  class out_of_range : public logic_error
  {
  public:
#if __cpp_lib_constexpr_exceptions >= 202502L
    [[__gnu__::__gnu_inline__]]
    constexpr inline explicit out_of_range(const string& __arg)
      _GLIBCXX_TXN_SAFE

#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__arg))
#endif
    : logic_error(__arg) { }
    [[__gnu__::__gnu_inline__]]
    constexpr inline explicit out_of_range(const char* __arg) _GLIBCXX_TXN_SAFE
    : logic_error(__arg) { }
    constexpr out_of_range(const out_of_range&) = default;
    constexpr out_of_range& operator=(const out_of_range&) = default;
    constexpr out_of_range(out_of_range&&) = default;
    constexpr out_of_range& operator=(out_of_range&&) = default;
    [[__gnu__::__gnu_inline__]]
    constexpr inline virtual ~out_of_range() noexcept { }
#else
    explicit out_of_range(const string& __arg) _GLIBCXX_TXN_SAFE;
#if __cplusplus >= 201103L
    explicit out_of_range(const char*) _GLIBCXX_TXN_SAFE;
    out_of_range(const out_of_range&) = default;
    out_of_range& operator=(const out_of_range&) = default;
    out_of_range(out_of_range&&) = default;
    out_of_range& operator=(out_of_range&&) = default;
#endif
    virtual ~out_of_range() _GLIBCXX_NOTHROW;
#endif
  };

  /** Runtime errors represent problems outside the scope of a program;
   *  they cannot be easily predicted and can generally only be caught as
   *  the program executes.
   *  @brief One of two subclasses of exception.
   */
  class runtime_error : public exception
  {
    __cow_string _M_msg;

  public:
#if __cpp_lib_constexpr_exceptions >= 202502L
    [[__gnu__::__gnu_inline__]]
    constexpr inline explicit
    runtime_error(const string& __arg) _GLIBCXX_TXN_SAFE
#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
    pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__arg))
#endif
    : _M_msg(__arg) {}

    [[__gnu__::__gnu_inline__]]
    constexpr inline explicit
    runtime_error(const char* __arg) _GLIBCXX_TXN_SAFE
    : _M_msg(__arg) {}

#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
    // See logic_error's own identical special members for why these
    // are explicit bodies (delegating to the base class, then '_M_msg')
    // rather than '=default' with a precondition attached.
    constexpr runtime_error(runtime_error&& __arg) noexcept
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__arg))
      : exception(std::move(__arg)), _M_msg(std::move(__arg._M_msg)) { }
    constexpr runtime_error& operator=(runtime_error&& __arg) noexcept
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (this))
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__arg))
    {
      exception::operator=(std::move(__arg));
      _M_msg = std::move(__arg._M_msg);
      return *this;
    }
    constexpr runtime_error(const runtime_error& __arg) noexcept
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__arg))
      : exception(__arg), _M_msg(__arg._M_msg) { }
    // D4324/P2680: the original '=default' declaration here omitted
    // 'constexpr' explicitly (matching every other special member in
    // this class) yet was still implicitly constexpr -- a '=default'
    // special member's constexpr-ness is computed from whether the
    // operation qualifies, independent of the keyword being written.
    // A hand-written body has no such implicit rule, so 'constexpr'
    // must be added explicitly here to keep this working for every
    // constexpr-marked derived class's own '=default' copy assignment
    // (overflow_error/underflow_error/range_error), which otherwise
    // fails to compile ("defaulted constructor calls non-constexpr ...
    // operator=") the moment this stops being implicitly constexpr.
    constexpr runtime_error& operator=(const runtime_error& __arg) noexcept
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (this))
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__arg))
    {
      exception::operator=(__arg);
      _M_msg = __arg._M_msg;
      return *this;
    }
#else
    constexpr runtime_error(runtime_error&&) noexcept = default;
    constexpr runtime_error& operator=(runtime_error&&) noexcept = default;
    constexpr runtime_error(const runtime_error&) noexcept = default;
    runtime_error& operator=(const runtime_error&) noexcept = default;
#endif

    [[__gnu__::__gnu_inline__]]
    constexpr inline virtual ~runtime_error() _GLIBCXX_TXN_SAFE_DYN noexcept
    {}

    [[__gnu__::__gnu_inline__]]
    constexpr inline virtual const char*
    what() const _GLIBCXX_TXN_SAFE_DYN noexcept
#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
    pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (this))
#endif
    {
      return _M_msg.c_str();
    }
#else
    /** Takes a character string describing the error.  */
    explicit
    runtime_error(const string& __arg) _GLIBCXX_TXN_SAFE;

#if __cplusplus >= 201103L
    explicit
    runtime_error(const char*) _GLIBCXX_TXN_SAFE;

    runtime_error(runtime_error&&) noexcept;
    runtime_error& operator=(runtime_error&&) noexcept;
#endif

#if _GLIBCXX_USE_CXX11_ABI || _GLIBCXX_DEFINE_STDEXCEPT_COPY_OPS
    runtime_error(const runtime_error&) _GLIBCXX_NOTHROW;
    runtime_error& operator=(const runtime_error&) _GLIBCXX_NOTHROW;
#elif __cplusplus >= 201103L
    runtime_error(const runtime_error&) = default;
    runtime_error& operator=(const runtime_error&) = default;
#endif

    virtual ~runtime_error() _GLIBCXX_TXN_SAFE_DYN _GLIBCXX_NOTHROW;

    /** Returns a C-style character string describing the general cause of
     *  the current error (the same string passed to the ctor).  */
    virtual const char*
    what() const _GLIBCXX_TXN_SAFE_DYN _GLIBCXX_NOTHROW;
#endif

# ifdef _GLIBCXX_TM_TS_INTERNAL
    friend void*
    ::_txnal_runtime_error_get_msg(void* e);
# endif
  };

  /** Thrown to indicate arithmetic overflow.  */
  class overflow_error : public runtime_error
  {
  public:
#if __cpp_lib_constexpr_exceptions >= 202502L
    [[__gnu__::__gnu_inline__]]
    constexpr inline explicit overflow_error(const string& __arg)
      _GLIBCXX_TXN_SAFE

#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__arg))
#endif
    : runtime_error(__arg) { }
    [[__gnu__::__gnu_inline__]]
    constexpr inline explicit overflow_error(const char* __arg)
      _GLIBCXX_TXN_SAFE
    : runtime_error(__arg) { }
    constexpr overflow_error(const overflow_error&) = default;
    constexpr overflow_error& operator=(const overflow_error&) = default;
    constexpr overflow_error(overflow_error&&) = default;
    constexpr overflow_error& operator=(overflow_error&&) = default;
    [[__gnu__::__gnu_inline__]]
    constexpr inline virtual ~overflow_error() noexcept { }
#else
    explicit overflow_error(const string& __arg) _GLIBCXX_TXN_SAFE;
#if __cplusplus >= 201103L
    explicit overflow_error(const char*) _GLIBCXX_TXN_SAFE;
    overflow_error(const overflow_error&) = default;
    overflow_error& operator=(const overflow_error&) = default;
    overflow_error(overflow_error&&) = default;
    overflow_error& operator=(overflow_error&&) = default;
#endif
    virtual ~overflow_error() _GLIBCXX_NOTHROW;
#endif
  };

  /** Thrown to indicate arithmetic underflow.  */
  class underflow_error : public runtime_error
  {
  public:
#if __cpp_lib_constexpr_exceptions >= 202502L
    [[__gnu__::__gnu_inline__]]
    constexpr inline explicit underflow_error(const string& __arg)
      _GLIBCXX_TXN_SAFE

#if defined(_GLIBCXX_CONVEYOR_ASSERTIONS) && defined(__cpp_contract_control_objects)
      pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&__arg))
#endif
    : runtime_error(__arg) { }
    [[__gnu__::__gnu_inline__]]
    constexpr inline explicit underflow_error(const char* __arg)
      _GLIBCXX_TXN_SAFE
    : runtime_error(__arg) { }
    constexpr underflow_error(const underflow_error&) = default;
    constexpr underflow_error& operator=(const underflow_error&) = default;
    constexpr underflow_error(underflow_error&&) = default;
    constexpr underflow_error& operator=(underflow_error&&) = default;
    [[__gnu__::__gnu_inline__]]
    constexpr inline virtual ~underflow_error() noexcept { }
#else
    explicit underflow_error(const string& __arg) _GLIBCXX_TXN_SAFE;
#if __cplusplus >= 201103L
    explicit underflow_error(const char*) _GLIBCXX_TXN_SAFE;
    underflow_error(const underflow_error&) = default;
    underflow_error& operator=(const underflow_error&) = default;
    underflow_error(underflow_error&&) = default;
    underflow_error& operator=(underflow_error&&) = default;
#endif
    virtual ~underflow_error() _GLIBCXX_NOTHROW;
#endif
  };

  /// @} group exceptions

_GLIBCXX_END_NAMESPACE_VERSION
} // namespace

#endif /* _STDEXCEPT_EXCEPT_H */
