/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-message-builder.c Build messages from text files for testing (internal to D-BUS implementation)
 * 
 * Copyright (C) 2003 Red Hat, Inc.
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
#include <config.h>

#ifdef DBUS_BUILD_TESTS

#include "dbus-message-builder.h"
#include "dbus-hash.h"
#include "dbus-internals.h"
#include "dbus-marshal.h"

/**
 * @defgroup DBusMessageBuilder code for loading test message data
 * @ingroup  DBusInternals
 * @brief code for loading up test data for unit tests
 *
 * The code in here is used for unit testing, it loads
 * up message data from a description in a file.
 *
 * @{
 */

typedef struct
{
  DBusString name;
  int start;  /**< Calculate length since here */
  int length; /**< length to write */
  int offset; /**< where to write it into the data */
  int endian; /**< endianness to write with */
} SavedLength;

static void
free_saved_length (void *data)
{
  SavedLength *sl = data;

  if (sl == NULL)
    return; /* all hash free functions have to accept NULL */
  
  _dbus_string_free (&sl->name);
  dbus_free (sl);
}

static SavedLength*
ensure_saved_length (DBusHashTable    *hash,
                     const DBusString *name)
{
  SavedLength *sl;
  const char *s;

  _dbus_string_get_const_data (name, &s);

  sl = _dbus_hash_table_lookup_string (hash, s);
  if (sl != NULL)
    return sl;
  
  sl = dbus_new0 (SavedLength, 1);

  if (!_dbus_string_init (&sl->name, _DBUS_INT_MAX))
    {
      dbus_free (sl);
      return NULL;
    }

  if (!_dbus_string_copy (name, 0, &sl->name, 0))
    goto failed;

  _dbus_string_get_const_data (&sl->name, &s);

  if (!_dbus_hash_table_insert_string (hash, (char*)s, sl))
    goto failed;

  sl->start = -1;
  sl->length = -1;
  sl->offset = -1;
  sl->endian = -1;
  
  return sl;
  
 failed:
  free_saved_length (sl);
  return NULL;
}

static dbus_bool_t
save_start (DBusHashTable    *hash,
            const DBusString *name,
            int               start)
{
  SavedLength *sl;

  sl = ensure_saved_length (hash, name);

  if (sl == NULL)
    return FALSE;
  else if (sl->start >= 0)
    {
      _dbus_warn ("Same START_LENGTH given twice\n");
      return FALSE;
    }
  else
    sl->start = start;

  return TRUE;
}

static dbus_bool_t
save_length (DBusHashTable    *hash,
             const DBusString *name,
             int               length)
{
  SavedLength *sl;

  sl = ensure_saved_length (hash, name);

  if (sl == NULL)
    return FALSE;
  else if (sl->length >= 0)
    {
      _dbus_warn ("Same END_LENGTH given twice\n");
      return FALSE;
    }
  else
    sl->length = length;

  return TRUE;
}

static dbus_bool_t
save_offset (DBusHashTable    *hash,
             const DBusString *name,
             int               offset,
             int               endian)
{
  SavedLength *sl;

  sl = ensure_saved_length (hash, name);

  if (sl == NULL)
    return FALSE;
  else if (sl->offset >= 0)
    {
      _dbus_warn ("Same LENGTH given twice\n");
      return FALSE;
    }
  else
    {
      sl->offset = offset;
      sl->endian = endian;
    }

  return TRUE;
}

/** Saves the segment to delete in order to unalign the next item */
#define SAVE_FOR_UNALIGN(str, boundary)                                 \
  int align_pad_start = _dbus_string_get_length (str);                  \
  int align_pad_end = _DBUS_ALIGN_VALUE (align_pad_start, (boundary))

/** Deletes the alignment padding */
#define PERFORM_UNALIGN(str)                                    \
  if (unalign)                                                  \
    {                                                           \
      _dbus_string_delete ((str), align_pad_start,              \
                           align_pad_end - align_pad_start);    \
      unalign = FALSE;                                          \
    }


