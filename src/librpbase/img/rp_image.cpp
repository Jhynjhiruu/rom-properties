/***************************************************************************
 * ROM Properties Page shell extension. (librpbase)                        *
 * rp_image.hpp: Image class.                                              *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "rp_image.hpp"
#include "rp_image_p.hpp"
#include "rp_image_backend.hpp"

#include "common.h"
#include "aligned_malloc.h"

// C includes. (C++ namespace)
#include <cassert>
#include <cerrno>
#include <cstring>

// Workaround for RP_D() expecting the no-underscore, UpperCamelCase naming convention.
#define rp_imagePrivate rp_image_private

namespace LibRpBase {

/** rp_image_backend_default **/

class rp_image_backend_default : public rp_image_backend
{
 	public:
		rp_image_backend_default(int width, int height, rp_image::Format format);
		virtual ~rp_image_backend_default();

	private:
		typedef rp_image_backend super;
		RP_DISABLE_COPY(rp_image_backend_default)

	public:
		void *data(void) final
		{
			return m_data;
		}

		const void *data(void) const final
		{
			return m_data;
		}

		size_t data_len(void) const final
		{
			return m_data_len;
		}

		uint32_t *palette(void) final
		{
			return m_palette;
		}

		const uint32_t *palette(void) const final
		{
			return m_palette;
		}

		int palette_len(void) const final
		{
			return m_palette_len;
		}

	private:
		void *m_data;
		size_t m_data_len;

		uint32_t *m_palette;
		int m_palette_len;
};

rp_image_backend_default::rp_image_backend_default(int width, int height, rp_image::Format format)
	: super(width, height, format)
	, m_data(nullptr)
	, m_data_len(0)
	, m_palette(nullptr)
	, m_palette_len(0)
{
	if (width == 0 || height == 0) {
		// Error initializing the backend.
		// (Width, height, or format is probably broken.)
		return;
	}

	// Allocate memory for the image.
	// We're using the full stride for the last row
	// to make it easier to manage.
	m_data_len = height * stride;
	assert(m_data_len > 0);
	if (m_data_len == 0) {
		// Somehow we have a 0-length image...
		clear_properties();
		return;
	}

	m_data = aligned_malloc(16, m_data_len);
	assert(m_data != nullptr);
	if (!m_data) {
		// Failed to allocate memory.
		clear_properties();
		return;
	}

	// Do we need to allocate memory for the palette?
	if (format == rp_image::FORMAT_CI8) {
		// Palette is initialized to 0 to ensure
		// there's no weird artifacts if the caller
		// is converting a lower-color image.
		const size_t palette_sz = 256*sizeof(*m_palette);
		m_palette = static_cast<uint32_t*>(aligned_malloc(16, palette_sz));
		if (!m_palette) {
			// Failed to allocate memory.
			aligned_free(m_data);
			m_data = nullptr;
			m_data_len = 0;
			clear_properties();
			return;
		}

		// 256 colors allocated in the palette.
		memset(m_palette, 0, palette_sz);
		m_palette_len = 256;
	}
}

rp_image_backend_default::~rp_image_backend_default()
{
	aligned_free(m_data);
	aligned_free(m_palette);
}

/** rp_image_private **/

rp_image::rp_image_backend_creator_fn rp_image_private::backend_fn = nullptr;

/**
 * Create an rp_image_private.
 *
 * If an rp_image_backend has been registered, that backend
 * will be used; otherwise, the defaul tbackend will be used.
 *
 * @param width Image width.
 * @param height Image height.
 * @param format Image format.
 */
rp_image_private::rp_image_private(int width, int height, rp_image::Format format)
	: has_sBIT(false)
{
	// Clear the metadata.
	memset(&sBIT, 0, sizeof(sBIT));

	if (width <= 0 || height <= 0 ||
	    (format != rp_image::FORMAT_CI8 && format != rp_image::FORMAT_ARGB32))
	{
		// Invalid image specifications.
		this->backend = new rp_image_backend_default(0, 0, rp_image::FORMAT_NONE);
		return;
	}

	// Allocate a storage object for the image.
	if (backend_fn != nullptr) {
		this->backend = backend_fn(width, height, format);
	} else {
		this->backend = new rp_image_backend_default(width, height, format);
	}
}

