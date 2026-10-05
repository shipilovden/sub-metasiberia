#pragma once
#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/MeshPrimitiveBuilding.h>
#include <graphics/SRGBUtils.h>

// Fixed-size client adapter: GLUITextButton ignores requested dimensions.
class ChatOverlayButton final : public GLUIWidget
{
public:
	ChatOverlayButton(GLUI& ui_, Reference<OpenGLEngine>& engine_, const std::string& text, int font, const std::string& tip)
	: ui(ui_), engine(engine_), clip(Vec2f(-100), Vec2f(100)) {
		tooltip=tip; m_z=-.04f;
		background=new OverlayObject(); background->mesh_data=engine->getUnitQuadMeshData(); engine->addOverlayObject(background);
		GLUITextView::CreateArgs args; args.font_size_px=font; args.padding_px=0;
		args.background_alpha=0; args.text_selectable=false; args.text_colour=toLinearSRGB(Colour3f(.94f)); args.z=m_z-.001f;
		label=new GLUITextView(ui,engine,text,Vec2f(0),args);
		setPosAndDims(Vec2f(0), ui.getUIWidthForDevIndepPixelWidths(Vec2f(40)));
	}
	~ChatOverlayButton() { engine->removeOverlayObject(background); }
	GLUICallbackHandler* handler=nullptr;
	void setText(const std::string& text) { label->setText(ui,text); updateGLTransform(); }
	void setToggled(bool value) { toggled=value; updateColour(false); }
	bool isVisible() override { return shown; }
	void setVisible(bool value) override { shown=value; background->draw=value; label->setVisible(value); }
	void setPos(const Vec2f& pos) override { setPosAndDims(pos,getDims()); }
	void setPosAndDims(const Vec2f& pos,const Vec2f& dims) override { rect=Rect2f(pos,pos+dims); updateGLTransform(); }
	void setZ(float z) override { m_z=z; label->setZ(z-.001f); updateGLTransform(); }
	void setClipRegion(const Rect2f& region) override {
		clip=region; background->clip_region=ui.OpenGLRectCoordsForUICoords(region); label->setClipRegion(region);
	}
	void updateGLTransform() override {
		const Vec2f dims=getDims();
		if(dims!=last_dims) {
			background->mesh_data=MeshPrimitiveBuilding::makeRoundedCornerRect(*engine->vert_buf_allocator,
				Vec4f(1,0,0,0),Vec4f(0,1,0,0),dims.x,dims.y,myMin(ui.getUIWidthForDevIndepPixelWidth(7),myMin(dims.x,dims.y)*.25f),8);
			last_dims=dims;
		}
		const float aspect=engine->getViewPortAspectRatio();
		background->ob_to_world_matrix=Matrix4f::translationMatrix(rect.getMin().x,rect.getMin().y*aspect,m_z+.001f)*Matrix4f::scaleMatrix(1,aspect,1);
		label->updateGLTransform(); label->setPos(Vec2f(0));
		const auto bounds=label->getRect();
		label->setPos(rect.getMin()+(dims-bounds.getWidths())*.5f-bounds.getMin());
		setClipRegion(clip); updateColour(false);
	}
	void handleMousePress(MouseEvent& event) override {
		const Vec2f p=ui.UICoordsForOpenGLCoords(event.gl_coords);
		if(shown && rect.inClosedRectangle(p) && clip.inClosedRectangle(p)) {
			event.accepted=true;
			if(handler) { GLUICallbackEvent e; e.widget=this; handler->eventOccurred(e); }
		}
	}
	void doHandleMouseMoved(MouseEvent& event) override {
		const Vec2f p=ui.UICoordsForOpenGLCoords(event.gl_coords);
		const bool over=shown && rect.inClosedRectangle(p) && clip.inClosedRectangle(p);
		updateColour(over); if(over) event.accepted=true;
	}
private:
	void updateColour(bool over) { background->material.albedo_linear_rgb=toLinearSRGB(toggled?Colour3f(.33f,.29f,.48f):(over?Colour3f(.27f,.26f,.31f):Colour3f(.19f,.18f,.22f))); }
	GLUI& ui;
	Reference<OpenGLEngine> engine;
	OverlayObjectRef background;
	GLUITextViewRef label;
	Rect2f clip;
	Vec2f last_dims=Vec2f(-1);
	bool shown=true,toggled=false;
};
typedef Reference<ChatOverlayButton> ChatOverlayButtonRef;
