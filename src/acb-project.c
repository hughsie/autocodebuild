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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>

#include "acb-project.h"
#include "acb-common.h"

#define ACB_PROJECT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ACB_TYPE_PROJECT, AcbProjectPrivate))

typedef enum
{
	ACB_PROJECT_RCS_GIT,
	ACB_PROJECT_RCS_SVN,
	ACB_PROJECT_RCS_CVS,
	ACB_PROJECT_RCS_BZR,
	ACB_PROJECT_RCS_UNKNOWN
} AcbProjectRcs;

typedef enum {
	ACB_PROJECT_KIND_BUILDING_LOCALLY,
	ACB_PROJECT_KIND_BUILDING_PACKAGE,
	ACB_PROJECT_KIND_COPYING_TARBALL,
	ACB_PROJECT_KIND_CREATING_TARBALL,
	ACB_PROJECT_KIND_CLEANING,
	ACB_PROJECT_KIND_GARBAGE_COLLECTING,
	ACB_PROJECT_KIND_GETTING_UPDATES,
	ACB_PROJECT_KIND_SHOWING_UPDATES,
	ACB_PROJECT_KIND_UPDATING,
	ACB_PROJECT_KIND_LAST
} AcbProjectKind;

typedef struct
{
	gchar			*path;
	gchar			*path_build;
	gchar			*default_code_path;
	gchar			*rpmbuild_path;
	gchar			*package_name;
	gchar			*version;
	gchar			*tarball_name;
	gboolean		 disabled;
	gboolean		 use_ninja;
	guint			 release;
	AcbProjectRcs		 rcs;
} AcbProjectPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (AcbProject, acb_project, G_TYPE_OBJECT)

#define GET_PRIVATE(o) (acb_project_get_instance_private (o))

static gboolean
acb_project_path_suffix_exists (AcbProject *project, const gchar *suffix)
{
	AcbProjectPrivate *priv = GET_PRIVATE (project);
	g_autofree gchar *path = NULL;
	gboolean ret;

	path = g_build_filename (priv->path, suffix, NULL);
	ret = g_file_test (path, G_FILE_TEST_EXISTS);
	return ret;
}

static gboolean
acb_project_write_conf (AcbProject *project, GError **error)
{
	AcbProjectPrivate *priv = GET_PRIVATE (project);
	g_autofree gchar *data = NULL;
	g_autofree gchar *defaults = NULL;
	g_autoptr(GKeyFile) file = NULL;

	/* load file */
	file = g_key_file_new ();
	defaults = g_strdup_printf ("%s/autocodebuild/%s.conf",
				    g_get_user_data_dir (),
				    priv->package_name);

	/* does not yet exist */
	if (!g_file_test (defaults, G_FILE_TEST_EXISTS)) {
		if (!g_file_set_contents (defaults, "#auto-generated\n\n[defaults]\n", -1, error))
			return FALSE;
	}

	/* load latest copy */
	if (!g_key_file_load_from_file (file, defaults, G_KEY_FILE_NONE, error))
		return FALSE;

	/* set new value */
	g_key_file_set_integer (file, "defaults", "Release", priv->release);

	/* push to text */
	data = g_key_file_to_data (file, NULL, error);
	if (data == NULL)
		return FALSE;

	/* save to file */
	return g_file_set_contents (defaults, data, -1, error);
}

static gboolean
acb_project_load_defaults (AcbProject *project)
{
	AcbProjectPrivate *priv = GET_PRIVATE (project);
	gboolean ret;
	g_autofree gchar *defaults;
	g_autoptr(GError) error = NULL;
	g_autoptr(GKeyFile) file = NULL;

	/* find file, but it's okay not to have a file if the defaults are okay */
	defaults = g_strdup_printf ("%s/%s/%s.conf",
				    g_get_user_data_dir (),
				    "autocodebuild",
				    priv->package_name);
	if (!g_file_test (defaults, G_FILE_TEST_EXISTS)) {
		g_debug ("no %s file, creating", defaults);
		ret = acb_project_write_conf (project, &error);
		if (!ret)
			g_warning ("failed to write: %s", error->message);
		return ret;
	}

	/* load file */
	file = g_key_file_new ();
	if (!g_key_file_load_from_file (file, defaults, G_KEY_FILE_NONE, &error)) {
		g_warning ("failed to read: %s", error->message);
		return FALSE;
	}

	/* get values */
//	priv->package_name = g_key_file_get_string (file, "defaults", "PackageName", NULL);
	priv->version = g_key_file_get_string (file, "defaults", "Version", NULL);
	priv->tarball_name = g_key_file_get_string (file, "defaults", "TarballName", NULL);
	priv->disabled = g_key_file_get_boolean (file, "defaults", "Disabled", NULL);
	priv->release = g_key_file_get_integer (file, "defaults", "Release", NULL);
	priv->path = g_key_file_get_string (file, "defaults", "Path", NULL);
	return ret;
}