static dbus_bool_t
append_quoted_string (DBusString       *dest,
                      const DBusString *quoted,
		      int               start_pos,
		      int              *new_pos)
{
  dbus_bool_t in_quotes = FALSE;
  int i;

  /* FIXME: We might want to add escaping in case we want to put '
   * characters in our strings.
   */
  
  i = start_pos;
  while (i < _dbus_string_get_length (quoted))
    {
      unsigned char b;

      b = _dbus_string_get_byte (quoted, i);
      
      if (in_quotes)
        {
          if (b == '\'')
	    break;
          else
            {
              if (!_dbus_string_append_byte (dest, b))
                return FALSE;
            }
        }
      else
        {
          if (b == '\'')
            in_quotes = TRUE;
          else if (b == ' ' || b == '\n' || b == '\t')
            break; /* end on whitespace if not quoted */
          else
            {
              if (!_dbus_string_append_byte (dest, b))
                return FALSE;
            }
        }
      
      ++i;
    }

  if (new_pos)
    *new_pos = i;
  
  if (!_dbus_string_append_byte (dest, '\0'))
    return FALSE;
  return TRUE;
}

static dbus_bool_t
append_saved_length (DBusString       *dest,
                     DBusHashTable    *length_hash,
                     const DBusString *name,
                     int               offset,
                     int               endian)
{
  if (!save_offset (length_hash, name,
                    offset, endian))
    {
      _dbus_warn ("failed to save offset to LENGTH\n");
      return FALSE;
    }
  
  if (!_dbus_marshal_uint32 (dest, endian,
                             -1))
    {
      _dbus_warn ("failed to append a length\n");
      return FALSE;
    }

  return TRUE;
}

