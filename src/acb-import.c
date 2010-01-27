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

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <cairo/cairo-svg.h>
#include <pango/pangocairo.h>

#include "acb-import.h"
#include "acb-project.h"

#include "egg-debug.h"

#define ACB_IMPORT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ACB_TYPE_IMPORT, AcbImportPrivate))

typedef enum
{
	ACB_IMPORT_CMD0_INDIVIDUAL,
	ACB_IMPORT_CMD0_FAMILY,
	ACB_IMPORT_CMD0_UNKNOWN
} AcbImportCmd0;

typedef enum
{
	ACB_IMPORT_CMD1_NAME,
	ACB_IMPORT_CMD1_SEX,
	ACB_IMPORT_CMD1_BIRTH,
	ACB_IMPORT_CMD1_DEATH,
	ACB_IMPORT_CMD1_HUSBAND,
	ACB_IMPORT_CMD1_WIFE,
	ACB_IMPORT_CMD1_MARRIAGE,
	ACB_IMPORT_CMD1_CHILDREN,
	ACB_IMPORT_CMD1_UNKNOWN
} AcbImportCmd1;

typedef enum
{
	ACB_IMPORT_CMD2_GIVEN,
	ACB_IMPORT_CMD2_SURNAME,
	ACB_IMPORT_CMD2_DATE,
	ACB_IMPORT_CMD2_PLACE,
	ACB_IMPORT_CMD2_UNKNOWN
} AcbImportCmd2;

struct _AcbImportPrivate
{
	GPtrArray		*people;
	AcbImportCmd0		 cmd0;
	AcbImportCmd1		 cmd1;
	AcbImportCmd2		 cmd2;
	gchar			*date;
	gchar			*given;
	gchar			*husband;
	gchar			*wife;
	GHashTable		*hash;
	AcbProject		*current_project;
	gboolean		 set_name;
	GPtrArray		*siblings;
};

G_DEFINE_TYPE (AcbImport, acb_import, G_TYPE_OBJECT)

/**
 * acb_import_convert_cmd0:
 **/
static AcbImportCmd0
acb_import_convert_cmd0 (const gchar *text)
{
	if (g_strcmp0 (text, "INDI") == 0)
		return ACB_IMPORT_CMD0_INDIVIDUAL;
	if (g_strcmp0 (text, "FAM") == 0)
		return ACB_IMPORT_CMD0_FAMILY;
	return ACB_IMPORT_CMD0_UNKNOWN;
}

/**
 * acb_import_convert_cmd1:
 **/
static AcbImportCmd1
acb_import_convert_cmd1 (const gchar *text)
{
	if (g_strcmp0 (text, "NAME") == 0)
		return ACB_IMPORT_CMD1_NAME;
	if (g_strcmp0 (text, "SEX") == 0)
		return ACB_IMPORT_CMD1_SEX;
	if (g_strcmp0 (text, "BIRT") == 0)
		return ACB_IMPORT_CMD1_BIRTH;
	if (g_strcmp0 (text, "DEAT") == 0)
		return ACB_IMPORT_CMD1_DEATH;
	if (g_strcmp0 (text, "HUSB") == 0)
		return ACB_IMPORT_CMD1_HUSBAND;
	if (g_strcmp0 (text, "WIFE") == 0)
		return ACB_IMPORT_CMD1_WIFE;
	if (g_strcmp0 (text, "MARR") == 0)
		return ACB_IMPORT_CMD1_MARRIAGE;
	if (g_strcmp0 (text, "CHIL") == 0)
		return ACB_IMPORT_CMD1_CHILDREN;
	return ACB_IMPORT_CMD1_UNKNOWN;
}

/**
 * acb_import_convert_cmd2:
 **/
static AcbImportCmd2
acb_import_convert_cmd2 (const gchar *text)
{
	if (g_strcmp0 (text, "GIVN") == 0)
		return ACB_IMPORT_CMD2_GIVEN;
	if (g_strcmp0 (text, "SURN") == 0)
		return ACB_IMPORT_CMD2_SURNAME;
	if (g_strcmp0 (text, "DATE") == 0)
		return ACB_IMPORT_CMD2_DATE;
	if (g_strcmp0 (text, "PLAC") == 0)
		return ACB_IMPORT_CMD2_PLACE;
	return ACB_IMPORT_CMD2_UNKNOWN;
}