static gboolean
acb_project_get_from_config_h (AcbProject *project)
{
	AcbProjectPrivate *priv = GET_PRIVATE (project);
	gchar *ptr;
	guint i;
	g_autofree gchar *configh = NULL;
	g_autofree gchar *contents = NULL;
	g_auto(GStrv) split = NULL;

	/* find file */
	configh = g_build_filename (priv->path_build, "config.h", NULL);
	if (!g_file_test (configh, G_FILE_TEST_EXISTS))
		return TRUE;

	/* get contents */
	if (!g_file_get_contents (configh, &contents, NULL, NULL))
		return FALSE;

	/* split into lines */
	split = g_strsplit (contents, "\n", -1);
	for (i = 0; split[i] != NULL; i++) {
		ptr = split[i];
		if (ptr[0] == '\0')
			continue;
		if (g_str_has_prefix (ptr, "#define ")) {
			ptr += 8;
			if (priv->version == NULL &&
			    g_str_has_prefix (ptr, "PACKAGE_VERSION")) {
				ptr += 16;
				g_strdelimit (ptr, "\"", ' ');
				g_strstrip (ptr);
				priv->version = g_strdup (ptr);
			}
			if (priv->version == NULL &&
			    g_str_has_prefix (ptr, "VERSION")) {
				ptr += 8;
				g_strdelimit (ptr, "\"", ' ');
				g_strstrip (ptr);
				priv->version = g_strdup (ptr);
			}
		}
	}
	return TRUE;
}

static gboolean
acb_project_get_from_meson (AcbProject *project)
{
	AcbProjectPrivate *priv = GET_PRIVATE (project);
	guint i;
	g_autofree gchar *configh = NULL;
	g_autofree gchar *contents = NULL;
	g_auto(GStrv) split = NULL;

	/* find file */
	configh = g_build_filename (priv->path, "meson.build", NULL);
	if (!g_file_test (configh, G_FILE_TEST_EXISTS))
		return TRUE;

	/* get contents */
	if (!g_file_get_contents (configh, &contents, NULL, NULL))
		return FALSE;

	/* split into lines */
	split = g_strsplit (contents, "\n", -1);
	for (i = 0; split[i] != NULL; i++) {
		gchar *tmp;
		if (split[i][0] == '\0')
			continue;
		tmp = g_strstr_len (split[i], -1, "version : '");
		if (tmp == NULL)
			continue;
		priv->version = g_strdup (tmp + 11);
		g_strdelimit (priv->version, "'", '\0');
		break;
	}
	return TRUE;
}

void
acb_project_set_rpmbuild_path (AcbProject *project, const gchar *path)
{
	AcbProjectPrivate *priv = GET_PRIVATE (project);

	g_return_if_fail (ACB_IS_PROJECT (project));
	g_return_if_fail (path != NULL);

	g_free (priv->rpmbuild_path);
	priv->rpmbuild_path = g_strdup (path);
}

void
acb_project_set_default_code_path (AcbProject *project, const gchar *path)
{
	AcbProjectPrivate *priv = GET_PRIVATE (project);

	g_return_if_fail (ACB_IS_PROJECT (project));
	g_return_if_fail (path != NULL);

	g_free (priv->default_code_path);
	priv->default_code_path = g_strdup (path);
}

