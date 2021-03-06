/* darxenradarviewer.c
 *
 * Copyright (C) 2011 - Kevin Wells <kevin@darxen.org>
 *
 * This file is part of darxen
 *
 * darxen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * darxen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with darxen.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "darxenradarviewer.h"

#include "DarxenParserLevel3.h"
#include <darxenrenderer.h>

G_DEFINE_TYPE(DarxenRadarViewer, darxen_radar_viewer, GLTK_TYPE_WIDGET)

#define USING_PRIVATE(obj) DarxenRadarViewerPrivate* priv = DARXEN_RADAR_VIEWER_GET_PRIVATE(obj)
#define DARXEN_RADAR_VIEWER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), DARXEN_TYPE_RADAR_VIEWER, DarxenRadarViewerPrivate))

#define RAD_TO_DEG(v) ((v)/G_PI*180.0)

enum
{
	LAST_SIGNAL
};

typedef struct _DarxenRadarViewerPrivate	DarxenRadarViewerPrivate;
typedef struct _RenderData					RenderData;
struct _DarxenRadarViewerPrivate
{
	gchar* site;
	DarxenViewInfo* viewInfo;

	DarxenPoller* poller;

	DarxenRenderer* renderer;
	GLubyte* buffer;
	GQueue* data; //RenderData
	GList* pData; //RenderData

	gulong sigViewUpdated;
	gulong sigDataReceived;
};

struct _RenderData
{
	gchar* id;
	ProductsLevel3Data* data;
};

//static guint signals[LAST_SIGNAL] = {0,};

static void darxen_radar_viewer_dispose(GObject* gobject);
static void darxen_radar_viewer_finalize(GObject* gobject);

static void	darxen_radar_viewer_data_received	(DarxenPoller* poller, RadarData* data, DarxenRadarViewer* radarViewer);

static void		darxen_radar_viewer_size_allocate(GltkWidget* widget, GltkAllocation* allocation);
static gboolean darxen_radar_viewer_touch_event(GltkWidget* widget, GltkEventTouch* touch);
static gboolean	darxen_radar_viewer_drag_event(GltkWidget* widget, GltkEventDrag* event);
static gboolean darxen_radar_viewer_multi_drag_event(GltkWidget* widget, GltkEventMultiDrag* event);
static gboolean darxen_radar_viewer_pinch_event(GltkWidget* widget, GltkEventPinch* event);
static gboolean darxen_radar_viewer_rotate_event(GltkWidget* widget, GltkEventRotate* event);
static void		darxen_radar_viewer_render(GltkWidget* widget);

static gint compare_radar_data(RenderData* rd1, RenderData* rd2, gpointer user_data);

static void set_view_info(DarxenRadarViewer* viewer, DarxenViewInfo* viewInfo);
static void	config_viewUpdated(	DarxenConfig* config,
								const gchar* site,
								const gchar* viewName,
								DarxenViewInfo* viewInfo,
								DarxenRadarViewer* viewer);

static void	spawn_render	(DarxenRadarViewer* viewer);

static void
darxen_radar_viewer_class_init(DarxenRadarViewerClass* klass)
{
	GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
	GltkWidgetClass* gltkwidget_class = GLTK_WIDGET_CLASS(klass);

	g_type_class_add_private(klass, sizeof(DarxenRadarViewerPrivate));
	
	gobject_class->dispose = darxen_radar_viewer_dispose;
	gobject_class->finalize = darxen_radar_viewer_finalize;

	gltkwidget_class->size_allocate = darxen_radar_viewer_size_allocate;
	gltkwidget_class->touch_event = darxen_radar_viewer_touch_event;
	gltkwidget_class->drag_event = darxen_radar_viewer_drag_event;
	gltkwidget_class->pinch_event = darxen_radar_viewer_pinch_event;
	gltkwidget_class->multi_drag_event = darxen_radar_viewer_multi_drag_event;
	gltkwidget_class->rotate_event = darxen_radar_viewer_rotate_event;
	gltkwidget_class->render = darxen_radar_viewer_render;
}

static void
darxen_radar_viewer_init(DarxenRadarViewer* self)
{
	USING_PRIVATE(self);

	priv->site = NULL;
	priv->viewInfo = NULL;

	priv->poller = NULL;

	priv->renderer = NULL;
	priv->buffer = NULL;
	priv->data = NULL;
	priv->pData = NULL;

	priv->sigViewUpdated = 0;
	priv->sigDataReceived = 0;
}

static void
darxen_radar_viewer_dispose(GObject* gobject)
{
	DarxenRadarViewer* self = DARXEN_RADAR_VIEWER(gobject);
	USING_PRIVATE(self);

	//free and release references

	if (priv->renderer)
	{
		g_object_unref(G_OBJECT(priv->renderer));
		priv->renderer = NULL;
	}

	if (priv->poller)
	{
		g_signal_handler_disconnect(priv->poller, priv->sigDataReceived);
		g_object_unref(G_OBJECT(priv->poller));
		priv->poller = NULL;
	}

	//FIXME: Free data queue

	G_OBJECT_CLASS(darxen_radar_viewer_parent_class)->dispose(gobject);
}

static void
darxen_radar_viewer_finalize(GObject* gobject)
{
	DarxenRadarViewer* self = DARXEN_RADAR_VIEWER(gobject);
	USING_PRIVATE(self);

	g_free(priv->site);
	g_signal_handler_disconnect(darxen_config_get_instance(), priv->sigViewUpdated);

	G_OBJECT_CLASS(darxen_radar_viewer_parent_class)->finalize(gobject);
}

DarxenRadarViewer*
darxen_radar_viewer_new(const gchar* site, DarxenViewInfo* viewInfo)
{
	GObject *gobject = g_object_new(DARXEN_TYPE_RADAR_VIEWER, NULL);
	DarxenRadarViewer* self = DARXEN_RADAR_VIEWER(gobject);

	USING_PRIVATE(self);

	priv->site = g_strdup(site);

	priv->data = g_queue_new();

	set_view_info(self, viewInfo);

	priv->sigViewUpdated = g_signal_connect(darxen_config_get_instance(), "view-updated", (GCallback)config_viewUpdated, self);

	return (DarxenRadarViewer*)gobject;
}

void
darxen_radar_viewer_frame_first(DarxenRadarViewer* radarViewer)
{
	g_return_if_fail(DARXEN_IS_RADAR_VIEWER(radarViewer));
	USING_PRIVATE(radarViewer);

	priv->pData = priv->data->head;

	if (!priv->pData)
		return;

	darxen_renderer_set_data(priv->renderer, ((RenderData*)priv->pData->data)->data);
	gltk_widget_invalidate(GLTK_WIDGET(radarViewer));
}

void
darxen_radar_viewer_frame_last(DarxenRadarViewer* radarViewer)
{
	g_return_if_fail(DARXEN_IS_RADAR_VIEWER(radarViewer));
	USING_PRIVATE(radarViewer);

	priv->pData = priv->data->tail;

	if (!priv->pData)
		return;

	darxen_renderer_set_data(priv->renderer, ((RenderData*)priv->pData->data)->data);
	gltk_widget_invalidate(GLTK_WIDGET(radarViewer));
}

void
darxen_radar_viewer_frame_next(DarxenRadarViewer* radarViewer)
{
	g_return_if_fail(DARXEN_IS_RADAR_VIEWER(radarViewer));
	USING_PRIVATE(radarViewer);

	if (!priv->pData || !priv->pData->next)
		return;

	priv->pData = priv->pData->next;

	darxen_renderer_set_data(priv->renderer, ((RenderData*)priv->pData->data)->data);
	gltk_widget_invalidate(GLTK_WIDGET(radarViewer));
}

void
darxen_radar_viewer_frame_prev(DarxenRadarViewer* radarViewer)
{
	g_return_if_fail(DARXEN_IS_RADAR_VIEWER(radarViewer));
	USING_PRIVATE(radarViewer);

	if (!priv->pData || !priv->pData->prev)
		return;

	priv->pData = priv->pData->prev;

	darxen_renderer_set_data(priv->renderer, ((RenderData*)priv->pData->data)->data);
	gltk_widget_invalidate(GLTK_WIDGET(radarViewer));
}

gboolean
darxen_radar_viewer_has_frame_next(DarxenRadarViewer* radarViewer)
{
	g_return_val_if_fail(DARXEN_IS_RADAR_VIEWER(radarViewer), FALSE);
	USING_PRIVATE(radarViewer);

	return priv->pData && !!priv->pData->next;
}

gboolean
darxen_radar_viewer_has_frame_prev(DarxenRadarViewer* radarViewer)
{
	g_return_val_if_fail(DARXEN_IS_RADAR_VIEWER(radarViewer), FALSE);
	USING_PRIVATE(radarViewer);

	return priv->pData && !!priv->pData->prev;
}

GQuark
darxen_radar_viewer_error_quark()
{
	return g_quark_from_static_string("darxen-radar-viewer-error-quark");
}

/*********************
 * Private Functions *
 *********************/

