# Metasiberia: Temporary Wiki Capabilities Plan (Draft)

Status: draft for review (not final wiki)
Date: 2026-03-01
Scope: GUI client + web/admin capabilities audit for wiki planning

## 1. Purpose

This document is a temporary master checklist for future wiki pages.
Goal: capture all major user/admin capabilities in order, from installation and registration to advanced editing/admin workflows.

## 2. What was reviewed (code sources)

- Client menus/actions:
  - `gui_client/MainWindow.ui`
  - `gui_client/MainWindow.cpp`
  - `gui_client/MainWindow.h`
- In-world HUD/UI:
  - `gui_client/GestureUI.cpp`
  - `gui_client/PhotoModeUI.cpp`
- Object and material editing:
  - `gui_client/ObjectEditor.ui`
  - `gui_client/ObjectEditor.cpp`
  - `gui_client/MaterialEditor.ui`
  - `gui_client/MaterialEditor.cpp`
- Parcel/world/environment tools:
  - `gui_client/ParcelEditor.ui`
  - `gui_client/ParcelEditor.cpp`
  - `gui_client/WorldSettingsWidget.ui`
  - `gui_client/WorldSettingsWidget.cpp`
  - `gui_client/TerrainSpecSectionWidget.ui`
  - `gui_client/EnvironmentOptionsWidget.ui`
  - `gui_client/EnvironmentOptionsWidget.cpp`
- Add-model flow:
  - `gui_client/AddObjectDialog.ui`
  - `gui_client/AddObjectDialog.cpp`
- Web and admin routes:
  - `webserver/WebServerRequestHandler.cpp`
  - `webserver/AdminHandlers.cpp`

## 3. End-to-end user journey map (high-level)

1. Install client.
2. Start client and connect to server/world.
3. Register account or login.
4. Navigate world (first person, fly, third person).
5. Create and edit content (models/images/webview/video/audio/voxels/text/etc).
6. Manage parcel rights and settings.
7. Manage materials/textures/lightmaps.
8. Use media/social tools (photo mode, upload).
9. Use personal world and web account tools.
10. For admins: use `/admin` and related pages for moderation/operations.

---

## 4. Client UI map (top level)

### 4.1 Menu bar

- `Edit`
- `Movement`
- `Avatar`
- `Vehicles`
- `View`
- `Go`
- `Tools`
- `Window`
- `Help`

### 4.2 Toolbar quick actions

- `Add Model / Image`
- `Add Video`
- `Add Hypercard`
- `Add Voxels`
- `Add Web View`
- `Show Parcels`

### 4.3 Main dock panels

- Chat
- Webcam
- Editor (Object/Parcel editors)
- Help Information
- Material Browser
- Indigo View
- Diagnostics
- Environment
- World Settings

---

## 5. Menu-by-menu capabilities

## 5.1 Edit

- Undo / Redo
- Create content:
  - Add Model / Image
  - Add Hypercard
  - Add Text
  - Add Spotlight
  - Add Camera
  - Add Seat
  - Add Audio Source
  - Add Web View
  - Add Video
  - Add Decal
  - Add Portal
- Favorites:
  - Add to Favorites
- Object ops:
  - Copy Object
  - Paste Object
  - Clone Object
  - Delete Object
  - Find Object by ID
  - Find Objects Nearby
- Lightmaps:
  - Bake lightmaps (fast/high quality) for parcel
- Disk I/O:
  - Save Object To Disk
  - Save Parcel Objects To Disk
  - Load Object(s) From Disk

## 5.2 Movement

- Fly Mode (toggle)

## 5.3 Avatar

- Avatar Settings

## 5.4 Vehicles

- Summon Bike
- Summon Hovercar
- Summon Boat
- Summon Jet Ski
- Summon Car

## 5.5 View

- Third Person Camera (toggle)

## 5.6 Go

- Go Back
- Go to Position
- Go to Parcel
- Go To Start Location
- Go to Main World
- Go to Personal World (uses logged-in username)
- Go to Cryptovoxels world
- Go to Substrata / Metasiberia / Shki-nvkz server
- Go to Favorites (submenu)
- Set current location as start location

## 5.7 Tools

- Take Screenshot
- Show Screenshot Folder
- Show Log
- Export view to Indigo
- Mute Audio
- Options

## 5.8 Window

- Reset Layout
- Enter Fullscreen
- Language: English / Russian

## 5.9 Help

- About Metasiberia
- Update

---

## 6. "Add" actions and exact behavior

## 6.1 Add Model / Image