/**
 * acb_import_convert_sex:
 **/
static AcbProjectSex
acb_import_convert_sex (const gchar *text)
{
	if (g_strcmp0 (text, "M") == 0)
		return ACB_PROJECT_SEX_MALE;
	if (g_strcmp0 (text, "F") == 0)
		return ACB_PROJECT_SEX_FEMALE;
	return ACB_PROJECT_SEX_UNKNOWN;
}

/**
 * acb_import_set_date:
 **/
static void
acb_import_set_date (AcbImport *import, const gchar *text)
{
	AcbImportPrivate *priv = import->priv;
	g_free (priv->date);
	priv->date = g_strdup (text);
}

/**
 * acb_import_set_given:
 **/
static void
acb_import_set_given (AcbImport *import, const gchar *text)
{
	AcbImportPrivate *priv = import->priv;
	g_free (priv->given);
	priv->given = g_strdup (text);
}

/**
 * acb_import_set_husband:
 **/
static void
acb_import_set_husband (AcbImport *import, const gchar *text)
{
	AcbImportPrivate *priv = import->priv;
	g_free (priv->husband);
	priv->husband = g_strdup (text);
}

/**
 * acb_import_set_wife:
 **/
static void
acb_import_set_wife (AcbImport *import, const gchar *text)
{
	AcbImportPrivate *priv = import->priv;
	g_free (priv->wife);
	priv->wife = g_strdup (text);
}

/**
 * acb_import_convert_month:
 **/
static guint
acb_import_convert_month (const gchar *text)
{
	if (g_strcmp0 (text, "JAN") == 0)
		return 1;
	if (g_strcmp0 (text, "FEB") == 0)
		return 2;
	if (g_strcmp0 (text, "MAR") == 0)
		return 3;
	if (g_strcmp0 (text, "APR") == 0)
		return 4;
	if (g_strcmp0 (text, "MAY") == 0)
		return 5;
	if (g_strcmp0 (text, "JUN") == 0)
		return 6;
	if (g_strcmp0 (text, "JUL") == 0)
		return 7;
	if (g_strcmp0 (text, "AUG") == 0)
		return 8;
	if (g_strcmp0 (text, "SEP") == 0)
		return 9;
	if (g_strcmp0 (text, "OCT") == 0)
		return 10;
	if (g_strcmp0 (text, "NOV") == 0)
		return 11;
	if (g_strcmp0 (text, "DEC") == 0)
		return 12;
	return 0;
}

/**
 * acb_import_convert_date:
 **/
static gchar *
acb_import_convert_date (const gchar *text)
{
	gchar *date = NULL;
	gchar **sections;
	guint day;
	guint month;

	sections = g_strsplit (text, " ", 3);

	/* 12 JUL 1870 */
	if (g_strv_length (sections) == 3) {
		day = atoi (sections[0]);
		month = acb_import_convert_month (sections[1]);
		date = g_strdup_printf ("%02i-%02i-%s", day, month, sections[2]);
		goto out;
	}

	/* JUL 1870 */
	if (g_strv_length (sections) == 2) {
		month = acb_import_convert_month (sections[0]);
		date = g_strdup_printf ("%02i-%s", month, sections[1]);
		goto out;
	}

	date = g_strdup (text);
	egg_debug ("invalid date: '%s'\n", text);
out:
	g_strfreev (sections);
	return date;
}

/**
 * acb_import_remove_suffix:
 **/
static void
acb_import_remove_suffix (gchar *text, const gchar *suffix)
{
	guint len;
	guint len_suffix;

	if (!g_str_has_suffix (text, suffix))
		return;

	len_suffix = strlen (suffix);
	len = strlen (text);
	text[len-len_suffix] = '\0';
}

/**
 * acb_import_append_section:
 **/
