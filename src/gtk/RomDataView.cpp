/***************************************************************************
 * ROM Properties Page shell extension. (GTK+ common)                      *
 * RomDataView.cpp: RomData viewer widget.                                 *
 *                                                                         *
 * Copyright (c) 2017-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "RomDataView.hpp"
#include "rp-gtk-enums.h"

// librpbase
#include "librpbase/common.h"
#include "librpbase/TextFuncs.hpp"
#include "librpbase/RomData.hpp"
#include "librpbase/RomFields.hpp"
#include "librpbase/file/RpFile.hpp"
#include "librpbase/img/rp_image.hpp"
#include "librpbase/img/IconAnimData.hpp"
#include "librpbase/img/IconAnimHelper.hpp"
using namespace LibRpBase;

// libi18n
#include "libi18n/i18n.h"

// libromdata
#include "libromdata/RomDataFactory.hpp"
using LibRomData::RomDataFactory;

// C includes. (C++ namespace)
#include <cassert>

// C++ includes.
#include <string>
#include <vector>
using std::string;
using std::vector;

// PIMGTYPE
#include "PIMGTYPE.hpp"

// GTK+ 2.x compatibility macros.
// Reference: https://github.com/kynesim/wireshark/blob/master/ui/gtk/old-gtk-compat.h
#if !GTK_CHECK_VERSION(3,0,0)
static inline GtkWidget *gtk_tree_view_column_get_button(GtkTreeViewColumn *tree_column)
{
	/* This is too late, see https://bugzilla.gnome.org/show_bug.cgi?id=641089
	 * According to
	 * http://ftp.acc.umu.se/pub/GNOME/sources/gtk+/2.13/gtk+-2.13.4.changes
	 * access to the button element was sealed during 2.13. They also admit that
	 * they missed a use case and thus failed to provide an accessor function:
	 * http://mail.gnome.org/archives/commits-list/2010-December/msg00578.html
	 * An accessor function was finally added in 3.0.
	 */
# if (GTK_CHECK_VERSION(2,14,0) && defined(GSEAL_ENABLE))
	return tree_column->_g_sealed__button;
# else
	return tree_column->button;
# endif
}
#endif /* !GTK_CHECK_VERSION(3,0,0) */


// References:
// - audio-tags plugin
// - http://api.xfce.m8t.in/xfce-4.10/thunarx-1.4.0/ThunarxPropertyPage.html

/* Property identifiers */
enum {
	PROP_0,
	PROP_FILENAME,
	PROP_DESC_FORMAT_TYPE,
	PROP_LAST
};

static void	rom_data_view_dispose		(GObject	*object);
static void	rom_data_view_finalize		(GObject	*object);
static void	rom_data_view_get_property	(GObject	*object,
						 guint		 prop_id,
						 GValue		*value,
						 GParamSpec	*pspec);
static void	rom_data_view_set_property	(GObject	*object,
						 guint		 prop_id,
						 const GValue	*value,
						 GParamSpec	*pspec);
// TODO: Make 'page' the first argument?
static void	rom_data_view_filename_changed	(const gchar 	*filename,
						 RomDataView	*page);
static void	rom_data_view_desc_format_type_changed(RpDescFormatType desc_format_type,
						 RomDataView	*page);

static void	rom_data_view_init_header_row	(RomDataView	*page);
static void	rom_data_view_update_display	(RomDataView	*page);
static gboolean	rom_data_view_load_rom_data	(gpointer	 data);
static void	rom_data_view_delete_tabs	(RomDataView	*page);

/** Signal handlers. **/
static void	checkbox_no_toggle_signal_handler   (GtkToggleButton	*togglebutton,
						     gpointer		 user_data);
static void	rom_data_view_map_signal_handler    (RomDataView	*page,
						     gpointer		 user_data);
static void	rom_data_view_unmap_signal_handler  (RomDataView	*page,
						     gpointer		 user_data);
static void	tree_view_realize_signal_handler    (GtkTreeView	*treeView,
						     RomDataView	*page);

/** Icon animation timer. **/
static void	start_anim_timer(RomDataView *page);
static void	stop_anim_timer (RomDataView *page);
static gboolean	anim_timer_func (RomDataView *page);

// XFCE property page class.
struct _RomDataViewClass {
#if GTK_CHECK_VERSION(3,0,0)
	GtkBoxClass __parent__;
#else
	GtkVBoxClass __parent__;
#endif
};

// XFCE property page.
struct _RomDataView {
#if GTK_CHECK_VERSION(3,0,0)
	GtkBox __parent__;
#else
	GtkVBox __parent__;
#endif

	/* Timeouts */
	guint		changed_idle;

	// Header row.
	GtkWidget	*hboxHeaderRow;
	GtkWidget	*lblSysInfo;
	GtkWidget	*imgIcon;
	GtkWidget	*imgBanner;

	// Filename.
	gchar		*filename;

	// ROM data.
	RomData		*romData;

	// Animated icon data.
	// TODO: Use std::array<>?
	PIMGTYPE	iconFrames[IconAnimData::MAX_FRAMES];
	IconAnimHelper	*iconAnimHelper;
	int		last_frame_number;	// Last frame number.

	// Icon animation timer.
	guint		tmrIconAnim;
	int		last_delay;		// Last delay value.

	// Tab layout.
	GtkWidget	*tabWidget;
	struct tab {
		GtkWidget	*vbox;		// Either page or a GtkVBox/GtkBox.
		GtkWidget	*table;		// GtkTable (2.x); GtkGrid (3.x)
		GtkWidget	*lblCredits;
	};
	vector<tab> *tabs;

	// Description labels.
	RpDescFormatType		desc_format_type;
	vector<GtkWidget*>		*vecDescLabels;
};

// FIXME: G_DEFINE_TYPE() doesn't work in C++ mode with gcc-6.2
// due to an implicit int to GTypeFlags conversion.
#if GTK_CHECK_VERSION(3,0,0)
//G_DEFINE_TYPE(RomDataView, rom_data_view, GTK_TYPE_BOX);
G_DEFINE_TYPE_EXTENDED(RomDataView, rom_data_view,
	GTK_TYPE_BOX, static_cast<GTypeFlags>(0), {});
#else
//G_DEFINE_TYPE(RomDataView, rom_data_view, GTK_TYPE_VBOX);
G_DEFINE_TYPE_EXTENDED(RomDataView, rom_data_view,
	GTK_TYPE_VBOX, static_cast<GTypeFlags>(0), {});
#endif

static GParamSpec *properties[PROP_LAST];