/**
 * Create an rp_image_private using the specified rp_image_backend.
 *
 * NOTE: This rp_image will take ownership of the rp_image_backend.
 *
 * @param backend rp_image_backend.
 */
rp_image_private::rp_image_private(rp_image_backend *backend)
	: backend(backend)
	, has_sBIT(false)
{
	// Clear the metadata.
	// TODO: Store sBIT in the backend and copy it?
	memset(&sBIT, 0, sizeof(sBIT));
}

rp_image_private::~rp_image_private()
{
	delete backend;
}

/** rp_image **/

/**
 * Create an rp_image.
 *
 * If an rp_image_backend has been registered, that backend
 * will be used; otherwise, the defaul tbackend will be used.
 *
 * @param width Image width.
 * @param height Image height.
 * @param format Image format.
 */
rp_image::rp_image(int width, int height, rp_image::Format format)
	: d_ptr(new rp_image_private(width, height, format))
{ }

/**
 * Create an rp_image using the specified rp_image_backend.
 *
 * NOTE: This rp_image will take ownership of the rp_image_backend.
 *
 * @param backend rp_image_backend.
 */
rp_image::rp_image(rp_image_backend *backend)
	: d_ptr(new rp_image_private(backend))
{ }

rp_image::~rp_image()
{
	delete d_ptr;
}

/** Creator function. **/

/**
 * Set the image backend creator function.
 * @param backend Image backend creator function.
 */
void rp_image::setBackendCreatorFn(rp_image_backend_creator_fn backend_fn)
{
	rp_image_private::backend_fn = backend_fn;
}

/**
 * Get the image backend creator function.
 * @param backend Image backend creator function, or nullptr if the default is in use.
 */
rp_image::rp_image_backend_creator_fn rp_image::backendCreatorFn(void)
{
	return rp_image_private::backend_fn;
}

/**
 * Get this image's backend object.
 * @return Image backend object.
 */
const rp_image_backend *rp_image::backend(void) const
{
	RP_D(const rp_image);
	return d->backend;
}

/** Properties. **/

/**
 * Is the image valid?
 * @return True if the image is valid.
 */
bool rp_image::isValid(void) const
{
	RP_D(const rp_image);
	return d->backend->isValid();
}

/**
 * Get the image width.
 * @return Image width.
 */
int rp_image::width(void) const
{
	RP_D(const rp_image);
	return d->backend->width;
}

/**
 * Get the image height.
 * @return Image height.
 */
int rp_image::height(void) const
{
	RP_D(const rp_image);
	return d->backend->height;
}

/**
 * Is this rp_image square?
 * @return True if width == height; false if not.
 */
bool rp_image::isSquare(void) const
{
	RP_D(const rp_image);
	return (d->backend->width == d->backend->height);
}

/**
 * Get the total number of bytes per line.
 * This includes memory alignment padding.
 * @return Bytes per line.
 */
int rp_image::stride(void) const
{
	RP_D(const rp_image);
	return d->backend->stride;
}

/**
 * Get the number of active bytes per line.
 * This does not include memory alignment padding.
 * @return Number of active bytes per line.
 */
int rp_image::row_bytes(void) const
{
	RP_D(const rp_image);
	switch (d->backend->format) {
		case FORMAT_CI8:
			return d->backend->width;
		case rp_image::FORMAT_ARGB32:
			return d->backend->width * sizeof(uint32_t);
		default:
			assert(!"Unsupported rp_image::Format.");
			break;
	}

	// Should not get here...
	return 0;
}

/**
 * Get the image format.
 * @return Image format.
 */
rp_image::Format rp_image::format(void) const
{
	RP_D(const rp_image);
	return d->backend->format;
}

/**
 * Get a pointer to the first line of image data.
 * @return Image data.
 */
const void *rp_image::bits(void) const
{
	RP_D(const rp_image);
	return d->backend->data();
}

/**
 * Get a pointer to the first line of image data.
 * TODO: detach()
 * @return Image data.
 */
void *rp_image::bits(void)
{
	RP_D(rp_image);
	return d->backend->data();
}

/**
 * Get a pointer to the specified line of image data.
 * @param i Line number.
 * @return Line of image data, or nullptr if i is out of range.
 */
