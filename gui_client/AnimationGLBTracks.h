#pragma once
#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <functional>

// Bake STEP and cubic Hermite channels into the engine's LINEAR representation.
void bakeAnimationGLBTracks(QJsonObject& root, QByteArray& binary, const std::function<bool()>& cancelled);
