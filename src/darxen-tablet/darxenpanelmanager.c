/* darxenpanelmanager.c
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

#include "darxenpanelmanager.h"

#include "darxenmainview.h"
#include "darxenview.h"
#include "darxenviewconfig.h"

G_DEFINE_TYPE(DarxenPanelManager, darxen_panel_manager, GLTK_TYPE_BIN)

#define USING_PRIVATE(obj) DarxenPanelManagerPrivate* priv = DARXEN_PANEL_MANAGER_GET_PRIVATE(obj)
#define DARXEN_PANEL_MANAGER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), DARXEN_TYPE_PANEL_MANAGER, DarxenPanelManagerPrivate))

enum
{
	LAST_SIGNAL
};

typedef struct _DarxenPanelManagerPrivate		DarxenPanelManagerPrivate;
typedef struct _SiteViewPair					SiteViewPair;
typedef struct _ViewPair						ViewPair;

struct _DarxenPanelManagerPrivate
{
	GHashTable* viewMap; //SiteViewPair > ViewPair
	GltkWidget* mainView;
};

struct _SiteViewPair
{
	gchar* site;
	gchar* view;
};

struct _ViewPair
{
	DarxenView* view;
	DarxenViewConfig* config;
	GltkWidget* configScrollable;
};

//static guint signals[LAST_SIGNAL] = {0,};

static void darxen_panel_manager_dispose(GObject* gobject);
static void darxen_panel_manager_finalize(GObject* gobject);

static SiteViewPair*	site_view_pair_new		(const gchar* site, const gchar* view);
static void				site_view_pair_init		(SiteViewPair* pair, const gchar* site, const gchar* view);
static void				site_view_pair_free		(SiteViewPair* pair);
static guint			site_view_pair_hash		(SiteViewPair* pair);
static gboolean			site_view_pair_equal	(const SiteViewPair* o1, const SiteViewPair* o2);
static void				view_pair_free			(ViewPair* view);

static void				config_viewNameChanged		(	DarxenConfig* config,
														const gchar* site,
														DarxenViewInfo* viewInfo,
														const gchar* newName,
														DarxenPanelManager* manager);

static void				config_viewUpdated			(	DarxenConfig* config,
														const gchar* site,
														const gchar* viewName,
														DarxenViewInfo* viewInfo,
														DarxenPanelManager* manager);

static void
darxen_panel_manager_class_init(DarxenPanelManagerClass* klass)
{
	GObjectClass* gobject_class = G_OBJECT_CLASS(klass);

	g_type_class_add_private(klass, sizeof(DarxenPanelManagerPrivate));
	
	gobject_class->dispose = darxen_panel_manager_dispose;
	gobject_class->finalize = darxen_panel_manager_finalize;
}

static void
darxen_panel_manager_init(DarxenPanelManager* self)
{
	USING_PRIVATE(self);

	priv->viewMap = NULL;
	priv->mainView = darxen_main_view_get_root();
}

static void
darxen_panel_manager_dispose(GObject* gobject)
{
	DarxenPanelManager* self = DARXEN_PANEL_MANAGER(gobject);
	USING_PRIVATE(self);

	if (priv->viewMap)
	{
		g_hash_table_destroy(priv->viewMap);
		priv->viewMap = NULL;
	}

	G_OBJECT_CLASS(darxen_panel_manager_parent_class)->dispose(gobject);
}

static void
darxen_panel_manager_finalize(GObject* gobject)
{
	G_OBJECT_CLASS(darxen_panel_manager_parent_class)->finalize(gobject);
}

GltkWidget*
darxen_panel_manager_new()
{
	GObject *gobject = g_object_new(DARXEN_TYPE_PANEL_MANAGER, NULL);
	DarxenPanelManager* self = DARXEN_PANEL_MANAGER(gobject);
	DarxenConfig* config = darxen_config_get_instance();

	USING_PRIVATE(self);

	priv->viewMap = g_hash_table_new_full((GHashFunc)site_view_pair_hash, (GEqualFunc)site_view_pair_equal, (GDestroyNotify)site_view_pair_free, (GDestroyNotify)view_pair_free);

	darxen_panel_manager_view_main(self);

	g_signal_connect(config, "view-name-changed", (GCallback)config_viewNameChanged, self);
	g_signal_connect(config, "view-updated", (GCallback)config_viewUpdated, self);

	return (GltkWidget*)gobject;
}

void
darxen_panel_manager_create_view(DarxenPanelManager* manager, gchar* site, DarxenViewInfo* viewInfo)
{
	g_return_if_fail(DARXEN_IS_PANEL_MANAGER(manager));
	g_return_if_fail(site);
	g_return_if_fail(viewInfo);
	USING_PRIVATE(manager);

	ViewPair* pair = g_new(ViewPair, 1);

	pair->view = (DarxenView*)darxen_view_new(site, viewInfo);
	pair->config = (DarxenViewConfig*)darxen_view_config_new(site, viewInfo);
	pair->configScrollable = gltk_scrollable_new();
	gltk_scrollable_set_widget(GLTK_SCROLLABLE(pair->configScrollable), GLTK_WIDGET(pair->config));
	g_object_ref_sink(pair->view);
	g_object_ref_sink(pair->config);
	g_object_ref_sink(pair->configScrollable);

	g_hash_table_insert(priv->viewMap, site_view_pair_new(site, viewInfo->name), pair);
}

void
darxen_panel_manager_destroy_view(DarxenPanelManager* manager, gchar* site, gchar* view)
{
	g_return_if_fail(DARXEN_IS_PANEL_MANAGER(manager));
	g_return_if_fail(site);
	g_return_if_fail(view);
	USING_PRIVATE(manager);
	SiteViewPair siteViewPair;
	site_view_pair_init(&siteViewPair, site, view);

	g_hash_table_remove(priv->viewMap, &siteViewPair);
}

void
darxen_panel_manager_view_main(DarxenPanelManager* manager)
{
	g_return_if_fail(DARXEN_IS_PANEL_MANAGER(manager));
	USING_PRIVATE(manager);

	gltk_bin_set_widget(GLTK_BIN(manager), priv->mainView);
}

void
darxen_panel_manager_view_view(DarxenPanelManager* manager, gchar* site, gchar* view)
{
	g_return_if_fail(DARXEN_IS_PANEL_MANAGER(manager));
	g_return_if_fail(site);
	g_return_if_fail(view);
	USING_PRIVATE(manager);
	SiteViewPair siteViewPair;
	site_view_pair_init(&siteViewPair, site, view);

	ViewPair* pair = (ViewPair*)g_hash_table_lookup(priv->viewMap, &siteViewPair);
	g_return_if_fail(pair);
	g_return_if_fail(GLTK_IS_WIDGET(pair->view));

	gltk_bin_set_widget(GLTK_BIN(manager), (GltkWidget*)pair->view);
}

void
darxen_panel_manager_view_view_config(DarxenPanelManager* manager, gchar* site, gchar* view)
{
	g_return_if_fail(DARXEN_IS_PANEL_MANAGER(manager));
	g_return_if_fail(site);
	g_return_if_fail(view);
	USING_PRIVATE(manager);
	SiteViewPair siteViewPair;
	site_view_pair_init(&siteViewPair, site, view);

	ViewPair* pair = (ViewPair*)g_hash_table_lookup(priv->viewMap, &siteViewPair);
	g_return_if_fail(pair);
	g_return_if_fail(GLTK_IS_WIDGET(pair->config));
	
	gltk_bin_set_widget(GLTK_BIN(manager), pair->configScrollable);
}

void
darxen_panel_manager_save_view_config(DarxenPanelManager* manager, gchar* site, gchar* view)
{
	g_return_if_fail(DARXEN_IS_PANEL_MANAGER(manager));
	g_return_if_fail(site);
	g_return_if_fail(view);
	USING_PRIVATE(manager);
	SiteViewPair siteViewPair;
	site_view_pair_init(&siteViewPair, site, view);

	ViewPair* pair = (ViewPair*)g_hash_table_lookup(priv->viewMap, &siteViewPair);
	g_return_if_fail(pair);
	g_return_if_fail(GLTK_IS_WIDGET(pair->config));
	
	darxen_view_config_save(pair->config);
}

void
darxen_panel_manager_revert_view_config(DarxenPanelManager* manager, gchar* site, gchar* view)
{
	g_return_if_fail(DARXEN_IS_PANEL_MANAGER(manager));
	g_return_if_fail(site);
	g_return_if_fail(view);
	USING_PRIVATE(manager);
	SiteViewPair siteViewPair;
	site_view_pair_init(&siteViewPair, site, view);

	ViewPair* pair = (ViewPair*)g_hash_table_lookup(priv->viewMap, &siteViewPair);
	g_return_if_fail(pair);
	g_return_if_fail(GLTK_IS_WIDGET(pair->config));
	
	darxen_view_config_revert(pair->config);
}

GQuark
darxen_panel_manager_error_quark()
{
	return g_quark_from_static_string("darxen-panel-manager-error-quark");
}

/*********************
 * Private Functions *
 *********************/