/**
 * Reads the given filename, which should be in "message description
 * language" (look at some examples), and builds up the message data
 * from it.  The message data may be invalid, or valid.
 *
 * The parser isn't very strict, it's just a hack for test programs.
 * 
 * The file format is:
 * @code
 *   VALID_HEADER normal header; byte order, padding, header len, body len, serial
 *   BIG_ENDIAN switch to big endian
 *   LITTLE_ENDIAN switch to little endian
 *   OPPOSITE_ENDIAN switch to opposite endian
 *   ALIGN <N> aligns to the given value
 *   UNALIGN skips alignment for the next marshal
 *   BYTE <N> inserts the given integer in [0,255] or char in 'a' format
 *   START_LENGTH <name> marks the start of a length to measure
 *   END_LENGTH <name> records the length since START_LENGTH under the given name
 *                     (or if no START_LENGTH, absolute length)
 *   LENGTH <name> inserts the saved length of the same name
 *   CHOP <N> chops last N bytes off the data
 *   FIELD_NAME <abcd> inserts 4-byte field name
 *   TYPE <typename> inserts a typecode byte 
 * @endcode
 * 
 * Following commands insert aligned data unless
 * preceded by "UNALIGN":
 * @code
 *   INT32 <N> marshals an INT32
 *   UINT32 <N> marshals a UINT32
 *   DOUBLE <N> marshals a double
 *   STRING 'Foo' marshals a string
 *   INT32_ARRAY { 3, 4, 5, 6} marshals an INT32 array
 *   UINT32_ARRAY { 3, 4, 5, 6} marshals an UINT32 array
 *   DOUBLE_ARRAY { 1.0, 2.0, 3.0, 4.0} marshals a DOUBLE array  
 * @endcode
 *
 * @todo add support for array types INT32_ARRAY { 3, 4, 5, 6 }
 * and so forth.
 * 
 * @param dest the string to append the message data to
 * @param filename the filename to load
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_message_data_load (DBusString       *dest,
                         const DBusString *filename)
{
  DBusString file;
  DBusResultCode result;
  DBusString line;
  dbus_bool_t retval;
  int line_no;
  dbus_bool_t unalign;
  DBusHashTable *length_hash;
  int endian;
  DBusHashIter iter;
  
  retval = FALSE;
  length_hash = NULL;
  
  if (!_dbus_string_init (&file, _DBUS_INT_MAX))
    return FALSE;

  if (!_dbus_string_init (&line, _DBUS_INT_MAX))
    {
      _dbus_string_free (&file);
      return FALSE;
    }

  {
    const char *s;
    _dbus_string_get_const_data (filename, &s);
    _dbus_verbose ("Loading %s\n", s);
  }
  
  if ((result = _dbus_file_get_contents (&file, filename)) != DBUS_RESULT_SUCCESS)
    {
      const char *s;
      _dbus_string_get_const_data (filename, &s);
      _dbus_warn ("Getting contents of %s failed: %s\n",
                  s, dbus_result_to_string (result));
                     
      goto out;
    }

  length_hash = _dbus_hash_table_new (DBUS_HASH_STRING,
                                      NULL,
                                      free_saved_length);
  if (length_hash == NULL)
    goto out;
  
  endian = DBUS_COMPILER_BYTE_ORDER;
  unalign = FALSE;
  line_no = 0;
 next_iteration:
  while (_dbus_string_pop_line (&file, &line))
    {
      dbus_bool_t just_set_unalign;

      just_set_unalign = FALSE;
      line_no += 1;

      _dbus_string_delete_leading_blanks (&line);

      if (_dbus_string_get_length (&line) == 0)
        {
          /* empty line */
          goto next_iteration;
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "#"))
        {
          /* Ignore this comment */
          goto next_iteration;
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "VALID_HEADER"))
        {
          int i;
          DBusString name;
          
          if (!_dbus_string_append_byte (dest, endian))
            {
              _dbus_warn ("could not append endianness\n");
              goto parse_failed;
            }

          i = 0;
          while (i < 3)
            {
              if (!_dbus_string_append_byte (dest, '\0'))
                {
                  _dbus_warn ("could not append nul pad\n");
                  goto parse_failed;
                }
              ++i;
            }

          _dbus_string_init_const (&name, "Header");
          if (!append_saved_length (dest, length_hash,
                                    &name, _dbus_string_get_length (dest),
                                    endian))
            goto parse_failed;

          _dbus_string_init_const (&name, "Body");
          if (!append_saved_length (dest, length_hash,
                                    &name, _dbus_string_get_length (dest),
                                    endian))
            goto parse_failed;
          
          /* client serial */
          if (!_dbus_marshal_int32 (dest, endian, 1))
            {
              _dbus_warn ("couldn't append client serial\n");
              goto parse_failed;
            }
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "BIG_ENDIAN"))
        {
          endian = DBUS_BIG_ENDIAN;
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "LITTLE_ENDIAN"))
        {
          endian = DBUS_LITTLE_ENDIAN;
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "OPPOSITE_ENDIAN"))
        {
          if (endian == DBUS_BIG_ENDIAN)
            endian = DBUS_LITTLE_ENDIAN;
          else
            endian = DBUS_BIG_ENDIAN;
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "ALIGN"))
        {
          long val;
          int end;
          int orig_len;
          
          _dbus_string_delete_first_word (&line);

          if (!_dbus_string_parse_int (&line, 0, &val, &end))
            {
              _dbus_warn ("Failed to parse integer\n");
              goto parse_failed;
            }

          if (val > 8)
            {
              _dbus_warn ("Aligning to %ld boundary is crack\n",
                          val);
              goto parse_failed;
            }

          orig_len = _dbus_string_get_length (dest);
          
          if (!_dbus_string_align_length (dest, val))
            goto parse_failed;

          if (_dbus_string_parse_int (&line, end, &val, NULL))
            {
              /* If there's an optional second int argument,
               * fill in align padding with that value
               */
              if (val < 0 || val > 255)
                {
                  _dbus_warn ("can't fill align padding with %ld, must be a byte value\n", val);
                  goto parse_failed;
                }

              end = orig_len;
              while (end < _dbus_string_get_length (dest))
                {
                  _dbus_string_set_byte (dest, end, val);
                  ++end;
                }
            }
        }
      else if (_dbus_string_starts_with_c_str (&line, "UNALIGN"))
        {
          unalign = TRUE;
          just_set_unalign = TRUE;
        }
      else if (_dbus_string_starts_with_c_str (&line, "CHOP"))
        {
          long val;

          /* FIXME if you CHOP the offset for a LENGTH
           * command, we segfault.
           */
          
          _dbus_string_delete_first_word (&line);

          if (!_dbus_string_parse_int (&line, 0, &val, NULL))
            {
              _dbus_warn ("Failed to parse integer to chop\n");
              goto parse_failed;
            }

          if (val > _dbus_string_get_length (dest))
            {
              _dbus_warn ("Trying to chop %ld bytes but we only have %d\n",
                          val,
                          _dbus_string_get_length (dest));
              goto parse_failed;
            }
          
          _dbus_string_shorten (dest, val);
        }
      else if (_dbus_string_starts_with_c_str (&line, "BYTE"))
        {
          unsigned char the_byte;
          
          _dbus_string_delete_first_word (&line);

          if (_dbus_string_equal_c_str (&line, "'\\''"))
            the_byte = '\'';
          else if (_dbus_string_get_byte (&line, 0) == '\'' &&
                   _dbus_string_get_length (&line) >= 3 &&
                   _dbus_string_get_byte (&line, 2) == '\'')
            the_byte = _dbus_string_get_byte (&line, 1);
          else
            {
              long val;
              if (!_dbus_string_parse_int (&line, 0, &val, NULL))
                {
                  _dbus_warn ("Failed to parse integer for BYTE\n");
                  goto parse_failed;
                }

              if (val > 255)
                {
                  _dbus_warn ("A byte must be in range 0-255 not %ld\n",
                                 val);
                  goto parse_failed;
                }
              the_byte = (unsigned char) val;
            }

          _dbus_string_append_byte (dest, the_byte);
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "START_LENGTH"))
        {
          _dbus_string_delete_first_word (&line);

          if (!save_start (length_hash, &line,
                           _dbus_string_get_length (dest)))
            {
              _dbus_warn ("failed to save length start\n");
              goto parse_failed;
            }
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "END_LENGTH"))
        {
          _dbus_string_delete_first_word (&line);

          if (!save_length (length_hash, &line,
                            _dbus_string_get_length (dest)))
            {
              _dbus_warn ("failed to save length end\n");
              goto parse_failed;
            }
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "LENGTH"))
        {
          SAVE_FOR_UNALIGN (dest, 4);
          
          _dbus_string_delete_first_word (&line);

          if (!append_saved_length (dest, length_hash,
                                    &line,
                                    unalign ? align_pad_start : align_pad_end,
                                    endian))
            {
              _dbus_warn ("failed to add LENGTH\n");
              goto parse_failed;
            }

          PERFORM_UNALIGN (dest);
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "FIELD_NAME"))
        {
          _dbus_string_delete_first_word (&line);

          if (_dbus_string_get_length (&line) != 4)
            {
              const char *s;
              _dbus_string_get_const_data (&line, &s);
              _dbus_warn ("Field name must be four characters not \"%s\"\n",
                             s);
              goto parse_failed;
            }

          if (unalign)
            unalign = FALSE;
          else
            _dbus_string_align_length (dest, 4);
          
          if (!_dbus_string_copy (&line, 0, dest,
                                  _dbus_string_get_length (dest)))
            goto parse_failed;
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "TYPE"))
        {
          int code;
          
          _dbus_string_delete_first_word (&line);          

          if (_dbus_string_starts_with_c_str (&line, "INVALID"))
            code = DBUS_TYPE_INVALID;
          else if (_dbus_string_starts_with_c_str (&line, "NIL"))
            code = DBUS_TYPE_NIL;
          else if (_dbus_string_starts_with_c_str (&line, "BOOLEAN_ARRAY"))
            code = DBUS_TYPE_BOOLEAN_ARRAY;
          else if (_dbus_string_starts_with_c_str (&line, "INT32_ARRAY"))
            code = DBUS_TYPE_INT32_ARRAY;
          else if (_dbus_string_starts_with_c_str (&line, "UINT32_ARRAY"))
            code = DBUS_TYPE_UINT32_ARRAY;
          else if (_dbus_string_starts_with_c_str (&line, "DOUBLE_ARRAY"))
            code = DBUS_TYPE_DOUBLE_ARRAY;
          else if (_dbus_string_starts_with_c_str (&line, "BYTE_ARRAY"))
            code = DBUS_TYPE_BYTE_ARRAY;
          else if (_dbus_string_starts_with_c_str (&line, "STRING_ARRAY"))
            code = DBUS_TYPE_STRING_ARRAY;
          else if (_dbus_string_starts_with_c_str (&line, "BOOLEAN"))
            code = DBUS_TYPE_BOOLEAN;
          else if (_dbus_string_starts_with_c_str (&line, "INT32"))
            code = DBUS_TYPE_INT32;
          else if (_dbus_string_starts_with_c_str (&line, "UINT32"))
            code = DBUS_TYPE_UINT32;
          else if (_dbus_string_starts_with_c_str (&line, "DOUBLE"))
            code = DBUS_TYPE_DOUBLE;
          else if (_dbus_string_starts_with_c_str (&line, "STRING"))
            code = DBUS_TYPE_STRING;
          else
            {
              const char *s;
              _dbus_string_get_const_data (&line, &s);
              _dbus_warn ("%s is not a valid type name\n", s);
              goto parse_failed;
            }

          if (!_dbus_string_append_byte (dest, code))
            {
              _dbus_warn ("could not append typecode byte\n");
              goto parse_failed;
            }
        }
      else if (_dbus_string_starts_with_c_str (&line,
					       "BOOLEAN_ARRAY"))
	{
	  SAVE_FOR_UNALIGN (dest, 4);
	  int i, len, allocated;
	  unsigned char *values;
	  unsigned char b, val;

	  allocated = 4;
	  values = dbus_new (unsigned char, allocated);
	  if (!values)
	    {
	      _dbus_warn ("could not allocate memory for BOOLEAN_ARRAY\n");
	      goto parse_failed;
	    }

	  len = 0;
	  
	  _dbus_string_delete_first_word (&line);
	  _dbus_string_skip_blank (&line, 0, &i);
	  b = _dbus_string_get_byte (&line, i++);
	  
	  if (b != '{')
	    goto parse_failed;

	  while (i < _dbus_string_get_length (&line))
	    {
	      _dbus_string_skip_blank (&line, i, &i);	      
	      
	      if (_dbus_string_find_to (&line, i, i + 5,
					"false", NULL))
		{
		  i += 5;
		  val = TRUE;
		}
	      else if (_dbus_string_find_to (&line, i, i + 4,
					     "true", NULL))
		{
		  i += 4;
		  val = FALSE;
		}
	      else
		{
		  _dbus_warn ("could not parse BOOLEAN_ARRAY\n");
		  goto parse_failed;
		}

	      values[len++] = val;
	      if (len == allocated)
		{
		  allocated *= 2;
		  values = dbus_realloc (values, allocated * sizeof (unsigned char));
		  if (!values)
		    {
		      _dbus_warn ("could not allocate memory for BOOLEAN_ARRAY\n");
		      goto parse_failed;
		    }
		}
	      
	      _dbus_string_skip_blank (&line, i, &i);
	      
	      b = _dbus_string_get_byte (&line, i++);

	      if (b == '}')
		break;
	      else if (b != ',')
		goto parse_failed;
	    }

	  if (!_dbus_marshal_int32 (dest, endian, len) ||
	      !_dbus_string_append_len (dest, values, len))
            {
              _dbus_warn ("failed to append BOOLEAN_ARRAY\n");
              goto parse_failed;
            }
	  dbus_free (values);
	  
	  PERFORM_UNALIGN (dest);
	}
      else if (_dbus_string_starts_with_c_str (&line,
					       "INT32_ARRAY"))
	{
	  SAVE_FOR_UNALIGN (dest, 4);
	  int i, len, allocated;
	  dbus_int32_t *values;
	  long val;
	  unsigned char b;

	  allocated = 4;
	  values = dbus_new (dbus_int32_t, allocated);
	  if (!values)
	    {
	      _dbus_warn ("could not allocate memory for INT32_ARRAY\n");
	      goto parse_failed;
	    }
	  
	  len = 0;
	  
	  _dbus_string_delete_first_word (&line);
	  _dbus_string_skip_blank (&line, 0, &i);
	  b = _dbus_string_get_byte (&line, i++);

	  if (b != '{')
	    goto parse_failed;

	  while (i < _dbus_string_get_length (&line))
	    {
	      _dbus_string_skip_blank (&line, i, &i);

	      if (!_dbus_string_parse_int (&line, i, &val, &i))
		{
		  _dbus_warn ("could not parse integer for INT32_ARRAY\n");
		  goto parse_failed;
		}

	      values[len++] = val;
	      if (len == allocated)
		{
		  allocated *= 2;
		  values = dbus_realloc (values, allocated * sizeof (dbus_int32_t));
		  if (!values)
		    {
		      _dbus_warn ("could not allocate memory for INT32_ARRAY\n");
		      goto parse_failed;
		    }
		}
	      
	      _dbus_string_skip_blank (&line, i, &i);
	      
	      b = _dbus_string_get_byte (&line, i++);

	      if (b == '}')
		break;
	      else if (b != ',')
		goto parse_failed;
	    }

          if (!_dbus_marshal_int32_array (dest, endian, values, len))
            {
              _dbus_warn ("failed to append INT32_ARRAY\n");
              goto parse_failed;
            }
	  dbus_free (values);
	  
	  PERFORM_UNALIGN (dest);
	}
      else if (_dbus_string_starts_with_c_str (&line,
					       "UINT32_ARRAY"))
	{
	  SAVE_FOR_UNALIGN (dest, 4);
	  int i, len, allocated;
	  dbus_uint32_t *values;
	  long val;
	  unsigned char b;

	  allocated = 4;
	  values = dbus_new (dbus_uint32_t, allocated);
	  if (!values)
	    {
	      _dbus_warn ("could not allocate memory for UINT32_ARRAY\n");
	      goto parse_failed;
	    }
	  
	  len = 0;
	  
	  _dbus_string_delete_first_word (&line);
	  _dbus_string_skip_blank (&line, 0, &i);
	  b = _dbus_string_get_byte (&line, i++);

	  if (b != '{')
	    goto parse_failed;

	  while (i < _dbus_string_get_length (&line))
	    {
	      _dbus_string_skip_blank (&line, i, &i);

	      if (!_dbus_string_parse_int (&line, i, &val, &i))
		{
		  _dbus_warn ("could not parse integer for UINT32_ARRAY\n");
		  goto parse_failed;
		}

	      values[len++] = val;
	      if (len == allocated)
		{
		  allocated *= 2;
		  values = dbus_realloc (values, allocated * sizeof (dbus_uint32_t));
		  if (!values)
		    {
		      _dbus_warn ("could not allocate memory for UINT32_ARRAY\n");
		      goto parse_failed;
		    }
		}
	      
	      _dbus_string_skip_blank (&line, i, &i);
	      
	      b = _dbus_string_get_byte (&line, i++);

	      if (b == '}')
		break;
	      else if (b != ',')
		goto parse_failed;
	    }

          if (!_dbus_marshal_uint32_array (dest, endian, values, len))
            {
              _dbus_warn ("failed to append UINT32_ARRAY\n");
              goto parse_failed;
            }
	  dbus_free (values);
	  
	  PERFORM_UNALIGN (dest);
	}
      else if (_dbus_string_starts_with_c_str (&line,
					       "DOUBLE_ARRAY"))
	{
	  SAVE_FOR_UNALIGN (dest, 8);
	  int i, len, allocated;
	  double *values;
	  double val;
	  unsigned char b;

	  allocated = 4;
	  values = dbus_new (double, allocated);
	  if (!values)
	    {
	      _dbus_warn ("could not allocate memory for DOUBLE_ARRAY\n");
	      goto parse_failed;
	    }
	  
	  len = 0;
	  
	  _dbus_string_delete_first_word (&line);
	  _dbus_string_skip_blank (&line, 0, &i);
	  b = _dbus_string_get_byte (&line, i++);

	  if (b != '{')
	    goto parse_failed;

	  while (i < _dbus_string_get_length (&line))
	    {
	      _dbus_string_skip_blank (&line, i, &i);

	      if (!_dbus_string_parse_double (&line, i, &val, &i))
		{
		  _dbus_warn ("could not parse double for DOUBLE_ARRAY\n");
		  goto parse_failed;
		}

	      values[len++] = val;
	      if (len == allocated)
		{
		  allocated *= 2;
		  values = dbus_realloc (values, allocated * sizeof (double));
		  if (!values)
		    {
		      _dbus_warn ("could not allocate memory for DOUBLE_ARRAY\n");
		      goto parse_failed;
		    }
		}
	      
	      _dbus_string_skip_blank (&line, i, &i);
	      
	      b = _dbus_string_get_byte (&line, i++);

	      if (b == '}')
		break;
	      else if (b != ',')
		goto parse_failed;
	    }

          if (!_dbus_marshal_double_array (dest, endian, values, len))
            {
              _dbus_warn ("failed to append DOUBLE_ARRAY\n");
              goto parse_failed;
            }
	  dbus_free (values);
	  
	  PERFORM_UNALIGN (dest);
	}
      else if (_dbus_string_starts_with_c_str (&line,
					       "STRING_ARRAY"))
	{
	  SAVE_FOR_UNALIGN (dest, 4);
	  int i, len, allocated;
	  char **values;
	  char *val;
	  DBusString val_str;
	  unsigned char b;

	  allocated = 4;
	  values = dbus_new (char *, allocated);
	  if (!values)
	    {
	      _dbus_warn ("could not allocate memory for DOUBLE_ARRAY\n");
	      goto parse_failed;
	    }
	  
	  len = 0;
	  
	  _dbus_string_delete_first_word (&line);
	  _dbus_string_skip_blank (&line, 0, &i);
	  b = _dbus_string_get_byte (&line, i++);

	  if (b != '{')
	    goto parse_failed;

	  _dbus_string_init (&val_str, _DBUS_INT_MAX);
	  while (i < _dbus_string_get_length (&line))
	    {
	      _dbus_string_skip_blank (&line, i, &i);

	      if (!append_quoted_string (&val_str, &line, i, &i))
		{
		  _dbus_warn ("could not parse quoted string for STRING_ARRAY\n");
		  goto parse_failed;
		}
	      i++;

	      if (!_dbus_string_steal_data (&val_str, &val))
		{
		  _dbus_warn ("could not allocate memory for STRING_ARRAY string\n");
		  goto parse_failed;
		}
	      
	      values[len++] = val;
	      if (len == allocated)
		{
		  allocated *= 2;
		  values = dbus_realloc (values, allocated * sizeof (char *));
		  if (!values)
		    {
		      _dbus_warn ("could not allocate memory for STRING_ARRAY\n");
		      goto parse_failed;
		    }
		}
	      
	      _dbus_string_skip_blank (&line, i, &i);
	      
	      b = _dbus_string_get_byte (&line, i++);

	      if (b == '}')
		break;
	      else if (b != ',')
		{
		  _dbus_warn ("missing comma when parsing STRING_ARRAY\n");
		  goto parse_failed;
		}
	    }
	  _dbus_string_free (&val_str);
	  
          if (!_dbus_marshal_string_array (dest, endian, (const char **)values, len))
            {
              _dbus_warn ("failed to append STRING_ARRAY\n");
              goto parse_failed;
            }

	  values[len] = NULL;
	  dbus_free_string_array (values);
	  
	  PERFORM_UNALIGN (dest);
	}
      else if (_dbus_string_starts_with_c_str (&line,
					       "BOOLEAN"))
	{
	  unsigned char val;

	  _dbus_string_delete_first_word (&line);

	  if (_dbus_string_starts_with_c_str (&line, "true"))
	    val = TRUE;
	  else if (_dbus_string_starts_with_c_str (&line, "false"))
	    val = FALSE;
	  else
	    {
	      _dbus_warn ("could not parse BOOLEAN\n");
	      goto parse_failed;
	    }
	  if (!_dbus_string_append_byte (dest, val))
            {
              _dbus_warn ("failed to append BOOLEAN\n");
              goto parse_failed;
            }
	}
      
      else if (_dbus_string_starts_with_c_str (&line,
                                               "INT32"))
        {
          SAVE_FOR_UNALIGN (dest, 4);
          long val;
          
          _dbus_string_delete_first_word (&line);

          if (!_dbus_string_parse_int (&line, 0, &val, NULL))
            {
              _dbus_warn ("could not parse integer for INT32\n");
              goto parse_failed;
            }
          
          if (!_dbus_marshal_int32 (dest, endian,
                                    val))
            {
              _dbus_warn ("failed to append INT32\n");
              goto parse_failed;
            }

          PERFORM_UNALIGN (dest);
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "UINT32"))
        {
          SAVE_FOR_UNALIGN (dest, 4);
          long val;
          
          _dbus_string_delete_first_word (&line);

          /* FIXME should have _dbus_string_parse_uint32 */
          if (!_dbus_string_parse_int (&line, 0, &val, NULL))
            goto parse_failed;
          
          if (!_dbus_marshal_uint32 (dest, endian,
                                     val))
            {
              _dbus_warn ("failed to append UINT32\n");
              goto parse_failed;
            }

          PERFORM_UNALIGN (dest);
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "DOUBLE"))
        {
          SAVE_FOR_UNALIGN (dest, 8);
          double val;
          
          _dbus_string_delete_first_word (&line);

          if (!_dbus_string_parse_double (&line, 0, &val, NULL))
            goto parse_failed;
          
          if (!_dbus_marshal_double (dest, endian,
                                     val))
            {
              _dbus_warn ("failed to append DOUBLE\n");
              goto parse_failed;
            }

          PERFORM_UNALIGN (dest);
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "STRING"))
        {
          SAVE_FOR_UNALIGN (dest, 4);
          int size_offset;
          int old_len;
          
          _dbus_string_delete_first_word (&line);

          size_offset = _dbus_string_get_length (dest);
          size_offset = _DBUS_ALIGN_VALUE (size_offset, 4);
          if (!_dbus_marshal_uint32 (dest, endian, 0))
            {
              _dbus_warn ("Failed to append string size\n");
              goto parse_failed;
            }

          old_len = _dbus_string_get_length (dest);
          if (!append_quoted_string (dest, &line, 0, NULL))
            {
              _dbus_warn ("Failed to append quoted string\n");
              goto parse_failed;
            }

          _dbus_marshal_set_uint32 (dest, endian, size_offset,
                                    /* subtract 1 for nul */
                                    _dbus_string_get_length (dest) - old_len - 1);
          
          PERFORM_UNALIGN (dest);
        }
      else
        goto parse_failed;
      
      if (!just_set_unalign && unalign)
        {
          _dbus_warn ("UNALIGN prior to something that isn't aligned\n");
          goto parse_failed;
        }

      goto next_iteration; /* skip parse_failed */
      
    parse_failed:
      {
        const char *s;
        _dbus_string_get_const_data (&line, &s);
        _dbus_warn ("couldn't process line %d \"%s\"\n",
                    line_no, s);
        goto out;
      }
    }

  _dbus_hash_iter_init (length_hash, &iter);
  while (_dbus_hash_iter_next (&iter))
    {
      SavedLength *sl = _dbus_hash_iter_get_value (&iter);
      const char *s;

      _dbus_string_get_const_data (&sl->name, &s);
      
      if (sl->length < 0)
        {
          _dbus_warn ("Used LENGTH %s but never did END_LENGTH\n",
                      s);
          goto out;
        }
      else if (sl->offset < 0)
        {
          _dbus_warn ("Did END_LENGTH %s but never used LENGTH\n",
                      s);
          goto out;
        }
      else
        {
          if (sl->start < 0)
            sl->start = 0;
          
          _dbus_verbose ("Filling in length %s endian = %d offset = %d start = %d length = %d\n",
                         s, sl->endian, sl->offset, sl->start, sl->length);
          _dbus_marshal_set_int32 (dest,
                                   sl->endian,
                                   sl->offset,
                                   sl->length - sl->start);
        }

      _dbus_hash_iter_remove_entry (&iter);
    }
  
  retval = TRUE;
  
 out:
  if (length_hash != NULL)
    _dbus_hash_table_unref (length_hash);
  
  _dbus_string_free (&file);
  _dbus_string_free (&line);
  return retval;
}

/** @} */
#endif /* DBUS_BUILD_TESTS */
