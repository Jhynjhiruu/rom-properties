/***************************************************************************
 * ROM Properties Page shell extension. (libcachemgr)                      *
 * stdafx.h: Common definitions and includes for COM.                      *
 *                                                                         *
 * Copyright (c) 2016 by David Korth.                                      *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#ifndef __ROMPROPERTIES_LIBCACHEMGR_STDAFX_H__
#define __ROMPROPERTIES_LIBCACHEMGR_STDAFX_H__

#ifndef _WIN32
#error stdafx.h is Windows only.
#endif

// Windows SDK defines and includes.
#include "libwin32common/RpWin32_sdk.h"

// Additional Windows headers.
#include <shlobj.h>

#endif /* __ROMPROPERTIES_LIBCACHEMGR_STDAFX_H__ */