static void
rom_data_view_class_init(RomDataViewClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
	gobject_class->dispose = rom_data_view_dispose;
	gobject_class->finalize = rom_data_view_finalize;
	gobject_class->get_property = rom_data_view_get_property;
	gobject_class->set_property = rom_data_view_set_property;

	/**
	 * RomDataView:filename:
	 *
	 * The name of the file being displayed on this page.
	 **/
	properties[PROP_FILENAME] = g_param_spec_string(
		"filename", "Filename", "Filename of the ROM image being displayed.",
		nullptr,
		(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	/**
	 * RomDataView:desc_format_type:
	 *
	 * The formatting to use for description labels.
	 **/
	properties[PROP_DESC_FORMAT_TYPE] = g_param_spec_enum(
		"desc-format-type", "desc-format-type",
		"Description format type.",
		RP_TYPE_DESC_FORMAT_TYPE,
		RP_DFT_XFCE,
		(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	// Install the properties.
	g_object_class_install_property(gobject_class, PROP_FILENAME, properties[PROP_FILENAME]);
	g_object_class_install_property(gobject_class, PROP_DESC_FORMAT_TYPE, properties[PROP_DESC_FORMAT_TYPE]);
}

/**
 * Set the label format type.
 * @param page RomDataView.
 * @param label GtkLabel.
 * @param desc_format_type Format type.
 */
static inline void
set_label_format_type(GtkLabel *label, RpDescFormatType desc_format_type)
{
	PangoAttrList *attr_lst = pango_attr_list_new();

	// Check if this label has the "Warning" flag set.
	const gboolean is_warning = (gboolean)GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(label), "RFT_STRING_warning"));
	if (is_warning) {
		// Use the "Warning" format.
		pango_attr_list_insert(attr_lst,
			pango_attr_weight_new(PANGO_WEIGHT_BOLD));
		pango_attr_list_insert(attr_lst,
			pango_attr_foreground_new(65535, 0, 0));
	}

	// Check for DE-specific formatting.
	switch (desc_format_type) {
		case RP_DFT_XFCE:
		default:
			// XFCE style.
			// NOTE: No changes between GTK+ 2.x and 3.x.

			// Text alignment: Right
			gtk_label_set_justify(label, GTK_JUSTIFY_RIGHT);
#if GTK_CHECK_VERSION(3,16,0)
			// NOTE: gtk_widget_set_?align() doesn't work properly
			// when using a GtkSizeGroup on GTK+ 3.x.
			// gtk_label_set_?align() was introduced in GTK+ 3.16.
			gtk_label_set_xalign(label, 1.0);
			gtk_label_set_yalign(label, 0.0);
#else
			// NOTE: GtkMisc is deprecated on GTK+ 3.x, but it's
			// needed for proper text alignment when using
			// GtkSizeGroup prior to GTK+ 3.16.
			gtk_misc_set_alignment(GTK_MISC(label), 1.0f, 0.0f);
#endif

			if (!is_warning) {
				// Text style: Bold
				pango_attr_list_insert(attr_lst,
					pango_attr_weight_new(PANGO_WEIGHT_BOLD));
			}
			break;

		case RP_DFT_GNOME:
			// GNOME style. (Also used for MATE.)
			// TODO: Changes for GNOME 2.

			// Text alignment: Left
			gtk_label_set_justify(label, GTK_JUSTIFY_LEFT);
#if GTK_CHECK_VERSION(3,16,0)
			// NOTE: gtk_widget_set_?align() doesn't work properly
			// when using a GtkSizeGroup on GTK+ 3.x.
			// gtk_label_set_?align() was introduced in GTK+ 3.16.
			gtk_label_set_xalign(label, 0.0);
			gtk_label_set_yalign(label, 0.0);
#else
			// NOTE: GtkMisc is deprecated on GTK+ 3.x, but it's
			// needed for proper text alignment when using
			// GtkSizeGroup prior to GTK+ 3.16.
			gtk_misc_set_alignment(GTK_MISC(label), 0.0f, 0.0f);
#endif

			// Text style: Normal (no Pango attributes)
			break;
	}

	gtk_label_set_attributes(label, attr_lst);
	pango_attr_list_unref(attr_lst);
}

static void
rom_data_view_init(RomDataView *page)
{
	// No ROM data initially.
	page->filename = nullptr;
	page->romData = nullptr;
	page->last_frame_number = 0;
	page->iconAnimHelper = new IconAnimHelper();
	page->tabWidget = nullptr;
	page->tabs = new vector<RomDataView::tab>();

	page->desc_format_type = RP_DFT_XFCE;
	page->vecDescLabels = new vector<GtkWidget*>();

	// Animation timer.
	page->tmrIconAnim = 0;
	page->last_delay = 0;

	/**
	 * Base class is:
	 * - GTK+ 2.x: GtkVBox
	 * - GTK+ 3.x: GtkBox
	 */

	// NOTE: This matches Thunar (GTK+2) and Nautilus (GTK+3).
	g_object_set(page, "border-width", 8, nullptr);

#if GTK_CHECK_VERSION(3,0,0)
	// Make this a VBox.
	gtk_orientable_set_orientation(GTK_ORIENTABLE(page), GTK_ORIENTATION_VERTICAL);

	// Header row.
	page->hboxHeaderRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(page->hboxHeaderRow, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(page), page->hboxHeaderRow, false, false, 0);
	gtk_widget_show(page->hboxHeaderRow);
#else
	// Center-align the header row.
	GtkWidget *centerAlign = gtk_alignment_new(0.5f, 0.0f, 0.0f, 0.0f);
	gtk_box_pack_start(GTK_BOX(page), centerAlign, false, false, 0);
	gtk_widget_show(centerAlign);

	// Header row.
	page->hboxHeaderRow = gtk_hbox_new(false, 8);
	gtk_container_add(GTK_CONTAINER(centerAlign), page->hboxHeaderRow);
	gtk_widget_show(page->hboxHeaderRow);
#endif

	// System information.
	page->lblSysInfo = gtk_label_new(nullptr);
	gtk_label_set_justify(GTK_LABEL(page->lblSysInfo), GTK_JUSTIFY_CENTER);
	gtk_box_pack_start(GTK_BOX(page->hboxHeaderRow), page->lblSysInfo, false, false, 0);
	gtk_widget_show(page->lblSysInfo);

	// Banner.
	page->imgBanner = gtk_image_new();
	gtk_box_pack_start(GTK_BOX(page->hboxHeaderRow), page->imgBanner, false, false, 0);

	// Icon.
	page->imgIcon = gtk_image_new();
	gtk_box_pack_start(GTK_BOX(page->hboxHeaderRow), page->imgIcon, false, false, 0);

	// Make lblSysInfo bold.
	PangoAttrList *attr_lst = pango_attr_list_new();
	pango_attr_list_insert(attr_lst,
		pango_attr_weight_new(PANGO_WEIGHT_BOLD));
	gtk_label_set_attributes(GTK_LABEL(page->lblSysInfo), attr_lst);
	pango_attr_list_unref(attr_lst);

	// Connect the "map" and "unmap" signals.
	// These are needed in order to start and stop the animation.
	g_signal_connect(page, "map",
		reinterpret_cast<GCallback>(rom_data_view_map_signal_handler), nullptr);
	g_signal_connect(page, "unmap",
		reinterpret_cast<GCallback>(rom_data_view_unmap_signal_handler), nullptr);

	// Table layout is created in rom_data_view_update_display().
}

static void
rom_data_view_dispose(GObject *object)
{
	RomDataView *page = ROM_DATA_VIEW(object);

	/* Unregister the changed_idle */
	if (G_UNLIKELY(page->changed_idle > 0)) {
		g_source_remove(page->changed_idle);
		page->changed_idle = 0;
	}

	// Delete the animation timer.
	if (page->tmrIconAnim > 0) {
		g_source_remove(page->tmrIconAnim);
		page->tmrIconAnim = 0;
	}

	// Delete the icon frames and tabs.
	rom_data_view_delete_tabs(page);

	// Call the superclass dispose() function.
	G_OBJECT_CLASS(rom_data_view_parent_class)->dispose(object);
}

static void
rom_data_view_finalize(GObject *object)
{
	RomDataView *page = ROM_DATA_VIEW(object);

	// Free the filename.
	g_free(page->filename);

	// Delete the C++ objects.
	delete page->iconAnimHelper;
	delete page->tabs;
	delete page->vecDescLabels;

	// Unreference romData.
	if (page->romData) {
		page->romData->unref();
	}

	// Call the superclass finalize() function.
	G_OBJECT_CLASS(rom_data_view_parent_class)->finalize(object);
}

GtkWidget*
rom_data_view_new(void)
{
	return static_cast<GtkWidget*>(g_object_new(TYPE_ROM_DATA_VIEW, nullptr));
}

static void
rom_data_view_get_property(GObject	*object,
			   guint	 prop_id,
			   GValue	*value,
			   GParamSpec	*pspec)
{
	RomDataView *page = ROM_DATA_VIEW(object);

	switch (prop_id) {
		case PROP_FILENAME:
			g_value_set_string(value, page->filename);
			break;

		case PROP_DESC_FORMAT_TYPE:
			g_value_set_enum(value, page->desc_format_type);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

static void
rom_data_view_set_property(GObject	*object,
			   guint	 prop_id,
			   const GValue	*value,
			   GParamSpec	*pspec)
{
	RomDataView *page = ROM_DATA_VIEW(object);

	switch (prop_id) {
		case PROP_FILENAME:
			rom_data_view_set_filename(page, g_value_get_string(value));
			break;

		case PROP_DESC_FORMAT_TYPE:
			rom_data_view_set_desc_format_type(page,
				static_cast<RpDescFormatType>(g_value_get_enum(value)));
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

/**
 * rom_data_view_get_filename:
 * @page : a #RomDataView.
 *
 * Returns the current filename for the @page.
 *
 * Return value: the file associated with this property page.
 **/
const gchar*
rom_data_view_get_filename(RomDataView *page)
{
	g_return_val_if_fail(IS_ROM_DATA_VIEW(page), nullptr);
	return page->filename;
}

/**
 * rom_data_view_set_filename:
 * @page : a #RomDataView.
 * @filename : a filename.
 *
 * Sets the filename for this @page.
 **/
void
rom_data_view_set_filename(RomDataView	*page,
			   const gchar	*filename)
{
	g_return_if_fail(IS_ROM_DATA_VIEW(page));

	/* Check if we already use this file */
	if (G_UNLIKELY(!g_strcmp0(page->filename, filename)))
		return;

	/* Disconnect from the previous file (if any) */
	if (G_LIKELY(page->filename != nullptr)) {
		g_free(page->filename);
		page->filename = nullptr;

		// Unreference the existing RomData object.
		if (page->romData) {
			page->romData->unref();
			page->romData = nullptr;
		}

		// Delete the icon frames and tabs.
		rom_data_view_delete_tabs(page);
	}

	/* Assign the value */
	page->filename = g_strdup(filename);

	/* Connect to the new file (if any) */
	if (G_LIKELY(page->filename != nullptr)) {
		rom_data_view_filename_changed(page->filename, page);
	} else {
		// Hide the header row
		if (page->hboxHeaderRow) {
			gtk_widget_hide(page->hboxHeaderRow);
		}
	}

	// Filename has been changed.
	g_object_notify_by_pspec(G_OBJECT(page), properties[PROP_FILENAME]);
}

static void
rom_data_view_filename_changed(const gchar	*filename,
			       RomDataView	*page)
{
	g_return_if_fail(filename != nullptr);
	g_return_if_fail(IS_ROM_DATA_VIEW(page));
	g_return_if_fail(!g_strcmp0(page->filename, filename));

	if (page->changed_idle == 0) {
		page->changed_idle = g_idle_add(rom_data_view_load_rom_data, page);
	}
}

RpDescFormatType
rom_data_view_get_desc_format_type(RomDataView *page)
{
	g_return_val_if_fail(IS_ROM_DATA_VIEW(page), RP_DFT_XFCE);
	return page->desc_format_type;
}

void
rom_data_view_set_desc_format_type(RomDataView *page, RpDescFormatType desc_format_type)
{
	g_return_if_fail(IS_ROM_DATA_VIEW(page));
	g_return_if_fail(desc_format_type >= RP_DFT_XFCE && desc_format_type < RP_DFT_LAST);
	if (desc_format_type == page->desc_format_type) {
		// Nothing to change.
		// NOTE: g_return_if_fail() prints an assertion warning,
		// so we can't use that for this check.
		return;
	}

	page->desc_format_type = desc_format_type;
	rom_data_view_desc_format_type_changed(desc_format_type, page);
	g_object_notify_by_pspec(G_OBJECT(page), properties[PROP_DESC_FORMAT_TYPE]);
}

static void
rom_data_view_desc_format_type_changed(RpDescFormatType	desc_format_type,
				       RomDataView	*page)
{
	g_return_if_fail(IS_ROM_DATA_VIEW(page));
	g_return_if_fail(desc_format_type >= RP_DFT_XFCE && desc_format_type < RP_DFT_LAST);

	for (auto iter = page->vecDescLabels->cbegin();
	     iter != page->vecDescLabels->cend(); ++iter)
	{
		set_label_format_type(GTK_LABEL(*iter), desc_format_type);
	}
}

static void
rom_data_view_init_header_row(RomDataView *page)
{
	// Initialize the header row.
	assert(page != nullptr);

	// NOTE: romData might be nullptr in some cases.
	//assert(page->romData != nullptr);
	if (!page->romData) {
		// No ROM data.
		// Hide the widgets.
		gtk_widget_hide(page->hboxHeaderRow);
		return;
	}

	// System name and file type.
	// TODO: System logo and/or game title?
	const char *systemName = page->romData->systemName(
		RomData::SYSNAME_TYPE_LONG | RomData::SYSNAME_REGION_ROM_LOCAL);
	const char *fileType = page->romData->fileType_string();
	assert(systemName != nullptr);
	assert(fileType != nullptr);
	if (!systemName) {
		systemName = C_("RomDataView", "(unknown system)");
	}
	if (!fileType) {
		fileType = C_("RomDataView", "(unknown filetype)");
	}

	const string sysInfo = rp_sprintf_p(
		// tr: %1$s == system name, %2$s == file type
		C_("RomDataView", "%1$s\n%2$s"), systemName, fileType);
	gtk_label_set_text(GTK_LABEL(page->lblSysInfo), sysInfo.c_str());

	// Supported image types.
	const uint32_t imgbf = page->romData->supportedImageTypes();

	// Banner.
	gtk_widget_hide(page->imgBanner);
	if (imgbf & RomData::IMGBF_INT_BANNER) {
		// Get the banner.
		const rp_image *banner = page->romData->image(RomData::IMG_INT_BANNER);
		if (banner && banner->isValid()) {
			PIMGTYPE pImgType = rp_image_to_PIMGTYPE(banner);
			if (pImgType) {
				gtk_image_set_from_PIMGTYPE(GTK_IMAGE(page->imgBanner), pImgType);
				PIMGTYPE_destroy(pImgType);
				gtk_widget_show(page->imgBanner);
			}
		}
	}

	// Icon.
	gtk_widget_hide(page->imgIcon);
	if (imgbf & RomData::IMGBF_INT_ICON) {
		// Get the icon.
		const rp_image *icon = page->romData->image(RomData::IMG_INT_ICON);
		if (icon && icon->isValid()) {
			// Is this an animated icon?
			const IconAnimData *const iconAnimData = page->romData->iconAnimData();
			if (iconAnimData) {
				// Convert the icons to PIMGTYPE.
				for (int i = iconAnimData->count-1; i >= 0; i--) {
					const rp_image *const frame = iconAnimData->frames[i];
					if (frame && frame->isValid()) {
						PIMGTYPE pImgType = rp_image_to_PIMGTYPE(frame);
						if (pImgType) {
							page->iconFrames[i] = pImgType;
						}
					}
				}

				// Set up the IconAnimHelper.
				page->iconAnimHelper->setIconAnimData(iconAnimData);
				// Initialize the animation.
				page->last_frame_number = page->iconAnimHelper->frameNumber();

				// Show the first frame.
				gtk_image_set_from_PIMGTYPE(GTK_IMAGE(page->imgIcon),
					page->iconFrames[page->last_frame_number]);
				gtk_widget_show(page->imgIcon);

				// Icon animation timer is set in start_anim_timer().
			} else {
				// Not an animated icon.
				page->last_frame_number = 0;
				PIMGTYPE pImgType = rp_image_to_PIMGTYPE(icon);
				if (pImgType) {
					gtk_image_set_from_PIMGTYPE(GTK_IMAGE(page->imgIcon), pImgType);
					page->iconFrames[0] = pImgType;
					gtk_widget_show(page->imgIcon);
				}
			}
		}
	}

	// Show the header row.
	gtk_widget_show(page->hboxHeaderRow);
}

#if GTK_CHECK_VERSION(3,0,0)
#define GTK_WIDGET_HALIGN_LEFT(widget)		gtk_widget_set_halign((widget), GTK_ALIGN_START)
#define GTK_WIDGET_HALIGN_CENTER(widget)	gtk_widget_set_halign((widget), GTK_ALIGN_CENTER)
#define GTK_WIDGET_HALIGN_RIGHT(widget)		gtk_widget_set_halign((widget), GTK_ALIGN_END)
#else
#define GTK_WIDGET_HALIGN_LEFT(widget)		gtk_misc_set_alignment(GTK_MISC(widget), 0.0f, 0.0f)
#define GTK_WIDGET_HALIGN_CENTER(widget)	gtk_misc_set_alignment(GTK_MISC(widget), 0.5f, 0.0f)
#define GTK_WIDGET_HALIGN_RIGHT(widget)		gtk_misc_set_alignment(GTK_MISC(widget), 1.0f, 0.0f)
#endif

/**
 * Initialize a string field.
 * @param page	[in] RomDataView object.
 * @param field	[in] RomFields::Field
 * @param str	[in,opt] String data. (If nullptr, field data is used.)
 * @return Display widget, or nullptr on error.
 */
static GtkWidget*
rom_data_view_init_string(RomDataView *page, const RomFields::Field *field, const char *str = nullptr)
{
	// String type.
	GtkWidget *widget = gtk_label_new(nullptr);
	gtk_label_set_use_underline(GTK_LABEL(widget), false);
	gtk_widget_show(widget);

	if (!str && field->data.str) {
		str = field->data.str->c_str();
	}

	if (field->type == RomFields::RFT_STRING &&
	    (field->desc.flags & RomFields::STRF_CREDITS))
	{
		// Credits text. Enable formatting and center alignment.
		gtk_label_set_justify(GTK_LABEL(widget), GTK_JUSTIFY_CENTER);
		GTK_WIDGET_HALIGN_CENTER(widget);
		if (str) {
			// NOTE: Pango markup does not support <br/>.
			// It uses standard newlines for line breaks.
			gtk_label_set_markup(GTK_LABEL(widget), str);
		}
	} else {
		// Standard text with no formatting.
		gtk_label_set_selectable(GTK_LABEL(widget), true);
		gtk_label_set_justify(GTK_LABEL(widget), GTK_JUSTIFY_LEFT);
		GTK_WIDGET_HALIGN_LEFT(widget);
		if (str) {
			gtk_label_set_text(GTK_LABEL(widget), str);
		}
	}

	// Check for any formatting options. (RFT_STRING only)
	if (field->type == RomFields::RFT_STRING && field->desc.flags != 0) {
		PangoAttrList *const attr_lst = pango_attr_list_new();

		// Monospace font?
		if (field->desc.flags & RomFields::STRF_MONOSPACE) {
			pango_attr_list_insert(attr_lst,
				pango_attr_family_new("monospace"));
		}

		// "Warning" font?
		if (field->desc.flags & RomFields::STRF_WARNING) {
			pango_attr_list_insert(attr_lst,
				pango_attr_weight_new(PANGO_WEIGHT_BOLD));
			pango_attr_list_insert(attr_lst,
				pango_attr_foreground_new(65535, 0, 0));
		}

		gtk_label_set_attributes(GTK_LABEL(widget), attr_lst);
		pango_attr_list_unref(attr_lst);

		if (field->desc.flags & RomFields::STRF_CREDITS) {
			// Credits row goes at the end.
			// There should be a maximum of one STRF_CREDITS per tab.
			auto &tab = page->tabs->at(field->tabIdx);
			assert(tab.lblCredits == nullptr);

			// Credits row.
			gtk_box_pack_end(GTK_BOX(tab.vbox), widget, false, false, 0);

			// NULL out widget to hide the description field.
			// NOTE: Not destroying the widget since we still
			// need it to be displayed.
			widget = nullptr;
		}
	}

	return widget;
}

/**
 * Initialize a bitfield.
 * @param page	[in] RomDataView object.
 * @param field	[in] RomFields::Field
 * @return Display widget, or nullptr on error.
 */
static GtkWidget*
rom_data_view_init_bitfield(RomDataView *page, const RomFields::Field *field)
{
	// Bitfield type. Create a grid of checkboxes.
	// TODO: Description label needs some padding on the top...
	const auto &bitfieldDesc = field->desc.bitfield;

	int count = (int)bitfieldDesc.names->size();
	assert(count <= 32);
	if (count > 32)
		count = 32;

#if GTK_CHECK_VERSION(3,0,0)
	GtkWidget *widget = gtk_grid_new();
	//gtk_grid_set_row_spacings(GTK_TABLE(widget), 2);
	//gtk_grid_set_column_spacings(GTK_TABLE(widget), 8);
#else
	// Determine the total number of rows and columns.
	int totalRows, totalCols;
	if (bitfieldDesc.elemsPerRow == 0) {
		// All elements are in one row.
		totalCols = count;
		totalRows = 1;
	} else {
		// Multiple rows.
		totalCols = bitfieldDesc.elemsPerRow;
		totalRows = count / bitfieldDesc.elemsPerRow;
		if ((count % bitfieldDesc.elemsPerRow) > 0) {
			totalRows++;
		}
	}

	GtkWidget *widget = gtk_table_new(totalRows, totalCols, false);
	//gtk_table_set_row_spacings(GTK_TABLE(widget), 2);
	//gtk_table_set_col_spacings(GTK_TABLE(widget), 8);
#endif
	gtk_widget_show(widget);

	int row = 0, col = 0;
	auto iter = bitfieldDesc.names->cbegin();
	for (int bit = 0; bit < count; bit++, ++iter) {
		const string &name = *iter;
		if (name.empty())
			continue;

		GtkWidget *checkBox = gtk_check_button_new_with_label(name.c_str());
		gtk_widget_show(checkBox);
		gboolean value = !!(field->data.bitfield & (1 << bit));
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkBox), value);

		// Save the bitfield checkbox's value in the GObject.
		g_object_set_data(G_OBJECT(checkBox), "RFT_BITFIELD_value", GUINT_TO_POINTER((guint)value));

		// Disable user modifications.
		// NOTE: Unlike Qt, both the "clicked" and "toggled" signals are
		// emitted for both user and program modifications, so we have to
		// connect this signal *after* setting the initial value.
		g_signal_connect(checkBox, "toggled",
			reinterpret_cast<GCallback>(checkbox_no_toggle_signal_handler),
			page);

#if GTK_CHECK_VERSION(3,0,0)
		// TODO: GTK_FILL
		gtk_grid_attach(GTK_GRID(widget), checkBox, col, row, 1, 1);
#else
		gtk_table_attach(GTK_TABLE(widget), checkBox, col, col+1, row, row+1,
			GTK_FILL, GTK_FILL, 0, 0);
#endif
		col++;
		if (col == bitfieldDesc.elemsPerRow) {
			row++;
			col = 0;
		}
	}

	return widget;
}

/**
 * Initialize a list data field.
 * @param page	[in] RomDataView object.
 * @param field	[in] RomFields::Field
 * @return Display widget, or nullptr on error.
 */
static GtkWidget*
rom_data_view_init_listdata(G_GNUC_UNUSED RomDataView *page, const RomFields::Field *field)
{
	// ListData type. Create a GtkListStore for the data.
	const auto &listDataDesc = field->desc.list_data;
	// NOTE: listDataDesc.names can be nullptr,
	// which means we don't have any column headers.

	const auto list_data = field->data.list_data.data;
	assert(list_data != nullptr);

	// Validate flags.
	// Cannot have both checkboxes and icons.
	const bool hasCheckboxes = !!(listDataDesc.flags & RomFields::RFT_LISTDATA_CHECKBOXES);
	const bool hasIcons = !!(listDataDesc.flags & RomFields::RFT_LISTDATA_ICONS);
	assert(!(hasCheckboxes && hasIcons));
	if (hasCheckboxes && hasIcons) {
		// Both are set. This shouldn't happen...
		return nullptr;
	}

	if (hasIcons) {
		assert(field->data.list_data.mxd.icons != nullptr);
		if (!field->data.list_data.mxd.icons) {
			// No icons vector...
			return nullptr;
		}
	}

	int col_count = 1;
	if (listDataDesc.names) {
		col_count = (int)listDataDesc.names->size();
	} else {
		// No column headers.
		// Use the first row.
		if (list_data && !list_data->empty()) {
			col_count = (int)list_data->at(0).size();
		}
	}

	GtkListStore *listStore;
	int col_start = 0;
	if (hasCheckboxes) {
		// Prepend an extra column for checkboxes.
		GType *types = new GType[col_count+1];
		types[0] = G_TYPE_BOOLEAN;
		for (int i = col_count; i > 0; i--) {
			types[i] = G_TYPE_STRING;
		}
		listStore = gtk_list_store_newv(col_count+1, types);
		delete[] types;
		col_start = 1;	// Skip the checkbox column for strings.
	} else if (hasIcons) {
		// Prepend an extra column for icons.
		GType *types = new GType[col_count+1];
		types[0] = PIMGTYPE_GOBJECT_TYPE;
		for (int i = col_count; i > 0; i--) {
			types[i] = G_TYPE_STRING;
		}
		listStore = gtk_list_store_newv(col_count+1, types);
		delete[] types;
		col_start = 1;	// Skip the icon column for strings.
	} else {
		// All strings.
		GType *types = new GType[col_count];
		for (int i = col_count-1; i >= 0; i--) {
			types[i] = G_TYPE_STRING;
		}
		listStore = gtk_list_store_newv(col_count, types);
		delete[] types;
	}

	// Add the row data.
	if (list_data) {
		uint32_t checkboxes = 0;
		if (hasCheckboxes) {
			checkboxes = field->data.list_data.mxd.checkboxes;
		}
		unsigned int row = 0;	// for icons [TODO: Use iterator?]
		for (auto iter = list_data->cbegin(); iter != list_data->cend(); ++iter, row++) {
			const vector<string> &data_row = *iter;
			// FIXME: Skip even if we don't have checkboxes?
			// (also check other UI frontends)
			if (hasCheckboxes && data_row.empty()) {
				// Skip this row.
				checkboxes >>= 1;
				continue;
			}

			GtkTreeIter treeIter;
			gtk_list_store_insert_before(listStore, &treeIter, nullptr);
			if (hasCheckboxes) {
				// Checkbox column.
				gtk_list_store_set(listStore, &treeIter,
					0, (checkboxes & 1), -1);
				checkboxes >>= 1;
			} else if (hasIcons) {
				// Icon column.
				PIMGTYPE pixbuf = rp_image_to_PIMGTYPE(
					field->data.list_data.mxd.icons->at(row));
				if (pixbuf) {
					// TODO: Ideal icon size?
					// Using 32x32 for now.
					static const int icon_sz = 32;
					// NOTE: GtkCellRendererPixbuf can't scale the
					// pixbuf itself...
					if (!PIMGTYPE_size_check(pixbuf, icon_sz, icon_sz)) {
						// TODO: Use nearest-neighbor if upscaling.
						// Also, preserve the aspect ratio.
						PIMGTYPE scaled = PIMGTYPE_scale(pixbuf, icon_sz, icon_sz, true);
						if (scaled) {
							PIMGTYPE_destroy(pixbuf);
							pixbuf = scaled;
						}
					}
					gtk_list_store_set(listStore, &treeIter,
						0, pixbuf, -1);
					PIMGTYPE_destroy(pixbuf);
				}
			}

			int col = col_start;
			for (auto iter = data_row.cbegin(); iter != data_row.cend(); ++iter, ++col) {
				gtk_list_store_set(listStore, &treeIter,
					col, iter->c_str(), -1);
			}
		}
	}

	// Scroll area for the GtkTreeView.
	GtkWidget *widget = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget),
			GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(widget), GTK_SHADOW_IN);
	gtk_widget_show(widget);

	// Create the GtkTreeView.
	GtkWidget *treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(listStore));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeView),
		(listDataDesc.names != nullptr));
	gtk_widget_show(treeView);
	gtk_container_add(GTK_CONTAINER(widget), treeView);

	// TODO: Set fixed height mode?
	// May require fixed columns...
	// Reference: https://developer.gnome.org/gtk3/stable/GtkTreeView.html#gtk-tree-view-set-fixed-height-mode