- Dialog tabs:
  - Model library (built-in primitives/assets: Quad, Cube, Capsule, Cylinder, Icosahedron, Platonic_Solid, Torus)
  - From disk (OBJ, GLTF, GLB, VOX, STL, IGMESH; JPG, PNG, GIF, EXR, KTX, KTX2)
  - From Web (URL)
- Includes preview and texture loading checks.
- Enforces parcel write permission before create.

## 6.2 Add Hypercard

- Creates `ObjectType_Hypercard` with editable text/content.
- Faces player direction.
- Enforces parcel permission.

## 6.3 Add Text

- Creates `ObjectType_Text` with default text.
- Sets default double-sided/alpha-friendly material.

## 6.4 Add Spotlight

- Creates `ObjectType_Spotlight` with cone angles, emitting material.

## 6.5 Add Camera

- Requires connected server.
- Protocol gate: server protocol must be `>= 50`.
- Creates pair: `ObjectType_Camera` + `ObjectType_CameraScreen`.
- Auto-queues pending camera/screen link workflow.
- Defaults include render size/FOV/max FPS and enabled flags.

## 6.6 Add Seat

- Protocol gate: server protocol must be `>= 49`.
- Creates `ObjectType_Seat` with default limb angles.

## 6.7 Add Portal

- Creates `ObjectType_Portal` at ground-aligned forward position.

## 6.8 Add Web View

- Creates `ObjectType_WebView`.
- Default target URL: `https://vr.metasiberia.com/`.

## 6.9 Add Video

- Source from local file or URL.
- Video URL stored in material emission texture field.
- Defaults: autoplay + loop flags.

## 6.10 Add Audio Source

- Select `.mp3/.wav`, copies into resources, assigns audio URL.
- Creates generic object with audio source URL and volume.

## 6.11 Add Decal

- Creates non-collidable generic decal object.
- Uses material decal flag.

## 6.12 Add Voxels

- Creates `ObjectType_VoxelGroup` with starter voxel.

---

## 7. Object editing capabilities (Object Editor)

## 7.1 Core transform/script/content

- Model URL/file
- Position x/y/z
- Scale x/y/z + linked scaling
- Rotation z/y/x
- 3D gizmo controls toggle
- Snap to grid + grid spacing
- Script text
- Content field
- Target URL field

## 7.2 Materials

- Multi-material selection
- Create new material
- Full material editor integration

## 7.3 Material editor fields

- Colour
- Texture URL
- Texture scale x/y
- Roughness
- Metallic fraction
- Metallic-roughness texture
- Normal map
- Opacity
- Emission colour
- Emission texture
- Luminance (nits)
- Flags:
  - Hologram
  - Use vert colours for wind
  - Double sided
  - Decal

## 7.4 Lightmaps

- Show lightmap URL
- Remove lightmap
- Bake lightmap (fast / high quality)
- Bake status

## 7.5 Physics

- Collidable / Dynamic / Sensor
- Mass
- Friction
- Restitution
- Center of mass offset

## 7.6 Audio group

- Audio file URL/path
- Volume
- Audio autoplay
- Audio loop

## 7.7 Video group

- Video URL
- Video volume
- Video autoplay/loop/muted

## 7.8 Spotlight group

- Luminous flux
- Light colour
- Cone start/end angles

## 7.9 Seat group

- Upper/lower leg angles
- Upper/lower arm angles

## 7.10 Camera group

- Enabled
- FOV Y
- Near distance
- Far distance
- Render width
- Render height
- Max FPS

## 7.11 Camera Screen group

- Enabled
- Source camera UID
- Material index

## 7.12 Object-type specific visibility

Editor switches visible groups depending on object type:
- Generic
- Hypercard
- Voxel Group
- Spotlight
- Web View
- Video
- Text
- Portal
- Seat
- Camera
- Camera Screen

---

## 8. Parcel editor capabilities

- Parcel ID / Created time
- Owner (user ref by id/name)
- Title / Description
- Admins list
- Writers list
- All Writeable
- Mute outside audio
- Spawn point x/y/z
- Parcel bounds display (min/max)
- Parcel geometry editing:
  - Position x/y/z
  - Size x/y/z + linked scaling
- "Show parcel on web" link
- Permission-aware edit modes:
  - basic fields
  - owner + geometry
  - member lists

---

## 9. World/environment editing

## 9.1 World Settings

- Terrain sections list in scroll area
- Create/remove terrain section
- Per-section fields:
  - section x, y
  - Height map URL
  - Mask map URL
  - Tree mask map URL
