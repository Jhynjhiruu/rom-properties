/***************************************************************************
 * ROM Properties Page shell extension. (Win32)                            *
 * RP_ExtractIcon_Fallback.cpp: IExtractIcon implementation.               *
 * Fallback functions for unsupported files.                               *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "stdafx.h"
#include "RP_ExtractIcon.hpp"
#include "RP_ExtractIcon_p.hpp"

// librpbase
#include "librpbase/TextFuncs.hpp"
#include "librpbase/TextFuncs_wchar.hpp"
#include "librpbase/file/FileSystem.hpp"
using namespace LibRpBase;

// libwin32common
#include "libwin32common/RegKey.hpp"
#include "libwin32common/sdk/GUID_fn.h"
using LibWin32Common::RegKey;

// C++ includes.
#include <memory>
#include <string>
using std::unique_ptr;
using std::tstring;

// COM smart pointer typedefs.
#ifndef _MSC_VER
// MSVC: Defined in comdefsp.h.
_COM_SMARTPTR_TYPEDEF(IClassFactory, IID_IClassFactory);
_COM_SMARTPTR_TYPEDEF(IPersistFile,  IID_IPersistFile);
#endif

/**
 * Use IExtractIconW from a fallback icon handler.
 * @param pExtractIconW Pointer to IExtractIconW interface.
 * @param phiconLarge Large icon.
 * @param phiconSmall Small icon.
 * @param nIconSize Icon sizes.
 * @return ERROR_SUCCESS on success; Win32 error code on error.
 */
LONG RP_ExtractIcon_Private::DoExtractIconW(IExtractIconW *pExtractIconW,
	HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize)
{
	// Get the IPersistFile interface.
	IPersistFilePtr pPersistFile;
	HRESULT hr = pExtractIconW->QueryInterface(IID_PPV_ARGS(&pPersistFile));
	if (FAILED(hr)) {
		// Failed to get the IPersistFile interface.
		return ERROR_FILE_NOT_FOUND;
	}

	// Load the file.
	hr = pPersistFile->Load(U82W_s(this->filename), STGM_READ);
	if (FAILED(hr)) {
		// Failed to load the file.
		return ERROR_FILE_NOT_FOUND;
	}

	// Get the icon location.
	wchar_t szIconFileW[MAX_PATH];
	int nIconIndex;
	UINT wFlags;
	// TODO: Handle S_FALSE with GIL_DEFAULTICON?
	hr = pExtractIconW->GetIconLocation(0, szIconFileW, ARRAY_SIZE(szIconFileW), &nIconIndex, &wFlags);
	if (FAILED(hr)) {
		// GetIconLocation() failed.
		return ERROR_FILE_NOT_FOUND;
	}

	if (wFlags & GIL_NOTFILENAME) {
		// Icon is not available on disk.
		// Use IExtractIcon::Extract().
		hr = pExtractIconW->Extract(szIconFileW, nIconIndex, phiconLarge, phiconSmall, nIconSize);
		return (SUCCEEDED(hr) ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND);
	} else {
		// Icon is available on disk.

		// PrivateExtractIcons() is published as of Windows XP SP1,
		// but it's "officially" private.
		// Reference: https://msdn.microsoft.com/en-us/library/windows/desktop/ms648075(v=vs.85).aspx
		// TODO: Verify that hIcons[x] is NULL if only one size is found.
		// TODO: Verify which icon is extracted.
		// TODO: What if the size isn't found?

		// TODO: ANSI versions: Use GetProcAddress().
		HICON hIcons[2];
		UINT uRet = PrivateExtractIconsW(szIconFileW, nIconIndex,
				nIconSize, nIconSize, hIcons, nullptr, 2, 0);
		if (uRet == 0) {
			// No icons were extracted.
			return ERROR_FILE_NOT_FOUND;
		}

		// At least one icon was extracted.
		*phiconLarge = hIcons[0];
		*phiconSmall = hIcons[1];
	}
	return ERROR_SUCCESS;
}

/**
 * Use IExtractIconA from an old fallback icon handler.
 * @param pExtractIconA Pointer to IExtractIconW interface.
 * @param phiconLarge Large icon.
 * @param phiconSmall Small icon.
 * @param nIconSize Icon sizes.
 * @return ERROR_SUCCESS on success; Win32 error code on error.
 */
