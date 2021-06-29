/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-plugin-private.h"

#include "fu-redfish-common.h"
#include "fu-redfish-network.h"

typedef struct {
	FuPlugin		*plugin;
} FuTest;

static void
fu_test_self_init (FuTest *self)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new ();
	g_autoptr(GError) error = NULL;
	g_autofree gchar *pluginfn = NULL;

	ret = fu_context_load_quirks (ctx,
				      FU_QUIRKS_LOAD_FLAG_NO_CACHE |
				      FU_QUIRKS_LOAD_FLAG_NO_VERIFY,
				      &error);
	g_assert_no_error (error);
	g_assert (ret);

	self->plugin = fu_plugin_new (ctx);
	pluginfn = g_build_filename (PLUGINBUILDDIR,
				     "libfu_plugin_redfish." G_MODULE_SUFFIX,
				     NULL);
	ret = fu_plugin_open (self->plugin, pluginfn, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_plugin_runner_startup (self->plugin, &error);
	if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE)) {
		g_test_skip ("no redfish.py running");
		return;
	}
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_plugin_runner_coldplug (self->plugin, &error);
	g_assert_no_error (error);
	g_assert (ret);
}

static void
fu_test_redfish_common_func (void)
{
	const guint8 buf[16] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
				 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f  };
	g_autofree gchar *ipv4 = NULL;
	g_autofree gchar *ipv6 = NULL;
	g_autofree gchar *maca = NULL;

	ipv4 = fu_redfish_common_buffer_to_ipv4 (buf);
	g_assert_cmpstr (ipv4, ==, "0.1.2.3");
	ipv6 = fu_redfish_common_buffer_to_ipv6 (buf);
	g_assert_cmpstr (ipv6, ==, "00010203:04050607:08090a0b:0c0d0e0f");
	maca = fu_redfish_common_buffer_to_mac (buf);
	g_assert_cmpstr (maca, ==, "00:01:02:03:04:05");
}

static void
fu_test_redfish_common_version_func (void)
{
	struct {
		const gchar *in;
		const gchar *op;
	} strs[] = {
		{ "1.2.3",		"1.2.3" },
		{ "P50 v1.2.3 PROD",	"1.2.3" },
		{ "P50 1.2.3 DEV",	"1.2.3" },
		{ NULL, NULL }
	};
	for (guint i = 0; strs[i].in != NULL; i++) {
		g_autofree gchar *tmp = fu_redfish_common_fix_version (strs[i].in);
		g_assert_cmpstr (tmp, ==, strs[i].op);
	}
}

static void
fu_test_redfish_network_mac_addr_func (void)
{
	g_autofree gchar *ip_addr = NULL;
	g_autoptr(GError) error = NULL;

	ip_addr = fu_redfish_network_ip_for_mac_addr ("00:13:F7:29:C2:D8", &error);
	if (ip_addr == NULL &&
	    g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
		g_test_skip ("no hardware");
		return;
	}
	g_assert_no_error (error);
	g_assert_nonnull (ip_addr);
}

static void
fu_test_redfish_network_vid_pid_func (void)
{
	g_autofree gchar *ip_addr = NULL;
	g_autoptr(GError) error = NULL;

	ip_addr = fu_redfish_network_ip_for_vid_pid (0x0707, 0x0201, &error);
	if (ip_addr == NULL &&
	    g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
		g_test_skip ("no hardware");
		return;
	}
	g_assert_no_error (error);
	g_assert_nonnull (ip_addr);
}

