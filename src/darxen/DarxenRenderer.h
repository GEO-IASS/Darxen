/* DarxenRenderer.h

   Copyright (C) 2008 Kevin Wells <kevin@darxen.org>

   This file is part of darxen.

   darxen is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   darxen is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with darxen.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __DARXEN_RENDERER_H__
#define __DARXEN_RENDERER_H__

#include "DarxenCommon.h"
#include "DarxenSettings.h"
#include "DarxenDataStructs.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define DARXEN_TYPE_RENDERER			(darxen_renderer_get_type())
#define DARXEN_RENDERER(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), DARXEN_TYPE_RENDERER, DarxenRenderer))
#define DARXEN_RENDERER_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass), DARXEN_TYPE_RENDERER, DarxenRendererClass))
#define DARXEN_IS_RENDERER(obj)			(G_TYPE_CHECK_INSTANCE_TYPE((obj), DARXEN_TYPE_RENDERER))
#define DARXEN_IS_RENDERER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), DARXEN_TYPE_RENDERER))
#define DARXEN_RENDERER_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), DARXEN_TYPE_RENDERER, DarxenRendererClass))

typedef struct _DarxenRenderer			DarxenRenderer;
typedef struct _DarxenRendererClass		DarxenRendererClass;

struct _DarxenRenderer
{
	GObject parent_instance;
};

struct _DarxenRendererClass
{
	GObjectClass parent_class;
};

G_EXPORT GType		darxen_renderer_get_type() G_GNUC_CONST;
G_EXPORT GObject*	darxen_renderer_new(SettingsView* viewSettings, SettingsSite* site);

/* Get */
G_EXPORT ProductsLevel3Data*	darxen_renderer_get_data(DarxenRenderer* renderer);

/* Set */
G_EXPORT void darxen_renderer_set_data(DarxenRenderer *renderer, ProductsLevel3Data* objData);
G_EXPORT void darxen_renderer_set_size(DarxenRenderer *renderer, int width, int height);

/* Utility */
G_EXPORT void darxen_renderer_render(DarxenRenderer* renderer);

G_END_DECLS

#endif
