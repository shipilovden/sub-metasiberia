/*=====================================================================
AnimationImport.h
-----------------
Native Qt boundary for importing avatar gesture animations.
=====================================================================*/
#pragma once

#if !defined(USE_SDL)
#include <graphics/AnimationData.h>
#include <QtCore/QString>
#include <functional>

namespace AnimationImport
{
struct Result
{
	// Fresh, validated data, prepared for appendAnimationData() like AnimationManager.
	// Do not call build() on prepared data (its accessors live in AnimationDatum).
	Reference<AnimationData> animation_data;
	QString subanim_path;
	QString name; // Exactly matches animation_data->animations[0]->name (UTF-8).
	float duration_seconds = 0;
};

// Synchronous; throws glare::Exception on failure. No resource registration or UI work.
// Supports self-contained GLB 2 with one skin/clip (STEP/CUBICSPLINE are baked), FBX,
// and SUBA .subanim versions 1-4. Requires Mixamo-style humanoid bone names, with
// optional "mixamorig:" prefix. This is not arbitrary-skeleton retargeting: the
// caller must use a compatible avatar; gesture playback matches bones by name.
// Textual glTF, BVH, morph tracks and multi-clip files are explicitly rejected.
// Limits: 64 MiB input, 4 MiB GLB JSON, 512 nodes, 1536 tracks/accessors,
// 100000 keys per track, 2000000 total samples, 1 hour resulting duration.
//
// Caller owns output lifetime, normally by placing it inside its QTemporaryDir;
// retain that directory until preview/resource copy has finished. Output must be
// a NEW .subanim file, distinct from input. Validated output is written atomically.
// Empty name preserves the clip name (falling back to the source file stem).
// speed [0.05, 4] bakes playback rate into fresh keyframe times (time /= speed).
// Thus this also exports/renames/retimes an existing .subanim without changing
// the original or any AnimationManager cache. Trim/clip selection are unsupported.
// driver_subanim_path: installed Idle.subanim; when supplied, bake imported local
// bone frames to the world's canonical rig before serialization (no source edits).
Result importFile(const QString& input_path, const QString& output_subanim_path,
	const QString& name = QString(), double speed = 1.0, const std::function<bool()>& cancelled = {},
	const QString& driver_subanim_path = QString());
}
#endif