void
acb_project_set_name (AcbProject *project, const gchar *name)
{
	AcbProjectPrivate *priv = GET_PRIVATE (project);
	g_autofree gchar *defaults = NULL;
	g_autofree gchar *path_build = NULL;

	g_return_if_fail (ACB_IS_PROJECT (project));
	g_return_if_fail (name != NULL);

	g_free (priv->package_name);
	priv->package_name = g_strdup (name);

	/* load defaults */
	acb_project_load_defaults (project);
	if (priv->path == NULL) {
		priv->path = g_build_filename (priv->default_code_path,
					       priv->package_name,
					       NULL);
		g_debug ("no Path, so falling back to %s", priv->path);
	}

	/* check is a directory */
	if (!priv->disabled &&
	    !g_file_test (priv->path, G_FILE_TEST_EXISTS)) {
		defaults = g_strdup_printf ("%s/%s/%s.conf",
					    g_get_user_data_dir (),
					    "autocodebuild",
					    priv->package_name);
		g_print ("%s not found, perhaps you have to add 'Path' to %s\n",
			 priv->path, defaults);
		priv->disabled = TRUE;
		return;
	}

	/* set build dir */
	path_build = g_build_filename (priv->path, "build", NULL);
	if (g_file_test (path_build, G_FILE_TEST_EXISTS)) {
		priv->path_build = g_steal_pointer (&path_build);
		priv->use_ninja = TRUE;
	} else {
		priv->path_build = g_strdup (priv->path);
	}

	/* load from config.h */
	acb_project_get_from_config_h (project);

	/* load from meson */
	acb_project_get_from_meson (project);

	/* generate fallbacks */
	if (priv->tarball_name == NULL)
		priv->tarball_name = g_strdup (priv->package_name);

	if (acb_project_path_suffix_exists (project, ".git"))
		priv->rcs = ACB_PROJECT_RCS_GIT;
	else if (acb_project_path_suffix_exists (project, ".svn"))
		priv->rcs = ACB_PROJECT_RCS_SVN;
	else if (acb_project_path_suffix_exists (project, "CVS"))
		priv->rcs = ACB_PROJECT_RCS_CVS;
	else if (acb_project_path_suffix_exists (project, ".bzr"))
		priv->rcs = ACB_PROJECT_RCS_BZR;

	/* debugging */
	g_debug ("path:         %s", priv->path);
	g_debug ("package name: %s", priv->package_name);
	g_debug ("tarball name: %s", priv->tarball_name);
	g_debug ("version:      %s", priv->version);
	g_debug ("release:      %i", priv->release);
	g_debug ("disabled:     %i", priv->disabled);
}

static const gchar *
acb_project_kind_to_title (AcbProjectKind kind)
{
	if (kind == ACB_PROJECT_KIND_BUILDING_LOCALLY)
		return "Building locally";
	if (kind == ACB_PROJECT_KIND_BUILDING_PACKAGE)
		return "Building package";
	if (kind == ACB_PROJECT_KIND_COPYING_TARBALL)
		return "Copying tarball";
	if (kind == ACB_PROJECT_KIND_CREATING_TARBALL)
		return "Creating tarball";
	if (kind == ACB_PROJECT_KIND_CLEANING)
		return "Cleaning";
	if (kind == ACB_PROJECT_KIND_GARBAGE_COLLECTING)
		return "Garbage collecting";
	if (kind == ACB_PROJECT_KIND_UPDATING)
		return "Updating";
	if (kind == ACB_PROJECT_KIND_GETTING_UPDATES)
		return "Getting updates";
	if (kind == ACB_PROJECT_KIND_SHOWING_UPDATES)
		return "Showing updates";
	return NULL;
}

static gchar *
acb_project_get_logfile (AcbProject *project, AcbProjectKind kind)
{
	AcbProjectPrivate *priv = GET_PRIVATE (project);
	g_autofree gchar *filename = NULL;

	if (kind == ACB_PROJECT_KIND_BUILDING_LOCALLY)
		filename = g_strdup_printf ("%s-%s", priv->package_name, "make.log");
	else if (kind == ACB_PROJECT_KIND_BUILDING_PACKAGE)
		filename = g_strdup_printf ("%s-%s", priv->package_name, "build.log");
	else if (kind == ACB_PROJECT_KIND_COPYING_TARBALL)
		filename = g_strdup_printf ("%s-%s", priv->package_name, "copy.log");
	else if (kind == ACB_PROJECT_KIND_CREATING_TARBALL)
		filename = g_strdup_printf ("%s-%s", priv->package_name, "dist.log");
	else if (kind == ACB_PROJECT_KIND_CLEANING)
		filename = g_strdup_printf ("%s-%s", priv->package_name, "clean.log");
	else if (kind == ACB_PROJECT_KIND_UPDATING)
		filename = g_strdup_printf ("%s-%s", priv->package_name, "update.log");
	else if (kind == ACB_PROJECT_KIND_GETTING_UPDATES)
		filename = g_strdup_printf ("%s-%s", priv->package_name, "fetch.log");
	else if (kind == ACB_PROJECT_KIND_SHOWING_UPDATES)
		filename = g_strdup_printf ("%s-%s", priv->package_name, "diffstat.log");
	else
		return NULL;

	return g_build_filename (g_get_user_data_dir (),
				 "autocodebuild",
				 filename,
				 NULL);
}