#if GTK_CHECK_VERSION(3,0,0)
	// FIXME: Alternating row colors isn't working in GTK+ 3.x...
#else
	// GTK+ 2.x: Use the "rules hint" for alternating row colors.
	gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(treeView), true);
#endif

	if (hasCheckboxes) {
		// Prepend an extra column for checkboxes.
		GtkCellRenderer *const renderer = gtk_cell_renderer_toggle_new();
		GtkTreeViewColumn *const column = gtk_tree_view_column_new_with_attributes(
			"", renderer, "active", 0, nullptr);
		gtk_tree_view_column_set_resizable(column, true);
		gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);
	} else if (hasIcons) {
		// Prepend an extra column for icons.
		GtkCellRenderer *const renderer = gtk_cell_renderer_pixbuf_new();
		GtkTreeViewColumn *const column = gtk_tree_view_column_new_with_attributes(
			"", renderer, GTK_CELL_RENDERER_PIXBUF_PROPERTY, 0, nullptr);
		gtk_tree_view_column_set_resizable(column, true);
		gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);
	}

	// Format tables.
	// Pango enum values are known to fit in uint8_t.
	static const gfloat align_tbl_xalign[4] = {
		// Order: TXA_D, TXA_L, TXA_C, TXA_R
		0.0f, 0.0f, 0.5f, 1.0f
	};
	static const uint8_t align_tbl_pango[4] = {
		// Order: TXA_D, TXA_L, TXA_C, TXA_R
		PANGO_ALIGN_LEFT, PANGO_ALIGN_LEFT,
		PANGO_ALIGN_CENTER, PANGO_ALIGN_RIGHT
	};

	// Set up the column names.
	uint32_t align_headers = listDataDesc.alignment.headers;
	uint32_t align_data = listDataDesc.alignment.data;
	for (int i = 0; i < col_count; i++, align_headers >>= 2, align_data >>= 2) {
		// NOTE: Not skipping empty column names.
		// TODO: Hide them.
		GtkCellRenderer *const renderer = gtk_cell_renderer_text_new();
		GtkTreeViewColumn *const column = gtk_tree_view_column_new_with_attributes(
			(listDataDesc.names ? listDataDesc.names->at(i).c_str() : ""),
			renderer, "text", i+col_start, nullptr);
		gtk_tree_view_column_set_resizable(column, true);
		gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);

		// Header alignment
		const float header_xalign = align_tbl_xalign[align_headers & 3];
		// Data alignment
		const float data_xalign = align_tbl_xalign[align_data & 3];
		const PangoAlignment data_alignment =
			static_cast<PangoAlignment>(align_tbl_pango[align_data & 3]);

		g_object_set(column, "alignment", header_xalign, nullptr);
		g_object_set(renderer,
			"xalign", data_xalign,
			"alignment", data_alignment, nullptr);
	}

	// Set a minimum height for the scroll area.
	// TODO: Adjust for DPI, and/or use a font size?
	// TODO: Force maximum horizontal width somehow?
	gtk_widget_set_size_request(widget, -1, 128);

	// Resize the columns to fit the contents.
	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(treeView));

	// Row height is recalculated when the window is first visible
	// and/or the system theme is changed.
	// TODO: Set an actual default number of rows, or let Qt handle it?
	// (Windows uses 5.)
	g_object_set_data(G_OBJECT(treeView), "RFT_LISTDATA_rows_visible",
		GINT_TO_POINTER(listDataDesc.rows_visible));
	if (listDataDesc.rows_visible > 0) {
		g_signal_connect(treeView, "realize",
			reinterpret_cast<GCallback>(tree_view_realize_signal_handler), page);
	}

	return widget;
}

