/* Copyright (C) 2005, 2012 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#include <locale.h>
#include <wchar.h>


size_t
__wcsnrtombs_chk (char *dst, const wchar_t **src, size_t nwc, size_t len,
		  mbstate_t *ps, size_t dstlen)
{
  if (__builtin_expect (dstlen < len, 0))
    __chk_fail ();

  return __wcsnrtombs (dst, src, nwc, len, ps);
}