static void
darxen_radar_viewer_data_received(DarxenPoller* poller, RadarData* data, DarxenRadarViewer* radarViewer)
{
	USING_PRIVATE(radarViewer);

	FILE* f = tmpfile();
	fwrite(data->data, 1, data->len, f);
	fseek(f, 0, SEEK_SET);
	ProductsLevel3Data* parsed;
	if (!(parsed = parser_lvl3_parse_file(f)))
	{
		fclose(f);
		g_critical("Failed to parse level 3 data, skipping");
		return;
	}
	fclose(f);
	//printf("Header: %s\n", parsed->chrWmoHeader);

	RenderData* renderData = g_new(RenderData, 1);
	renderData->id = g_strdup(data->ID);
	renderData->data = parsed;

	g_queue_insert_sorted(priv->data, renderData, (GCompareDataFunc)compare_radar_data, NULL);

	darxen_radar_viewer_frame_last(radarViewer);
	gltk_widget_invalidate(GLTK_WIDGET(radarViewer));
}


static void
darxen_radar_viewer_size_allocate(GltkWidget* widget, GltkAllocation* allocation)
{
	USING_PRIVATE(widget);
	
	darxen_renderer_set_size(priv->renderer, allocation->width, allocation->height);

	g_assert(allocation->width && allocation->height);

	priv->buffer = g_renew(GLubyte, priv->buffer, allocation->width * allocation->height * 3);

	GLTK_WIDGET_CLASS(darxen_radar_viewer_parent_class)->size_allocate(widget, allocation);
}