/**
 * Initialize a Date/Time field.
 * @param page	[in] RomDataView object.
 * @param field	[in] RomFields::Field
 * @return Display widget, or nullptr on error.
 */
static GtkWidget*
rom_data_view_init_datetime(G_GNUC_UNUSED RomDataView *page, const RomFields::Field *field)
{
	// Date/Time.
	if (field->data.date_time == -1) {
		// tr: Invalid date/time.
		return rom_data_view_init_string(page, field, C_("RomDataView", "Unknown"));
	}

	GDateTime *dateTime;
	if (field->desc.flags & RomFields::RFT_DATETIME_IS_UTC) {
		dateTime = g_date_time_new_from_unix_utc(field->data.date_time);
	} else {
		dateTime = g_date_time_new_from_unix_local(field->data.date_time);
	}

	static const char *const formats[8] = {
		nullptr,	// No date or time.
		"%x",		// Date
		"%X",		// Time
		"%x %X",	// Date Time

		// TODO: Better localization here.
		nullptr,	// No date or time.
		"%b %d",	// Date (no year)
		"%X",		// Time
		"%b %d %X",	// Date Time (no year)
	};

	const char *const format = formats[field->desc.flags & RomFields::RFT_DATETIME_HAS_DATETIME_NO_YEAR_MASK];
	assert(format != nullptr);
	GtkWidget *widget = nullptr;
	if (format) {
		gchar *str = g_date_time_format(dateTime, format);
		if (str) {
			widget = rom_data_view_init_string(page, field, str);
			g_free(str);
		}
	}

	g_date_time_unref(dateTime);
	return widget;
}

