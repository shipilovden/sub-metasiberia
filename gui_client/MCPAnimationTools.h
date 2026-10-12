#pragma once

// Qt-local tools. The remote server does not own the user's native editor.
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QString>

namespace MCPAnimationTools
{
inline bool handles(const QString& name)
{
	return name == "animation_editor_list" || name == "animation_editor_control";
}

inline QJsonArray schemas()
{
	return QJsonDocument::fromJson(R"json([
{
  "name":"animation_editor_list",
  "description":"Inspect the connected Qt Animation Editor: current user's gesture library, resource URLs, local availability, live preview status, and last asynchronous save_copy job. Poll last_save.status until completed, failed or cancelled before claiming a copy exists. completed returns animation_url; its server upload/other-client playback is not confirmed by this result. This is the existing editor, not a separate animation state. Does not play a world gesture.",
  "inputSchema":{"type":"object","properties":{},"additionalProperties":false}
},
{
  "name":"animation_editor_control",
  "description":"Control the actual native Qt Animation Editor using existing gestures. Call animation_editor_list first. select opens/selects a resource; poll preview.duration_seconds > 0 before play/seek. play/pause/seek/speed affect only the preview; speed is a 0.05..4 multiplier. flags persists loop/animate_head on the user's gesture. save_copy requires a unique name, writes a new SUBANIM through the existing background converter and normal resource/gesture settings path; returns a pending job, not completion. Poll list and report failures. perform explicitly plays the selected gesture on the current user's avatar in the multiplayer world; stop stops that world gesture. No arbitrary file paths, skeletal editing or selected world-object animation are performed by these editor tools.",
  "inputSchema":{
    "type":"object",
    "properties":{
      "action":{"type":"string","enum":["select","play","pause","seek","speed","flags","save_copy","perform","stop"]},
      "animation_url":{"type":"string","description":"Exact URL returned by list; defaults to editor selection."},
      "position":{"type":"number","minimum":0,"maximum":1,"description":"Required for seek: normalized clip time."},
      "speed":{"type":"number","minimum":0.05,"maximum":4,"description":"Required for speed; optional for save_copy (defaults to preview speed)."},
      "loop":{"type":"boolean","description":"flags only"},
      "animate_head":{"type":"boolean","description":"flags only"},
      "name":{"type":"string","minLength":1,"maxLength":160,"description":"Required for save_copy: unique gesture name."}
    },
    "required":["action"],"additionalProperties":false
  }
}
])json").array();
}
}
