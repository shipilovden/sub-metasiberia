#pragma once
#include <opengl/ui/GLUIImage.h>
#include <QtCore/QVariantMap>
#include <QtCore/QSize>
#include <array>

// Render-only GL layers, deliberately NOT registered as interactive GLUI widgets.
class PhotoModeOverlay {
public:
	PhotoModeOverlay(GLUI& ui, Reference<OpenGLEngine>& engine);
	void update(const QVariantMap& values, const QSize& viewport);
	void hide();
private:
	GLUI& ui;
	std::array<GLUIImageRef, 8> lines;
	std::array<GLUIImageRef, 4> masks;
	GLUIImageRef warmth, tint, vignette;
};