/**
 * Initialize an Age Ratings field.
 * @param page	[in] RomDataView object.
 * @param field	[in] RomFields::Field
 * @return Display widget, or nullptr on error.
 */
static GtkWidget*
rom_data_view_init_age_ratings(G_GNUC_UNUSED RomDataView *page, const RomFields::Field *field)
{
	// Age ratings.
	const RomFields::age_ratings_t *age_ratings = field->data.age_ratings;
	assert(age_ratings != nullptr);
	if (!age_ratings) {
		// tr: No age ratings data.
		return rom_data_view_init_string(page, field, C_("RomDataView", "ERROR"));
	}

	// Convert the age ratings field to a string.
	string str = RomFields::ageRatingsDecode(age_ratings);
	return rom_data_view_init_string(page, field, str.c_str());
}

/**
 * Initialize a Dimensions field.
 * @param page	[in] RomDataView object.
 * @param field	[in] RomFields::Field
 * @return Display widget, or nullptr on error.
 */
static GtkWidget*
rom_data_view_init_dimensions(G_GNUC_UNUSED RomDataView *page, const RomFields::Field *field)
{
	// Dimensions.
	// TODO: 'x' or '×'? Using 'x' for now.
	const int *const dimensions = field->data.dimensions;
	char buf[64];
	if (dimensions[1] > 0) {
		if (dimensions[2] > 0) {
			snprintf(buf, sizeof(buf), "%dx%dx%d",
				dimensions[0], dimensions[1], dimensions[2]);
		} else {
			snprintf(buf, sizeof(buf), "%dx%d",
				dimensions[0], dimensions[1]);
		}
	} else {
		snprintf(buf, sizeof(buf), "%d", dimensions[0]);
	}

	return rom_data_view_init_string(page, field, buf);
}

