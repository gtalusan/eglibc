/* Set floating-point environment exception handling.
   Copyright (C) 1997-2014 Free Software Foundation, Inc.
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
   License along with the GNU C Library.  If not, see
   <http://www.gnu.org/licenses/>.  */

#include <fenv.h>
#include <math.h>
#include <fpu_control.h>
#include <arm-features.h>


int
fesetexceptflag (const fexcept_t *flagp, int excepts)
{
  fpu_control_t fpscr;

  /* Fail if a VFP unit isn't present unless nothing needs to be done.  */
  if (!ARM_HAVE_VFP)
    return (excepts != 0);

  _FPU_GETCW (fpscr);

  /* Set the desired exception mask.  */
  fpscr &= ~(excepts & FE_ALL_EXCEPT);
  fpscr |= (*flagp & excepts & FE_ALL_EXCEPT);

  /* Save state back to the FPU.  */
  _FPU_SETCW (fpscr);
  return 0;
}
