/*=====================================================================
MCPBotEditorTools.h
-------------------
Local MCP surface for the existing Qt bot editor.
=====================================================================*/
#pragma once


#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QString>


namespace MCPBotEditorTools
{
inline bool handles(const QString& name)
{
	return name == "bot_editor_list" || name == "bot_editor_create" || name == "bot_editor_get" || name == "bot_editor_configure";
}


inline QJsonArray schemas()
{
	return QJsonDocument::fromJson(R"json([
{
  "name":"bot_editor_create",
  "description":"Request a new bot at an explicit world position through the existing create-bot protocol. The server checks parcel permissions and confirms asynchronously. Call bot_editor_list after confirmation to obtain its bot_id, then configure it with bot_editor_configure.",
  "inputSchema":{
    "type":"object",
    "properties":{
      "position":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}},"required":["x","y","z"],"additionalProperties":false},
      "heading_degrees":{"type":"number","description":"Optional facing direction in degrees; defaults to 0."}
    },
    "required":["position"],"additionalProperties":false
  }
},
{
  "name":"bot_editor_list",
  "description":"List bots owned by the current account in the connected world. Returns bot_id as a decimal string, avatar_uid, name and position. Bot configuration changes require bot_editor_configure and are authorized by the connected server.",
  "inputSchema":{"type":"object","properties":{},"additionalProperties":false}
},
{
  "name":"bot_editor_get",
  "description":"Read the selected bot's configuration using the native Qt Bot Editor's JSON format. Secret API-key fields are omitted. Read bot_editor_list first and pass bot_id as the returned decimal string.",
  "inputSchema":{"type":"object","properties":{"bot_id":{"type":"string"}},"required":["bot_id"],"additionalProperties":false}
},
{
  "name":"bot_editor_configure",
  "description":"Apply a partial settings patch to an existing bot through the native Qt Bot Editor and normal server update protocol. Read bot_editor_list, then bot_editor_get first so you preserve settings the user did not ask to change. Omitted top-level settings remain unchanged. For animations supply anim_slots with slot names greeting,idle,reactive,surprise,acknowledge,farewell,walk,talk,interaction and an uploaded Metasiberia resource URL; this selects an existing clip and does not create an animation file. For a branching conversation set use_dialog=true, dialog_start_node and dialog_nodes with bot_text and labeled choices; next_node_id=-1 ends that branch. Use waypoints for patrol routes. Never include API keys or other secrets. The server still enforces bot ownership; after a mutation, read bot_editor_get again to confirm the saved values.",
  "inputSchema":{
    "type":"object",
    "properties":{
      "bot_id":{"type":"string","description":"Decimal string bot_id returned by bot_editor_list."},
      "settings":{
        "type":"object",
        "description":"Partial bot-editor configuration. Only include fields the user asked to change; omitted fields stay unchanged. API-key fields are intentionally unsupported.",
        "properties":{
          "name":{"type":"string","maxLength":200},"avatar_url":{"type":"string","maxLength":10000},
          "scale_x":{"type":"number","minimum":0.01,"maximum":10},"scale_y":{"type":"number","minimum":0.01,"maximum":10},"scale_z":{"type":"number","minimum":0.01,"maximum":10},
          "pos_x":{"type":"number","minimum":-100000,"maximum":100000},"pos_y":{"type":"number","minimum":-100000,"maximum":100000},"pos_z":{"type":"number","minimum":-1000,"maximum":10000},"heading_deg":{"type":"number","minimum":-180,"maximum":180},
          "prompt":{"type":"string","maxLength":10000},"ai_model_id":{"type":"string","maxLength":200},"ai_personality":{"type":"string","maxLength":200},"ai_temperature":{"type":"number","minimum":0,"maximum":2},"ai_max_tokens":{"type":"integer","minimum":0,"maximum":32000},"ai_knowledge":{"type":"string","maxLength":10000},"fallback_message":{"type":"string","maxLength":500},"api_endpoint":{"type":"string","maxLength":300},
          "greeting_distance":{"type":"number","minimum":0.5,"maximum":100},"farewell_distance":{"type":"number","minimum":0.5,"maximum":100},"chat_radius":{"type":"number","minimum":0.5,"maximum":100},"movement_type":{"type":"integer","enum":[0,1,2],"description":"0 stationary, 1 patrol waypoints, 2 wander."},"walk_speed":{"type":"number","minimum":0.1,"maximum":20},"wander_radius":{"type":"number","minimum":0.5,"maximum":200},
          "disabled":{"type":"boolean"},"always_face":{"type":"boolean"},"stationary":{"type":"boolean"},"audio_loop":{"type":"boolean"},"audio_spatial":{"type":"boolean"},"audio_autoplay":{"type":"boolean"},"listens_global_chat":{"type":"boolean"},"react_to_mention":{"type":"boolean"},"react_to_bots":{"type":"boolean"},"use_dialog":{"type":"boolean"},"stream_llm":{"type":"boolean"},"active_hours_enabled":{"type":"boolean"},"trigger_proximity":{"type":"boolean"},"trigger_chat":{"type":"boolean"},"trigger_keywords_enabled":{"type":"boolean"},"trigger_gesture":{"type":"boolean"},"trigger_use":{"type":"boolean"},"trigger_keywords":{"type":"string","maxLength":4000},"trigger_cooldown":{"type":"number","minimum":0,"maximum":3600},
          "waypoints":{"type":"array","maxItems":64,"items":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"},"heading_override":{"type":"number"},"dwell_time_s":{"type":"number","minimum":0}},"required":["x","y","z","heading_override","dwell_time_s"],"additionalProperties":false}},
          "use_actions":{"type":"array","maxItems":16,"items":{"type":"object","properties":{"type":{"type":"integer","minimum":0,"maximum":8},"label":{"type":"string","maxLength":100},"param":{"type":"string","maxLength":1000},"required_avatar_uid":{"type":"string","maxLength":64}},"required":["type","label","param","required_avatar_uid"],"additionalProperties":false}},
          "anim_slots":{"type":"array","maxItems":9,"items":{"type":"object","properties":{"slot":{"type":"string","enum":["greeting","idle","reactive","surprise","acknowledge","farewell","walk","talk","interaction"]},"name":{"type":"string","maxLength":200},"url":{"type":"string","maxLength":10000},"cd":{"type":"number","minimum":0,"maximum":3600},"loop":{"type":"boolean"},"animate_head":{"type":"boolean"}},"required":["slot","name","url","cd","loop","animate_head"],"additionalProperties":false}},
          "audio_url":{"type":"string","maxLength":10000},"audio_volume":{"type":"number","minimum":0,"maximum":1},"audio_radius":{"type":"number","minimum":0.5,"maximum":200},"audio_activation_distance":{"type":"number","minimum":0.5,"maximum":500},"audio_cooldown":{"type":"number","minimum":0,"maximum":3600},"audio_min_distance":{"type":"number","minimum":0,"maximum":200},"audio_start_delay":{"type":"number","minimum":0,"maximum":3600},"greeting_audio_url":{"type":"string","maxLength":10000},"farewell_audio_url":{"type":"string","maxLength":10000},"interaction_audio_url":{"type":"string","maxLength":10000},
          "conv_timeout_s":{"type":"number","minimum":0,"maximum":3600},"max_llm_calls_hour":{"type":"integer","minimum":0,"maximum":10000},"webhook_url":{"type":"string","maxLength":500},"active_hours_start":{"type":"integer","minimum":0,"maximum":23},"active_hours_end":{"type":"integer","minimum":0,"maximum":23},
          "scripted_responses":{"type":"array","maxItems":32,"items":{"type":"object","properties":{"keywords":{"type":"string","maxLength":500},"response":{"type":"string","maxLength":2000}},"required":["keywords","response"],"additionalProperties":false}},
          "player_whitelist":{"type":"array","maxItems":64,"items":{"type":"string","maxLength":64}},"player_blacklist":{"type":"array","maxItems":64,"items":{"type":"string","maxLength":64}},
          "tool_functions":{"type":"array","maxItems":64,"items":{"type":"object","properties":{"name":{"type":"string","maxLength":1000},"description":{"type":"string","maxLength":10000},"result":{"type":"string","maxLength":100000}},"required":["name","description","result"],"additionalProperties":false}},
          "ai_provider":{"type":"integer","minimum":0,"maximum":6},"top_p":{"type":"number","minimum":0,"maximum":1},"top_k":{"type":"integer","minimum":0,"maximum":500},"freq_penalty":{"type":"number","minimum":-2,"maximum":2},"pres_penalty":{"type":"number","minimum":-2,"maximum":2},"max_ctx_msgs":{"type":"integer","minimum":0,"maximum":200},"dialog_start_node":{"type":"integer","minimum":0,"maximum":63},
          "dialog_nodes":{"type":"array","maxItems":64,"items":{"type":"object","properties":{"node_id":{"type":"integer","minimum":0,"maximum":63},"bot_text":{"type":"string","maxLength":2000},"action_type":{"type":"integer","enum":[0,1,2,3]},"action_param":{"type":"string","maxLength":500},"choices":{"type":"array","maxItems":8,"items":{"type":"object","properties":{"keywords":{"type":"string","maxLength":300},"label":{"type":"string","maxLength":200},"next_node_id":{"type":"integer","minimum":-1,"maximum":63}},"required":["keywords","label","next_node_id"],"additionalProperties":false}}},"required":["node_id","bot_text","action_type","action_param","choices"],"additionalProperties":false}},
          "enable_memory":{"type":"boolean"},"memory_tokens":{"type":"integer","minimum":50,"maximum":2000},"jailbreak_guard":{"type":"boolean"},"content_filter":{"type":"string","maxLength":2000},"player_rate_limit":{"type":"integer","minimum":0,"maximum":10000},"cache_enabled":{"type":"boolean"},"cache_ttl_s":{"type":"integer","minimum":30,"maximum":86400},"fallback_model":{"type":"string","maxLength":200},"fallback_endpoint":{"type":"string","maxLength":300},"llm_retries":{"type":"integer","minimum":0,"maximum":3}
        },
        "additionalProperties":false
      }
    },
    "required":["bot_id","settings"],"additionalProperties":false
  }
}
])json").array();
}
}
