#include "PhotoModeOverlay.h"
#include "PhotoModeEffects.h"
#include <graphics/ImageMap.h>
#include <graphics/SRGBUtils.h>

PhotoModeOverlay::PhotoModeOverlay(GLUI& ui_, Reference<OpenGLEngine>& engine) : ui(ui_) {
	auto image = [&](float z) { return GLUIImageRef(new GLUIImage(ui, engine, "", "", z)); };
	warmth = image(-0.90f);
	tint = image(-0.91f);
	vignette = image(-0.92f);
	for(auto& mask : masks) { mask = image(-0.9998f); mask->setColour(Colour3f(0)); mask->setAlpha(0.55f); }
	for(size_t i=0; i<lines.size(); ++i) {
		lines[i] = image(i<4 ? -0.99981f : -0.99982f);
		lines[i]->setColour(Colour3f(i<4 ? 0.f : 1.f));
		lines[i]->setAlpha(i<4 ? 0.35f : 0.65f);
	}
	ImageMapUInt8Ref map = new ImageMapUInt8(128, 128, 4);
	for(int y=0; y<128; ++y) for(int x=0; x<128; ++x) {
		uint8* p = map->getPixel(x,y); p[0]=p[1]=p[2]=255;
		p[3]=(uint8)qRound(255*PhotoModeEffects::vignette(2.0*(x+0.5)/128-1, 2.0*(y+0.5)/128-1));
	}
	TextureParams params; params.allow_compression=false; params.wrapping=OpenGLTexture::Wrapping_Clamp;
	vignette->overlay_ob->material.albedo_texture = engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey("__photo_vignette"), *map, params);
	vignette->overlay_ob->material.tex_translation = Vec2f(0.f, 1.f);
	vignette->setColour(Colour3f(0));
	hide();
}

void PhotoModeOverlay::hide() {
	for(auto& image : lines) image->setVisible(false);
	for(auto& image : masks) image->setVisible(false);
	warmth->setVisible(false); tint->setVisible(false); vignette->setVisible(false);
}

void PhotoModeOverlay::update(const QVariantMap& values, const QSize& viewport) {
	if(viewport.isEmpty()) { hide(); return; }
	const QRect frame = PhotoModeEffects::frameRect(viewport, values.value("aspect_ratio").toDouble());
	const float scale = 2.f/viewport.width();
	auto place = [&](GLUIImageRef image, const QRect& rect) {
		image->setPosAndDims(Vec2f(-1+rect.x()*scale, (viewport.height()*0.5f-rect.y()-rect.height())*scale), Vec2f(rect.width()*scale,rect.height()*scale));
		image->setVisible(!rect.isEmpty());
	};
	for(int pass=0; pass<2; ++pass) for(int i=0;i<4;++i) {
		const int width=pass==0 ? 3 : 1;
		const int third=i%2+1;
		const QRect line=i<2 ? QRect(frame.x()+frame.width()*third/3-width/2,frame.y(),width,frame.height()) :
			QRect(frame.x(),frame.y()+frame.height()*third/3-width/2,frame.width(),width);
		place(lines[pass*4+i],line);
		lines[pass*4+i]->setVisible(values.value("show_grid").toBool());
	}
	place(masks[0],QRect(0,0,viewport.width(),frame.y()));
	place(masks[1],QRect(0,frame.bottom()+1,viewport.width(),viewport.height()-frame.bottom()-1));
	place(masks[2],QRect(0,frame.y(),frame.x(),frame.height()));
	place(masks[3],QRect(frame.right()+1,frame.y(),viewport.width()-frame.right()-1,frame.height()));
	for(int i=0;i<2;++i) {
		auto image=i==0 ? warmth : tint;
		const double amount=values.value(i==0 ? "warmth" : "tint").toDouble();
		const QColor col=PhotoModeEffects::tone(amount,i==1);
		place(image,frame);
		image->setColour(toLinearSRGB(Colour3f(col.redF(),col.greenF(),col.blueF())));
		image->setAlpha((float)(std::abs(amount)*0.2)); image->setVisible(amount!=0);
	}
	place(vignette,frame); vignette->setAlpha(values.value("vignette").toFloat());
	vignette->setVisible(values.value("vignette").toDouble()>0);
}
