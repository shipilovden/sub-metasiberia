// Standalone native GPU regression; link this file + PhotoPostProcess.cpp with
// Qt Core/Gui/Widgets (Qt 5: OpenGL for --qgl; Qt 6: OpenGL). No real client,
// network, user settings, CMake changes or engine libraries. Outputs only PASS
// or an error; fixtures live in QTemporaryDir. Requires desktop GL 3.3+.
#include "../../gui_client/PhotoPostProcess.h"
#include <QApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QTemporaryDir>
#include <QFile>
#include <QImage>
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
#include <QGLWidget>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using Pixels=std::vector<unsigned char>;
using GL=QOpenGLFunctions_3_3_Core;
void require(bool v, const char* message) { if(!v) throw std::runtime_error(message); }
template<class F> void throws(F f) { bool caught=false; try { f(); } catch(const std::runtime_error&) { caught=true; } require(caught,"expected runtime_error"); }

struct Fixture
{
	GL& gl;
	QSize size;
	GLuint source=0,target=0,output=0,extra=0;
	Pixels input;
	Fixture(GL& f,QSize dimensions) : gl(f),size(dimensions),input(size.width()*size.height()*4)
	{
		for(int y=0;y<size.height();++y) for(int x=0;x<size.width();++x)
		{
			const size_t i=(y*size.width()+x)*4;
			input[i]=static_cast<unsigned char>(x*255/std::max(1,size.width()-1));
			input[i+1]=static_cast<unsigned char>(y*255/std::max(1,size.height()-1));
			input[i+2]=static_cast<unsigned char>((x+y)*255/std::max(1,size.width()+size.height()-2));
			input[i+3]=static_cast<unsigned char>(30+(x*7+y*11)%226);
		}
		gl.glGenTextures(1,&source); gl.glGenTextures(1,&output); gl.glGenTextures(1,&extra);
		for(GLuint t : {source,output,extra})
		{
			gl.glBindTexture(GL_TEXTURE_2D,t);
			gl.glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,size.width(),size.height(),0,GL_RGBA,GL_UNSIGNED_BYTE,t==source?input.data():nullptr);
			gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
			gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
		}
		gl.glGenFramebuffers(1,&target); gl.glBindFramebuffer(GL_FRAMEBUFFER,target);
		gl.glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
		gl.glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,extra,0);
		require(gl.glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"fixture FBO");
	}
	~Fixture()
	{
		gl.glDeleteFramebuffers(1,&target);
		for(GLuint t : {source,output,extra}) gl.glDeleteTextures(1,&t);
	}
	void upload()
	{
		gl.glActiveTexture(GL_TEXTURE0); gl.glBindTexture(GL_TEXTURE_2D,source);
		gl.glTexSubImage2D(GL_TEXTURE_2D,0,0,0,size.width(),size.height(),GL_RGBA,GL_UNSIGNED_BYTE,input.data());
	}
	Pixels read()
	{
		GLint previous; gl.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous);
		gl.glBindFramebuffer(GL_READ_FRAMEBUFFER,target);
		GLint previousBuffer; gl.glGetIntegerv(GL_READ_BUFFER,&previousBuffer);
		gl.glReadBuffer(GL_COLOR_ATTACHMENT0);
		Pixels out(input.size()); gl.glReadPixels(0,0,size.width(),size.height(),GL_RGBA,GL_UNSIGNED_BYTE,out.data());
		gl.glReadBuffer(previousBuffer); gl.glBindFramebuffer(GL_READ_FRAMEBUFFER,previous);
		return out;
	}
};

