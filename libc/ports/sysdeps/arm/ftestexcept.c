/* Test exception in current environment.
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
#include <fpu_control.h>
#include <arm-features.h>


int
fetestexcept (int excepts)
{
  fpu_control_t fpscr;

  /* Return no exception flags if a VFP unit isn't present.  */
  if (!ARM_HAVE_VFP)
    return 0;

  /* Get current exceptions.  */
  _FPU_GETCW (fpscr);

  return fpscr & excepts & FE_ALL_EXCEPT;
}
libm_hidden_def (fetestexcept)