static void
rom_data_view_update_display(RomDataView *page)
{
	assert(page != nullptr);

	// Delete the icon frames and tabs.
	rom_data_view_delete_tabs(page);

	// Initialize the header row.
	rom_data_view_init_header_row(page);

	if (!page->romData) {
		// No ROM data...
		return;
	}

	// Get the fields.
	const RomFields *fields = page->romData->fields();
	if (!fields) {
		// No fields.
		// TODO: Show an error?
		return;
	}
	const int count = fields->count();
#if !GTK_CHECK_VERSION(3,0,0)
	int rowCount = count;
#endif

	// Create the GtkNotebook.
	int tabCount = fields->tabCount();
	if (tabCount > 1) {
		page->tabs->resize(tabCount);
		page->tabWidget = gtk_notebook_new();

		// Add spacing between the system info header and the table.
		g_object_set(page, "spacing", 8, nullptr);

		auto tabIter = page->tabs->begin();
		for (int i = 0; i < tabCount; i++, ++tabIter) {
			// Create a tab.
			const char *name = fields->tabName(i);
			if (!name) {
				// Skip this tab.
				continue;
			}

			auto &tab = *tabIter;
#if GTK_CHECK_VERSION(3,0,0)
			tab.vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
			tab.table = gtk_grid_new();
			gtk_grid_set_row_spacing(GTK_GRID(tab.table), 2);
			gtk_grid_set_column_spacing(GTK_GRID(tab.table), 8);
#else
			tab.vbox = gtk_vbox_new(false, 8);
			// TODO: Adjust the table size?
			tab.table = gtk_table_new(rowCount, 2, false);
			gtk_table_set_row_spacings(GTK_TABLE(tab.table), 2);
			gtk_table_set_col_spacings(GTK_TABLE(tab.table), 8);
#endif

			gtk_container_set_border_width(GTK_CONTAINER(tab.table), 8);
			gtk_box_pack_start(GTK_BOX(tab.vbox), tab.table, false, false, 0);
			gtk_widget_show(tab.table);
			gtk_widget_show(tab.vbox);

			// Add the tab.
			GtkWidget *label = gtk_label_new(name);
			gtk_notebook_append_page(GTK_NOTEBOOK(page->tabWidget), tab.vbox, label);
		}
		gtk_box_pack_start(GTK_BOX(page), page->tabWidget, true, true, 0);
		gtk_widget_show(page->tabWidget);
	} else {
		// No tabs.
		// Don't create a GtkNotebook, but simulate a single
		// tab in page->tabs[] to make it easier to work with.
		tabCount = 1;
		page->tabs->resize(1);
		auto &tab = page->tabs->at(0);
		tab.vbox = GTK_WIDGET(page);

#if GTK_CHECK_VERSION(3,0,0)
		tab.table = gtk_grid_new();
		gtk_grid_set_row_spacing(GTK_GRID(tab.table), 2);
		gtk_grid_set_column_spacing(GTK_GRID(tab.table), 8);
#else
		// TODO: Adjust the table size?
		tab.table = gtk_table_new(count, 2, false);
		gtk_table_set_row_spacings(GTK_TABLE(tab.table), 2);
		gtk_table_set_col_spacings(GTK_TABLE(tab.table), 8);
#endif

		gtk_container_set_border_width(GTK_CONTAINER(tab.table), 8);
		gtk_box_pack_start(GTK_BOX(page), tab.table, false, false, 0);
		gtk_widget_show(tab.table);
	}

	// Reserve enough space for vecDescLabels.
	page->vecDescLabels->reserve(count);
	// Per-tab row counts.
	vector<int> tabRowCount(page->tabs->size());

	// Use a GtkSizeGroup to ensure that the description
	// labels on all tabs have the same width.
	// NOTE: GtkSizeGroup automatically unreferences itself
	// once all referenced widgets are deleted, so we don't
	// need to manage it ourselves.
	GtkSizeGroup *const size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

	// tr: Field description label.
	const char *const desc_label_fmt = C_("RomDataView", "%s:");

	// Create the data widgets.
	for (int i = 0; i < count; i++) {
		const RomFields::Field *const field = fields->field(i);
		assert(field != nullptr);
		if (!field || !field->isValid)
			continue;

		// Verify the tab index.
		const int tabIdx = field->tabIdx;
		assert(tabIdx >= 0 && tabIdx < (int)page->tabs->size());
		if (tabIdx < 0 || tabIdx >= (int)page->tabs->size()) {
			// Tab index is out of bounds.
			continue;
		} else if (!page->tabs->at(tabIdx).table) {
			// Tab name is empty. Tab is hidden.
			continue;
		}

		GtkWidget *widget = nullptr;
		bool separate_rows = false;
		switch (field->type) {
			case RomFields::RFT_INVALID:
				// No data here.
				break;
			default:
				// Unsupported right now.
				assert(!"Unsupported RomFields::RomFieldsType.");
				break;

			case RomFields::RFT_STRING:
				widget = rom_data_view_init_string(page, field);
				break;
			case RomFields::RFT_BITFIELD:
				widget = rom_data_view_init_bitfield(page, field);
				break;
			case RomFields::RFT_LISTDATA:
				separate_rows = !!(field->desc.list_data.flags & RomFields::RFT_LISTDATA_SEPARATE_ROW);
				widget = rom_data_view_init_listdata(page, field);
				break;
			case RomFields::RFT_DATETIME:
				widget = rom_data_view_init_datetime(page, field);
				break;
			case RomFields::RFT_AGE_RATINGS:
				widget = rom_data_view_init_age_ratings(page, field);
				break;
			case RomFields::RFT_DIMENSIONS:
				widget = rom_data_view_init_dimensions(page, field);
				break;
		}

		if (widget) {
			// Add the widget to the table.
			auto &tab = page->tabs->at(tabIdx);

			// tr: Field description label.
			string txt = rp_sprintf(desc_label_fmt, field->name.c_str());
			GtkWidget *lblDesc = gtk_label_new(txt.c_str());
			gtk_label_set_use_underline(GTK_LABEL(lblDesc), false);
			gtk_widget_show(lblDesc);
			gtk_size_group_add_widget(size_group, lblDesc);
			page->vecDescLabels->push_back(lblDesc);

			// Check if this is an RFT_STRING with warning set.
			// If it is, set the "RFT_STRING_warning" flag.
			const gboolean is_warning = (field->type == RomFields::RFT_STRING &&
						     field->desc.flags & RomFields::STRF_WARNING);
			g_object_set_data(G_OBJECT(lblDesc), "RFT_STRING_warning", GUINT_TO_POINTER((guint)is_warning));

			// Set the label format type.
			set_label_format_type(GTK_LABEL(lblDesc), page->desc_format_type);

			// Value widget.
			int &row = tabRowCount[tabIdx];
#if GTK_CHECK_VERSION(3,0,0)
			// TODO: GTK_FILL
			gtk_grid_attach(GTK_GRID(tab.table), lblDesc, 0, row, 1, 1);
			// Widget halign is set above.
			gtk_widget_set_valign(widget, GTK_ALIGN_START);
#else
			gtk_table_attach(GTK_TABLE(tab.table), lblDesc, 0, 1, row, row+1,
				GTK_FILL, GTK_FILL, 0, 0);
#endif

			if (separate_rows) {
				// Separate rows.
				// Make sure the description label is left-aligned.
#if GTK_CHECK_VERSION(3,16,0)
				gtk_label_set_xalign(GTK_LABEL(lblDesc), 0.0);
#else
				gtk_misc_set_alignment(GTK_MISC(lblDesc), 0.0f, 0.0f);
#endif

				// If this is the last field in the tab,
				// put the RFT_LISTDATA in the GtkGrid instead.
				bool doVBox = false;
				if (tabIdx + 1 == tabCount && i == count-1) {
					// Last tab, and last field.
					doVBox = true;
				} else {
					// Check if the next field is on the next tab.
					const RomFields::Field *nextField = fields->field(i+1);
					if (nextField && nextField->tabIdx != tabIdx) {
						// Next field is on the next tab.
						doVBox = true;
					}
				}

				if (doVBox) {
					// FIXME: There still seems to be a good amount of space
					// between tab.vbox and the RFT_LISTDATA widget here...
					// (Moreso on Thunar GTK2 than Nautilus.)

					// Unset this property to prevent the event filter from
					// setting a fixed height.
					g_object_set_data(G_OBJECT(widget), "RFT_LISTDATA_rows_visible",
						GINT_TO_POINTER(0));

#if GTK_CHECK_VERSION(3,0,0)
					// Change valign to FILL.
					gtk_widget_set_valign(widget, GTK_ALIGN_FILL);

					// Set margin, since it's located outside of the GtkTable/GtkGrid.
					// NOTE: Setting top margin to 0 due to spacing from the
					// GtkTable/GtkGrid. (Still has extra spacing that needs to be fixed...)
					g_object_set(widget,
						"margin-left", 8, "margin-right",  8,
						"margin-top",  0, "margin-bottom", 8,
						nullptr);

					// Add the widget to the GtkBox.
					gtk_box_pack_start(GTK_BOX(tab.vbox), widget, true, true, 0);
					if (tab.lblCredits) {
						// Need to move it before credits.
						// TODO: Verify this.
						gtk_box_reorder_child(GTK_BOX(tab.vbox), widget, 1);
					}
#else /* !GTK_CHECK_VERSION(3,0,0) */
					// Need to use GtkAlignment on GTK+ 2.x.
					GtkWidget *const alignment = gtk_alignment_new(0.0f, 0.0f, 1.0f, 1.0f);
					gtk_alignment_set_padding(GTK_ALIGNMENT(alignment), 0, 8, 8, 8);
					gtk_container_add(GTK_CONTAINER(alignment), widget);
					gtk_widget_show(alignment);

					// Add the GtkAlignment to the GtkBox.
					gtk_box_pack_start(GTK_BOX(tab.vbox), alignment, true, true, 0);
					if (tab.lblCredits) {
						// Need to move it before credits.
						// TODO: Verify this.
						gtk_box_reorder_child(GTK_BOX(tab.vbox), alignment, 1);
					}
#endif
					// Increment row by one, since only one widget is
					// actually being added to the GtkTable/GtkGrid.
					row++;
				} else {
					// Add the widget to the GtkTable/GtkGrid.
#if GTK_CHECK_VERSION(3,0,0)
					gtk_grid_attach(GTK_GRID(tab.table), widget, 0, row+1, 2, 1);
#else
					rowCount++;
					gtk_table_resize(GTK_TABLE(tab.table), rowCount, 2);
					gtk_table_attach(GTK_TABLE(tab.table), widget, 0, 2, row+1, row+2,
						GTK_FILL, GTK_FILL, 0, 0);
#endif
					row += 2;
				}
			} else {
				// Single row.
#if GTK_CHECK_VERSION(3,0,0)
				gtk_grid_attach(GTK_GRID(tab.table), widget, 1, row, 1, 1);
#else
				gtk_table_attach(GTK_TABLE(tab.table), widget, 1, 2, row, row+1,
					GTK_FILL, GTK_FILL, 0, 0);
#endif
				row++;
			}
		}
	}
}

