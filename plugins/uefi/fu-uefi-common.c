/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <efivar.h>
#include <gio/gio.h>

#include "fu-common.h"
#include "fu-uefi-common.h"
#include "fu-uefi-vars.h"

#include "fwupd-common.h"
#include "fwupd-error.h"

gboolean
fu_uefi_get_framebuffer_size (guint32 *width, guint32 *height, GError **error)
{
	guint32 height_tmp;
	guint32 width_tmp;
	g_autofree gchar *sysfsdriverdir = NULL;
	g_autofree gchar *fbdir = NULL;

	sysfsdriverdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_DRIVERS);
	fbdir = g_build_filename (sysfsdriverdir, "efi-framebuffer", "efi-framebuffer.0", NULL);
	if (!g_file_test (fbdir, G_FILE_TEST_EXISTS)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "EFI framebuffer not found");
		return FALSE;
	}
	height_tmp = fu_uefi_read_file_as_uint64 (fbdir, "height");
	width_tmp = fu_uefi_read_file_as_uint64 (fbdir, "width");
	if (width_tmp == 0 || height_tmp == 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "EFI framebuffer has invalid size "
			     "%"G_GUINT32_FORMAT"x%"G_GUINT32_FORMAT,
			     width_tmp, height_tmp);
		return FALSE;
	}
	if (width != NULL)
		*width = width_tmp;
	if (height != NULL)
		*height = height_tmp;
	return TRUE;
}

gboolean
fu_uefi_get_bitmap_size (const guint8 *buf,
			 gsize bufsz,
			 guint32 *width,
			 guint32 *height,
			 GError **error)
{
	guint32 ui32;

	g_return_val_if_fail (buf != NULL, FALSE);

	/* check header */
	if (bufsz < 26) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "blob was too small %" G_GSIZE_FORMAT, bufsz);
		return FALSE;
	}
	if (memcmp (buf, "BM", 2) != 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid BMP header signature");
		return FALSE;
	}

	/* starting address */
	ui32 = fu_common_read_uint32 (buf + 10, G_LITTLE_ENDIAN);
	if (ui32 < 26) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "BMP header invalid @ %"G_GUINT32_FORMAT"x", ui32);
		return FALSE;
	}

	/* BITMAPINFOHEADER header */
	ui32 = fu_common_read_uint32 (buf + 14, G_LITTLE_ENDIAN);
	if (ui32 < 26 - 14) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "BITMAPINFOHEADER invalid @ %"G_GUINT32_FORMAT"x", ui32);
		return FALSE;
	}

	/* dimensions */
	if (width != NULL)
		*width = fu_common_read_uint32 (buf + 18, G_LITTLE_ENDIAN);
	if (height != NULL)
		*height = fu_common_read_uint32 (buf + 22, G_LITTLE_ENDIAN);
	return TRUE;
}

gboolean
fu_uefi_secure_boot_enabled (void)
{
	gsize data_size = 0;
	g_autofree guint8 *data = NULL;

	if (!fu_uefi_vars_get_data (FU_UEFI_VARS_GUID_EFI_GLOBAL, "SecureBoot",
				    &data, &data_size, NULL, NULL))
		return FALSE;
	if (data_size >= 1 && data[0] & 1)
		return TRUE;
	return FALSE;
}

static gint
fu_uefi_strcmp_sort_cb (gconstpointer a, gconstpointer b)
{
	const gchar *stra = *((const gchar **) a);
	const gchar *strb = *((const gchar **) b);
	return g_strcmp0 (stra, strb);
}

GPtrArray *
fu_uefi_get_esrt_entry_paths (const gchar *esrt_path, GError **error)
{
	GPtrArray *entries = g_ptr_array_new_with_free_func (g_free);
	const gchar *fn;
	g_autofree gchar *esrt_entries = NULL;
	g_autoptr(GDir) dir = NULL;

	/* search ESRT */
	esrt_entries = g_build_filename (esrt_path, "entries", NULL);
	dir = g_dir_open (esrt_entries, 0, error);
	if (dir == NULL)
		return NULL;
	while ((fn = g_dir_read_name (dir)) != NULL)
		g_ptr_array_add (entries, g_build_filename (esrt_entries, fn, NULL));

	/* sort by name */
	g_ptr_array_sort (entries, fu_uefi_strcmp_sort_cb);
	return entries;
}

guint64
fu_uefi_read_file_as_uint64 (const gchar *path, const gchar *attr_name)
{
	g_autofree gchar *data = NULL;
	g_autofree gchar *fn = g_build_filename (path, attr_name, NULL);
	if (!g_file_get_contents (fn, &data, NULL, NULL))
		return 0x0;
	if (g_str_has_prefix (data, "0x"))
		return g_ascii_strtoull (data + 2, NULL, 16);
	return g_ascii_strtoull (data, NULL, 10);
}

