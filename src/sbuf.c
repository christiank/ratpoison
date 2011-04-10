/* Functions for handling string buffers.
 * Copyright (C) 2000, 2001, 2002, 2003, 2004 Shawn Betts <sabetts@vcn.bc.ca>
 *
 * This file is part of ratpoison.
 *
 * ratpoison is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * ratpoison is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307 USA
 *
 * Pretty much all of the documentation here was written by Christian Koch
 * <cfkoch@sdf.lonestar.org>.
 */

#include <string.h>

#include "ratpoison.h"
#include "sbuf.h"

/*
 * Creates a new sbuf. You have to provide the sbuf's initial size, but since
 * ->len and ->maxsz are updated automatically anyway, you should be able to
 * just pass 0 every time. Admittedly, This observation comes from little
 * familiarty on the author's part.
 *
 * You are expected to free the sbuf when you are done with it, either with
 * sbuf_free() or sbuf_free_struct().
 */
struct sbuf *
sbuf_new (size_t initsz)
{
  struct sbuf *b = (struct sbuf*) xmalloc (sizeof (struct sbuf));

  if (initsz < 1)
    initsz = 1;

  b->data = (char*) xmalloc (initsz);
  b->maxsz = initsz;

  b->data[0] = '\0';
  b->len = 0;

  return b;
}


/*
 * Frees the structure and the string which it contains.
 */
void
sbuf_free (struct sbuf *b)
{
  if (b != NULL)
    {
      if (b->data != NULL)
        free (b->data);

      free (b);
    }
}


/* 
 * Frees the structure, but returns the string.
 */
char *
sbuf_free_struct (struct sbuf *b)
{
  if (b != NULL)
    {
      char *tmp;
      tmp = b->data;
      free (b);
      return tmp;
    }

  return NULL;
}


/*
 * Concatenates no more than the first `len' characters of `str' with the string
 * contained in `b'. Returns the newly formed string. 
 *
 * You might be more interested in sbuf_concat(), which takes care of the `len'
 * argument for you.
 */
char *
sbuf_nconcat (struct sbuf *b, const char *str, int len)
{
  size_t minsz = b->len + len + 1;

  if (b->maxsz < minsz)
    {
      b->data = (char*) xrealloc (b->data, minsz);
      b->maxsz = minsz;
    }

  memcpy (b->data + b->len, str, minsz - b->len - 1);
  b->len = minsz - 1;
  *(b->data + b->len) = 0;

  return b->data;
}


/*
 * Concatenates `str' with the string contained in `b'. Returns the newly formed
 * string.
 */
char *
sbuf_concat (struct sbuf *b, const char *str)
{
  return sbuf_nconcat (b, str, strlen (str));
}


/*
 * Replaces the string contained in `b' with `str'. Returns the new contents of
 * `b', in other words, `str' itself.
 */
char *
sbuf_copy (struct sbuf *b, const char *str)
{
  b->len = 0;
  return sbuf_concat (b, str);
}


/*
 * Truncates the string contained in `b' to zero length. Returns the new
 * contents of `b', in other words, the empty string.
 */
char *
sbuf_clear (struct sbuf *b)
{
  b->len = 0;
  b->data[0] = '\0';
  return b->data;
}


/*
 * Returns the string contained in `b'. This is merely a convenient front-end to
 * manually accessing the ->data member of the sbuf.
 */
char *
sbuf_get (struct sbuf *b)
{
  return b->data;
}


/*
 * Replaces the string contained in `b' with the formatted string `fmt'. 
 *
 * Even though the function is named after printf(), it behaves more like
 * sprintf(), whose destination stream is the string contained in the sbuf.
 */
char *
sbuf_printf (struct sbuf *b, char *fmt, ...)
{
  va_list ap;

  free (b->data);

  va_start (ap, fmt);
  b->data = xvsprintf (fmt, ap);
  va_end (ap);

  return b->data;
}


/*
 * Concatenates the string contained in `b' with the formatted string `fmt'.
 *
 * Even though this function is named after printf(), it behaves more like
 * sprintf(), whose destination stream is the string contained in the sbuf.
 */
char *
sbuf_printf_concat (struct sbuf *b, char *fmt, ...)
{
  char *buffer;
  va_list ap;

  va_start (ap, fmt);
  buffer = xvsprintf (fmt, ap);
  va_end (ap);

  sbuf_concat (b, buffer);
  free (buffer);

  return b->data;
}