LONG RP_ExtractIcon_Private::DoExtractIconA(IExtractIconA *pExtractIconA,
	HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize)
{
	// TODO: Verify that LPCOLESTR is still Unicode in IExtractIconA.
	// TODO: Needs testing.

	// Get the IPersistFile interface.
	IPersistFilePtr pPersistFile;
	HRESULT hr = pExtractIconA->QueryInterface(IID_PPV_ARGS(&pPersistFile));
	if (FAILED(hr)) {
		// Failed to get the IPersistFile interface.
		return ERROR_FILE_NOT_FOUND;
	}

	// Load the file.
	// TODO: Proper string conversion.
	hr = pPersistFile->Load((LPCOLESTR)this->filename.c_str(), STGM_READ);
	if (FAILED(hr)) {
		// Failed to load the file.
		return ERROR_FILE_NOT_FOUND;
	}

	// Get the icon location.
	char szIconFileA[MAX_PATH];
	int nIconIndex;
	UINT wFlags;
	// TODO: Handle S_FALSE with GIL_DEFAULTICON?
	hr = pExtractIconA->GetIconLocation(0, szIconFileA, ARRAY_SIZE(szIconFileA), &nIconIndex, &wFlags);
	if (FAILED(hr)) {
		// GetIconLocation() failed.
		return ERROR_FILE_NOT_FOUND;
	}

	if (wFlags & GIL_NOTFILENAME) {
		// Icon is not available on disk.
		// Use IExtractIcon::Extract().
		hr = pExtractIconA->Extract(szIconFileA, nIconIndex, phiconLarge, phiconSmall, nIconSize);
		return (SUCCEEDED(hr) ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND);
	} else {
		// Icon is available on disk.

		// PrivateExtractIcons() is published as of Windows XP SP1,
		// but it's "officially" private.
		// Reference: https://msdn.microsoft.com/en-us/library/windows/desktop/ms648075(v=vs.85).aspx
		// TODO: Verify that hIcons[x] is NULL if only one size is found.
		// TODO: Verify which icon is extracted.
		// TODO: What if the size isn't found?

		// TODO: ANSI versions: Use GetProcAddress().
		HICON hIcons[2];
		UINT uRet = PrivateExtractIconsA(szIconFileA, nIconIndex,
				nIconSize, nIconSize, hIcons, nullptr, 2, 0);
		if (uRet == 0) {
			// No icons were extracted.
			return ERROR_FILE_NOT_FOUND;
		}

		// At least one icon was extracted.
		*phiconLarge = hIcons[0];
		*phiconSmall = hIcons[1];
	}
	return ERROR_SUCCESS;
}

/**
 * Fallback icon handler function. (internal)
 * @param hkey_Assoc File association key to check.
 * @param phiconLarge Large icon.
 * @param phiconSmall Small icon.
 * @param nIconSize Icon sizes.
 * @return ERROR_SUCCESS on success; Win32 error code on error.
 */