- Global terrain fields:
  - detail color maps 0..3
  - detail height map 0
  - terrain section width
  - default terrain Z
  - water Z
  - water enabled
- Apply world settings update

## 9.2 Environment Options

- Use local sun direction
- Sun angle from vertical (theta)
- Sun azimuth (phi)
- Northern Lights toggle

---

## 10. In-world HUD capabilities

From `GestureUI` and `PhotoModeUI`:

- Gestures panel:
  - open/hide
  - play/stop gesture animations
  - manage gestures UI
- Vehicles quick menu:
  - summon bike/car/boat/jetski/hovercar
- Photo mode:
  - toggle photo mode
  - camera/post-processing sliders (focus, blur, EV, saturation, focal length, roll)
  - upload UI
- Microphone toggle + mic level indicator
- Webcam toggle/button (Qt dock visibility or capture toggle depending build)

---

## 11. Registration/login/account and personal world capabilities

Client-side:
- Login dialog and logout action
- Signup dialog
- Go to Personal World (logged-in username based world routing)

Web-side routes (user flows):
- Auth/account:
  - `/login`, `/signup`, `/logout_post`
  - `/reset_password`, `/change_password`, related POST handlers
  - `/account`, `/account_gestures`
- World/user pages:
  - `/world/<name>`
  - `/create_world`, `/create_world_post`
  - `/edit_world/<name>`, `/edit_world_post`
- Chatbots:
  - `/new_chatbot`, `/create_new_chatbot_post`
  - `/edit_chatbot`, `/edit_chatbot_post`
  - `/delete_chatbot_post`
- Photos/events/news:
  - `/photos`, `/photo/<id>`, `/edit_photo_parcel`, `/delete_photo_post`
  - `/events`, `/create_event`, `/edit_event`, `/event/<id>`
  - `/news`, `/news_post/<id>`

---

## 12. Admin panel capabilities (web)

Main admin nav pages:
- `/admin`
- `/admin_users`, `/admin_user/<id>`
- `/admin_parcels`
- `/admin_world_parcels`, `/admin_world_parcel`
- `/admin_parcel_auctions`, `/admin_parcel_auction/<id>`
- `/admin_orders`, `/admin_order/<id>`
- `/admin_sub_eth_transactions`, `/admin_sub_eth_transaction/<id>`
- `/admin_map`
- `/admin_news_posts`
- `/admin_lod_chunks`
- `/admin_worlds`
- `/admin_chatbots`

Key admin operations:
- Parcel operations:
  - create parcel (root and world contexts)
  - set owner
  - set geometry
  - set writers/editors
  - delete parcel
  - regenerate parcel screenshots
  - bulk owner/editor update for world parcels
  - bulk world parcel delete (with safeguards)
- Feature flags and server controls:
  - read-only mode toggle
  - server admin message
  - server script execution flag
  - Lua HTTP flag
  - world maintenance flag
  - chatbots run flag
  - force dynamic texture update checker
  - reload web data
- User administration:
  - view user details
  - set world gardener flag
  - set dynamic texture update permission
  - admin reset user password
- Chatbot administration:
  - filter chatbots by world/user
  - enable/disable single/multiple
  - delete single/multiple
  - bulk chatbot profile update
- Map administration:
  - regenerate map tiles (mark not done)
  - recreate map tiles
  - map tile status view
- Transactions/orders/auctions moderation tools
- Admin audit log

---

## 13. What should become wiki pages (proposed order)

1. Install (Windows)
2. Registration and Login
3. First launch and connection to world
4. Movement and camera modes
5. UI overview (menus, toolbar, docks)
6. Add content: model/image/video/webview/audio/voxels/text/hypercard/decal/portal
7. Object Editor full guide
8. Materials and texturing guide
9. Lightmaps and optimization basics
10. Parcel system and permissions
11. Personal world and world creation/edit
12. Photo mode and social upload
13. Vehicles and gestures
14. Camera + Camera Screen streaming guide
15. Web account features (chatbots/events/news/photos)
16. Admin panel operations (full)
17. Troubleshooting and common errors
18. Release/update checklist

---

## 14. Open verification checklist before final wiki publish

- Verify install flow against latest release package (`0.0.11` target).
- Re-verify all menu labels in current build screenshot (RU/EN).
- Re-verify each Add action with live test in one parcel.
- Re-verify personal world create/edit permissions on server.
- Re-verify admin actions under superadmin and non-superadmin accounts.
- Add screenshots for each major page/workflow.
- Add short "quick start" and deep "advanced" versions for each topic.

---

End of draft.
