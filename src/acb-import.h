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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __ACB_IMPORT_H
#define __ACB_IMPORT_H

#include <glib-object.h>

#include "acb-import.h"
#include "acb-project.h"

G_BEGIN_DECLS

#define ACB_TYPE_IMPORT		(acb_import_get_type ())
#define ACB_IMPORT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ACB_TYPE_IMPORT, AcbImport))
#define ACB_IMPORT_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ACB_TYPE_IMPORT, AcbImportClass))
#define ACB_IS_IMPORT(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), ACB_TYPE_IMPORT))
#define ACB_IS_IMPORT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ACB_TYPE_IMPORT))
#define ACB_IMPORT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ACB_TYPE_IMPORT, AcbImportClass))

typedef struct _AcbImport		AcbImport;
typedef struct _AcbImportPrivate	AcbImportPrivate;
typedef struct _AcbImportClass		AcbImportClass;

struct _AcbImport
{
	GObject			 parent;
	AcbImportPrivate	*priv;
};

struct _AcbImportClass
{
	GObjectClass		 parent_class;
};

GType		 acb_import_get_type		(void);
AcbImport	*acb_import_new			(void);

gboolean	 acb_import_parse_filename	(AcbImport		*import,
						 const gchar		*filename);
AcbProject	*acb_import_get_project		(AcbImport		*import,
						 const gchar		*name);

G_END_DECLS

#endif /* __ACB_IMPORT_H */