static gboolean
darxen_radar_viewer_touch_event(GltkWidget* widget, GltkEventTouch* touch)
{
	switch (touch->touchType)
	{
		case TOUCH_BEGIN:
			gltk_screen_set_widget_pressed(widget->screen, widget);
			break;
		case TOUCH_END:
			gltk_screen_set_widget_unpressed(widget->screen, widget);
			break;
		default:
			return FALSE;
	}

	return TRUE;
}

static gboolean
darxen_radar_viewer_drag_event(GltkWidget* widget, GltkEventDrag* event)
{
	USING_PRIVATE(widget);

	if (event->longTouched)
		return FALSE;

	//1. Convert screen coordinates to renderer coordinates (determined by current aspect ratio)
	GltkAllocation allocation = gltk_widget_get_allocation(widget);
	float scaleFactor = MIN(allocation.width, allocation.height) / 2.0f;

	darxen_renderer_translate(priv->renderer, event->dx / scaleFactor, -event->dy / scaleFactor);

	gltk_widget_invalidate(widget);
	
	return TRUE;
}

static gboolean
darxen_radar_viewer_multi_drag_event(GltkWidget* widget, GltkEventMultiDrag* event)
{
	USING_PRIVATE(widget);

	//TODO: this method works, but it assumes the center of the event is unchanging, which is not always true

	//1. Convert screen coordinates to renderer coordinates (determined by current aspect ratio)
	GltkAllocation allocation = gltk_widget_get_allocation(widget);
	float scaleFactor = MIN(allocation.width, allocation.height) / 2.0f;

	darxen_renderer_translate(priv->renderer, event->offset.x / scaleFactor, -event->offset.y / scaleFactor);

	gltk_widget_invalidate(widget);
	
	return TRUE;
}

