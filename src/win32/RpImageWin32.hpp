/***************************************************************************
 * ROM Properties Page shell extension. (Win32)                            *
 * RpImageWin32.hpp: rp_image to Win32 conversion functions.               *
 *                                                                         *
 * Copyright (c) 2016 by David Korth.                                      *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#ifndef __ROMPROPERTIES_WIN32_RPIMAGEWIN32_HPP__
#define __ROMPROPERTIES_WIN32_RPIMAGEWIN32_HPP__

#include "librpbase/common.h"
namespace LibRpBase {
	class rp_image;
}

// C includes.
#include <stdint.h>

class RpImageWin32
{
	private:
		RpImageWin32();
		~RpImageWin32();
	private:
		RP_DISABLE_COPY(RpImageWin32)

	protected:
		/**
		 * Convert an rp_image to a HBITMAP for use as an icon mask.
		 * @param image rp_image.
		 * @return HBITMAP, or nullptr on error.
		 */
		static HBITMAP toHBITMAP_mask(const LibRpBase::rp_image *image);

	public:
		/**
		 * Convert an rp_image to HBITMAP.
		 * @param image		[in] rp_image.
		 * @param bgColor	[in] Background color for images with alpha transparency. (ARGB32 format)
		 * @return HBITMAP, or nullptr on error.
		 */
		static HBITMAP toHBITMAP(const LibRpBase::rp_image *image, uint32_t bgColor);

		/**
		 * Convert an rp_image to HBITMAP.
		 * This version resizes the image.
		 * @param image		[in] rp_image.
		 * @param bgColor	[in] Background color for images with alpha transparency. (ARGB32 format)
		 * @param size		[in] If non-zero, resize the image to this size.
		 * @param nearest	[in] If true, use nearest-neighbor scaling.
		 * @return HBITMAP, or nullptr on error.
		 */
		static HBITMAP toHBITMAP(const LibRpBase::rp_image *image, uint32_t bgColor,
					const SIZE &size, bool nearest);

		/**
		 * Convert an rp_image to HBITMAP.
		 * This version preserves the alpha channel.
		 * @param image	[in] rp_image.
		 * @return HBITMAP, or nullptr on error.
		 */
		static HBITMAP toHBITMAP_alpha(const LibRpBase::rp_image *image);

		/**
		 * Convert an rp_image to HBITMAP.
		 * This version preserves the alpha channel and resizes the image.
		 * @param image		[in] rp_image.
		 * @param size		[in] If non-zero, resize the image to this size.
		 * @param nearest	[in] If true, use nearest-neighbor scaling.
		 * @return HBITMAP, or nullptr on error.
		 */
		static HBITMAP toHBITMAP_alpha(const LibRpBase::rp_image *image, const SIZE &size, bool nearest);

		/**
		 * Convert an rp_image to HICON.
		 * @param image rp_image.
		 * @return HICON, or nullptr on error.
		 */
		static HICON toHICON(const LibRpBase::rp_image *image);

		/**
		 * Convert an HBITMAP to rp_image.
		 * @param hBitmap HBITMAP.
		 * @return rp_image.
		 */
		static LibRpBase::rp_image *fromHBITMAP(HBITMAP hBitmap);

		/**
		 * Convert an HBITMAP to HICON.
		 * @param hBitmap HBITMAP.
		 * @return HICON, or nullptr on error.
		 */
		static HICON toHICON(HBITMAP hBitmap);
};

#endif /* __ROMPROPERTIES_WIN32_RPIMAGEWIN32_HPP__ */
