/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-internals.h  random utility stuff (internal to D-BUS implementation)
 *
 * Copyright (C) 2002, 2003  Red Hat, Inc.
 *
 * Licensed under the Academic Free License version 1.2
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#ifdef DBUS_INSIDE_DBUS_H
#error "You can't include dbus-internals.h in the public header dbus.h"
#endif

#ifndef DBUS_INTERNALS_H
#define DBUS_INTERNALS_H

#include <config.h>

#include <dbus/dbus-memory.h>
#include <dbus/dbus-types.h>
#include <dbus/dbus-errors.h>
#include <dbus/dbus-sysdeps.h>
#include <dbus/dbus-threads.h>

DBUS_BEGIN_DECLS;

#if     __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)
#define _DBUS_GNUC_PRINTF( format_idx, arg_idx )    \
  __attribute__((__format__ (__printf__, format_idx, arg_idx)))
#define _DBUS_GNUC_SCANF( format_idx, arg_idx )     \
  __attribute__((__format__ (__scanf__, format_idx, arg_idx)))
#define _DBUS_GNUC_FORMAT( arg_idx )                \
  __attribute__((__format_arg__ (arg_idx)))
#define _DBUS_GNUC_NORETURN                         \
  __attribute__((__noreturn__))
#else   /* !__GNUC__ */
#define _DBUS_GNUC_PRINTF( format_idx, arg_idx )
#define _DBUS_GNUC_SCANF( format_idx, arg_idx )
#define _DBUS_GNUC_FORMAT( arg_idx )
#define _DBUS_GNUC_NORETURN
#endif  /* !__GNUC__ */

void _dbus_warn         (const char *format,
                         ...) _DBUS_GNUC_PRINTF (1, 2);
void _dbus_verbose_real (const char *format,
                         ...) _DBUS_GNUC_PRINTF (1, 2);


#ifdef DBUS_ENABLE_VERBOSE_MODE
#  define _dbus_verbose _dbus_verbose_real
#else
#  ifdef HAVE_ISO_VARARGS
#    define _dbus_verbose(...)
#  elif defined (HAVE_GNUC_VARARGS)
#    define _dbus_verbose(format...)
#  else
#    error "This compiler does not support varargs macros and thus verbose mode can't be disabled meaningfully"
#  endif
#endif /* !DBUS_ENABLE_VERBOSE_MODE */

const char* _dbus_strerror (int error_number);

#ifdef DBUS_DISABLE_ASSERT
#define _dbus_assert(condition)
#else
void _dbus_real_assert (dbus_bool_t  condition,
                        const char  *condition_text,
                        const char  *file,
                        int          line);