static SiteViewPair*
site_view_pair_new(const gchar* site, const gchar* view)
{
	SiteViewPair* pair = g_new(SiteViewPair, 1);
	site_view_pair_init(pair, g_strdup(site), g_strdup(view));
	return pair;
}

static void
site_view_pair_init(SiteViewPair* pair, const gchar* site, const gchar* view)
{
	pair->site = (gchar*)site;
	pair->view = (gchar*)view;
}

static void
site_view_pair_free(SiteViewPair* pair)
{
	g_free(pair->site);
	g_free(pair->view);
	g_free(pair);
}

static guint
site_view_pair_hash(SiteViewPair* pair)
{
	return g_str_hash(pair->site) * 13 + g_str_hash(pair->view);
}

static gboolean
site_view_pair_equal(const SiteViewPair* o1, const SiteViewPair* o2)
{
	return g_str_equal(o1->site, o2->site) && g_str_equal(o1->view, o2->view);
}

static void
view_pair_free(ViewPair* pair)
{
	g_object_unref(G_OBJECT(pair->view));
	g_object_unref(G_OBJECT(pair->config));
	g_object_unref(G_OBJECT(pair->configScrollable));
}

//static void
//viewConfig_siteChanged(DarxenViewConfig* viewConfig, DarxenPanelManager* manager)
//{
//	USING_PRIVATE(manager);
//
//	g_critical("TODO: update all references to this view name");
//}

static void				
config_viewNameChanged(	DarxenConfig* config,
						const gchar* site,
						DarxenViewInfo* viewInfo,
						const gchar* oldName,
						DarxenPanelManager* manager)
{
	USING_PRIVATE(manager);

	SiteViewPair siteViewPair;
	site_view_pair_init(&siteViewPair, site, oldName);
	SiteViewPair* key;
	ViewPair* value;

	g_return_if_fail(g_hash_table_lookup_extended(priv->viewMap, &siteViewPair, (gpointer*)&key, (gpointer*)&value));
	g_return_if_fail(g_hash_table_steal(priv->viewMap, key));

	g_free(key->view);
	key->view = g_strdup(viewInfo->name);
	g_hash_table_insert(priv->viewMap, key, value);
}

static void				
config_viewUpdated(	DarxenConfig* config,
					const gchar* site,
					const gchar* viewName,
					DarxenViewInfo* viewInfo,
					DarxenPanelManager* manager)
{
	//nothing useful to do here
}

