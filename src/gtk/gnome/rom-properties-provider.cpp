/***************************************************************************
 * ROM Properties Page shell extension. (GNOME)                            *
 * rom-properties-provider.cpp: Nautilus Provider Definition.              *
 *                                                                         *
 * Copyright (c) 2017-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

/**
 * References:
 * - audio-tags plugin for Xfce/Thunar
 * - http://api.xfce.m8t.in/xfce-4.10/thunarx-1.4.0/ThunarxPropertyPage.html
 * - https://developer.gnome.org/libnautilus-extension//3.16/libnautilus-extension-nautilus-property-page.html
 * - https://developer.gnome.org/libnautilus-extension//3.16/libnautilus-extension-nautilus-property-page-provider.html
 * - https://github.com/GNOME/nautilus/blob/bb433582165da10ab07337d707ea448703c3865f/src/nautilus-image-properties-page.c
 */

#include "rom-properties-provider.hpp"

// librpbase
#include "librpbase/file/RpFile.hpp"
#include "librpbase/RomData.hpp"
using namespace LibRpBase;

// libi18n
#include "libi18n/i18n.h"

// libromdata
#include "libromdata/RomDataFactory.hpp"
using LibRomData::RomDataFactory;

#include "../RomDataView.hpp"

#if defined(RP_UI_GTK3_GNOME)
// GNOME 3 desktop
#elif defined(RP_UI_GTK3_MATE)
// MATE desktop (v1.18.0+; GTK+ 3.x)
typedef CajaPropertyPageProviderIface			NautilusPropertyPageProviderIface;
typedef CajaPropertyPageProvider			NautilusPropertyPageProvider;
typedef CajaPropertyPage				NautilusPropertyPage;
# define NAUTILUS_TYPE_PROPERTY_PAGE_PROVIDER		CAJA_TYPE_PROPERTY_PAGE_PROVIDER
# define NAUTILUS_FILE_INFO(obj)			CAJA_FILE_INFO(obj)
# define NAUTILUS_IS_FILE_INFO(obj)			CAJA_IS_FILE_INFO(obj)
# define nautilus_file_info_get_uri(file)		caja_file_info_get_uri(file)
# define nautilus_property_page_new(name, label, page)	caja_property_page_new((name), (label), (page))
#else
# error GTK3 desktop environment not set and/or supported.
#endif

static void   rom_properties_provider_page_provider_init	(NautilusPropertyPageProviderIface	*iface);
static GList *rom_properties_provider_get_pages			(NautilusPropertyPageProvider		*provider,
								 GList					*files);

struct _RomPropertiesProviderClass {
	GObjectClass __parent__;
};

struct _RomPropertiesProvider {
	GObject __parent__;
};

#if !GLIB_CHECK_VERSION(2,59,1)
# if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 2))
/* Disable GCC 8 -Wcast-function-type warnings. (Fixed in glib-2.59.1 upstream.) */
#  if (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6))
#   pragma GCC diagnostic push
#  endif
#  pragma GCC diagnostic ignored "-Wcast-function-type"
# endif
#endif /* !GLIB_CHECK_VERSION(2,59,1) */

// FIXME: G_DEFINE_DYNAMIC_TYPE() doesn't work in C++ mode with gcc-6.2
// due to an implicit int to GTypeFlags conversion.
G_DEFINE_DYNAMIC_TYPE_EXTENDED(RomPropertiesProvider, rom_properties_provider,
	G_TYPE_OBJECT, 0,
	G_IMPLEMENT_INTERFACE(NAUTILUS_TYPE_PROPERTY_PAGE_PROVIDER,
			rom_properties_provider_page_provider_init));

#if !GLIB_CHECK_VERSION(2,59,1)
# if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6))
#  pragma GCC diagnostic pop
# endif
#endif /* !GLIB_CHECK_VERSION(2,59,1) */

void
rom_properties_provider_register_type_ext(GTypeModule *module)
{
	rom_properties_provider_register_type(module);
}

static void
rom_properties_provider_class_init(RomPropertiesProviderClass *klass)
{
	RP_UNUSED(klass);
}

static void
rom_properties_provider_class_finalize(RomPropertiesProviderClass *klass)
{
	RP_UNUSED(klass);
}

static void
rom_properties_provider_init(RomPropertiesProvider *sbr_provider)
{
	RP_UNUSED(sbr_provider);
}

static void
rom_properties_provider_page_provider_init(NautilusPropertyPageProviderIface *iface)
{
	iface->get_pages = rom_properties_provider_get_pages;
}

static GList*
rom_properties_provider_get_pages(NautilusPropertyPageProvider *provider, GList *files)
{
	RP_UNUSED(provider);
	GList *pages = nullptr;
	GList *file;
	NautilusFileInfo *info;

	if (g_list_length(files) != 1) 
		return nullptr;

	file = g_list_first(files);
	if (G_UNLIKELY(file == nullptr))
		return nullptr;

	info = NAUTILUS_FILE_INFO(file->data);

	if (G_LIKELY(rom_properties_get_file_supported(info))) {
		// Get the filename.
		gchar *uri = nautilus_file_info_get_uri(info);
		gchar *filename = g_filename_from_uri(uri, nullptr, nullptr);
		g_free(uri);

		// Create the RomDataView.
		// NOTE: Unlike the Xfce/Thunar (GTK+ 2.x) version, we don't
		// need to subclass NautilusPropertyPage. Instead, we create a
		// NautilusPropertyPage and add a RomDataView widget to it.
		// TODO: Add some extra padding to the top...
		GtkWidget *romDataView = static_cast<GtkWidget*>(
			g_object_new(rom_data_view_get_type(), nullptr));
		rom_data_view_set_desc_format_type(ROM_DATA_VIEW(romDataView), RP_DFT_GNOME);
		rom_data_view_set_filename(ROM_DATA_VIEW(romDataView), filename);
		gtk_widget_show(romDataView);
		g_free(filename);

		// tr: Tab title.
		const char *const tabTitle = C_("RomDataView", "ROM Properties");

		// Create the NautilusPropertyPage.
		NautilusPropertyPage *page = nautilus_property_page_new(
			"RomPropertiesPage::property_page",
			gtk_label_new(tabTitle), romDataView);

		/* Add the page to the pages provided by this plugin */
		pages = g_list_prepend(pages, page);
	}

	return pages;
}

gboolean
rom_properties_get_file_supported(NautilusFileInfo *info)
{
	gchar *uri;
	gchar *filename;
	gboolean supported = false;

	g_return_val_if_fail(info != nullptr || NAUTILUS_IS_FILE_INFO(info), false);

	// TODO: Support for gvfs.
	uri = nautilus_file_info_get_uri(info);
	filename = g_filename_from_uri(uri, nullptr, nullptr);
	g_free(uri);

	if (G_UNLIKELY(filename == nullptr))
		return false;

	// TODO: Check file extensions and/or MIME types?

	// Open the ROM file.
	RpFile *const file = new RpFile(filename, RpFile::FM_OPEN_READ_GZ);
	if (file->isOpen()) {
		// Is this ROM file supported?
		// NOTE: We have to create an instance here in order to
		// prevent false positives caused by isRomSupported()
		// saying "yes" while new RomData() says "no".
		RomData *const romData = RomDataFactory::create(file);
		if (romData != nullptr) {
			supported = true;
			romData->unref();
		}
	}
	file->unref();

	g_free(filename);
	return supported;
}
