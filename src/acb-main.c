/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2011 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <glib-object.h>

#include "acb-project.h"
#include "acb-common.h"

#include "egg-debug.h"

/**
 * acb_main_process_project_name:
 **/
static gboolean
acb_main_process_project_name (const gchar *default_code_path,
			       const gchar *project_name,
			       gboolean clean,
			       gboolean update,
			       gboolean build,
			       gboolean make,
			       const gchar *rpmbuild_path)
{
	gchar *path = NULL;
	gboolean ret = TRUE;
	GError *error = NULL;
	AcbProject *project = NULL;

	/* operate on folder */
	project = acb_project_new ();
	acb_project_set_default_code_path (project, default_code_path);
	acb_project_set_rpmbuild_path (project, rpmbuild_path);
	acb_project_set_name (project, project_name);
	if (clean) {
		ret = acb_project_clean (project, &error);
		if (!ret) {
			g_print ("Failed to clean: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
	}
	if (update) {
		ret = acb_project_update (project, &error);
		if (!ret) {
			g_print ("Failed to update: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
	}
	if (make) {
		ret = acb_project_make (project, &error);
		if (!ret) {
			g_print ("Failed to make: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
	}
	if (build) {
		ret = acb_project_build (project, &error);
		if (!ret) {
			g_print ("Failed to build: %s\n", error->message);
			g_error_free (error);
			goto out;
		}
	}
out:
	if (project != NULL)
		g_object_unref (project);
	g_free (path);
	return ret;
}

/**
 * acb_main_ensure_has_path:
 **/
static gboolean
acb_main_ensure_has_path (const gchar *path)
{
	gint retval;
	gchar *folder;

	/* create leading path */
	folder = g_path_get_dirname (path);
	retval = g_mkdir_with_parents (folder, 0777);
	g_free (folder);
	return (retval == 0);
}

/**
 * acb_main_get_code_dir:
 **/
static gchar *
acb_main_get_code_dir ()
{
	GError *error = NULL;
	GKeyFile *file = NULL;
	gchar *config_file = NULL;
	gchar *data = NULL;
	gchar *code_dir = NULL;

	/* find the file, else create it */
	config_file = g_build_filename (g_get_user_config_dir (),
					"autocodebuild",
					"defaults.conf",
					NULL);
	if (!g_file_test (config_file, G_FILE_TEST_EXISTS)) {

		/* create a good default */
		code_dir = g_build_filename (g_get_home_dir (), "Code", NULL);
		data = g_strdup_printf ("[defaults]\nCodeDirectory=%s\n", code_dir);
		acb_main_ensure_has_path (config_file);

		/* save to the file */
		g_file_set_contents (config_file, data, -1, NULL);
		goto out;
	}

	/* load from file */
	file = g_key_file_new ();
	g_key_file_load_from_file (file, config_file, G_KEY_FILE_NONE, &error);
	code_dir = g_key_file_get_string (file, "defaults", "CodeDirectory", NULL);
	if (code_dir == NULL) {
		egg_error ("cannot load: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (data);
	g_free (config_file);
	if (file != NULL)
		g_key_file_free (file);
	return code_dir;
}

/**
 * acb_main_get_rpmbuild_dir:
 **/
static gchar *
acb_main_get_rpmbuild_dir ()
{
	gchar *data = NULL;
	gchar *macros = NULL;
	gchar *rpmbuild_path = NULL;
	gchar **lines = NULL;
	gboolean ret;
	guint i;

	/* get the data from the rpmmacros override */
	macros = g_build_path (g_get_home_dir (), ".rpmmacros", NULL);
	ret = g_file_get_contents (macros, &data, NULL, NULL);
	if (!ret) {
		rpmbuild_path = g_build_filename (g_get_home_dir (), "rpmbuild", NULL);
		goto out;
	}

	/* find the right line */
	lines = g_strsplit (data, "\n", -1);
	for (i=0; lines[i] != NULL; i++) {
		if (!g_str_has_prefix (lines[i], "%_topdir"))
			continue;
		rpmbuild_path = g_strdup (lines[i]+8);
		g_strstrip (rpmbuild_path);
		break;
	}

out:
	g_free (data);
	g_free (macros);
	g_strfreev (lines);
	return rpmbuild_path;
}

int
main (int argc, char **argv)
{
	GError *error = NULL;
	GDir *dir = NULL;
	gchar *options_help = NULL;
	GOptionContext *context;
	gboolean ret;
	gboolean verbose = FALSE;
	gboolean install = FALSE;
	gboolean clean = FALSE;
	gboolean update = FALSE;
	gboolean build = FALSE;
	gboolean make = FALSE;
	gchar **files = NULL;
	gchar *code_path = NULL;
	gchar *rpmbuild_path = NULL;
	guint i;
	const gchar *filename;
	gchar *project_name;
	gchar *tmp;
	gchar *user_data = NULL;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			"Show extra debugging information", NULL },
		{ "clean", 'c', 0, G_OPTION_ARG_NONE, &clean,
			"Clean projects", NULL},
		{ "update", 'u', 0, G_OPTION_ARG_NONE, &update,
			"Update projects", NULL},
		{ "build", 'b', 0, G_OPTION_ARG_NONE, &build,
			"Build projects", NULL},
		{ "make", 'm', 0, G_OPTION_ARG_NONE, &make,
			"Make projects", NULL},
		{ "install", 'i', 0, G_OPTION_ARG_NONE, &install,
			"Install projects", NULL},
		{ G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_FILENAME_ARRAY, &files,
			"Projects", NULL },
		{ NULL}
	};

	context = g_option_context_new ("AutoCodeBuild");
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	options_help = g_option_context_get_help (context, TRUE, NULL);
	g_option_context_free (context);

	egg_debug_init (verbose);
	g_type_init ();

	/* get the code location */
	code_path = acb_main_get_code_dir ();

	/* get the code location */
	rpmbuild_path = acb_main_get_rpmbuild_dir ();

	/* didn't specify any options */
	if (files == NULL && !clean && !update && !build && !make && !install) {
		g_print ("%s\n", options_help);
		goto out;
	}

	/* process the list */
	if (files != NULL) {
		for (i=0; files[i] != NULL; i++) {
			acb_main_process_project_name (code_path,
						       files[i],
						       clean,
						       update,
						       build,
						       make,
						       rpmbuild_path);
		}
	} else {
		/* try to open */
		user_data = g_build_filename (g_get_user_data_dir (),
					      "autocodebuild",
					      NULL);
		dir = g_dir_open (user_data, 0, &error);
		if (dir == NULL) {
			egg_warning ("cannot open directory: %s",
				     error->message);
			g_error_free (error);
			goto out;
		}

		/* find each */
		while ((filename = g_dir_read_name (dir))) {
			if (!g_str_has_suffix (filename, ".conf"))
				continue;
			project_name = g_strdup (filename);
			tmp = g_strrstr (project_name, ".");
			*tmp = '\0';
			acb_main_process_project_name (code_path,
						       project_name,
						       clean,
						       update,
						       build,
						       make,
						       rpmbuild_path);
			g_free (project_name);
		}
		g_dir_close (dir);
	}

	/* all install */
	if (install) {
		ret = g_spawn_command_line_sync ("pkexec rpm -Fvh /home/hughsie/rpmbuild/REPOS/fedora/15/i386/*.rpm",
						 NULL, NULL, NULL, &error);
		if (!ret) {
			egg_warning ("cannot install packages: %s", error->message);
			g_error_free (error);
			goto out;
		}
	}
out:
	g_free (user_data);
	g_free (code_path);
	g_free (rpmbuild_path);
	g_free (options_help);
	g_strfreev (files);
	return 0;
}

