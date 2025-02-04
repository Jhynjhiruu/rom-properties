/***************************************************************************
 * ROM Properties Page shell extension. (GTK+ common)                      *
 * PIMGTYPE.hpp: PIMGTYPE typedef and wrapper functions.                   *
 *                                                                         *
 * Copyright (c) 2017-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#ifndef __ROMPROPERTIES_GTK_PIMGTYPE_HPP__
#define __ROMPROPERTIES_GTK_PIMGTYPE_HPP__

#include <gtk/gtk.h>

#include "librpbase/img/rp_image.hpp"

// NOTE: GTK+ 3.x earlier than 3.10 is not supported.
#if GTK_CHECK_VERSION(3,0,0) && !GTK_CHECK_VERSION(3,10,0)
# error GTK+ 3.x earlier than 3.10 is not supported.
#endif

#if GTK_CHECK_VERSION(3,10,0)
# define RP_GTK_USE_CAIRO 1
# include "CairoImageConv.hpp"
# include <cairo-gobject.h>
# define PIMGTYPE_GOBJECT_TYPE CAIRO_GOBJECT_TYPE_SURFACE
# define GTK_CELL_RENDERER_PIXBUF_PROPERTY "surface"
# ifdef __cplusplus
extern "C"
# endif
typedef cairo_surface_t *PIMGTYPE;
#else
# include "GdkImageConv.hpp"
# define PIMGTYPE_GOBJECT_TYPE GDK_TYPE_PIXBUF
# define GTK_CELL_RENDERER_PIXBUF_PROPERTY "pixbuf"
# ifdef __cplusplus
extern "C"
# endif
typedef GdkPixbuf *PIMGTYPE;
#endif

#ifdef __cplusplus
// rp_image_to_PIMGTYPE wrapper function.
static inline PIMGTYPE rp_image_to_PIMGTYPE(const LibRpBase::rp_image *img)
{
#ifdef RP_GTK_USE_CAIRO
	return CairoImageConv::rp_image_to_cairo_surface_t(img);
#else /* !RP_GTK_USE_CAIRO */
	return GdkImageConv::rp_image_to_GdkPixbuf(img);
#endif /* RP_GTK_USE_CAIRO */
}
#endif /* __cplusplus **/

#ifdef __cplusplus
extern "C" {
#endif

// gtk_image_set_from_PIMGTYPE wrapper function.
static inline void gtk_image_set_from_PIMGTYPE(GtkImage *image, PIMGTYPE pImgType)
{
#ifdef RP_GTK_USE_CAIRO
	gtk_image_set_from_surface(image, pImgType);
#else /* !RP_GTK_USE_CAIRO */
	gtk_image_set_from_pixbuf(image, pImgType);
#endif /* RP_GTK_USE_CAIRO */
}

// PIMGTYPE free() wrapper function.
static inline void PIMGTYPE_destroy(PIMGTYPE pImgType)
{
#ifdef RP_GTK_USE_CAIRO
	cairo_surface_destroy(pImgType);
#else /* !RP_GTK_USE_CAIRO */
	g_object_unref(pImgType);
#endif /* RP_GTK_USE_CAIRO */
}

/**
 * PIMGTYPE size comparison function.
 * @param pImgType PIMGTYPE
 * @param width Expected width
 * @param height Expected height
 * @return True if the size matches; false if not.
 */
static inline bool PIMGTYPE_size_check(PIMGTYPE pImgType, int width, int height)
{
#ifdef RP_GTK_USE_CAIRO
	return (cairo_image_surface_get_width(pImgType)  == width &&
	        cairo_image_surface_get_height(pImgType) == height);
#else /* !RP_GTK_USE_CAIRO */
	return (gdk_pixbuf_get_width(pImgType)  == width &&
	        gdk_pixbuf_get_height(pImgType) == height);
#endif /* RP_GTK_USE_CAIRO */
}

#ifdef RP_GTK_USE_CAIRO
/**
 * PIMGTYPE scaling function.
 * @param pImgType PIMGTYPE
 * @param width New width
 * @param height New height
 * @param bilinear If true, use bilinear interpolation.
 * @return Rescaled image. (If unable to rescale, returns a new reference to pImgType.)
 */
PIMGTYPE PIMGTYPE_scale(PIMGTYPE pImgType, int width, int height, bool bilinear);
#else /* !RP_GTK_USE_CAIRO */
/**
 * PIMGTYPE scaling function.
 * @param pImgType PIMGTYPE
 * @param width New width
 * @param height New height
 * @param bilinear If true, use bilinear interpolation.
 * @return Rescaled image. (If unable to rescale, returns a new reference to pImgType.)
 */
static inline PIMGTYPE PIMGTYPE_scale(PIMGTYPE pImgType, int width, int height, bool bilinear)
{
	return gdk_pixbuf_scale_simple(pImgType, width, height,
		(bilinear ? GDK_INTERP_BILINEAR : GDK_INTERP_NEAREST));
}
#endif /* RP_GTK_USE_CAIRO */

#ifdef __cplusplus
}
#endif

#endif /* __ROMPROPERTIES_GTK_PIMGTYPE_HPP__ */