static gboolean
rom_data_view_load_rom_data(gpointer data)
{
	RomDataView *page = ROM_DATA_VIEW(data);
	g_return_val_if_fail(page != nullptr || IS_ROM_DATA_VIEW(page), false);
	g_return_val_if_fail(page->filename != nullptr, false);

	// Open the ROM file.
	// TODO: gvfs support.
	if (G_LIKELY(page->filename != nullptr)) {
		// Open the ROM file.
		RpFile *const file = new RpFile(page->filename, RpFile::FM_OPEN_READ_GZ);
		if (file->isOpen()) {
			// Create the RomData object.
			// file is ref()'d by RomData.
			page->romData = RomDataFactory::create(file);

			// Update the display widgets.
			rom_data_view_update_display(page);

			// Make sure the underlying file handle is closed,
			// since we don't need it once the RomData has been
			// loaded by RomDataView.
			if (page->romData) {
				page->romData->close();
			}
		}
		file->unref();
	}

	// Animation timer will be started when the page
	// receives the "map" signal.

	// Clear the timeout.
	page->changed_idle = 0;
	return false;
}

/**
 * Delete tabs and related widgets.
 * @param page RomDataView.
 */
static void
rom_data_view_delete_tabs(RomDataView *page)
{
	assert(page != nullptr);
	assert(page->tabs != nullptr);
	assert(page->vecDescLabels != nullptr);

	// Delete the tab contents.
	for (auto iter = page->tabs->begin(); iter != page->tabs->end(); ++iter) {
		auto &tab = *iter;
		if (tab.lblCredits) {
			gtk_widget_destroy(tab.lblCredits);
		}
		if (tab.table) {
			gtk_widget_destroy(tab.table);
		}
		if (tab.vbox && tab.vbox != GTK_WIDGET(page)) {
			gtk_widget_destroy(tab.vbox);
		}
	}
	page->tabs->clear();

	if (page->tabWidget) {
		// Delete the tab widget.
		gtk_widget_destroy(page->tabWidget);
		page->tabWidget = nullptr;
	}

	// Clear the various widget references.
	page->vecDescLabels->clear();

	// Delete the icon frames.
	for (int i = ARRAY_SIZE(page->iconFrames)-1; i >= 0; i--) {
		if (page->iconFrames[i]) {
			PIMGTYPE_destroy(page->iconFrames[i]);
			page->iconFrames[i] = nullptr;
		}
	}
}