std::vector<GLint> snapshot(GL& gl)
{
	std::vector<GLint> s;
	for(GLenum key : {GL_CURRENT_PROGRAM,GL_VERTEX_ARRAY_BINDING,GL_DRAW_FRAMEBUFFER_BINDING,GL_READ_FRAMEBUFFER_BINDING,
		GL_READ_BUFFER,GL_ACTIVE_TEXTURE,GL_ARRAY_BUFFER_BINDING,GL_ELEMENT_ARRAY_BUFFER_BINDING,GL_RENDERBUFFER_BINDING,
		GL_PIXEL_UNPACK_BUFFER_BINDING,GL_PIXEL_PACK_BUFFER_BINDING,GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_IMAGE_HEIGHT,
		GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_IMAGES,GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST,
		GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA,GL_DEPTH_FUNC,GL_DEPTH_WRITEMASK,
		GL_CULL_FACE_MODE,GL_FRONT_FACE})
	{
		GLint v; gl.glGetIntegerv(key,&v); s.push_back(v);
	}
	GLint drawCount=0; gl.glGetIntegerv(GL_MAX_DRAW_BUFFERS,&drawCount);
	for(int i=0;i<drawCount;++i)
	{
		GLint v; gl.glGetIntegerv(GL_DRAW_BUFFER0+i,&v); s.push_back(v);
		GLboolean mask[4]; gl.glGetBooleani_v(GL_COLOR_WRITEMASK,i,mask);
		for(auto m : mask) s.push_back(m);
		s.push_back(gl.glIsEnabledi(GL_BLEND,i));
	}
	for(GLenum key : {GL_DEPTH_TEST,GL_STENCIL_TEST,GL_CULL_FACE,GL_SCISSOR_TEST,GL_RASTERIZER_DISCARD,GL_FRAMEBUFFER_SRGB,
		GL_DITHER,GL_COLOR_LOGIC_OP,GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_ALPHA_TO_ONE,GL_SAMPLE_COVERAGE,GL_SAMPLE_MASK})
		s.push_back(gl.glIsEnabled(key));
	GLint clips=0; gl.glGetIntegerv(GL_MAX_CLIP_DISTANCES,&clips);
	for(int i=0;i<clips;++i) s.push_back(gl.glIsEnabled(GL_CLIP_DISTANCE0+i));
	for(GLenum key : {GL_VIEWPORT,GL_SCISSOR_BOX})
	{
		GLint v[4]; gl.glGetIntegerv(key,v); s.insert(s.end(),v,v+4);
	}
	GLint polygon[2] = {}; gl.glGetIntegerv(GL_POLYGON_MODE,polygon); s.insert(s.end(),polygon,polygon+2);
	GLint active; gl.glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
	for(int i=0;i<4;++i)
	{
		gl.glActiveTexture(GL_TEXTURE0+i);
		for(GLenum key : {GL_TEXTURE_BINDING_2D,GL_TEXTURE_BINDING_3D,GL_SAMPLER_BINDING})
		{
			GLint v; gl.glGetIntegerv(key,&v); s.push_back(v);
		}
	}
	gl.glActiveTexture(active);
	return s;
}

Pixels render(PhotoPostProcess& adapter,Fixture& f,const QVariantMap& s={},bool guides=false,double t=0)
{
	const auto before=snapshot(f.gl);
	adapter.render(f.source,f.size,f.target,s,guides,t);
	require(snapshot(f.gl)==before,"GL state changed after render");
	require(f.gl.glGetError()==GL_NO_ERROR,"GL error after render");
	return f.read();
}
int delta(const Pixels& a,const Pixels& b)
{
	int d=0; for(size_t i=0;i<a.size();++i) d=std::max(d,std::abs(int(a[i])-int(b[i]))); return d;
}
QString cubeText(bool invert=false)
{
	QString s="TITLE \"Regression # cube\"\nLUT_3D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n";
	for(int b=0;b<2;++b) for(int g=0;g<2;++g) for(int r=0;r<2;++r)
		s+=QString("%1 %2 %3\n").arg(invert?1-r:r).arg(invert?1-g:g).arg(invert?1-b:b);
	return s;
}
void writeFixture(const QString& path,const QString& data)
{
	QFile file(path); require(file.open(QIODevice::WriteOnly),"fixture open");
	const QByteArray bytes=data.toUtf8(); require(file.write(bytes)==bytes.size(),"fixture write");
}

