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

#ifndef __ACB_PROJECT_H
#define __ACB_PROJECT_H

#include <glib-object.h>

G_BEGIN_DECLS

#define ACB_TYPE_PROJECT (acb_project_get_type ())
G_DECLARE_DERIVABLE_TYPE (AcbProject, acb_project, ACB, PROJECT, GObject)

struct _AcbProjectClass
{
	GObjectClass		parent_class;
};

AcbProject	*acb_project_new			(void);
void		 acb_project_set_default_code_path	(AcbProject		*project,
							 const gchar		*path);
void		 acb_project_set_rpmbuild_path		(AcbProject		*project,
							 const gchar		*path);
void		 acb_project_set_name			(AcbProject		*project,
							 const gchar		*path);
gboolean	 acb_project_clean			(AcbProject		*project,
							 GError			**error);
gboolean	 acb_project_update			(AcbProject		*project,
							 GError			**error);
gboolean	 acb_project_build			(AcbProject		*project,
							 GError			**error);
gboolean	 acb_project_make			(AcbProject		*project,
							 GError			**error);

G_END_DECLS

#endif /* __ACB_PROJECT_H */

