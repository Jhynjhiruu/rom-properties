/***************************************************************************
 * ROM Properties Page shell extension. (Win32)                            *
 * CreateThumbnail.hpp: TCreateThumbnail<HBITMAP> implementation.          *
 *                                                                         *
 * Copyright (c) 2016-2018 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#ifndef __ROMPROPERTIES_WIN32_CREATETHUMBNAIL_HPP__
#define __ROMPROPERTIES_WIN32_CREATETHUMBNAIL_HPP__

#include "libromdata/img/TCreateThumbnail.hpp"
#include "libwin32common/RpWin32_sdk.h"

/**
 * CreateThumbnail implementation for Windows.
 * This version uses alpha transparency.
 */
class CreateThumbnail : public LibRomData::TCreateThumbnail<HBITMAP>
{
	public:
		CreateThumbnail() { }

	private:
		typedef TCreateThumbnail<HBITMAP> super;
		RP_DISABLE_COPY(CreateThumbnail)

	public:
		/** TCreateThumbnail functions. **/

		/**
		 * Wrapper function to convert rp_image* to ImgClass.
		 * @param img rp_image
		 * @return ImgClass
		 */
		HBITMAP rpImageToImgClass(const LibRpBase::rp_image *img) const override;

		/**
		 * Wrapper function to check if an ImgClass is valid.
		 * @param imgClass ImgClass
		 * @return True if valid; false if not.
		 */
		bool isImgClassValid(const HBITMAP &imgClass) const final;

		/**
		 * Wrapper function to get a "null" ImgClass.
		 * @return "Null" ImgClass.
		 */
		HBITMAP getNullImgClass(void) const final;

		/**
		 * Free an ImgClass object.
		 * @param imgClass ImgClass object.
		 */
		void freeImgClass(HBITMAP &imgClass) const final;

		/**
		 * Rescale an ImgClass using nearest-neighbor scaling.
		 * @param imgClass ImgClass object.
		 * @param sz New size.
		 * @return Rescaled ImgClass.
		 */
		HBITMAP rescaleImgClass(const HBITMAP &imgClass, const ImgSize &sz) const override;

		/**
		 * Get the size of the specified ImgClass.
		 * @param imgClass	[in] ImgClass object.
		 * @param pOutSize	[out] Pointer to ImgSize to store the image size.
		 * @return 0 on success; non-zero on error.
		 */
		int getImgClassSize(const HBITMAP &imgClass, ImgSize *pOutSize) const final;

		/**
		 * Get the proxy for the specified URL.
		 * @return Proxy, or empty string if no proxy is needed.
		 */
		std::string proxyForUrl(const std::string &url) const final;
};

/**
 * CreateThumbnail implementation for Windows.
 * This version does NOT use alpha transparency.
 * COLOR_WINDOW is used for the background.
 */
class CreateThumbnailNoAlpha : public CreateThumbnail
{
	public:
		CreateThumbnailNoAlpha() { }

	private:
		typedef CreateThumbnail super;
		RP_DISABLE_COPY(CreateThumbnailNoAlpha)

	public:
		/** TCreateThumbnail functions. **/

		/**
		 * Wrapper function to convert rp_image* to ImgClass.
		 * @param img rp_image
		 * @return ImgClass
		 */
		HBITMAP rpImageToImgClass(const LibRpBase::rp_image *img) const final;

		/**
		 * Rescale an ImgClass using nearest-neighbor scaling.
		 * @param imgClass ImgClass object.
		 * @param sz New size.
		 * @return Rescaled ImgClass.
		 */
		HBITMAP rescaleImgClass(const HBITMAP &imgClass, const ImgSize &sz) const final;
};

#endif /* __ROMPROPERTIES_WIN32_CREATETHUMBNAIL_HPP__ */