static gboolean
darxen_radar_viewer_pinch_event(GltkWidget* widget, GltkEventPinch* event)
{
	USING_PRIVATE(widget);

	//TODO: this method works, but it assumes the center of the event is unchanging, which is not always true

	GltkAllocation allocation = gltk_widget_get_allocation(widget);
	//g_message("Pinch (%i %i) %f - %f", (int)event->center.x, (int)event->center.y, event->radius, event->dradius);
	
	float scaleFactor = MIN(allocation.width, allocation.height) / 2.0f;

	int centerX = allocation.width / 2;
	int centerY = allocation.height / 2;
	
	//1. Translate to center of zoom
	int dCenterX = event->center.x - centerX;
	int dCenterY = event->center.y - centerY;
	darxen_renderer_translate(priv->renderer, -dCenterX / scaleFactor, dCenterY / scaleFactor);
	
	//2. Scale
	float oldRadius = event->radius - event->dradius;
	float factor = event->radius / oldRadius;
	darxen_renderer_scale(priv->renderer, factor);

	//3. Translate back to old center
	darxen_renderer_translate(priv->renderer, dCenterX / scaleFactor, -dCenterY / scaleFactor);

	gltk_widget_invalidate(widget);
	
	return TRUE;
}

static gboolean
darxen_radar_viewer_rotate_event(GltkWidget* widget, GltkEventRotate* event)
{
	USING_PRIVATE(widget);

	//TODO: this method works, but it assumes the center of the event is unchanging, which is not always true
	if (!darxen_config_get_instance()->allowRotation)
		return FALSE;

	GltkAllocation allocation = gltk_widget_get_allocation(widget);
	//g_message("Pinch (%i %i) %f - %f", (int)event->center.x, (int)event->center.y, event->radius, event->dradius);
	
	float scaleFactor = MIN(allocation.width, allocation.height) / 2.0f;

	int centerX = allocation.width / 2;
	int centerY = allocation.height / 2;
	
	//1. Translate to center of zoom
	int dCenterX = event->center.x - centerX;
	int dCenterY = event->center.y - centerY;
	darxen_renderer_translate(priv->renderer, -dCenterX / scaleFactor, dCenterY / scaleFactor);
	
	//2. Rotate
	darxen_renderer_rotate(priv->renderer, RAD_TO_DEG(event->dtheta), 0.0f, 0.0f, -1.0f);

	//3. Translate back to old center
	darxen_renderer_translate(priv->renderer, dCenterX / scaleFactor, -dCenterY / scaleFactor);

	gltk_widget_invalidate(widget);
	
	return TRUE;
}

