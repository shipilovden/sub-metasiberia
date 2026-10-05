#pragma once
#include <graphics/AnimationData.h>
#include <QtCore/QByteArray>
#include <functional>

// CPU-only, bounded import. No filesystem callbacks, textures, GL or client state.
Reference<AnimationData> readFBXAnimation(const QByteArray& bytes, const std::function<bool()>& cancelled);