/** Signal handlers. **/

/**
 * Prevent bitfield checkboxes from being toggled.
 * @param togglebutton Bitfield checkbox.
 * @param user_data RomDataView*.
 */
static void
checkbox_no_toggle_signal_handler(GtkToggleButton	*togglebutton,
				  gpointer		 user_data)
{
	RP_UNUSED(user_data);

	// Get the saved RFT_BITFIELD value.
	const gboolean value = (gboolean)GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(togglebutton), "RFT_BITFIELD_value"));
	if (gtk_toggle_button_get_active(togglebutton) != value) {
		// Toggle this box.
		gtk_toggle_button_set_active(togglebutton, value);
	}
}

/**
 * RomDataView is being mapped onto the screen.
 * @param page RomDataView
 * @param user_data User data.
 */
static void
rom_data_view_map_signal_handler(RomDataView	*page,
				 gpointer	 user_data)
{
	RP_UNUSED(user_data);
	start_anim_timer(page);
}

/**
 * RomDataView is being unmapped from the screen.
 * @param page RomDataView
 * @param user_data User data.
 */
static void
rom_data_view_unmap_signal_handler(RomDataView	*page,
				   gpointer	 user_data)
{
	RP_UNUSED(user_data);
	stop_anim_timer(page);
}

/**
 * GtkTreeView widget has been realized.
 * @param treeView GtkTreeView
 * @param page RomDataView
 */
static void
tree_view_realize_signal_handler(GtkTreeView	*treeView,
				 RomDataView	*page)
{
	// TODO: Redo this if the system font and/or style changes.
	// TODO: Remove the `page` parameter?
	RP_UNUSED(page);

	// Recalculate the row heights for this GtkTreeView.
	const int rows_visible = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(treeView), "RFT_LISTDATA_rows_visible"));
	if (rows_visible <= 0) {
		// This GtkTreeView doesn't have a fixed number of rows.
		return;
	}

	// Get the parent widget.
	// This should be a GtkScrolledWindow.
	GtkWidget *scrolledWindow = gtk_widget_get_ancestor(GTK_WIDGET(treeView), GTK_TYPE_SCROLLED_WINDOW);
	if (!scrolledWindow || !GTK_IS_SCROLLED_WINDOW(scrolledWindow)) {
		// No parent widget, or not a GtkScrolledWindow.
		return;
	}

	// Get the height of the first item.
	GtkTreePath *path = gtk_tree_path_new_from_string("0");
	GdkRectangle rect;
	gtk_tree_view_get_background_area(GTK_TREE_VIEW(treeView), path, nullptr, &rect);
	gtk_tree_path_free(path);
	if (rect.height <= 0) {
		// GtkListStore probably doesn't have any items.
		return;
	}
	int height = rect.height * rows_visible;

	if (gtk_tree_view_get_headers_visible(treeView)) {
		// Get the header widget of the first column.
		GtkTreeViewColumn *column = gtk_tree_view_get_column(treeView, 0);
		assert(column != nullptr);
		if (!column) {
			// No columns...
			return;
		}

		GtkWidget *header = gtk_tree_view_column_get_widget(column);
		if (!header) {
			header = gtk_tree_view_column_get_button(column);
		}
		if (header) {
			// TODO: gtk_widget_get_allocated_height() for GTK+ 3.x?
			GtkAllocation allocation;
			gtk_widget_get_allocation(header, &allocation);
			height += allocation.height;
		}
	}

#if GTK_CHECK_VERSION(3,0,0)
	// Get the GtkScrolledWindow's border, padding, and margin.
	GtkStyleContext *context = gtk_widget_get_style_context(scrolledWindow);
	GtkBorder border, padding, margin;
	gtk_style_context_get_border(context, GTK_STATE_FLAG_NORMAL, &border);
	gtk_style_context_get_padding(context, GTK_STATE_FLAG_NORMAL, &padding);
	gtk_style_context_get_margin(context, GTK_STATE_FLAG_NORMAL, &margin);
	height += border.top + border.bottom;
	height += padding.top + padding.bottom;
	height += margin.top + margin.bottom;
#else
	// Get the GtkScrolledWindow's border.
	// NOTE: Assuming we have a border set.
	GtkStyle *style = gtk_widget_get_style(scrolledWindow);
	height += (style->ythickness * 2);
#endif

	// Set the GtkScrolledWindow's height.
	// NOTE: gtk_scrolled_window_set_max_content_height() doesn't seem to
	// work properly for rows_visible=4, and it's GTK+ 3.x only.
	gtk_widget_set_size_request(scrolledWindow, -1, height);
}

/** Icon animation timer. **/

/**
 * Start the animation timer.
 */
static void start_anim_timer(RomDataView *page)
{
	if (!page->iconAnimHelper->isAnimated()) {
		// Not an animated icon.
		return;
	}

	// Get the current frame information.
	page->last_frame_number = page->iconAnimHelper->frameNumber();
	const int delay = page->iconAnimHelper->frameDelay();
	assert(delay > 0);
	if (delay <= 0) {
		// Invalid delay value.
		return;
	}

	// Stop the animation timer.
	stop_anim_timer(page);

	// Set a timer for the current frame.
	page->last_delay = delay;
	page->tmrIconAnim = g_timeout_add(delay,
		reinterpret_cast<GSourceFunc>(anim_timer_func), page);
}

/**
 * Stop the animation timer.
 */
static void stop_anim_timer(RomDataView *page)
{
	if (page->tmrIconAnim > 0) {
		g_source_remove(page->tmrIconAnim);
		page->tmrIconAnim = 0;
		page->last_delay = 0;
	}
}

/**
 * Animated icon timer.
 */
static gboolean anim_timer_func(RomDataView *page)
{
	if (page->tmrIconAnim == 0) {
		// Shutting down...
		return false;
	}

	// Next frame.
	int delay = 0;
	int frame = page->iconAnimHelper->nextFrame(&delay);
	if (delay <= 0 || frame < 0) {
		// Invalid frame...
		page->tmrIconAnim = 0;
		return false;
	}

	if (frame != page->last_frame_number) {
		// New frame number.
		// Update the icon.
		gtk_image_set_from_PIMGTYPE(GTK_IMAGE(page->imgIcon), page->iconFrames[frame]);
		page->last_frame_number = frame;
	}

	// Set a new timer and unset the current one.
	if (page->last_delay != delay) {
		page->last_delay = delay;
		page->tmrIconAnim = g_timeout_add(delay,
			reinterpret_cast<GSourceFunc>(anim_timer_func), page);
		return false;
	}
	return true;
}