static void
fu_test_redfish_devices_func (gconstpointer user_data)
{
	FuDevice *dev;
	FuTest *self = (FuTest *) user_data;
	GPtrArray *devices;
	g_autofree gchar *devstr0 = NULL;
	g_autofree gchar *devstr1 = NULL;

	devices = fu_plugin_get_devices (self->plugin);
	g_assert_nonnull (devices);
	if (devices->len == 0) {
		g_test_skip ("no redfish support");
		return;
	}
	g_assert_cmpint (devices->len, ==, 2);

	/* BMC */
	dev = g_ptr_array_index (devices, 1);
	devstr0 = fu_device_to_string (dev);
	g_debug ("%s", devstr0);
	g_assert_cmpstr (fu_device_get_id (dev), ==, "62c1cd95692c5225826cf8568a460427ea3b1827");
	g_assert_cmpstr (fu_device_get_name (dev), ==, "BMC Firmware");
	g_assert_cmpstr (fu_device_get_vendor (dev), ==, "Contoso");
	g_assert_cmpstr (fu_device_get_version (dev), ==, "1.45.455b66-rev4");
	g_assert_cmpstr (fu_device_get_version_lowest (dev), ==, "1.30.367a12-rev1");
	g_assert_cmpint (fu_device_get_version_format (dev), ==, FWUPD_VERSION_FORMAT_PLAIN);
	g_assert_true (fu_device_has_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_true (fu_device_has_protocol (dev, "org.dmtf.redfish"));
	g_assert_true (fu_device_has_guid (dev, "1624a9df-5e13-47fc-874a-df3aff143089"));
	g_assert_true (fu_device_has_vendor_id (dev, "REDFISH:CONTOSO"));

	/* BIOS */
	dev = g_ptr_array_index (devices, 0);
	devstr1 = fu_device_to_string (dev);
	g_debug ("%s", devstr1);
	g_assert_cmpstr (fu_device_get_id (dev), ==, "562313e34c756a05a2e878861377765582bbf971");
	g_assert_cmpstr (fu_device_get_name (dev), ==, "BIOS Firmware");
	g_assert_cmpstr (fu_device_get_vendor (dev), ==, "Contoso");
	g_assert_cmpstr (fu_device_get_version (dev), ==, "1.45");
	g_assert_cmpstr (fu_device_get_version_lowest (dev), ==, "1.10");
	g_assert_cmpint (fu_device_get_version_format (dev), ==, FWUPD_VERSION_FORMAT_PAIR);
	g_assert_true (fu_device_has_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_true (fu_device_has_protocol (dev, "org.dmtf.redfish"));
	g_assert_true (fu_device_has_guid (dev, "fee82a67-6ce2-4625-9f44-237ad2402c28"));
	g_assert_true (fu_device_has_vendor_id (dev, "REDFISH:CONTOSO"));
}

static void
fu_test_redfish_update_func (gconstpointer user_data)
{
	FuDevice *dev;
	FuTest *self = (FuTest *) user_data;
	GPtrArray *devices;
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob_fw = NULL;

	devices = fu_plugin_get_devices (self->plugin);
	g_assert_nonnull (devices);
	if (devices->len == 0) {
		g_test_skip ("no redfish support");
		return;
	}
	g_assert_cmpint (devices->len, ==, 2);

	/* BMC */
	dev = g_ptr_array_index (devices, 1);
	blob_fw = g_bytes_new_static ("hello", 5);
	ret = fu_plugin_runner_update (self->plugin, dev, blob_fw,
				       FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
}

static void
fu_test_self_free (FuTest *self)
{
	if (self->plugin != NULL)
		g_object_unref (self->plugin);
	g_free (self);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuTest, fu_test_self_free)
#pragma clang diagnostic pop

int
main (int argc, char **argv)
{
	g_autoptr(FuTest) self = g_new0 (FuTest, 1);
	g_autofree gchar *smbios_data_fn = NULL;

	g_setenv ("FWUPD_REDFISH_VERBOSE", "1", TRUE);

	smbios_data_fn = g_build_filename (TESTDATADIR, "redfish-smbios.bin", NULL);
	g_setenv ("FWUPD_REDFISH_SMBIOS_DATA", smbios_data_fn, TRUE);
	g_setenv ("FWUPD_SYSFSFWDIR", TESTDATADIR, TRUE);
	g_setenv ("CONFIGURATION_DIRECTORY", TESTDATADIR, TRUE);
	g_test_init (&argc, &argv, NULL);

	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	fu_test_self_init (self);
	g_test_add_func ("/redfish/common", fu_test_redfish_common_func);
	g_test_add_func ("/redfish/common{version}", fu_test_redfish_common_version_func);
	g_test_add_func ("/redfish/network{mac_addr}", fu_test_redfish_network_mac_addr_func);
	g_test_add_func ("/redfish/network{vid_pid}", fu_test_redfish_network_vid_pid_func);
	g_test_add_data_func ("/redfish/plugin{devices}", self, fu_test_redfish_devices_func);
	g_test_add_data_func ("/redfish/plugin{update}", self, fu_test_redfish_update_func);
	return g_test_run ();
}