static gchar *
acb_import_append_section (const gchar *text1, const gchar *text2)
{
	gchar *tmp;
	if (text1 == NULL)
		tmp = g_strdup (text2);
	else if (text2 == NULL)
		tmp = g_strdup (text1);
	else 
		tmp = g_strdup_printf ("%s %s", text1, text2);

	/* countries */
	acb_import_remove_suffix (tmp, ", England");
	acb_import_remove_suffix (tmp, ", Poland");
	acb_import_remove_suffix (tmp, ", Wales");
	acb_import_remove_suffix (tmp, ", France");

	/* counties */
	acb_import_remove_suffix (tmp, ", Sussex");
	acb_import_remove_suffix (tmp, ", Buckinghamshire");
	acb_import_remove_suffix (tmp, ", Hertfordshire");
	acb_import_remove_suffix (tmp, ", Birkshire");

	/* places */
	acb_import_remove_suffix (tmp, ", Warsaw");
	acb_import_remove_suffix (tmp, ", London");
	acb_import_remove_suffix (tmp, ", Croydon");
	return tmp;
}

/**
 * acb_import_append_text:
 **/
static gchar *
acb_import_append_text (const gchar *text1, const gchar *text2)
{
	if (text1 == NULL)
		return g_strdup (text2);
	if (text2 == NULL)
		return g_strdup (text1);
	return g_strdup_printf ("%s %s", text1, text2);
}

/**
 * acb_import_parse_line:
 **/