#define _dbus_assert(condition)                                         \
  _dbus_real_assert ((condition) != 0, #condition, __FILE__, __LINE__)
#endif /* !DBUS_DISABLE_ASSERT */

#ifdef DBUS_DISABLE_ASSERT
#define _dbus_assert_not_reached(explanation)
#else
void _dbus_real_assert_not_reached (const char *explanation,
                                    const char *file,
                                    int         line);
#define _dbus_assert_not_reached(explanation)                                   \
  _dbus_real_assert_not_reached (explanation, __FILE__, __LINE__)
#endif /* !DBUS_DISABLE_ASSERT */

#define _DBUS_N_ELEMENTS(array) ((int) (sizeof ((array)) / sizeof ((array)[0])))

#define _DBUS_POINTER_TO_INT(pointer) ((long)(pointer))
#define _DBUS_INT_TO_POINTER(integer) ((void*)((long)(integer)))

#define _DBUS_ZERO(object) (memset (&(object), '\0', sizeof ((object))))

#define _DBUS_STRUCT_OFFSET(struct_type, member)	\
    ((long) ((unsigned char*) &((struct_type*) 0)->member))

#define _DBUS_ASSERT_ERROR_IS_SET(error)   _dbus_assert ((error) == NULL || dbus_error_is_set ((error)))
#define _DBUS_ASSERT_ERROR_IS_CLEAR(error) _dbus_assert ((error) == NULL || !dbus_error_is_set ((error)))

/* This alignment thing is from ORBit2 */
/* Align a value upward to a boundary, expressed as a number of bytes.
 * E.g. align to an 8-byte boundary with argument of 8.
 */

/*
 *   (this + boundary - 1)
 *          &
 *    ~(boundary - 1)
 */

#define _DBUS_ALIGN_VALUE(this, boundary) \
  (( ((unsigned long)(this)) + (((unsigned long)(boundary)) -1)) & (~(((unsigned long)(boundary))-1)))

#define _DBUS_ALIGN_ADDRESS(this, boundary) \
  ((void*)_DBUS_ALIGN_VALUE(this, boundary))

char*       _dbus_strdup                (const char  *str);
dbus_bool_t _dbus_string_array_contains (const char **array,
                                         const char  *str);
char**      _dbus_dup_string_array      (const char **array);


#define _DBUS_INT_MIN	(-_DBUS_INT_MAX - 1)
#define _DBUS_INT_MAX	2147483647
#define _DBUS_UINT_MAX	0xffffffff
#define _DBUS_ONE_KILOBYTE 1024
#define _DBUS_ONE_MEGABYTE 1024 * _DBUS_ONE_KILOBYTE
#define _DBUS_ONE_HOUR_IN_MILLISECONDS (1000 * 60 * 60)
#define _DBUS_USEC_PER_SECOND          (1000000)

#undef	MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

#undef	MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

#undef	ABS
#define ABS(a)	   (((a) < 0) ? -(a) : (a))

typedef void (* DBusForeachFunction) (void *element,
                                      void *data);

dbus_bool_t _dbus_set_fd_nonblocking (int             fd,
                                      DBusError      *error);

void _dbus_verbose_bytes           (const unsigned char *data,
                                    int                  len);
void _dbus_verbose_bytes_of_string (const DBusString    *str,
                                    int                  start,
                                    int                  len);


const char* _dbus_type_to_string (int type);

extern const char _dbus_no_memory_message[];
#define _DBUS_SET_OOM(error) dbus_set_error ((error), DBUS_ERROR_NO_MEMORY, _dbus_no_memory_message)

#ifdef DBUS_BUILD_TESTS
/* Memory debugging */
void        _dbus_set_fail_alloc_counter        (int  until_next_fail);
int         _dbus_get_fail_alloc_counter        (void);
void        _dbus_set_fail_alloc_failures       (int  failures_per_failure);
int         _dbus_get_fail_alloc_failures       (void);
dbus_bool_t _dbus_decrement_fail_alloc_counter  (void);
dbus_bool_t _dbus_disable_mem_pools             (void);
int         _dbus_get_malloc_blocks_outstanding (void);

typedef dbus_bool_t (* DBusTestMemoryFunction)  (void *data);
dbus_bool_t _dbus_test_oom_handling (const char             *description,
                                     DBusTestMemoryFunction  func,
                                     void                   *data);
#else
#define _dbus_set_fail_alloc_counter(n)
#define _dbus_get_fail_alloc_counter _DBUS_INT_MAX

/* These are constant expressions so that blocks
 * they protect should be optimized away
 */
#define _dbus_decrement_fail_alloc_counter() (FALSE)
#define _dbus_disable_mem_pools()            (FALSE)
#define _dbus_get_malloc_blocks_outstanding  (0)
#endif /* !DBUS_BUILD_TESTS */

typedef void (* DBusShutdownFunction) (void *data);
dbus_bool_t _dbus_register_shutdown_func (DBusShutdownFunction  function,
                                          void                 *data);

extern int _dbus_current_generation;

/* Thread initializers */
#define _DBUS_LOCK_NAME(name)           _dbus_lock_##name
#define _DBUS_DECLARE_GLOBAL_LOCK(name) extern DBusMutex  *_dbus_lock_##name
#define _DBUS_DEFINE_GLOBAL_LOCK(name)  DBusMutex         *_dbus_lock_##name  
#define _DBUS_LOCK(name)                dbus_mutex_lock   (_dbus_lock_##name)
#define _DBUS_UNLOCK(name)              dbus_mutex_unlock (_dbus_lock_##name)

_DBUS_DECLARE_GLOBAL_LOCK (list);
_DBUS_DECLARE_GLOBAL_LOCK (connection_slots);
_DBUS_DECLARE_GLOBAL_LOCK (server_slots);
_DBUS_DECLARE_GLOBAL_LOCK (atomic);
_DBUS_DECLARE_GLOBAL_LOCK (message_handler);
_DBUS_DECLARE_GLOBAL_LOCK (user_info);
_DBUS_DECLARE_GLOBAL_LOCK (bus);
_DBUS_DECLARE_GLOBAL_LOCK (shutdown_funcs);
#define _DBUS_N_GLOBAL_LOCKS (8)

DBUS_END_DECLS;

#endif /* DBUS_INTERNALS_H */