LONG RP_ExtractIcon_Private::Fallback_int(RegKey &hkey_Assoc,
	HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize)
{
	// Is RP_Fallback present?
	RegKey hkey_RP_Fallback(hkey_Assoc, _T("RP_Fallback"), KEY_READ, false);
	if (!hkey_RP_Fallback.isOpen()) {
		return hkey_RP_Fallback.lOpenRes();
	}

	// Get the DefaultIcon key.
	DWORD dwType;
	tstring defaultIcon = hkey_RP_Fallback.read_expand(_T("DefaultIcon"), &dwType);
	if (defaultIcon.empty()) {
		// No default icon.
		return ERROR_FILE_NOT_FOUND;
	} else if (defaultIcon == _T("%1")) {
		// Forward to the IconHandler.
		const tstring iconHandler = hkey_RP_Fallback.read(_T("IconHandler"));
		if (iconHandler.empty()) {
			// No IconHandler.
			return ERROR_FILE_NOT_FOUND;
		}

		// Parse the CLSID string.
		// TODO: Use IIDFromString() instead to skip ProgID handling?
		// Reference: https://blogs.msdn.microsoft.com/oldnewthing/20151015-00/?p=91351
		CLSID clsidIconHandler;
		HRESULT hr = CLSIDFromString(iconHandler.c_str(), &clsidIconHandler);
		if (FAILED(hr)) {
			// Failed to convert the CLSID from string.
			return ERROR_FILE_NOT_FOUND;
		}

		// Get the class object.
		IClassFactoryPtr pCF;
		hr = CoGetClassObject(clsidIconHandler, CLSCTX_INPROC_SERVER, nullptr, IID_PPV_ARGS(&pCF));
		if (FAILED(hr) || !pCF) {
			// Failed to get the IClassFactory.
			return ERROR_FILE_NOT_FOUND;
		}

		// Try getting the IExtractIconW interface.
		IExtractIconW *pExtractIconW;
		hr = pCF->CreateInstance(nullptr, IID_PPV_ARGS(&pExtractIconW));
		if (SUCCEEDED(hr) && pExtractIconW) {
			// Extract the icon.
			LONG lResult = DoExtractIconW(pExtractIconW, phiconLarge, phiconSmall, nIconSize);
			pExtractIconW->Release();
			return lResult;
		} else {
			// Try getting the IExtractIconA interface.
			IExtractIconA *pExtractIconA;
			hr = pCF->CreateInstance(nullptr, IID_PPV_ARGS(&pExtractIconA));
			if (SUCCEEDED(hr) && pExtractIconA) {
				// Extract the icon.
				LONG lResult = DoExtractIconA(pExtractIconA, phiconLarge, phiconSmall, nIconSize);
				pExtractIconA->Release();
				return lResult;
			} else {
				// Failed to get an IExtractIcon interface from the fallback class.
				return ERROR_FILE_NOT_FOUND;
			}
		}

		// Should not get here...
		return ERROR_INVALID_FUNCTION;
	}

	// DefaultIcon is set but IconHandler isn't, which means
	// the file's icon is stored as an icon resource.

	// DefaultIcon format: "C:\\Windows\\Some.DLL,1"
	// TODO: Can the filename be quoted?
	// TODO: Better error codes?
	int nIconIndex;
	size_t comma = defaultIcon.find_last_of(L',');
	if (comma != tstring::npos) {
		// Found the comma.
		if (comma > 0 && comma < defaultIcon.size()-1) {
			TCHAR *endptr = nullptr;
			errno = 0;
			nIconIndex = (int)_tcstol(&defaultIcon[comma+1], &endptr, 10);
			if (errno == ERANGE || *endptr != 0) {
				// _tcstol() failed.
				// DefaultIcon is invalid.
				return ERROR_FILE_NOT_FOUND;
			}
		} else {
			// Comma is the last character.
			return ERROR_FILE_NOT_FOUND;
		}

		// Remove the comma portion.
		defaultIcon.resize(comma);
	} else {
		// Assume the default icon index.
		nIconIndex = 0;
	}

	// PrivateExtractIcons() is published as of Windows XP SP1,
	// but it's "officially" private.
	// Reference: https://msdn.microsoft.com/en-us/library/windows/desktop/ms648075(v=vs.85).aspx
	// TODO: Verify that hIcons[x] is NULL if only one size is found.
	// TODO: Verify which icon is extracted.
	// TODO: What if the size isn't found?
	HICON hIcons[2];
	UINT uRet = PrivateExtractIcons(defaultIcon.c_str(), nIconIndex,
			nIconSize, nIconSize, hIcons, nullptr, 2, 0);
	if (uRet == 0) {
		// No icons were extracted.
		return ERROR_FILE_NOT_FOUND;
	}

	// At least one icon was extracted.
	*phiconLarge = hIcons[0];
	*phiconSmall = hIcons[1];
	return ERROR_SUCCESS;
}

/**
 * Fallback icon handler function.
 * @param phiconLarge Large icon.
 * @param phiconSmall Small icon.
 * @return ERROR_SUCCESS on success; Win32 error code on error.
 */
LONG RP_ExtractIcon_Private::Fallback(HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize)
{
	// TODO: Check HKCU first.

	// Get the file extension.
	if (filename.empty()) {
		return ERROR_FILE_NOT_FOUND;
	}
	const char *file_ext = FileSystem::file_ext(filename);
	if (!file_ext) {
		// Invalid or missing file extension.
		return ERROR_FILE_NOT_FOUND;
	}

	// Open the filetype key in HKCR.
	RegKey hkey_Assoc(HKEY_CLASSES_ROOT, U82T_c(file_ext), KEY_READ, false);
	if (!hkey_Assoc.isOpen()) {
		return hkey_Assoc.lOpenRes();
	}

	// If we have a ProgID, check it first.
	const tstring progID = hkey_Assoc.read(nullptr);
	if (!progID.empty()) {
		// Custom ProgID is registered.
		// TODO: Get the correct top-level registry key.
		RegKey hkcr_ProgID(HKEY_CLASSES_ROOT, progID.c_str(), KEY_READ, false);
		if (hkcr_ProgID.isOpen()) {
			LONG lResult = Fallback_int(hkcr_ProgID, phiconLarge, phiconSmall, nIconSize);
			if (lResult == ERROR_SUCCESS) {
				// ProgID icon extracted.
				return lResult;
			}
		}
	}

	// Extract the icon from the filetype key.
	return Fallback_int(hkey_Assoc, phiconLarge, phiconSmall, nIconSize);
}