static gboolean
acb_project_ensure_has_path (const gchar *path)
{
	gint retval;
	g_autofree gchar *folder = NULL;

	/* create leading path */
	folder = g_path_get_dirname (path);
	retval = g_mkdir_with_parents (folder, 0777);
	return (retval == 0);
}

static gboolean
acb_project_run (AcbProject *project,
		 const gchar *command_line,
		 AcbProjectKind kind,
		 GError **error)
{
	AcbProjectPrivate *priv = GET_PRIVATE (project);
	const gchar *title;
	gboolean ret;
	gint exit_status;
	g_autofree gchar *diffstat = NULL;
	g_autofree gchar *logfile = NULL;
	g_autofree gchar *standard_error = NULL;
	g_autofree gchar *standard_out = NULL;
	g_auto(GStrv) argv = NULL;

	g_return_val_if_fail (ACB_IS_PROJECT (project), FALSE);

	title = acb_project_kind_to_title (kind);
	g_print ("%s %s...", title, priv->package_name);

	argv = g_strsplit (command_line, " ", -1);
	ret = g_spawn_sync (priv->path_build,
			    argv,
			    NULL,
			    G_SPAWN_SEARCH_PATH,
			    NULL,
			    NULL,
			    &standard_out,
			    &standard_error,
			    &exit_status,
			    error);
	if (!ret)
		return FALSE;

	/* fail if we got the wrong retval */
	if (exit_status != 0) {
		ret = FALSE;
		g_set_error (error, 1, 0, "%s: %s\n%s", "Failed to run", command_line, standard_error);
		return FALSE;
	}

	/* save to file */
	logfile = acb_project_get_logfile (project, kind);
	if (logfile != NULL) {
		acb_project_ensure_has_path (logfile);
		if (!g_file_set_contents (logfile, standard_out, -1, error))
			return FALSE;
	}

	/* show any updates */
	if (kind == ACB_PROJECT_KIND_SHOWING_UPDATES) {
		if (standard_out[0] == '\0') {
			g_print ("%s\n", "No updates");
		} else {
			diffstat = g_strdup_printf ("/usr/bin/diffstat %s", logfile);
			ret = g_spawn_command_line_sync (diffstat,
							 &standard_out,
							 NULL,
							 NULL,
							 error);
			if (!ret)
				return FALSE;
			if (standard_out == NULL || standard_out[0] == '\0')
				g_print ("Updated (but no diffstat):\n");
			else
				g_print ("Updated:\n%s\n", standard_out);
		}
	} else {
		g_print ("\t%s\n", "Done");
	}
	return TRUE;
}