static gboolean
acb_import_parse_line (AcbImport *import, guint idx, const gchar *type, const gchar *text)
{
	AcbImportPrivate *priv = import->priv;
	gchar *tmp = NULL;
	const gchar *id;
	AcbProject *project;
	AcbProject *project2;
	guint i;

//	egg_debug ("idx=%i, type=%s, text=%s", idx, type, text);

	if (idx == 0) {
		/* type and text are switched!? */
		priv->cmd0 = acb_import_convert_cmd0 (text);
		if (priv->cmd0 == ACB_IMPORT_CMD0_INDIVIDUAL) {
			egg_debug ("create new object: %s", type);
			project = acb_project_new ();
			g_ptr_array_add (priv->people, project);
			g_hash_table_insert (priv->hash, g_strdup (type), g_object_ref (project));

			/* save weak reference for speed */
			priv->current_project = project;
			priv->set_name = FALSE;
		}

		/* reset saved state */
		acb_import_set_date (import, NULL);
		acb_import_set_given (import, NULL);
		acb_import_set_husband (import, NULL);
		acb_import_set_wife (import, NULL);

		/* reset */
		g_ptr_array_foreach (priv->siblings, (GFunc) g_free, NULL);
		g_ptr_array_set_size (priv->siblings, 0);

		goto out;
	}

	if (idx == 1) {
		priv->cmd1 = acb_import_convert_cmd1 (type);
		if (priv->cmd0 == ACB_IMPORT_CMD0_INDIVIDUAL) {
			if (priv->cmd1 == ACB_IMPORT_CMD1_SEX) {
				acb_project_set_sex (priv->current_project, acb_import_convert_sex (text));
				egg_debug ("setting sex to %s", text);
			}
		} else if (priv->cmd0 == ACB_IMPORT_CMD0_FAMILY) {
			if (priv->cmd1 == ACB_IMPORT_CMD1_HUSBAND) {
				egg_debug ("setting husband to %s", text);
				acb_import_set_husband (import, text);
			} else if (priv->cmd1 == ACB_IMPORT_CMD1_WIFE) {
				egg_debug ("setting wife to %s", text);
				acb_import_set_wife (import, text);
			} else if (priv->cmd1 == ACB_IMPORT_CMD1_CHILDREN) {
				/* get child */
				project = acb_import_get_project (import, text);

				if (g_strcmp0 (priv->husband, text) == 0)
					egg_error ("impossible for husband (%s) to be the same as the child (%s)", priv->husband, text);
				if (g_strcmp0 (priv->wife, text) == 0)
					egg_error ("impossible for wife (%s) to be the same as the child (%s)", priv->wife, text);

				/* set father */
				if (priv->husband != NULL) {
					project2 = acb_import_get_project (import, priv->husband);
					if (project == project2)
						egg_error ("impossible for husband (%p) to be the same as the child (%p)", project2, project);
//					acb_project_add_sibling (project2, project);
					acb_project_set_father (project, project2);
				}

				/* set mother */
				if (priv->wife != NULL) {
					project2 = acb_import_get_project (import, priv->wife);
					if (project == project2)
						egg_error ("impossible for wife (%p) to be the same as the child (%p)", project2, project);
					acb_project_set_mother (project, project2);
				}

				/* link siblings */
				for (i=0; i<priv->siblings->len; i++) {
					id = g_ptr_array_index (priv->siblings, i);
					project2 = acb_import_get_project (import, id);
					acb_project_add_sibling (project2, project);
					acb_project_add_sibling (project, project2);
				}

				/* add to array */
				g_ptr_array_add (priv->siblings, g_strdup (text));

				egg_debug ("setting parents for %s", text);
			}
		}
		goto out;
	}

	if (idx == 2) {
		priv->cmd2 = acb_import_convert_cmd2 (type);

		if (priv->cmd0 == ACB_IMPORT_CMD0_INDIVIDUAL) {
			if (priv->cmd1 == ACB_IMPORT_CMD1_BIRTH) {
				if (priv->cmd2 == ACB_IMPORT_CMD2_DATE) {
					tmp = acb_import_convert_date (text);
					acb_import_set_date (import, tmp);
					acb_project_set_birth (priv->current_project, tmp);
					egg_debug ("setting birth to %s", text);
				} else if (priv->cmd2 == ACB_IMPORT_CMD2_PLACE) {
					/* set dob */
					tmp = acb_import_append_section (priv->date, text);
					acb_project_set_birth (priv->current_project, tmp);
					egg_debug ("setting birth to %s", tmp);
				}
			} else if (priv->cmd1 == ACB_IMPORT_CMD1_DEATH) {
				if (priv->cmd2 == ACB_IMPORT_CMD2_DATE) {
					tmp = acb_import_convert_date (text);
					acb_import_set_date (import, tmp);
					acb_project_set_death (priv->current_project, tmp);
					egg_debug ("setting death to %s", text);
				} else if (priv->cmd2 == ACB_IMPORT_CMD2_PLACE) {
					/* set dod */
					tmp = acb_import_append_section (priv->date, text);
					acb_project_set_death (priv->current_project, tmp);
					egg_debug ("setting death to %s", tmp);
				}
			} else if (priv->cmd1 == ACB_IMPORT_CMD1_NAME) {
				if (priv->cmd2 == ACB_IMPORT_CMD2_GIVEN) {
					/* already set with main name */
					if (priv->set_name) {
						egg_debug ("already set name");
						goto out;
					}
					/* save first and middle names */
					acb_import_set_given (import, text);
					acb_project_set_name (priv->current_project, text);
				} else if (priv->cmd2 == ACB_IMPORT_CMD2_SURNAME) {
					/* already set with main name */
					if (priv->set_name) {
						egg_debug ("already set name");
						goto out;
					}

					/* make full name */
					tmp = acb_import_append_text (priv->given, text);
					acb_project_set_name (priv->current_project, tmp);
					egg_debug ("setting name to %s", tmp);
					priv->set_name = TRUE;
				}
			}
		} else if (priv->cmd0 == ACB_IMPORT_CMD0_FAMILY) {
			if (priv->cmd1 == ACB_IMPORT_CMD1_MARRIAGE) {
				if (priv->cmd2 == ACB_IMPORT_CMD2_DATE) {
					tmp = acb_import_convert_date (text);
					acb_import_set_date (import, tmp);
					if (priv->husband != NULL) {
						project = acb_import_get_project (import, priv->husband);
						acb_project_set_marriage (project, tmp);
						egg_debug ("setting %s marriage to %s", priv->husband, text);
					}
					if (priv->wife != NULL) {
						project = acb_import_get_project (import, priv->wife);
						acb_project_set_marriage (project, tmp);
						egg_debug ("setting %s marriage to %s", priv->wife, text);
					}
				} else if (priv->cmd2 == ACB_IMPORT_CMD2_PLACE) {
					/* set marriage */
					tmp = acb_import_append_section (priv->date, text);
					if (priv->husband != NULL) {
						project = acb_import_get_project (import, priv->husband);
						acb_project_set_marriage (project, tmp);
						egg_debug ("setting %s marriage to %s", priv->husband, text);
					}
					if (priv->wife != NULL) {
						project = acb_import_get_project (import, priv->wife);
						acb_project_set_marriage (project, tmp);
						egg_debug ("setting %s marriage to %s", priv->wife, text);
					}
				}

			}
		}
		goto out;
	}
out:
	g_free (tmp);
	return TRUE;
}

