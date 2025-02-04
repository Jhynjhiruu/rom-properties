/***************************************************************************
 * ROM Properties Page shell extension. (Win32)                            *
 * resource.rc: Win32 resource script.                                     *
 *                                                                         *
 * Copyright (c) 2016-2018 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#ifndef __ROMPROPERTIES_WIN32_RESOURCE_H__
#define __ROMPROPERTIES_WIN32_RESOURCE_H__

/** Icons. **/
#define IDI_KEY_VALID           201

// Dialogs
#define IDD_PROPERTY_SHEET                      100	/* Generic property sheet. */
#define IDD_SUBTAB_CHILD_DIALOG                 101	/* Subtab child dialog. */

// Standard controls
#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

// Property sheet controls.
// These are defined in Wine's comctl32.h, but not the Windows SDK.
// Reference: https://source.winehq.org/git/wine.git/blob/HEAD:/dlls/comctl32/comctl32.h
#define IDC_TABCONTROL                          12320
#define IDC_APPLY_BUTTON                        12321
#define IDC_BACK_BUTTON                         12323
#define IDC_NEXT_BUTTON                         12324
#define IDC_FINISH_BUTTON                       12325
#define IDC_SUNKEN_LINE                         12326
#define IDC_SUNKEN_LINEHEADER                   12327

// Custom property sheet controls.
#define IDC_RP_RESET                            13431
#define IDC_RP_DEFAULTS                         13432

// "Reset" and "Defaults" messages.
// The parent PropertySheet doesn't store any user data, so we can't
// store a reference to the private class. Hence, we have to send a
// message to all child property sheet pages instead.
#define WM_RP_PROP_SHEET_RESET			(WM_USER + 0x1234)
#define WM_RP_PROP_SHEET_DEFAULTS		(WM_USER + 0x1235)
// Enable/disable the "Defaults" button.
// We can't access the ITab objects from the PropertySheet callback function.
// wParam: 0 == disable, 1 == enable
#define WM_RP_PROP_SHEET_ENABLE_DEFAULTS	(WM_USER + 0x1236)
#define RpPropSheet_EnableDefaults(hWnd,enable)	(void)SNDMSG(hWnd,WM_RP_PROP_SHEET_ENABLE_DEFAULTS,(WPARAM)(enable),0)

// KeyStoreWin32 messages.
// Basically the equivalent of Qt's signals.
// NOTE: We could pack sectIdx/keyIdx into WPARAM,
// but that would change the way signals are handled.
// Define two messages for consistency.

// wParam: sectIdx
// lParam: keyIdx
#define WM_KEYSTORE_KEYCHANGED_SECTKEY				(WM_USER + 0x2001)
#define KeyStore_KeyChanged_SectKey(hWnd,sectIdx,keyIdx)	(void)SNDMSG(hWnd,WM_KEYSTORE_KEYCHANGED_SECTKEY,(sectIdx),(keyIdx))

// wParam: 0
// lParam: idx
#define WM_KEYSTORE_KEYCHANGED_IDX				(WM_USER + 0x2002)
#define KeyStore_KeyChanged_Idx(hWnd,idx)			(void)SNDMSG(hWnd,WM_KEYSTORE_KEYCHANGED_IDX,0,(idx))

// wParam: 0
// lParam: 0
#define WM_KEYSTORE_ALLKEYSCHANGED				(WM_USER + 0x2003)
#define KeyStore_AllKeysChanged_Idx(hWnd)			(void)SNDMSG(hWnd,WM_KEYSTORE_ALLKEYSCHANGED,0,0)

// wParam: 0
// lParam: 0
#define WM_KEYSTORE_MODIFIED					(WM_USER + 0x2004)
#define KeyStore_Modified(hWnd)					(void)SNDMSG(hWnd,WM_KEYSTORE_MODIFIED,0,0)

/** Configuration dialog **/
#define IDD_CONFIG_IMAGETYPES                   110
#define IDD_CONFIG_OPTIONS                      111
#define IDD_CONFIG_CACHE                        112
#define IDD_CONFIG_CACHE_XP                     113
#define IDD_CONFIG_KEYMANAGER                   114
#define IDD_CONFIG_ABOUT                        115

// Image type priorities.
#define IDC_IMAGETYPES_DESC1                    40001
#define IDC_IMAGETYPES_DESC2                    40002
#define IDC_IMAGETYPES_CREDITS                  40003
#define IDC_IMAGETYPES_VIEWPORT_OUTER		40010
#define IDC_IMAGETYPES_VIEWPORT_INNER		40011

// Options
#define IDC_EXTIMGDL                            40101
#define IDC_INTICONSMALL                        40102
#define IDC_HIGHRESDL                           40103
#define IDC_DANGEROUSPERMISSIONS                40104
#define IDC_ENABLETHUMBNAILONNETWORKFS          40105

// Thumbnail cache
#define IDC_CACHE_DESCRIPTION                   40201
#define IDC_CACHE_CLEAR_SYS_THUMBS              40202
#define IDC_CACHE_CLEAR_RP_DL                   40203
#define IDC_CACHE_STATUS                        40204
#define IDC_CACHE_PROGRESS                      40205
#define IDC_CACHE_XP_FIND_DRIVES                40206
#define IDC_CACHE_XP_FIND_PATH                  40207
#define IDC_CACHE_XP_DRIVES                     40208
#define IDC_CACHE_XP_PATH                       40209
#define IDC_CACHE_XP_BROWSE                     40210
#define IDC_CACHE_XP_CLEAR_SYS_THUMBS           40211

// FIXME: Only if ENABLE_DECRYPTION, but we can't include config.librpbase.h.
#if 1
// Key Manager
#define IDC_KEYMANAGER_LIST                     40301
#define IDC_KEYMANAGER_EDIT                     40302
#define IDC_KEYMANAGER_IMPORT                   40303

// Key Manager: "Import" menu
#define IDR_KEYMANAGER_IMPORT                   30301
#define IDM_KEYMANAGER_IMPORT_WII_KEYS_BIN      30302
#define IDM_KEYMANAGER_IMPORT_WIIU_OTP_BIN      30303
#define IDM_KEYMANAGER_IMPORT_3DS_BOOT9_BIN     30304
#define IDM_KEYMANAGER_IMPORT_3DS_AESKEYDB      30305
#endif

// About
#define IDC_ABOUT_ICON                          40401
#define IDC_ABOUT_LINE1                         40402
#define IDC_ABOUT_LINE2                         40403
#define IDC_ABOUT_VERSION                       40404
#define IDC_ABOUT_TABCONTROL                    40405
#define IDC_ABOUT_RICHEDIT                      40406

#endif /* __ROMPROPERTIES_WIN32_RESOURCE_H__ */
