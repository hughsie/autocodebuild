/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>

#include "acb-project.h"
#include "acb-common.h"

#include "egg-debug.h"

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

struct _AcbProjectPrivate
{
	gchar			*path;
	gchar			*basename;
	gchar			*packagename;
	gchar			*version;
	gboolean		 disabled;
	guint			 release;
	AcbProjectRcs		 rcs;
};

G_DEFINE_TYPE (AcbProject, acb_project, G_TYPE_OBJECT)

/**
 * acb_project_path_suffix_exists:
 **/
static gboolean
acb_project_path_suffix_exists (AcbProject *project, const gchar *suffix)
{
	gchar *path;
	gboolean ret;

	path = g_build_filename (project->priv->path, suffix, NULL);
	ret = g_file_test (path, G_FILE_TEST_EXISTS);
	g_free (path);
	return ret;
}

/**
 * acb_project_load_defaults:
 **/
static gboolean
acb_project_load_defaults (AcbProject *project)
{
	GError *error = NULL;
	gchar *defaults;
	GKeyFile *file = NULL;
	gboolean ret = TRUE;
	AcbProjectPrivate *priv = project->priv;

	/* find file, but it's okay not to have a file if the defaults are okay */
	defaults = g_build_filename (priv->path, ".acb", "defaults.conf", NULL);
	if (!g_file_test (defaults, G_FILE_TEST_EXISTS))
		goto out;

	/* load file */
	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, defaults, G_KEY_FILE_NONE, &error);
	if (!ret) {
		egg_warning ("failed to read: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get values */
	priv->packagename = g_key_file_get_string (file, "defaults", "PackageName", NULL);
	priv->version = g_key_file_get_string (file, "defaults", "Version", NULL);
	priv->disabled = g_key_file_get_boolean (file, "defaults", "Disabled", NULL);
	priv->release = g_key_file_get_integer (file, "defaults", "Release", NULL);
out:
	g_free (defaults);
	if (file != NULL)
		g_key_file_free (file);
	return ret;
}

/**
 * acb_project_get_from_config_h:
 **/
static gboolean
acb_project_get_from_config_h (AcbProject *project)
{
	gchar *configh;
	gchar *contents = NULL;
	gchar **split = NULL;
	gchar *ptr;
	gboolean ret = TRUE;
	guint i;
	AcbProjectPrivate *priv = project->priv;

	/* find file */
	configh = g_build_filename (priv->path, "config.h", NULL);
	if (!g_file_test (configh, G_FILE_TEST_EXISTS))
		goto out;

	/* get contents */
	ret = g_file_get_contents (configh, &contents, NULL, NULL);
	if (!ret)
		goto out;

	/* split into lines */
	split = g_strsplit (contents, "\n", -1);
	for (i=0; split[i] != NULL; i++) {
		ptr = split[i];
		if (ptr[0] == '\0')
			continue;
		if (g_str_has_prefix (ptr, "#define ")) {
			ptr += 8;
			if (priv->version == NULL && g_str_has_prefix (ptr, "PACKAGE_VERSION")) {
				ptr += 16;
				g_strdelimit (ptr, "\"", ' ');
				g_strstrip (ptr);
				priv->version = g_strdup (ptr);
			}
			if (priv->version == NULL && g_str_has_prefix (ptr, "VERSION")) {
				ptr += 8;
				g_strdelimit (ptr, "\"", ' ');
				g_strstrip (ptr);
				priv->version = g_strdup (ptr);
			}
		}
	}
out:
	g_free (configh);
	g_free (contents);
	g_strfreev (split);
	return ret;
}

/**
 * acb_project_set_path:
 **/
gboolean
acb_project_set_path (AcbProject *project, const gchar *path)
{
	AcbProjectPrivate *priv = project->priv;

	g_return_val_if_fail (ACB_IS_PROJECT (project), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	g_free (priv->path);
	g_free (priv->basename);
	priv->path = g_strdup (path);
	priv->basename = g_path_get_basename (path);

	if (acb_project_path_suffix_exists (project, ".git"))
		priv->rcs = ACB_PROJECT_RCS_GIT;
	else if (acb_project_path_suffix_exists (project, ".svn"))
		priv->rcs = ACB_PROJECT_RCS_SVN;
	else if (acb_project_path_suffix_exists (project, "CVS"))
		priv->rcs = ACB_PROJECT_RCS_CVS;
	else if (acb_project_path_suffix_exists (project, ".bzr"))
		priv->rcs = ACB_PROJECT_RCS_BZR;

	/* load defaults */
	acb_project_load_defaults (project);

	/* load from config.h */
	acb_project_get_from_config_h (project);

	/* generate fallbacks */
	if (priv->packagename == NULL)
		priv->packagename = g_strdup (priv->basename);
	return TRUE;
}

/**
 * acb_project_kind_to_title:
 **/
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

/**
 * acb_project_get_logfile:
 **/
static gchar *
acb_project_get_logfile (AcbProject *project, AcbProjectKind kind)
{
	AcbProjectPrivate *priv = project->priv;

	if (kind == ACB_PROJECT_KIND_BUILDING_LOCALLY)
		return g_build_filename (priv->path, ".acb", "make.log", NULL);
	if (kind == ACB_PROJECT_KIND_BUILDING_PACKAGE)
		return g_build_filename (priv->path, ".acb", "build.log", NULL);
	if (kind == ACB_PROJECT_KIND_COPYING_TARBALL)
		return g_build_filename (priv->path, ".acb", "copy.log", NULL);
	if (kind == ACB_PROJECT_KIND_CREATING_TARBALL)
		return g_build_filename (priv->path, ".acb", "dist.log", NULL);
	if (kind == ACB_PROJECT_KIND_CLEANING)
		return g_build_filename (priv->path, ".acb", "clean.log", NULL);
	if (kind == ACB_PROJECT_KIND_UPDATING)
		return g_build_filename (priv->path, ".acb", "update.log", NULL);
	if (kind == ACB_PROJECT_KIND_GETTING_UPDATES)
		return g_build_filename (priv->path, ".acb", "fetch.log", NULL);
	return NULL;
}

/**
 * acb_project_ensure_has_path:
 **/
static gboolean
acb_project_ensure_has_path (const gchar *path)
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
 * acb_project_run:
 **/
static gboolean
acb_project_run (AcbProject *project, const gchar *command_line, AcbProjectKind kind, GError **error)
{
	gboolean ret;
	gchar *standard_error = NULL;
	gchar *standard_out = NULL;
	gchar **argv = NULL;
	gint exit_status;
	const gchar *title;
	gchar *logfile = NULL;
	AcbProjectPrivate *priv = project->priv;

	g_return_val_if_fail (ACB_IS_PROJECT (project), FALSE);

	title = acb_project_kind_to_title (kind);
	g_print ("%s %s...", title, priv->basename);

	argv = g_strsplit (command_line, " ", -1);
	ret = g_spawn_sync (priv->path, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &standard_out, &standard_error, &exit_status, error);
	if (exit_status != 0) {
		g_print ("%s: %s\n%s", "Failed to run", command_line, standard_error);
		goto out;
	}

	/* save to file */
	logfile = acb_project_get_logfile (project, kind);
	if (logfile != NULL) {
		acb_project_ensure_has_path (logfile);
		ret = g_file_set_contents (logfile, standard_out, -1, error);
		if (!ret)
			goto out;
	}

	/* show any updates */
	if (kind == ACB_PROJECT_KIND_SHOWING_UPDATES) {
		if (standard_out[0] == '\0')
			g_print ("%s\n", "No updates");
		else
			g_print ("%s\n", standard_out);
	} else {
		g_print ("\t%s\n", "Done");
	}

out:
	g_strfreev (argv);
	g_free (standard_out);
	g_free (standard_error);
	g_free (logfile);
	return ret;
}

/**
 * acb_project_clean:
 **/
gboolean
acb_project_clean (AcbProject *project, GError **error)
{
	gboolean ret = TRUE;
	AcbProjectPrivate *priv = project->priv;

	g_return_val_if_fail (ACB_IS_PROJECT (project), FALSE);

	/* disabled */
	if (priv->disabled)
		goto out;

	/* clean the tree */
	ret = acb_project_run (project, "make clean", ACB_PROJECT_KIND_CLEANING, error);
	if (!ret)
		goto out;

	/* clean repo? */
	if (priv->rcs == ACB_PROJECT_RCS_GIT) {
		ret = acb_project_run (project, "git gc", ACB_PROJECT_KIND_GARBAGE_COLLECTING, error);
		if (!ret)
			goto out;
	}

out:
	return ret;
}

/**
 * acb_project_update:
 **/
gboolean
acb_project_update (AcbProject *project, GError **error)
{
	gboolean ret = TRUE;
	AcbProjectPrivate *priv = project->priv;

	g_return_val_if_fail (ACB_IS_PROJECT (project), FALSE);

	/* disabled */
	if (priv->disabled)
		goto out;

	/* git does this in two stages */
	if (priv->rcs == ACB_PROJECT_RCS_GIT) {
		ret = acb_project_run (project, "git fetch", ACB_PROJECT_KIND_GETTING_UPDATES, error);
		if (!ret)
			goto out;

		/* show differences */
		ret = acb_project_run (project, "git shortlog origin/master..master", ACB_PROJECT_KIND_SHOWING_UPDATES, error);
		if (!ret)
			goto out;
	}

	/* apply the updates */
	if (priv->rcs == ACB_PROJECT_RCS_GIT) {
		ret = acb_project_run (project, "git pull --rebase", ACB_PROJECT_KIND_UPDATING, error);
		goto out;
	}
	if (priv->rcs == ACB_PROJECT_RCS_SVN) {
		ret = acb_project_run (project, "svn up", ACB_PROJECT_KIND_UPDATING, error);
		goto out;
	}
	if (priv->rcs == ACB_PROJECT_RCS_CVS) {
		ret = acb_project_run (project, "cvs up", ACB_PROJECT_KIND_UPDATING, error);
		goto out;
	}
	if (priv->rcs == ACB_PROJECT_RCS_BZR) {
		ret = acb_project_run (project, "bzr up", ACB_PROJECT_KIND_UPDATING, error);
		goto out;
	}
out:
	return ret;
}

/**
 * acb_project_directory_remove_contents:
 *
 * Does not remove the directory itself, only the contents.
 **/
static gboolean
acb_project_directory_remove_contents (const gchar *directory)
{
	GDir *dir;
	GError *error = NULL;
	const gchar *filename;
	gchar *src;
	gint retval;

	/* try to open */
	dir = g_dir_open (directory, 0, &error);
	if (dir == NULL) {
		egg_warning ("cannot open directory: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find each */
	while ((filename = g_dir_read_name (dir))) {
		src = g_build_filename (directory, filename, NULL);
		retval = g_unlink (src);
		if (retval != 0)
			egg_warning ("failed to delete %s", src);
		g_free (src);
	}
	g_dir_close (dir);
out:
	return TRUE;
}

/**
 * acb_project_bump_release:
 **/
static gboolean
acb_project_bump_release (AcbProject *project, GError **error)
{
	gchar *data = NULL;
	gchar *defaults = NULL;
	GKeyFile *file = NULL;
	gboolean ret = TRUE;
	AcbProjectPrivate *priv = project->priv;

	/* inc */
	priv->release++;

	/* load file */
	file = g_key_file_new ();
	defaults = g_build_filename (priv->path, ".acb", "defaults.conf", NULL);

	/* does not yet exist */
	if (!g_file_test (defaults, G_FILE_TEST_EXISTS)) {
		ret = g_file_set_contents (defaults, "#auto-generated", -1, error);
		if (!ret)
			goto out;
	}

	/* load latest copy */
	ret = g_key_file_load_from_file (file, defaults, G_KEY_FILE_NONE, error);
	if (!ret)
		goto out;

	/* set new value */
	g_key_file_set_integer (file, "defaults", "Release", priv->release);

	/* push to text */
	data = g_key_file_to_data (file, NULL, error);
	if (data == NULL) {
		ret = FALSE;
		goto out;
	}

	/* save to file */
	ret = g_file_set_contents (defaults, data, -1, error);
	if (!ret)
		goto out;
out:
	g_free (data);
	g_free (defaults);
	g_key_file_free (file);
	return ret;
}

/**
 * acb_project_build:
 **/
gboolean
acb_project_build (AcbProject *project, GError **error)
{
	gboolean ret = TRUE;
	GDate *date;
	gchar shortdate[128];
	gchar longdate[128];
	gchar *alphatag = NULL;
	gchar *src = NULL;
	gchar *dest = NULL;
	gchar *standard_out = NULL;
	gchar *cmdline = NULL;
	gchar *cmdline2 = NULL;
	AcbProjectPrivate *priv = project->priv;

	g_return_val_if_fail (ACB_IS_PROJECT (project), FALSE);

	/* disabled */
	if (priv->disabled)
		goto out;

	/* first build locally */
	ret = acb_project_run (project, "make", ACB_PROJECT_KIND_BUILDING_LOCALLY, error);
	if (!ret)
		goto out;

	/* then make tarball */
	ret = acb_project_run (project, "make dist", ACB_PROJECT_KIND_CREATING_TARBALL, error);
	if (!ret)
		goto out;

	/* clean previous build files */
	g_print ("%s...", "Cleaning previous package files");
	acb_project_directory_remove_contents ("/home/hughsie/rpmbuild/SRPMS");
	acb_project_directory_remove_contents ("/home/hughsie/rpmbuild/RPMS");
	g_print ("\t%s\n", "Done");

	/* get the date formats */
	date = g_date_new ();
	g_date_set_time_t (date, time (NULL));
	g_date_strftime (shortdate, 128, "%Y%m%d", date); /* 20060501 */
	g_date_strftime (longdate, 128, "%a %b %d %Y", date); /* Wed May 01 2006 */
	g_date_free (date);

	/* get the alpha tag */
	if (priv->rcs == ACB_PROJECT_RCS_GIT)
		alphatag = g_strdup_printf (".%sgit", shortdate); /* .20070409svn */
	if (priv->rcs == ACB_PROJECT_RCS_SVN)
		alphatag = g_strdup_printf (".%ssvn", shortdate);
	if (priv->rcs == ACB_PROJECT_RCS_CVS)
		alphatag = g_strdup_printf (".%scvs", shortdate);

	/* do the replacement */
	src = g_strdup_printf ("%s/%s/%s.spec.in", priv->path, ".acb", priv->packagename);
	cmdline = g_strdup_printf ("sed \"s/#VERSION#/%s/g;s/#BUILD#/%i/g;s/#ALPHATAG#/%s/g;s/#LONGDATE#/%s/g\" %s",
				   priv->version, priv->release, alphatag, longdate, src);
	ret = g_spawn_command_line_sync (cmdline, &standard_out, NULL, NULL, error);
	if (!ret)
		goto out;

	/* save to the new file */
	dest = g_strdup_printf ("%s/%s.spec", "/home/hughsie/rpmbuild/SPECS", priv->packagename);
	ret = g_file_set_contents (dest, standard_out, -1, error);
	if (!ret)
		goto out;

	/* copy tarball .tar.* build root */
	cmdline2 = g_strdup_printf ("cp %s/%s-%s.tar.gz /home/hughsie/rpmbuild/SOURCES", priv->path, priv->packagename, priv->version);
	ret = acb_project_run (project, cmdline2, ACB_PROJECT_KIND_COPYING_TARBALL, error);
	if (!ret)
		goto out;

	/* build the rpm */
	cmdline2 = g_strdup_printf ("rpmbuild -ba %s", dest);
	ret = acb_project_run (project, cmdline2, ACB_PROJECT_KIND_BUILDING_PACKAGE, error);
	if (!ret)
		goto out;

	/* increment the release */
	g_print ("%s...", "Incrementing release");
	ret = acb_project_bump_release (project, error);
	if (!ret)
		goto out;
	g_print ("\t%s\n", "Done");

	/* delete old versions in repo directory */
	g_print ("%s...", "Deleting old versions");
	cmdline2 = g_strdup_printf ("rm /home/hughsie/rpmbuild/REPOS/fedora/12/i386/%s*.rpm", priv->packagename);
	ret = g_spawn_command_line_sync (cmdline2, NULL, NULL, NULL, error);
	if (!ret)
		goto out;
	cmdline2 = g_strdup_printf ("rm /home/hughsie/rpmbuild/REPOS/fedora/12/SRPMS/%s*.src.rpm", priv->packagename);
	ret = g_spawn_command_line_sync (cmdline2, NULL, NULL, NULL, error);
	if (!ret)
		goto out;
	g_print ("\t%s\n", "Done");

	/* copy into repo directory */
	g_print ("%s...", "Copying new version");
	cmdline2 = g_strdup_printf ("mv /home/hughsie/rpmbuild/RPMS/%s-*.rpm /home/hughsie/rpmbuild/REPOS/fedora/12/i386", priv->packagename);
	ret = g_spawn_command_line_sync (cmdline2, NULL, NULL, NULL, error);
	if (!ret)
		goto out;
	cmdline2 = g_strdup_printf ("mv /home/hughsie/rpmbuild/SRPMS/%s-*.rpm /home/hughsie/rpmbuild/REPOS/fedora/12/SRPMS", priv->packagename);
	ret = g_spawn_command_line_sync (cmdline2, NULL, NULL, NULL, error);
	if (!ret)
		goto out;
	g_print ("\t%s\n", "Done");

	/* remove generated file */
	g_unlink (dest);

out:
	g_free (standard_out);
	g_free (cmdline);
	g_free (cmdline2);
	g_free (alphatag);
	g_free (dest);
	g_free (src);
	return ret;
}

/**
 * acb_project_finalize:
 **/
static void
acb_project_finalize (GObject *object)
{
	AcbProject *project;
	AcbProjectPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ACB_IS_PROJECT (object));
	project = ACB_PROJECT (object);
	priv = project->priv;

	g_free (priv->path);
	g_free (priv->basename);
	g_free (priv->version);
	g_free (priv->packagename);

	G_OBJECT_CLASS (acb_project_parent_class)->finalize (object);
}

/**
 * acb_project_class_init:
 **/
static void
acb_project_class_init (AcbProjectClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = acb_project_finalize;

	g_type_class_add_private (klass, sizeof (AcbProjectPrivate));
}

/**
 * acb_project_init:
 **/
static void
acb_project_init (AcbProject *project)
{
	project->priv = ACB_PROJECT_GET_PRIVATE (project);
	project->priv->path = NULL;
	project->priv->basename = NULL;
	project->priv->version = NULL;
	project->priv->packagename = NULL;
	project->priv->rcs = ACB_PROJECT_RCS_UNKNOWN;
}

/**
 * acb_project_new:
 *
 * Return value: A new #AcbProject class instance.
 **/
AcbProject *
acb_project_new (void)
{
	AcbProject *project;
	project = g_object_new (ACB_TYPE_PROJECT, NULL);
	return ACB_PROJECT (project);
}