/**
 * acb_import_parse_filename:
 **/
gboolean
acb_import_parse_filename (AcbImport *import, const gchar *filename)
{
	gboolean ret;
	gchar *contents = NULL;
	gchar **lines = NULL;
	gchar **sections;
	GError *error = NULL;
	guint i;

	g_return_val_if_fail (ACB_IS_IMPORT (import), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	/* get contents */
	ret = g_file_get_contents (filename, &contents, NULL, &error);
	if (!ret) {
		egg_warning ("failed to get file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* split into lines */
	lines = g_strsplit (contents, "\n", -1);
	for (i=0; lines[i] != NULL; i++) {
		sections = g_strsplit (lines[i], " ", 3);
		if (sections[0] != NULL && sections[0][0] != '\0') {
			acb_import_parse_line (import, atoi (sections[0]), sections[1], sections[2]);
		}
		g_strfreev (sections);
	}

out:
	g_free (contents);
	g_strfreev (lines);
	return ret;
}

/**
 * acb_import_get_project:
 **/
AcbProject *
acb_import_get_project (AcbImport *import, const gchar *name)
{
	AcbImportPrivate *priv = import->priv;
	AcbProject *project;
	const gchar *name_tmp;
	guint i;

	g_return_val_if_fail (ACB_IS_IMPORT (import), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	/* find ID in hash */
	if (name[0] == '@') {
		project = g_hash_table_lookup (priv->hash, name);
		goto out;
	}

	/* look for the name */
	for (i=0; i<priv->people->len; i++) {
		project = g_ptr_array_index (priv->people, i);
		name_tmp = acb_project_get_name (project);
		if (g_strcmp0 (name_tmp, name) == 0)
			goto out;
	}

	/* nothing found */
	project = NULL;
out:
	return project;
}

/**
 * acb_import_finalize:
 **/
static void
acb_import_finalize (GObject *object)
{
	AcbImport *import;
	AcbImportPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ACB_IS_IMPORT (object));
	import = ACB_IMPORT (object);
	priv = import->priv;

	g_ptr_array_foreach (priv->siblings, (GFunc) g_free, NULL);
	g_ptr_array_free (priv->siblings, TRUE);
	g_ptr_array_foreach (priv->people, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (priv->people, TRUE);
	g_free (import->priv->date);
	g_free (import->priv->given);
	g_free (import->priv->husband);
	g_free (import->priv->wife);
	g_hash_table_unref (import->priv->hash);

	G_OBJECT_CLASS (acb_import_parent_class)->finalize (object);
}

/**
 * acb_import_class_init:
 **/
static void
acb_import_class_init (AcbImportClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = acb_import_finalize;

	g_type_class_add_private (klass, sizeof (AcbImportPrivate));
}

/**
 * acb_import_init:
 **/
static void
acb_import_init (AcbImport *import)
{
	import->priv = ACB_IMPORT_GET_PRIVATE (import);
	import->priv->people = g_ptr_array_new ();
	import->priv->cmd0 = ACB_IMPORT_CMD0_UNKNOWN;
	import->priv->cmd1 = ACB_IMPORT_CMD1_UNKNOWN;
	import->priv->cmd2 = ACB_IMPORT_CMD2_UNKNOWN;
	import->priv->date = NULL;
	import->priv->given = NULL;
	import->priv->husband = NULL;
	import->priv->wife = NULL;
	import->priv->current_project = NULL;
	import->priv->hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	import->priv->siblings = g_ptr_array_new ();
}

/**
 * acb_import_new:
 *
 * Return value: A new #AcbImport class instance.
 **/
AcbImport *
acb_import_new (void)
{
	AcbImport *import;
	import = g_object_new (ACB_TYPE_IMPORT, NULL);
	return ACB_IMPORT (import);
}