const void *rp_image::scanLine(int i) const
{
	RP_D(const rp_image);
	const uint8_t *data = static_cast<const uint8_t*>(d->backend->data());
	if (!data)
		return nullptr;

	return data + (d->backend->stride * i);
}

/**
 * Get a pointer to the specified line of image data.
 * TODO: Detach.
 * @param i Line number.
 * @return Line of image data, or nullptr if i is out of range.
 */
void *rp_image::scanLine(int i)
{
	RP_D(rp_image);
	uint8_t *data = static_cast<uint8_t*>(d->backend->data());
	if (!data)
		return nullptr;

	return data + (d->backend->stride * i);
}

/**
 * Get the image data size, in bytes.
 * This is height * stride.
 * @return Image data size, in bytes.
 */
size_t rp_image::data_len(void) const
{
	RP_D(const rp_image);
	return d->backend->data_len();
}

/**
 * Get the image palette.
 * @return Pointer to image palette, or nullptr if not a paletted image.
 */
const uint32_t *rp_image::palette(void) const
{
	RP_D(const rp_image);
	return d->backend->palette();
}

/**
 * Get the image palette.
 * @return Pointer to image palette, or nullptr if not a paletted image.
 */
uint32_t *rp_image::palette(void)
{
	RP_D(rp_image);
	return d->backend->palette();
}

/**
 * Get the number of elements in the image palette.
 * @return Number of elements in the image palette, or 0 if not a paletted image.
 */
int rp_image::palette_len(void) const
{
	RP_D(const rp_image);
	return d->backend->palette_len();
}

/**
 * Get the index of the transparency color in the palette.
 * This is useful for images that use a single transparency
 * color instead of alpha transparency.
 * @return Transparent color index, or -1 if ARGB32 is used or the palette has alpha transparent colors.
 */
int rp_image::tr_idx(void) const
{
	RP_D(const rp_image);
	assert(d->backend->format == FORMAT_CI8);
	if (d->backend->format != FORMAT_CI8)
		return -1;

	return d->backend->tr_idx;
}

/**
 * Set the index of the transparency color in the palette.
 * This is useful for images that use a single transparency
 * color instead of alpha transparency.
 * @param tr_idx Transparent color index. (Set to -1 if the palette has alpha transparent colors.)
 */
void rp_image::set_tr_idx(int tr_idx)
{
	RP_D(rp_image);
	assert(d->backend->format == FORMAT_CI8);
	assert(tr_idx >= -1 && tr_idx < d->backend->palette_len());

	if (d->backend->format == FORMAT_CI8 &&
	    tr_idx >= -1 && tr_idx < d->backend->palette_len())
	{
		d->backend->tr_idx = tr_idx;
	}
}

/**
* Get the name of a format
* @param format Format.
* @return String containing the user-friendly name of a format.
*/
const char *rp_image::getFormatName(Format format)
{
	assert(format >= FORMAT_NONE && format < FORMAT_MAX);
	if (format < FORMAT_NONE || format >= FORMAT_MAX) {
		return nullptr;
	}

	static const char format_names[][8] = {
		"None",
		"CI8",
		"ARGB32",
	};
	static_assert(ARRAY_SIZE(format_names) == FORMAT_MAX,
		"format_names[] needs to be updated.");

	return format_names[format];
}

/** Metadata. **/

/**
 * Set the number of significant bits per channel.
 * @param sBIT	[in] sBIT_t struct.
 */
void rp_image::set_sBIT(const sBIT_t *sBIT)
{
	RP_D(rp_image);
	if (sBIT) {
		d->sBIT = *sBIT;
		d->has_sBIT = true;
	} else {
		d->has_sBIT = false;
	}
}

/**
 * Get the number of significant bits per channel.
 * @param sBIT	[out,opt] sBIT_t struct.
 * @return 0 on success; non-zero if not set or error.
 */
int rp_image::get_sBIT(sBIT_t *sBIT) const
{
	RP_D(const rp_image);
	assert(sBIT != nullptr);
	if (!d->has_sBIT) {
		// sBIT data isn't set.
		return -ENOENT;
	}
	if (sBIT) {
		*sBIT = d->sBIT;
	}
	return 0;
}

/**
 * Clear the sBIT data.
 */
void rp_image::clear_sBIT(void)
{
	RP_D(rp_image);
	d->has_sBIT = false;
}

}