static void
darxen_radar_viewer_render(GltkWidget* widget)
{
	USING_PRIVATE(widget);

	if (!priv->data)
		return;

	GltkAllocation allocation = gltk_widget_get_global_allocation(widget);
	GltkSize size = gltk_screen_get_window_size(widget->screen);

	if (darxen_renderer_is_dirty(priv->renderer) || !priv->buffer)
	{
		//setup our rendering window how we like it
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		int offsetX = allocation.x;
		int offsetY = size.height - allocation.height - allocation.y;
		glViewport(offsetX, offsetY, allocation.width, allocation.height);
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		if (allocation.height > allocation.width)
		{
			double aspect = (double)allocation.height / allocation.width;
			glOrtho(-1, 1, -aspect, aspect, -1, 1);
		}
		else
		{
			double aspect = (double)allocation.width / allocation.height;
			glOrtho(-aspect, aspect, -1, 1, -1, 1);
		}
		glMatrixMode(GL_MODELVIEW);

		glPushMatrix();
		{
			glLoadIdentity();
			darxen_renderer_render(priv->renderer);
			if (!priv->renderer->loadPass)
				spawn_render(DARXEN_RADAR_VIEWER(widget));
		}
		glPopMatrix();

		//undo our changes to the state
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

		//save the rendered image for later
		glReadBuffer(GL_BACK);
		if (priv->buffer)
			glReadPixels(offsetX, offsetY, allocation.width-2, allocation.height-2, GL_RGB, GL_UNSIGNED_BYTE, priv->buffer);
	}
	else
	{
		//TODO: use texture to render and redraw instead
		//GLint viewport[4];
		//glGetIntegerv(GL_VIEWPORT, viewport);
		//glViewport(allocation.x, size.height - allocation.height - allocation.y, allocation.width, allocation.height);
		
		//redraw our scene
		glWindowPos2i(allocation.x, size.height - allocation.height - allocation.y);
		glDrawPixels(allocation.width-2, allocation.height-2, GL_RGB, GL_UNSIGNED_BYTE, priv->buffer);

		//glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
	}
}

static gint
compare_radar_data(RenderData* rd1, RenderData* rd2, gpointer user_data)
{
	ProductsLevel3Data* d1 = rd1->data;
	ProductsLevel3Data* d2 = rd2->data;
	int dateres = g_date_compare(d1->objHeader.objDate, d2->objHeader.objDate);

	if (dateres)
		return dateres;

	if (d1->objHeader.intTime < d2->objHeader.intTime)
		return -1;
	return (d1->objHeader.intTime > d2->objHeader.intTime);
}

typedef struct 
{
	DarxenRadarViewer* viewer;
	DarxenRestfulClient* client;
	int searchId;
	int searchIndex;
	int searchCount;
} LoadDataInfo;

static void
free_load_data_info(LoadDataInfo* dataInfo)
{
	USING_PRIVATE(dataInfo->viewer);

	//free the search
	if (!darxen_restful_client_free_search(dataInfo->client, dataInfo->searchId, NULL))
	{
		g_error("Failed to free search for %s/%s in view %s", priv->site, priv->viewInfo->productCode, priv->viewInfo->name);
	}

	g_object_unref(dataInfo->viewer);
	g_free(dataInfo);
}

static gboolean
load_initial_data(LoadDataInfo* dataInfo)
{
	USING_PRIVATE(dataInfo->viewer);

	int searchCount = MIN(2, dataInfo->searchCount);

	//get the search results
	gchar** ids;
	if (!(ids = darxen_restful_client_read_search(	dataInfo->client, 
													dataInfo->searchId, 
													dataInfo->searchIndex,
												   	searchCount,
													NULL)))
	{
		g_error("Failed to retrieve search records for %s/%s in view %s", priv->site, priv->viewInfo->productCode, priv->viewInfo->name);
	}

	//read the search records
	int i;
	for (i = 0; i < searchCount; i++)
	{
		size_t len;
		char* data = darxen_restful_client_read_data(dataInfo->client, priv->site, priv->viewInfo->productCode, ids[i], &len, NULL);

		if (!data)
		{
			g_warning("Failed to download data %s/%s with id %s", priv->site, priv->viewInfo->productCode, ids[i]);
			continue;
		}

		FILE* f = tmpfile();
		fwrite(data, 1, len, f);
		fseek(f, 0, SEEK_SET);
		ProductsLevel3Data* parsed;
		if (!(parsed = parser_lvl3_parse_file(f)))
		{
			g_critical("Failed to parse level 3 data");
			free(data);
			continue;
		}
		fclose(f);
		//printf("Header: %s\n", parsed->chrWmoHeader);fflush(stdout);
		RenderData* renderData = g_new(RenderData, 1);
		renderData->id = g_strdup(ids[i]);
		renderData->data = parsed;

		g_queue_insert_sorted(priv->data, renderData, (GCompareDataFunc)compare_radar_data, NULL);
		
		free(data);
	}
	g_strfreev(ids);

	//load the first frame to show the first set of loaded data
	if (dataInfo->searchIndex == 0)
		darxen_radar_viewer_frame_first(dataInfo->viewer);

	dataInfo->searchCount -= searchCount;
	dataInfo->searchIndex += searchCount;
	
	return dataInfo->searchCount > 0;
}