gboolean
acb_project_clean (AcbProject *project, GError **error)
{
	gboolean ret;
	AcbProjectPrivate *priv = GET_PRIVATE (project);

	g_return_val_if_fail (ACB_IS_PROJECT (project), FALSE);

	/* disabled */
	if (priv->disabled)
		return TRUE;

	/* clean the tree */
	if (!acb_project_run (project, "make clean", ACB_PROJECT_KIND_CLEANING, error))
		return FALSE;

	/* clean repo? */
	if (priv->rcs == ACB_PROJECT_RCS_GIT) {
		ret = acb_project_run (project, "git gc --aggressive",
				       ACB_PROJECT_KIND_GARBAGE_COLLECTING, error);
		if (!ret)
			return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
acb_project_update (AcbProject *project, GError **error)
{
	gboolean ret;
	AcbProjectPrivate *priv = GET_PRIVATE (project);

	g_return_val_if_fail (ACB_IS_PROJECT (project), FALSE);

	/* disabled */
	if (priv->disabled)
		return TRUE;

	/* git does this in two stages */
	if (priv->rcs == ACB_PROJECT_RCS_GIT) {
		ret = acb_project_run (project, "git fetch",
				       ACB_PROJECT_KIND_GETTING_UPDATES, error);
		if (!ret)
			return FALSE;

		/* TODO: don't assume master */

		/* show differences */
		ret = acb_project_run (project, "git diff master..origin/master", ACB_PROJECT_KIND_SHOWING_UPDATES, error);
		if (!ret)
			return FALSE;
	}

	/* apply the updates */
	if (priv->rcs == ACB_PROJECT_RCS_GIT) {
		return acb_project_run (project, "git pull --rebase", ACB_PROJECT_KIND_UPDATING, error);
	}
	if (priv->rcs == ACB_PROJECT_RCS_SVN) {
		return acb_project_run (project, "svn up",
					ACB_PROJECT_KIND_UPDATING, error);
	}
	if (priv->rcs == ACB_PROJECT_RCS_CVS) {
		return acb_project_run (project, "cvs up",
					ACB_PROJECT_KIND_UPDATING, error);
	}
	if (priv->rcs == ACB_PROJECT_RCS_BZR) {
		return acb_project_run (project, "bzr up",
					ACB_PROJECT_KIND_UPDATING, error);
	}
	g_print ("No detected RCS for %s!\n", priv->package_name);
	return TRUE;
}

static gboolean
acb_project_directory_remove_contents (const gchar *directory)
{
	const gchar *filename;
	gint retval;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GError) error = NULL;

	/* try to open */
	dir = g_dir_open (directory, 0, &error);
	if (dir == NULL) {
		g_warning ("cannot open directory: %s", error->message);
		return TRUE;
	}

	/* find each */
	while ((filename = g_dir_read_name (dir))) {
		g_autofree gchar *src = NULL;
		src = g_build_filename (directory, filename, NULL);
		retval = g_unlink (src);
		if (retval != 0)
			g_warning ("failed to delete %s", src);
	}
	return TRUE;
}

static gboolean
acb_project_bump_release (AcbProject *project, GError **error)
{
	AcbProjectPrivate *priv = GET_PRIVATE (project);
	priv->release++;
	return acb_project_write_conf (project, error);
}

static void
acb_project_remove_all_files_with_prefix (const gchar *directory, const gchar *prefix)
{
	const gchar *filename;
	gint retval;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GError) error = NULL;

	/* try to open */
	dir = g_dir_open (directory, 0, &error);
	if (dir == NULL) {
		g_warning ("cannot open directory: %s", error->message);
		return;
	}

	/* find each */
	while ((filename = g_dir_read_name (dir))) {
		g_autofree gchar *src = NULL;
		if (!g_str_has_prefix (filename, prefix))
			continue;
		src = g_build_filename (directory, filename, NULL);
		retval = g_unlink (src);
		if (retval != 0)
			g_warning ("failed to delete %s", src);
	}
}

static gboolean
acb_project_copy_file (const gchar *src, const gchar *dest)
{
	gsize length;
	g_autofree gchar *data = NULL;

	/* just copy data */
	if (!g_file_get_contents (src, &data, &length, NULL))
		return FALSE;
	if (!g_file_set_contents (dest, data, length, NULL))
		return FALSE;
	return TRUE;
}

static void
acb_project_move_all_files_with_prefix (const gchar *directory,
					const gchar *prefix,
					const gchar *directory_dest)
{
	const gchar *filename;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GError) error = NULL;

	/* try to open */
	dir = g_dir_open (directory, 0, &error);
	if (dir == NULL) {
		g_warning ("cannot open directory: %s", error->message);
		return;
	}

	/* find each */
	while ((filename = g_dir_read_name (dir))) {
		g_autofree gchar *src = NULL;
		g_autofree gchar *dest = NULL;
		if (!g_str_has_prefix (filename, prefix))
			continue;
		src = g_build_filename (directory, filename, NULL);
		dest = g_build_filename (directory_dest, filename, NULL);
		acb_project_copy_file (src, dest);
	}
}

gboolean
acb_project_make (AcbProject *project, GError **error)
{
	g_return_val_if_fail (ACB_IS_PROJECT (project), FALSE);
	return acb_project_run (project, "make",
				ACB_PROJECT_KIND_BUILDING_LOCALLY, error);
}

