/*=====================================================================
MiniMap.h
---------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "../shared/URLString.h"
#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/ui/GLUIImage.h>
#include <maths/vec3.h>
#include <utils/Array2D.h>
#include <utils/SocketBufferOutStream.h>
#include <map>


class GUIClient;
class Avatar;
class OpenGLScene;
class MapTilesResultReceivedMessage;


struct MapTile
{
	Reference<OverlayObject> ob;
};


struct MapTileInfo
{
	URLString image_URL;
};


/*=====================================================================
MiniMap
-------

=====================================================================*/
class MiniMap : public GLUICallbackHandler, public ThreadSafeRefCounted
{
public:
	MiniMap(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_);
	~MiniMap();

	void setVisible(bool visible); // Set expand, collapse button visibility, plus call setMapAndMarkersVisible().

	void think();

	void viewportResized(int w, int h);

	void updateMarkerForAvatar(Avatar* avatar, const Vec3d& avatar_pos);
	void removeMarkerForAvatar(Avatar* avatar);

	void handleMousePress(MouseEvent& mouse_event);
	void handleMouseRelease(MouseEvent& mouse_event);
	void handleMouseMoved(MouseEvent& mouse_event);

	//virtual bool doHandleMouseMoved(const Vec2f& coords) override;

	// GLUICallbackHandler interface:
	void eventOccurred(GLUICallbackEvent& event) override;
	void mouseWheelEventOccurred(GLUICallbackMouseWheelEvent& event) override;

	void handleMapTilesResultReceivedMessage(const MapTilesResultReceivedMessage& msg);

	void handleUploadedTexture(const OpenGLTextureKey& path, const URLString& URL, const OpenGLTextureRef& opengl_tex);
private:
	void setWidgetVisibility();
	void checkUpdateTilesForCurCamPosition();
	void updateWidgetPositions();
	void setTileOverlayObjectTransforms();
	Vec2f mapUICoordsForWorldSpacePos(const Vec3d& pos);
	Rect2f computeMiniMapContentRect() const;
	Rect2f computeFullscreenAvailableRect() const;
	Vec2f computeMiniMapBotLeft() const;
	Vec2f computeMiniMapDims() const;
	float computeMiniMapWidth() const;
	float computeMiniMapTopMargin() const;
	bool isInFullscreenResizeHandle(const Vec2f& ui_coords) const;
	void updateFullscreenResizeDrag(const Vec2f& ui_coords);

	GUIClient* gui_client;
	GLUIRef gl_ui;
	Reference<OpenGLEngine> opengl_engine;

	GLUIImageRef minimap_image;
	GLUIImageRef arrow_image;

	bool expanded;
	bool visible;
	bool fullscreen;
	bool last_is_metasiberia_map_world;
	GLUIButtonRef collapse_button;
	GLUIButtonRef expand_button;
	GLUIButtonRef fullscreen_button;
	GLUIImageRef resize_handle_image;
	bool fullscreen_resize_drag_active;
	bool fullscreen_has_user_dims;
	Vec2f fullscreen_resize_drag_start_ui;
	Vec2f fullscreen_resize_start_dims;
	Vec2f fullscreen_user_dims;

	Array2D<MapTile> tiles;
	int last_centre_x, last_centre_y;

	std::set<Vec3i> queried_tile_coords;
	std::map<Vec3i, MapTileInfo> tile_infos;

	Vec3d last_requested_campos;
	int last_requested_tile_z;
	double next_tile_refresh_query_time;

	float map_width_ws;

	SocketBufferOutStream scratch_packet;

	OpenGLTextureRef tile_placeholder_tex;

	std::unordered_map<URLString, Vec3i, URLStringHasher> loading_texture_URL_to_tile_indices_map;
};
