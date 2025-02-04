/***************************************************************************
 * ROM Properties Page shell extension. (librpbase)                        *
 * Config.hpp: Configuration manager.                                      *
 *                                                                         *
 * Copyright (c) 2016-2017 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#ifndef __ROMPROPERTIES_LIBRPBASE_CONFIG_CONFIG_HPP__
#define __ROMPROPERTIES_LIBRPBASE_CONFIG_CONFIG_HPP__

#include "ConfReader.hpp"

// C includes.
#include <stdint.h>

namespace LibRpBase {

class Config : public ConfReader
{
	protected:
		/**
		 * Config class.
		 *
		 * This class is a Singleton, so the caller must obtain a
		 * pointer to the class using instance().
		 */
		Config();

	private:
		typedef ConfReader super;
		RP_DISABLE_COPY(Config)

	private:
		friend class ConfigPrivate;

	public:
		/**
		 * Get the Config instance.
		 *
		 * This automatically initializes the object and
		 * reloads the configuration if it has been modified.
		 *
		 * @return Config instance.
		 */
		static Config *instance(void);

	public:
		/** Image types. **/

		// Image type priority data.
		struct ImgTypePrio_t {
			const uint8_t *imgTypes;	// Image types.
			uint32_t length;		// Length of imgTypes array.
		};

		// TODO: Function to get image type priority for a specified class.

	public:
		/** Image types. **/

		enum ImgTypeResult {
			IMGTR_ERR_MAP_CORRUPTED	= -2,	// Internal map is corrupted.
			IMGTR_ERR_INVALID_PARAMS = -1,	// Invalid parameters.
			IMGTR_SUCCESS		= 0,	// Image type priority data returned successfully.
			IMGTR_SUCCESS_DEFAULTS	= 1,	// Custom configuration not defined; returning defaults.
			IMGTR_DISABLED		= 2,	// Thumbnails are disabled for this class.
		};

		/**
		 * Get the image type priority data for the specified class name.
		 * NOTE: Call load() before using this function.
		 * @param className	[in] Class name. (ASCII)
		 * @param imgTypePrio	[out] Image type priority data.
		 * @return ImgTypeResult
		 */
		ImgTypeResult getImgTypePrio(const char *className, ImgTypePrio_t *imgTypePrio) const;

		/**
		 * Get the default image type priority data.
		 * This is the priority data used if a custom configuration
		 * is not defined for a given class.
		 * @param imgTypePrio	[out] Image type priority data.
		 */
		void getDefImgTypePrio(ImgTypePrio_t *imgTypePrio) const;

		/** Download options. **/

		/**
		 * Should we download images from external databases?
		 * NOTE: Call load() before using this function.
		 * @return True if downloads are enabled; false if not.
		 */
		bool extImgDownloadEnabled(void) const;

		/**
		 * Always use the internal icon (if present) for small sizes.
		 * TODO: Clarify "small sizes".
		 * NOTE: Call load() before using this function.
		 * @return True if we should use the internal icon for small sizes; false if not.
		 */
		bool useIntIconForSmallSizes(void) const;

		/**
		 * Download high-resolution scans if viewing large thumbnails.
		 * NOTE: Call load() before using this function.
		 * @return True if we should download high-resolution scans; false if not.
		 */
		bool downloadHighResScans(void) const;

		/**
		 * Show an overlay icon for "dangerous" permissions?
		 * NOTE: Call load() before using this function.
		 * @return True if we should show the overlay icon; false if not.
		 */
		bool showDangerousPermissionsOverlayIcon(void) const;

		/**
		 * Enable thumbnailing and metadata on network filesystems?
		 * @return True if we should enable; false if not.
		 */
		bool enableThumbnailOnNetworkFS(void) const;
};

}

#endif /* __ROMPROPERTIES_LIBRPBASE_CONFIG_CONFIG_HPP__ */