void run(GL& gl)
{
	PhotoPostProcess adapter;
	Fixture f(gl,QSize(128,80));
	std::cout << "GL " << gl.glGetString(GL_VERSION) << '\n';
	const Pixels original=render(adapter,f);
	require(original==f.input,"neutral must be pixel exact including alpha/orientation");
	const std::vector<std::pair<const char*,double>> effects={{"contrast",0.6},{"shadows",0.5},{"highlights",-0.5},
		{"blacks",0.5},{"whites",-0.5},{"temperature_kelvin",3200},{"wb_tint",0.6},{"warmth",0.5},{"tint",-0.5},
		{"grain",0.8},{"split_strength",0.7},{"vignette",0.9},{"diffusion",1},{"halation",1},{"aberration",1},{"distortion",0.5}};
	QVariantMap all;
	for(const auto& effect : effects)
	{
		QVariantMap s; s[effect.first]=effect.second;
		const Pixels changed=render(adapter,f,s);
		if(delta(original,changed)==0) throw std::runtime_error(std::string("effect inactive: ")+effect.first);
		all[effect.first]=effect.second;
	}
	all["compare_original"]=true; require(render(adapter,f,all)==original,"compare must bypass all effects");
	all["compare_original"]=false; all["filter_strength"]=0; require(render(adapter,f,all)==original,"zero strength must bypass all effects");
	all["filter_strength"]=1;
	QVariantMap diagnostics={{"clipping_zebra",true},{"focus_peaking",true},{"aspect_ratio",1.0},{"show_grid",true}};
	require(render(adapter,f,diagnostics)==original,"guide-free export changed by diagnostics");
	require(delta(render(adapter,f,diagnostics,true),original)>20,"guide shader inactive");
	const QRect crop=PhotoPostProcess::cropRect(f.size,1);
	require(crop==QRect(24,0,80,80),"crop geometry");
	QVariantMap mask={{"aspect_ratio",1.0}};
	const Pixels masked=render(adapter,f,mask,true);
	const size_t outside=(40*128+4)*4, inside=(40*128+50)*4;
	require(std::abs(masked[outside+1]-original[outside+1]*0.45)<1.1,"crop mask must be 55 percent black");
	require(masked[inside+1]==original[inside+1],"crop mask touched interior");
	const auto grain=render(adapter,f,{{"grain",1.0},{"grain_size",2.0}},false,3.0);
	require(grain==render(adapter,f,{{"grain",1.0},{"grain_size",2.0}},false,3.0),"grain must be deterministic at fixed time");
	require(grain!=render(adapter,f,{{"grain",1.0},{"grain_size",2.0}},false,3.2),"grain must animate");

	// Integer crop geometry, including odd margins, must match full-frame export.
	const QSize odd(127,82); const QRect oddCrop=PhotoPostProcess::cropRect(odd,1.3);
	Fixture full(gl,odd), cropped(gl,oddCrop.size());
	for(int y=0;y<cropped.size.height();++y) for(int x=0;x<cropped.size.width();++x) for(int c=0;c<4;++c)
		cropped.input[(y*cropped.size.width()+x)*4+c]=full.input[((y+odd.height()-oddCrop.y()-oddCrop.height())*odd.width()+x+oddCrop.x())*4+c];
	cropped.upload();
	QVariantMap frame={{"aspect_ratio",1.3},{"vignette",0.8},{"vignette_center_x",0.2},{"vignette_center_y",-0.15},{"grain",0.2}};
	const Pixels fullOut=render(adapter,full,frame,false,1.0);
	frame["aspect_ratio"]=0;
	const Pixels cropOut=render(adapter,cropped,frame,false,1.0);
	for(int y=0;y<cropped.size.height();++y) for(int x=0;x<cropped.size.width();++x) for(int c=0;c<4;++c)
		require(std::abs(int(cropOut[(y*cropped.size.width()+x)*4+c])-int(fullOut[((y+odd.height()-oddCrop.y()-oddCrop.height())*odd.width()+x+oddCrop.x())*4+c]))<=1,"vignette/grain not anchored to export crop");

	// Source UV agrees with actual green-channel distortion on a linear ramp.
	for(double distortion : {-0.5,0.5})
	{
		QVariantMap s={{"distortion",distortion},{"aspect_ratio",1.3},{"filter_strength",0.7}};
		const Pixels warped=render(adapter,full,s);
		for(int y=5;y<odd.height();y+=11) for(int x=5;x<odd.width();x+=13)
		{
			const QPointF uv=PhotoPostProcess::sourceUV(QPointF((x+0.5)/odd.width(),1-(y+0.5)/odd.height()),odd,s);
			const double expected=((1-uv.y())*odd.height()-0.5)*255/(odd.height()-1);
			require(std::abs(warped[(y*odd.width()+x)*4+1]-expected)<2,"focus source UV differs from shader");
		}
	}

	// Black and white endpoint controls; 6500 exact identity and actual correction
	// direction on neutral grey. Exercise every CCT/tint bound without NaNs.
	Fixture grey(gl,QSize(16,16));
	for(size_t i=0;i<grey.input.size();i+=4) { grey.input[i]=grey.input[i+1]=grey.input[i+2]=128; grey.input[i+3]=255; }
	grey.upload();
	require(render(adapter,grey,{{"temperature_kelvin",6500}})==grey.input,"6500 K grey identity");
	const auto cool=render(adapter,grey,{{"temperature_kelvin",2000}});
	const auto warm=render(adapter,grey,{{"temperature_kelvin",12000}});
	require(cool[2]>cool[0] && warm[0]>warm[2],"Bradford correction direction");
	Fixture larger(gl,QSize(48,48));
	for(size_t i=0;i<larger.input.size();i+=4) { larger.input[i]=larger.input[i+1]=larger.input[i+2]=128; larger.input[i+3]=255; }
	larger.upload();
	const auto smallGrain=render(adapter,grey,{{"grain",1.0}});
	const auto largeGrain=render(adapter,larger,{{"grain",1.0},{"photo_pixel_scale",3.0}});
	for(int y=0;y<16;++y) for(int x=0;x<16;++x)
		require(std::abs(int(smallGrain[(y*16+x)*4])-int(largeGrain[((y*3+1)*48+x*3+1)*4]))<=1,"grain export scale mismatch");
	for(double temperature : {2000.,2222.,4000.,6500.,12000.}) for(double tint : {-1.,0.,1.})
	{
		render(adapter,grey,{{"temperature_kelvin",temperature},{"wb_tint",tint}});
		// Float target exposes NaN/Inf instead of hiding them in normalized bytes.
		gl.glBindTexture(GL_TEXTURE_2D,grey.output);
		gl.glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,16,16,0,GL_RGBA,GL_FLOAT,nullptr);
		adapter.render(grey.source,grey.size,grey.target,{{"temperature_kelvin",temperature},{"wb_tint",tint}},false,0);
		gl.glBindFramebuffer(GL_READ_FRAMEBUFFER,grey.target); gl.glReadBuffer(GL_COLOR_ATTACHMENT0);
		std::vector<float> floats(16*16*4); gl.glReadPixels(0,0,16,16,GL_RGBA,GL_FLOAT,floats.data());
		for(float x : floats) require(std::isfinite(x) && x>=0 && x<=1,"nonfinite or non-SDR white balance output");
	}
	std::fill(grey.input.begin(),grey.input.end(),0); grey.upload();
	require(render(adapter,grey,{{"blacks",1.0}})[0]>0,"blacks must lift pure black");
	std::fill(grey.input.begin(),grey.input.end(),255); grey.upload();
	require(render(adapter,grey,{{"whites",-1.0}})[0]<255,"whites must lower white endpoint");

	QTemporaryDir fixtures; require(fixtures.isValid(),"temporary fixtures");
	const QString path=fixtures.filePath("lut.cube");
	writeFixture(path,cubeText()); PhotoPostProcess::validateLut(path);
	require(delta(render(adapter,f,{{"lut_path",path}}),original)<=1,"identity LUT axis order/interpolation");
	writeFixture(path,cubeText(true)); adapter.invalidateLutCache();
	const auto inverse=render(adapter,f,{{"lut_path",path}});
	for(size_t i=0;i<inverse.size();++i)
		require(std::abs(int(inverse[i])-int(i%4==3?original[i]:255-original[i]))<=1,"inverting LUT result");
	require(render(adapter,f,{{"lut_path",path},{"lut_strength",0}})==original,"zero LUT strength");
	for(const QString& invalid : {QString("LUT_3D_SIZE 65\n"),QString("LUT_3D_SIZE -3\n"),QString("LUT_1D_SIZE 2\n"),
		QString("LUT_3D_SIZE 2\n0 0 0\n"),cubeText()+"0 0 0\n",QString("DOMAIN_MIN -1 0 0\n")+cubeText(),
		QString("LUT_3D_SIZE 2\nnan 0 0\n"),QString("LUT_3D_SIZE 2\n1e900 0 0\n"),QString("LUT_3D_SIZE 2\n0 2 0\n"),
		QString("TITLE bad\n")+cubeText(),QString(5000,'x')})
	{
		writeFixture(path,invalid); adapter.invalidateLutCache();
		throws([&] { PhotoPostProcess::validateLut(path); });
		const auto before=snapshot(gl);
		throws([&] { adapter.render(f.source,f.size,f.target,{{"lut_path",path}},false,0); });
		require(snapshot(gl)==before,"GL state after invalid LUT");
		throws([&] { adapter.render(f.source,f.size,f.target,{{"lut_path",path}},false,0); }); // cached failure
	}
	writeFixture(path,cubeText()); adapter.invalidateLutCache();
	require(delta(render(adapter,f,{{"lut_path",path}}),original)<=1,"LUT recovery");
	throws([&] { PhotoPostProcess::validateLut(fixtures.filePath("missing.cube")); });

	// Poison engine-like state, including an unpack PBO for first LUT upload,
	// MRT masks, samplers, independent read FBO, wireframe, clip planes and sRGB.
	GLuint vao,buffer,sampler;
	gl.glGenVertexArrays(1,&vao); gl.glBindVertexArray(vao);
	gl.glGenBuffers(1,&buffer); gl.glBindBuffer(GL_ARRAY_BUFFER,buffer);
	gl.glBufferData(GL_ARRAY_BUFFER,256,nullptr,GL_STATIC_DRAW);
	gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffer); gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffer);
	gl.glGenSamplers(1,&sampler); gl.glSamplerParameteri(sampler,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_LINEAR);
	gl.glBindSampler(0,sampler); gl.glBindSampler(1,sampler);
	gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER,f.target); gl.glDrawBuffer(GL_COLOR_ATTACHMENT1);
	gl.glBindFramebuffer(GL_READ_FRAMEBUFFER,full.target);
	gl.glActiveTexture(GL_TEXTURE0); gl.glBindTexture(GL_TEXTURE_2D,f.source);
	gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,2);
	gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,8);
	gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
	gl.glActiveTexture(GL_TEXTURE3);
	gl.glViewport(3,5,17,19); gl.glScissor(2,4,6,8); gl.glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
	gl.glColorMaski(0,GL_FALSE,GL_TRUE,GL_FALSE,GL_FALSE); gl.glColorMaski(1,GL_TRUE,GL_FALSE,GL_TRUE,GL_FALSE);
	gl.glEnablei(GL_BLEND,0); gl.glDisablei(GL_BLEND,1);
	gl.glBlendFuncSeparate(GL_DST_COLOR,GL_ONE_MINUS_DST_COLOR,GL_ONE,GL_ZERO);
	for(GLenum cap : {GL_DEPTH_TEST,GL_STENCIL_TEST,GL_CULL_FACE,GL_SCISSOR_TEST,GL_RASTERIZER_DISCARD,GL_FRAMEBUFFER_SRGB,
		GL_DITHER,GL_COLOR_LOGIC_OP,GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_ALPHA_TO_ONE,GL_SAMPLE_COVERAGE,GL_SAMPLE_MASK,GL_CLIP_DISTANCE0}) gl.glEnable(cap);
	gl.glPixelStorei(GL_UNPACK_ALIGNMENT,8); gl.glPixelStorei(GL_UNPACK_ROW_LENGTH,17);
	gl.glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,19); gl.glPixelStorei(GL_UNPACK_SKIP_PIXELS,2);
	gl.glPixelStorei(GL_UNPACK_SKIP_ROWS,3); gl.glPixelStorei(GL_UNPACK_SKIP_IMAGES,1); gl.glPixelStorei(GL_UNPACK_SWAP_BYTES,1);
	QOpenGLShaderProgram existing;
	require(existing.addShaderFromSourceCode(QOpenGLShader::Vertex,"#version 150\nvoid main(){gl_Position=vec4(0);}"),"state shader vertex");
	require(existing.addShaderFromSourceCode(QOpenGLShader::Fragment,"#version 150\nout vec4 c;void main(){c=vec4(1);}"),"state shader fragment");
	require(existing.link() && existing.bind(),"state shader bind");
	adapter.invalidateLutCache();
	require(delta(render(adapter,f,{{"lut_path",path}}),original)<=1,"render under poisoned state");
	gl.glActiveTexture(GL_TEXTURE0);
	GLint parameter;
	gl.glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,&parameter); require(parameter==2,"source base level restore");
	gl.glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,&parameter); require(parameter==8,"source max level restore");
	gl.glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&parameter); require(parameter==GL_LINEAR_MIPMAP_LINEAR,"source sampler restore");
	const auto before=snapshot(gl);
	throws([&] { adapter.render(f.source,QSize(2,2),f.target,{},false,0); });
	if(snapshot(gl)!=before)
	{
		const auto after=snapshot(gl);
		for(size_t i=0;i<before.size();++i) if(before[i]!=after[i]) std::cerr << "state index " << i << ": " << before[i] << " -> " << after[i] << '\n';
	}
	require(snapshot(gl)==before,"state after invalid dimensions");
	throws([&] { adapter.render(f.source,f.size,f.target,{{"grain",std::numeric_limits<double>::quiet_NaN()}},false,0); });
	require(snapshot(gl)==before,"state after invalid setting");
	gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER,f.target);
	gl.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,f.source,0);
	throws([&] { adapter.render(f.source,f.size,f.target,{},false,0); });
	gl.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,f.output,0);
	require(snapshot(gl)==before,"state after texture feedback rejection");
	adapter.release(); adapter.release();
	require(snapshot(gl)==before,"release altered external state");
	gl.glUseProgram(0); gl.glDeleteSamplers(1,&sampler); gl.glDeleteBuffers(1,&buffer); gl.glDeleteVertexArrays(1,&vao);
	// Restore upload/raster state before constructing the final tiny fixture.
	gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
	for(GLenum k : {GL_UNPACK_ROW_LENGTH,GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_IMAGES,GL_UNPACK_SWAP_BYTES}) gl.glPixelStorei(k,0);
	gl.glPixelStorei(GL_UNPACK_ALIGNMENT,1);
	Fixture tiny(gl,QSize(1,1));
	require(render(adapter,tiny)==tiny.input,"1x1 neutral");
	render(adapter,tiny,all,true); adapter.release();
	require(gl.glGetError()==GL_NO_ERROR,"final GL error");
	std::cout << "PASS: identity, effects, compare/zero, crop/guides, focus UV, Bradford/endpoints, LUT/error cache, edge handling, GL state/release\n";
}
}

int main(int argc,char** argv)
{
	QApplication app(argc,argv);
	try
	{
		QSurfaceFormat format; format.setVersion(3,3); format.setProfile(QSurfaceFormat::CoreProfile); format.setSamples(0);
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
		if(app.arguments().contains("--qgl"))
		{
			QGLFormat legacy; legacy.setVersion(3,3); legacy.setProfile(QGLFormat::CoreProfile); legacy.setSampleBuffers(false);
			QGLWidget widget(legacy); widget.resize(128,80); widget.makeCurrent();
			require(widget.isValid() && QOpenGLContext::currentContext(),"hidden QGLWidget context unavailable");
			GL gl; require(gl.initializeOpenGLFunctions(),"GL functions"); run(gl); return 0;
		}
#endif
		QOpenGLContext context; context.setFormat(format); require(context.create(),"create GL context");
		QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
		require(surface.isValid() && context.makeCurrent(&surface),"offscreen context unavailable");
		GL gl; require(gl.initializeOpenGLFunctions(),"GL functions"); run(gl);
	}
	catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
	return 0;
}