static void
set_view_info(DarxenRadarViewer* viewer, DarxenViewInfo* viewInfo)
{
	USING_PRIVATE(viewer);

	//clean up an old mess
	{
		if (priv->poller)
		{
			g_object_unref(priv->poller);
			priv->poller = NULL;
		}

		RenderData* renderData;
		while ((renderData = (RenderData*)g_queue_pop_head(priv->data)))
		{
			g_free(renderData->id);
			parser_lvl3_free(renderData->data);
		}
		priv->pData = NULL;

		if (priv->renderer)
		{
			g_object_unref(priv->renderer);
			priv->renderer = NULL;
		}
	}
	
	//make a new one
	priv->viewInfo = viewInfo;

	priv->renderer = darxen_renderer_new(priv->site, viewInfo->productCode, viewInfo->shapefiles);
	darxen_renderer_set_smoothing(priv->renderer, viewInfo->smoothing);
	darxen_renderer_set_partial_load(priv->renderer, TRUE);

	DarxenRestfulClient* client = darxen_config_get_client(darxen_config_get_instance());
	switch (viewInfo->sourceType)
	{
		case DARXEN_VIEW_SOURCE_ARCHIVE:
		{
			GError* error = NULL;

			//run the search
			int searchId;
			int searchCount;
			if (!darxen_restful_client_search_data(	client, priv->site, viewInfo->productCode, 
													viewInfo->source.archive.startId, 
													viewInfo->source.archive.endId, 
													&searchId, &searchCount, &error))
			{
				g_error("Search failed for %s/%s in view %s", priv->site, viewInfo->productCode, viewInfo->name);
			}

			LoadDataInfo* dataInfo = g_new(LoadDataInfo, 1);
			g_object_ref(viewer);
			dataInfo->viewer = viewer;
			dataInfo->client = client;
			dataInfo->searchId = searchId;
			dataInfo->searchIndex = 0;
			dataInfo->searchCount = searchCount;

			g_idle_add_full(	G_PRIORITY_DEFAULT_IDLE + 50, 
								(GSourceFunc)load_initial_data, 
								dataInfo, 
								(GDestroyNotify)free_load_data_info);
		} break;

		case DARXEN_VIEW_SOURCE_LIVE:
		{
			GError* error = NULL;

			//setup the poller
			priv->poller = darxen_restful_client_add_poller(client, priv->site, viewInfo->productCode, &error);
			if (!priv->poller)
			{
				g_error("Failed to register poller for %s/%s in view %s", priv->site, viewInfo->productCode, viewInfo->name);
			}
			priv->sigDataReceived = g_signal_connect(priv->poller, "data-received", (GCallback)darxen_radar_viewer_data_received, viewer);
		} break;
	}
	
}

static void				
config_viewUpdated(	DarxenConfig* config,
					const gchar* site,
					const gchar* viewName,
					DarxenViewInfo* viewInfo,
					DarxenRadarViewer* viewer)
{
	USING_PRIVATE(viewer);

	if (g_strcmp0(site, priv->site) || g_strcmp0(viewName, priv->viewInfo->name))
		return;

	set_view_info(viewer, viewInfo);
}

static gboolean
spawn_render_callback(DarxenRadarViewer* viewer)
{
	gltk_widget_invalidate(GLTK_WIDGET(viewer));
	return FALSE;
}

static void
spawn_render(DarxenRadarViewer* viewer)
{
	g_idle_add((GSourceFunc)spawn_render_callback, viewer);
}