gboolean
acb_project_build (AcbProject *project, GError **error)
{
	AcbProjectPrivate *priv = GET_PRIVATE (project);
	GDate *date;
	gboolean ret = TRUE;
	gchar shortdate[128];
	gchar longdate[128];
	g_autofree gchar *alphatag = NULL;
	g_autofree gchar *cmdline2 = NULL;
	g_autofree gchar *cmdline = NULL;
	g_autofree gchar *dest = NULL;
	g_autofree gchar *rpmbuild_rpms = NULL;
	g_autofree gchar *rpmbuild_sources = NULL;
	g_autofree gchar *rpmbuild_specs = NULL;
	g_autofree gchar *rpmbuild_srpms = NULL;
	g_autofree gchar *spec = NULL;
	g_autofree gchar *src = NULL;
	g_autofree gchar *standard_out = NULL;
	g_autofree gchar *tarball = NULL;

	g_return_val_if_fail (ACB_IS_PROJECT (project), FALSE);

	/* disabled */
	if (priv->disabled)
		return TRUE;

	/* check we've got a spec file */
	spec = g_strdup_printf ("%s/%s/%s.spec.in",
				g_get_user_data_dir (),
				"autocodebuild",
				priv->package_name);
	ret = g_file_test (spec, G_FILE_TEST_EXISTS);
	if (!ret) {
		g_set_error (error, 1, 0, "spec file was not found: %s", spec);
		return FALSE;
	}

	/* then make tarball */
	if (priv->use_ninja) {
		ret = acb_project_run (project, "ninja-build dist",
				       ACB_PROJECT_KIND_CREATING_TARBALL, error);
		if (!ret)
			return FALSE;
	} else {
		ret = acb_project_run (project, "make dist",
				       ACB_PROJECT_KIND_CREATING_TARBALL, error);
		if (!ret)
			return FALSE;
	}

	/* clean previous build files */
	g_print ("%s...", "Cleaning previous package files");
	rpmbuild_rpms = g_build_filename (priv->rpmbuild_path, "RPMS", NULL);
	rpmbuild_srpms = g_build_filename (priv->rpmbuild_path, "SRPMS", NULL);
	rpmbuild_sources = g_build_filename (priv->rpmbuild_path, "SOURCES", NULL);
	rpmbuild_specs = g_build_filename (priv->rpmbuild_path, "SPECS", NULL);
	acb_project_directory_remove_contents (rpmbuild_rpms);
	acb_project_directory_remove_contents (rpmbuild_srpms);
	g_print ("\t%s\n", "Done");

	/* get the date formats */
	date = g_date_new ();
	g_date_set_time_t (date, time (NULL));
	g_date_strftime (shortdate, 128, "%Y%m%d", date);	/* 20060501 */
	g_date_strftime (longdate, 128, "%a %b %d %Y", date);	/* Wed May 01 2006 */
	g_date_free (date);

	/* get the alpha tag */
	if (priv->rcs == ACB_PROJECT_RCS_GIT)
		alphatag = g_strdup_printf (".%sgit", shortdate); /* .20070409svn */
	else if (priv->rcs == ACB_PROJECT_RCS_SVN)
		alphatag = g_strdup_printf (".%ssvn", shortdate);
	else if (priv->rcs == ACB_PROJECT_RCS_CVS)
		alphatag = g_strdup_printf (".%scvs", shortdate);

	/* do the replacement */
	cmdline = g_strdup_printf ("sed \"s/#VERSION#/%s/g;s/#BUILD#/%i/g;s/#ALPHATAG#/%s/g;s/#LONGDATE#/%s/g\" %s",
				   priv->version, priv->release, alphatag, longdate, spec);
	ret = g_spawn_command_line_sync (cmdline, &standard_out, NULL, NULL, error);
	if (!ret)
		return FALSE;

	/* save to the new file */
	dest = g_strdup_printf ("%s/%s.spec", rpmbuild_specs, priv->package_name);
	if (!g_file_set_contents (dest, standard_out, -1, error))
		return FALSE;

	/* get the tarball */
	if (g_strstr_len (priv->tarball_name, -1, ".") != NULL)
		tarball = g_strdup_printf ("%s/%s.tar.bz2", priv->path, priv->tarball_name);
	else
		tarball = g_strdup_printf ("%s/%s-%s.tar.bz2", priv->path, priv->tarball_name, priv->version);
	if (!g_file_test (tarball, G_FILE_TEST_EXISTS)) {
		g_debug ("bzipped tarball %s not found", tarball);
		tarball = g_strdup_printf ("%s/%s-%s.tar.gz", priv->path, priv->tarball_name, priv->version);
	}
	if (!g_file_test (tarball, G_FILE_TEST_EXISTS)) {
		g_debug ("gzipped tarball %s not found", tarball);
		tarball = g_strdup_printf ("%s/%s-%s.tar.xz", priv->path, priv->tarball_name, priv->version);
	}
	if (!g_file_test (tarball, G_FILE_TEST_EXISTS)) {
		g_debug ("gzipped meson tarball %s not found", tarball);
		tarball = g_strdup_printf ("%s/build/meson-dist/%s-%s.tar.xz", priv->path, priv->tarball_name, priv->version);
	}
	if (!g_file_test (tarball, G_FILE_TEST_EXISTS)) {
		g_debug ("xz tarball %s not found", tarball);
		tarball = g_strdup_printf ("%s/%s-%s.zip", priv->path, priv->tarball_name, priv->version);
	}
	if (!g_file_test (tarball, G_FILE_TEST_EXISTS)) {
		g_debug ("zipped tarball %s not found", tarball);
		g_set_error (error, 1, 0, "cannot find source in %s", priv->path);
		return FALSE;
	}

	/* copy tarball .tar.* build root */
	cmdline2 = g_strdup_printf ("cp %s %s", tarball, rpmbuild_sources);
	ret = acb_project_run (project, cmdline2, ACB_PROJECT_KIND_COPYING_TARBALL, error);
	if (!ret)
		return FALSE;

	/* build the rpm */
	cmdline2 = g_strdup_printf ("rpmbuild -ba %s", dest);
	ret = acb_project_run (project, cmdline2, ACB_PROJECT_KIND_BUILDING_PACKAGE, error);
	if (!ret)
		return FALSE;

	/* increment the release */
	g_print ("%s...", "Incrementing release");
	if (!acb_project_bump_release (project, error))
		return FALSE;
	g_print ("\t%s\n", "Done");

	/* delete old versions in repo directory */
	g_print ("%s...", "Deleting old versions");
	src = g_build_filename (priv->rpmbuild_path, "REPOS/fedora/26/x86_64", NULL);
	acb_project_remove_all_files_with_prefix (src, priv->package_name);
	src = g_build_filename (priv->rpmbuild_path, "REPOS/fedora/26/SRPMS", NULL);
	acb_project_remove_all_files_with_prefix (src, priv->package_name);
	g_print ("\t%s\n", "Done");

	/* copy into repo directory */
	g_print ("%s...", "Copying new version");
	dest = g_build_filename (priv->rpmbuild_path, "REPOS/fedora/26/x86_64", NULL);
	acb_project_move_all_files_with_prefix (rpmbuild_rpms, priv->package_name, dest);
	dest = g_build_filename (priv->rpmbuild_path, "REPOS/fedora/26/SRPMS", NULL);
	acb_project_move_all_files_with_prefix (rpmbuild_srpms, priv->package_name, dest);
	g_print ("\t%s\n", "Done");

	/* remove generated file */
	g_unlink (dest);
	return TRUE;
}

static void
acb_project_finalize (GObject *object)
{
	AcbProject *project;
	AcbProjectPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ACB_IS_PROJECT (object));
	project = ACB_PROJECT (object);
	priv = GET_PRIVATE (project);

	g_free (priv->path);
	g_free (priv->path_build);
	g_free (priv->default_code_path);
	g_free (priv->rpmbuild_path);
	g_free (priv->version);
	g_free (priv->tarball_name);
	g_free (priv->package_name);

	G_OBJECT_CLASS (acb_project_parent_class)->finalize (object);
}

static void
acb_project_class_init (AcbProjectClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = acb_project_finalize;
}

static void
acb_project_init (AcbProject *project)
{
	AcbProjectPrivate *priv = GET_PRIVATE (project);
	priv->rcs = ACB_PROJECT_RCS_UNKNOWN;
}

AcbProject *
acb_project_new (void)
{
	AcbProject *project;
	project = g_object_new (ACB_TYPE_PROJECT, NULL);
	return ACB_PROJECT (project);
}

