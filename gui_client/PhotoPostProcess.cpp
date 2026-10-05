#include "PhotoPostProcess.h"

#if !defined(USE_SDL)
#include <QtGui/QOpenGLContext>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtOpenGL/QOpenGLFunctions_3_2_Core>
#include <QtOpenGL/QOpenGLShaderProgram>
#else
#include <QtGui/QOpenGLFunctions_3_2_Core>
#include <QtGui/QOpenGLShaderProgram>
#endif
#include <QtCore/QPointer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QDateTime>
#include <QtGui/QVector2D>
#include <QtGui/QVector3D>
#include <QtGui/QVector4D>
#include <QtGui/QGenericMatrix>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <locale>
#include <stdexcept>
#include <vector>

namespace {
using GL = QOpenGLFunctions_3_2_Core;
using BindSampler = void (QOPENGLF_APIENTRYP)(GLuint, GLuint);
using ClipControl = void (QOPENGLF_APIENTRYP)(GLenum, GLenum);
using ViewportIndexed = void (QOPENGLF_APIENTRYP)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
using GetFloatIndexed = void (QOPENGLF_APIENTRYP)(GLenum, GLuint, GLfloat*);

void fail(const QString& message) { throw std::runtime_error(("PhotoPostProcess: " + message).toStdString()); }

float value(const QVariantMap& s, const char* key, float fallback, float lo, float hi)
{
	const auto it = s.constFind(QLatin1String(key));
	if(it == s.constEnd()) return fallback;
	bool ok = false;
	const double v = it.value().toDouble(&ok);
	if(!ok || !std::isfinite(v)) fail(QString("invalid numeric setting: %1").arg(QLatin1String(key)));
	return float(std::max(double(lo), std::min(double(hi), v)));
}

QMatrix3x3 whiteBalance(float kelvin, float tint)
{
	QMatrix3x3 result;
	result.setToIdentity();
	if(kelvin==6500 && tint==0) return result;
	// Bounded approximation of the Planckian locus (1667..25000 K polynomial;
	// this API restricts it to 2000..12000). XYZ is normalized to Y=1.
	const auto white = [](double t, double tintOffset, double* xyz)
	{
		const double x = t<=4000 ? -0.2661239e9/(t*t*t)-0.2343580e6/(t*t)+0.8776956e3/t+0.179910 :
			-3.0258469e9/(t*t*t)+2.1070379e6/(t*t)+0.2226347e3/t+0.240390;
		double y;
		if(t<=2222) y=-1.1063814*x*x*x-1.34811020*x*x+2.18555832*x-0.20219683;
		else if(t<=4000) y=-0.9549476*x*x*x-1.37418593*x*x+2.09137015*x-0.16748867;
		else y=3.0817580*x*x*x-5.87338670*x*x+3.75112997*x-0.37001483;
		// Tint describes a green displacement of the assumed illuminant. Applying
		// its inverse adaptation produces the requested magenta correction.
		y += tintOffset*0.025;
		xyz[0]=x/y; xyz[1]=1; xyz[2]=(1-x-y)/y;
	};
	const double rgbXYZ[3][3]={{0.4124564,0.3575761,0.1804375},{0.2126729,0.7151522,0.0721750},{0.0193339,0.1191920,0.9503041}};
	const double xyzRGB[3][3]={{3.2404542,-1.5371385,-0.4985314},{-0.9692660,1.8760108,0.0415560},{0.0556434,-0.2040259,1.0572252}};
	const double bradford[3][3]={{0.8951,0.2664,-0.1614},{-0.7502,1.7135,0.0367},{0.0389,-0.0685,1.0296}};
	const double inverse[3][3]={{0.9869929,-0.1470543,0.1599627},{0.4323053,0.5183603,0.0492912},{-0.0085287,0.0400428,0.9684867}};
	double reference[3], illuminant[3], gain[3];
	white(6500,0,reference); white(kelvin,tint,illuminant);
	for(int i=0; i<3; ++i)
	{
		double a=0,b=0;
		for(int j=0;j<3;++j) { a+=bradford[i][j]*reference[j]; b+=bradford[i][j]*illuminant[j]; }
		// Calibrate the Planckian 6500 white to sRGB D65 in cone space. This
		// ratio equals Bradford(selected calibrated white -> D65), exactly 1 at
		// neutral, avoiding the small D65/Planckian-6500 locus discrepancy.
		gain[i]=a/b;
	}
	for(int r=0;r<3;++r) for(int c=0;c<3;++c)
	{
		double a=0;
		for(int i=0;i<3;++i) for(int j=0;j<3;++j) for(int k=0;k<3;++k)
			a+=xyzRGB[r][i]*inverse[i][j]*gain[j]*bradford[j][k]*rgbXYZ[k][c];
		if(!std::isfinite(a)) fail("invalid white balance matrix");
		result(r,c)=float(a);
	}
	return result;
}

struct Cube { int size = 0; std::vector<float> rgb; };
Cube parseCube(const QString& path)
{
	if(QFileInfo(path).suffix().compare("cube", Qt::CaseInsensitive) != 0) fail("LUT must be a .cube file");
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly)) fail("cannot open LUT: " + file.errorString());
	constexpr qint64 maxBytes = 32 * 1024 * 1024;
	if(file.size() > maxBytes) fail("LUT exceeds 32 MiB limit");
	Cube cube;
	bool title = false, domainMin = false, domainMax = false, data = false;
	qint64 bytes = 0;
	int lineNumber = 0;
	while(!file.atEnd())
	{
		QByteArray line = file.readLine(4098);
		++lineNumber;
		bytes += line.size();
		const auto bad = [&](const char* reason) { fail(QString("LUT line %1: %2").arg(lineNumber).arg(reason)); };
		if(file.error() != QFileDevice::NoError) fail("LUT read failed: " + file.errorString());
		if(line.size() > 4096 || bytes > maxBytes) bad("input limit exceeded");
		if(lineNumber == 1 && line.startsWith("\xEF\xBB\xBF")) line.remove(0, 3);
		// Strip comments outside a quoted TITLE; reject embedded NUL/control bytes.
		bool quoted = false;
		for(int i=0; i<line.size(); ++i)
		{
			const unsigned char c = static_cast<unsigned char>(line[i]);
			if(c < 32 && c != '\t' && c != '\r' && c != '\n') bad("control character");
			if(c == '"') quoted = !quoted;
			if(c == '#' && !quoted) { line.truncate(i); break; }
		}
		line = line.trimmed();
		if(line.isEmpty()) continue;
		std::istringstream input(line.toStdString());
		input.imbue(std::locale::classic());
		std::string token, extra;
		input >> token;
		if(token == "TITLE")
		{
			const QByteArray name = line.mid(5).trimmed();
			if(title || data || name.size() < 2 || name.front() != '"' || name.back() != '"' || name.count('"') != 2)
				bad("invalid or repeated TITLE");
			title = true;
		}
		else if(token == "LUT_3D_SIZE")
		{
			int n = 0;
			if(cube.size || data || !(input >> n) || (input >> extra) || n < 2 || n > 64) bad("LUT_3D_SIZE must be 2..64, once before data");
			cube.size = n;
			cube.rgb.reserve(size_t(n)*n*n*3);
		}
		else if(token == "DOMAIN_MIN" || token == "DOMAIN_MAX")
		{
			bool& seen = token == "DOMAIN_MIN" ? domainMin : domainMax;
			const double expected = token == "DOMAIN_MIN" ? 0.0 : 1.0;
			double r, g, b;
			if(seen || data || !(input >> r >> g >> b) || (input >> extra) || r != expected || g != expected || b != expected)
				bad("only DOMAIN_MIN 0 0 0 / DOMAIN_MAX 1 1 1 are supported");
			seen = true;
		}
		else
		{
			if(!cube.size) bad("expected LUT_3D_SIZE before RGB data (1D/shaper LUTs are unsupported)");
			input.clear(); input.seekg(0);
			double r, g, b;
			if(!(input >> r >> g >> b) || (input >> extra)) bad("expected exactly three RGB numbers or a supported header");
			if(!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || r<0 || r>1 || g<0 || g>1 || b<0 || b>1)
				bad("RGB entries must be finite and in [0,1]");
			if(cube.rgb.size() >= size_t(cube.size)*cube.size*cube.size*3) bad("too many RGB entries");
			cube.rgb.insert(cube.rgb.end(), {float(r), float(g), float(b)});
			data = true;
		}
	}
	if(!cube.size || cube.rgb.size() != size_t(cube.size)*cube.size*cube.size*3) fail("LUT has missing RGB entries or size");
	return cube;
}

// Save actual driver state, not assumptions about the engine's cached state.
// Indexed blend/mask zero leaves other MRT outputs' independent state intact.
struct State
{
	GL& gl;
	BindSampler bindSampler;
	ClipControl clipControl;
	ViewportIndexed viewportIndexed;
	GLfloat preciseViewport[4] = {};
	GLint program, vao, framebuffer, viewport[4], active, texture2D, texture3D, sampler[2] = {}, polygon[2];
	GLint unpackBuffer, unpack[8], clipOrigin = GL_LOWER_LEFT, clipDepth = 0;
	GLboolean mask[4], blend;
	const GLenum caps[12] = {GL_DEPTH_TEST, GL_STENCIL_TEST, GL_CULL_FACE, GL_SCISSOR_TEST,
		GL_RASTERIZER_DISCARD, GL_FRAMEBUFFER_SRGB, GL_DITHER, GL_COLOR_LOGIC_OP,
		GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_ALPHA_TO_ONE, GL_SAMPLE_COVERAGE, GL_SAMPLE_MASK};
	GLboolean enabled[12];
	std::vector<GLboolean> clipEnabled;
	const GLenum unpackNames[8] = {GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_IMAGE_HEIGHT,
		GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_IMAGES, GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST};
	GLuint source = 0;
	GLint sourceParams[3] = {};
	bool targetSaved = false;
	GLuint target = 0;
	std::vector<GLenum> drawBuffers;

	State(GL& f, QOpenGLContext* context) : gl(f)
	{
		const QSurfaceFormat fmt = context->format();
		const bool samplers = fmt.majorVersion() > 3 || (fmt.majorVersion() == 3 && fmt.minorVersion() >= 3) || context->hasExtension("GL_ARB_sampler_objects");
		bindSampler = samplers ? reinterpret_cast<BindSampler>(context->getProcAddress("glBindSampler")) : nullptr;
		const bool clipping = fmt.majorVersion() > 4 || (fmt.majorVersion() == 4 && fmt.minorVersion() >= 5) || context->hasExtension("GL_ARB_clip_control");
		clipControl = clipping ? reinterpret_cast<ClipControl>(context->getProcAddress("glClipControl")) : nullptr;
		const bool arrays = fmt.majorVersion()>4 || (fmt.majorVersion()==4 && fmt.minorVersion()>=1) || context->hasExtension("GL_ARB_viewport_array");
		viewportIndexed = arrays ? reinterpret_cast<ViewportIndexed>(context->getProcAddress("glViewportIndexedf")) : nullptr;
		if(viewportIndexed)
		{
			auto get = reinterpret_cast<GetFloatIndexed>(context->getProcAddress("glGetFloati_v"));
			if(get) get(GL_VIEWPORT,0,preciseViewport); else viewportIndexed=nullptr;
		}
		gl.glGetIntegerv(GL_CURRENT_PROGRAM, &program);
		gl.glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
		gl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
		gl.glGetIntegerv(GL_VIEWPORT, viewport);
		gl.glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
		gl.glGetIntegerv(GL_POLYGON_MODE, polygon);
		gl.glGetBooleani_v(GL_COLOR_WRITEMASK, 0, mask);
		blend = gl.glIsEnabledi(GL_BLEND, 0);
		for(int i=0; i<12; ++i) enabled[i] = gl.glIsEnabled(caps[i]);
		GLint clips = 0;
		gl.glGetIntegerv(GL_MAX_CLIP_DISTANCES, &clips);
		clipEnabled.resize(clips);
		for(int i=0; i<clips; ++i) clipEnabled[i] = gl.glIsEnabled(GL_CLIP_DISTANCE0 + i);
		gl.glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
		for(int i=0; i<8; ++i) gl.glGetIntegerv(unpackNames[i], &unpack[i]);
		if(clipControl)
		{
			gl.glGetIntegerv(0x935C /*GL_CLIP_ORIGIN*/, &clipOrigin);
			gl.glGetIntegerv(0x935D /*GL_CLIP_DEPTH_MODE*/, &clipDepth);
		}
		gl.glActiveTexture(GL_TEXTURE0);
		gl.glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture2D);
		if(bindSampler) gl.glGetIntegerv(GL_SAMPLER_BINDING, &sampler[0]);
		gl.glActiveTexture(GL_TEXTURE1);
		gl.glGetIntegerv(GL_TEXTURE_BINDING_3D, &texture3D);
		if(bindSampler) gl.glGetIntegerv(GL_SAMPLER_BINDING, &sampler[1]);
		gl.glActiveTexture(active);
	}

	void bindSource(GLuint name)
	{
		gl.glActiveTexture(GL_TEXTURE0);
		gl.glBindTexture(GL_TEXTURE_2D, name);
		GLint bound = 0; gl.glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
		if(GLuint(bound) != name) fail("source is not a GL_TEXTURE_2D");
		source = name;
		gl.glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, &sourceParams[0]);
		gl.glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &sourceParams[1]);
		gl.glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &sourceParams[2]);
		gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		if(bindSampler) { bindSampler(0, 0); bindSampler(1, 0); }
	}

	void bindTarget(GLuint name)
	{
		target = name;
		gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
		GLint count = 1;
		if(target) gl.glGetIntegerv(GL_MAX_DRAW_BUFFERS, &count);
		drawBuffers.resize(count);
		for(int i=0; i<count; ++i)
		{
			GLint b = 0; gl.glGetIntegerv(GL_DRAW_BUFFER0+i, &b); drawBuffers[i] = GLenum(b);
		}
		targetSaved = true;
		if(target) gl.glDrawBuffer(GL_COLOR_ATTACHMENT0);
		else
		{
			GLboolean doubleBuffered = GL_FALSE;
			gl.glGetBooleanv(GL_DOUBLEBUFFER,&doubleBuffered);
			gl.glDrawBuffer(doubleBuffered ? GL_BACK : GL_FRONT);
		}
		if(gl.glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fail("target framebuffer is incomplete");
		if(target)
		{
			GLint type = 0, object = 0;
			gl.glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
			if(type == GL_NONE) fail("target needs colour attachment 0");
			// Reject feedback even on a currently disabled colour attachment.
			GLint attachments=0; gl.glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS,&attachments);
			for(int i=0;i<attachments;++i)
			{
				gl.glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+i, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
				if(type == GL_TEXTURE)
				{
					gl.glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+i, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &object);
					if(GLuint(object) == source) fail("source and target must not alias (texture feedback)");
				}
			}
		}
	}

	~State()
	{
		if(targetSaved)
		{
			gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
			if(target) gl.glDrawBuffers(GLsizei(drawBuffers.size()), drawBuffers.data());
			else gl.glDrawBuffer(drawBuffers[0]);
		}
		gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
		gl.glUseProgram(program);
		gl.glBindVertexArray(vao);
		if(viewportIndexed) viewportIndexed(0,preciseViewport[0],preciseViewport[1],preciseViewport[2],preciseViewport[3]);
		else gl.glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
		gl.glPolygonMode(GL_FRONT_AND_BACK, polygon[0]);
		gl.glColorMaski(0, mask[0], mask[1], mask[2], mask[3]);
		if(blend) gl.glEnablei(GL_BLEND, 0); else gl.glDisablei(GL_BLEND, 0);
		for(int i=0; i<12; ++i)
		{
			if(caps[i]==GL_SCISSOR_TEST && viewportIndexed) { if(enabled[i]) gl.glEnablei(caps[i],0); else gl.glDisablei(caps[i],0); }
			else { if(enabled[i]) gl.glEnable(caps[i]); else gl.glDisable(caps[i]); }
		}
		for(size_t i=0; i<clipEnabled.size(); ++i) { if(clipEnabled[i]) gl.glEnable(GL_CLIP_DISTANCE0+GLenum(i)); else gl.glDisable(GL_CLIP_DISTANCE0+GLenum(i)); }
		if(clipControl) clipControl(clipOrigin, clipDepth);
		gl.glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
		for(int i=0; i<8; ++i) gl.glPixelStorei(unpackNames[i], unpack[i]);
		gl.glActiveTexture(GL_TEXTURE0);
		if(source)
		{
			gl.glBindTexture(GL_TEXTURE_2D, source);
			gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, sourceParams[0]);
			gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, sourceParams[1]);
			gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sourceParams[2]);
		}
		gl.glBindTexture(GL_TEXTURE_2D, texture2D);
		gl.glActiveTexture(GL_TEXTURE1);
		gl.glBindTexture(GL_TEXTURE_3D, texture3D);
		if(bindSampler) { bindSampler(0, sampler[0]); bindSampler(1, sampler[1]); }
		gl.glActiveTexture(active);
	}
};

const char* vertexShader = R"GLSL(#version 150 core
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

const char* fragmentShader = R"GLSL(#version 150 core
uniform sampler2D sourceTex;
uniform sampler3D lutTex;
uniform vec2 imageSize, vignetteCenter;
uniform vec4 crop; // bottom-left pixel origin, integer dimensions
uniform vec4 tonal; // contrast, shadows, highlights, blacks
uniform vec4 balance; // whites, unused, unused, warmth
uniform mat3 whiteBalance;
uniform vec4 artistic; // old tint, shadow hue, highlight hue, split strength
uniform vec4 optics; // diffusion, halation, aberration, distortion
uniform vec4 vignette; // strength, radius, softness, roundness
uniform vec4 grain; // amount, pixel size, colour, bounded time
uniform float strength, lutStrength, pixelScale;
uniform int lutSize;
uniform bool colourActive, zebra, peaking, guides, showGrid;
out vec4 colourOut;

vec3 linearise(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
vec3 encode(vec3 c) {
    c = max(c, vec3(0.0));
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0/2.4)) - 0.055, step(vec3(0.0031308), c));
}
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec4 fetchPixel(ivec2 p) { return texelFetch(sourceTex, clamp(p, ivec2(0), ivec2(imageSize)-1), 0); }
// Manual interpolation avoids caller sampler/wrap state and interpolates light,
// not encoded sRGB. All edge taps clamp, including 1x1 images.
vec4 sampleLight(vec2 uv) {
    vec2 p = clamp(uv * imageSize - 0.5, vec2(0), imageSize-1.0);
    ivec2 i = ivec2(floor(p)); vec2 f = fract(p);
    vec4 a = fetchPixel(i), b = fetchPixel(i+ivec2(1,0));
    vec4 c = fetchPixel(i+ivec2(0,1)), d = fetchPixel(i+ivec2(1,1));
    a.rgb=linearise(a.rgb); b.rgb=linearise(b.rgb); c.rgb=linearise(c.rgb); d.rgb=linearise(d.rgb);
    return mix(mix(a,b,f.x), mix(c,d,f.x), f.y);
}
vec3 hue(float h) { return clamp(abs(fract(h/360.0+vec3(0,2.0/3.0,1.0/3.0))*6.0-3.0)-1.0,0.0,1.0); }
float hashNoise(vec2 p, float seed) { return fract(sin(dot(p,vec2(127.1,311.7))+seed*74.7)*43758.5453); }
float noise(vec2 p, float seed) {
    vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(hashNoise(i,seed),hashNoise(i+vec2(1,0),seed),f.x),
               mix(hashNoise(i+vec2(0,1),seed),hashNoise(i+vec2(1,1),seed),f.x),f.y)-0.5;
}
vec3 lut(vec3 c) {
    vec3 p=clamp(c,0.0,1.0)*float(lutSize-1), f=fract(p);
    ivec3 a=ivec3(floor(p)), b=min(a+1,ivec3(lutSize-1));
    return mix(mix(mix(texelFetch(lutTex,a,0).rgb,texelFetch(lutTex,ivec3(b.x,a.y,a.z),0).rgb,f.x),
                   mix(texelFetch(lutTex,ivec3(a.x,b.y,a.z),0).rgb,texelFetch(lutTex,ivec3(b.x,b.y,a.z),0).rgb,f.x),f.y),
               mix(mix(texelFetch(lutTex,ivec3(a.x,a.y,b.z),0).rgb,texelFetch(lutTex,ivec3(b.x,a.y,b.z),0).rgb,f.x),
                   mix(texelFetch(lutTex,ivec3(a.x,b.y,b.z),0).rgb,texelFetch(lutTex,b,0).rgb,f.x),f.y),f.z);
}
void main() {
    vec2 uv=gl_FragCoord.xy/imageSize;
    vec4 original=fetchPixel(ivec2(gl_FragCoord.xy));
    vec4 base=original;
    vec2 frameUV=(gl_FragCoord.xy-crop.xy)/crop.zw;
    vec2 p=frameUV*2.0-1.0;
    vec2 warped=uv + p*dot(p,p)*optics.w*0.25*crop.zw/imageSize;
    bool spatial=any(notEqual(optics,vec4(0.0))) || vignette.x>0.0;
    if(spatial) {
        vec4 light=sampleLight(warped);
        if(optics.z>0.0) {
            vec2 shift=p*dot(p,p)*optics.z*3.0*pixelScale/imageSize;
            light.r=sampleLight(warped+shift).r;
            light.b=sampleLight(warped-shift).b;
        }
        if(optics.x>0.0 || optics.y>0.0) {
            vec3 blurred=vec3(0), glow=vec3(0); float weights=0.0;
            // Fixed, bounded 5x5 soft kernel. Radius tracks output resolution.
            vec2 radius=vec2(max(pixelScale,min(crop.z,crop.w)*0.003))/imageSize;
            for(int y=-2;y<=2;++y) for(int x=-2;x<=2;++x) {
                float w=exp(-float(x*x+y*y)*0.5);
                vec3 tap=sampleLight(warped+vec2(x,y)*radius).rgb;
                blurred+=tap*w;
                glow+=tap*smoothstep(0.55,1.0,luma(tap))*w; weights+=w;
            }
            light.rgb=mix(light.rgb,blurred/weights,optics.x*0.5);
            // SDR highlight halo approximation; no unclipped scene radiance.
            light.rgb+=glow/weights*vec3(0.32,0.065,0.015)*optics.y;
        }
        if(vignette.x>0.0) {
            // Center controls follow UI coordinates (+y down); texture is bottom-up.
            vec2 q=(frameUV-vec2(0.5)-vignetteCenter*vec2(1,-1))*2.0;
            q.x*=mix(1.0,crop.z/crop.w,vignette.w);
            float d=length(q);
            float shade=smoothstep(vignette.y,max(vignette.y+0.0001,vignette.y+vignette.z),d);
            light.rgb*=1.0-vignette.x*shade*0.85;
        }
        base=vec4(clamp(encode(light.rgb),0.0,1.0),light.a);
    }
    vec3 result=base.rgb;
    if(colourActive) {
        vec3 c=linearise(base.rgb);
        float y=luma(base.rgb);
        float s=1.0-smoothstep(0.0,0.6,y), h=smoothstep(0.4,1.0,y);
        float b=1.0-smoothstep(0.0,0.25,y), w=smoothstep(0.75,1.0,y);
        c*=exp2(2.0*(tonal.y*s+tonal.z*h));
        c+=vec3(tonal.w*b*0.08+balance.x*w*0.25);
        c=whiteBalance*max(c,vec3(0));
        c=clamp(encode(c),0.0,1.0);
        c=clamp((c-0.5)*exp2(tonal.x*2.0)+0.5,0.0,1.0);
        c=mix(c,balance.w>=0.0 ? vec3(1,175.0/255.0,85.0/255.0) : vec3(95.0/255.0,160.0/255.0,1),abs(balance.w)*0.2);
        c=mix(c,artistic.x>=0.0 ? vec3(225.0/255.0,115.0/255.0,220.0/255.0) : vec3(115.0/255.0,225.0/255.0,155.0/255.0),abs(artistic.x)*0.2);
        c=mix(c,clamp(c+(hue(artistic.y)-vec3(0.5))*s*0.25+(hue(artistic.z)-vec3(0.5))*h*0.25,0.0,1.0),artistic.w);
        // Creative .cube LUTs operate on display-referred sRGB values.
        if(lutSize>0 && lutStrength>0.0) c=mix(c,lut(c),lutStrength);
        if(grain.x>0.0) {
            vec2 cell=(gl_FragCoord.xy-crop.xy)/grain.y;
            float seed=floor(grain.w*24.0);
            vec3 n=vec3(noise(cell,seed));
            if(grain.z>0.5) n=vec3(n.r,noise(cell,seed+19.0),noise(cell,seed+47.0));
            c+=n*grain.x*0.12*(0.3+0.7*sin(clamp(luma(c),0.0,1.0)*3.14159265));
        }
        result=mix(base.rgb,clamp(c,0.0,1.0),strength);
    }
    // These are display diagnostics, excluded from guide-free exports.
    if(peaking) {
        vec2 dx=vec2(1.0/imageSize.x,0), dy=vec2(0,1.0/imageSize.y);
        float edge=length(vec2(luma(sampleLight(warped+dx).rgb)-luma(sampleLight(warped-dx).rgb),
                               luma(sampleLight(warped+dy).rgb)-luma(sampleLight(warped-dy).rgb)));
        result=mix(result,vec3(0.1,1.0,0.25),smoothstep(0.08,0.25,edge)*0.8);
    }
    if(zebra && (max(result.r,max(result.g,result.b))>=0.995 || max(result.r,max(result.g,result.b))<=0.005)) {
        float stripe=step(0.5,fract((gl_FragCoord.x+gl_FragCoord.y+grain.w*12.0)/12.0));
        result=mix(result,mix(vec3(0.05),vec3(1.0,0.8,0.05),stripe),0.8);
    }
    if(guides) {
        bool inside=all(greaterThanEqual(frameUV,vec2(0))) && all(lessThan(frameUV,vec2(1)));
        if(!inside) result*=0.45;
        else if(showGrid) {
            vec2 d=min(abs(frameUV-vec2(1.0/3.0)),abs(frameUV-vec2(2.0/3.0)))*crop.zw;
            float line=min(d.x,d.y);
            result=mix(result,vec3(0), (1.0-smoothstep(1.0,2.0,line))*0.65);
            result=mix(result,vec3(1), (1.0-smoothstep(0.35,0.85,line))*0.85);
        }
    }
    colourOut=vec4(result,base.a);
}
)GLSL";
} // namespace

struct PhotoPostProcess::Impl
{
	QPointer<QOpenGLContext> context;
	std::unique_ptr<GL> gl;
	std::unique_ptr<QOpenGLShaderProgram> program;
	GLuint vao = 0, lutTexture = 0;
	int lutSize = 0;
	QString cachedPath, cachedError;
	qint64 cachedBytes = -1;
	QDateTime cachedModified;
	bool cached = false;

	void loadLut(const QString& path)
	{
		const QFileInfo info(path);
		const QString absolute = info.absoluteFilePath();
		const qint64 bytes = info.size();
		const QDateTime modified = info.lastModified();
		if(cached && cachedPath == absolute && cachedBytes == bytes && cachedModified == modified)
		{
			if(!cachedError.isEmpty()) throw std::runtime_error(cachedError.toStdString());
			return;
		}
		cached = false;
		Cube cube;
		try { cube = parseCube(path); }
		catch(const std::runtime_error& e)
		{
			cachedPath=absolute; cachedBytes=bytes; cachedModified=modified; cachedError=QString::fromUtf8(e.what()); cached=true;
			throw;
		}
		gl->glActiveTexture(GL_TEXTURE1);
		GLuint replacement = 0;
		gl->glGenTextures(1, &replacement);
		if(!replacement) fail("cannot allocate LUT texture");
		gl->glBindTexture(GL_TEXTURE_3D, replacement);
		gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		gl->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, 0);
		gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		for(GLenum key : {GL_UNPACK_ROW_LENGTH, GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS,
			GL_UNPACK_SKIP_IMAGES, GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST}) gl->glPixelStorei(key, 0);
		gl->glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB32F, cube.size, cube.size, cube.size, 0, GL_RGB, GL_FLOAT, cube.rgb.data());
		GLint allocated = 0;
		gl->glGetTexLevelParameteriv(GL_TEXTURE_3D, 0, GL_TEXTURE_WIDTH, &allocated);
		if(allocated != cube.size) { gl->glDeleteTextures(1, &replacement); fail("LUT texture allocation failed"); }
		if(lutTexture) gl->glDeleteTextures(1, &lutTexture);
		lutTexture=replacement; lutSize=cube.size;
		cachedPath=absolute; cachedBytes=bytes; cachedModified=modified; cachedError.clear(); cached=true;
	}
};

PhotoPostProcess::PhotoPostProcess() : impl(new Impl) {}
PhotoPostProcess::~PhotoPostProcess()
{
	// QOpenGLShaderProgram uses Qt's context-aware resource cleanup. Raw names are
	// owned by the original context; never delete recycled names in another one.
	if(impl->context && impl->context == QOpenGLContext::currentContext())
	{
		if(impl->vao) impl->gl->glDeleteVertexArrays(1,&impl->vao);
		if(impl->lutTexture) impl->gl->glDeleteTextures(1,&impl->lutTexture);
	}
}

void PhotoPostProcess::release()
{
	if(impl->context && impl->context != QOpenGLContext::currentContext()) fail("release requires the owning context current");
	if(impl->context && impl->gl)
	{
		if(impl->vao) impl->gl->glDeleteVertexArrays(1, &impl->vao);
		if(impl->lutTexture) impl->gl->glDeleteTextures(1, &impl->lutTexture);
	}
	impl.reset(new Impl);
}

void PhotoPostProcess::validateLut(const QString& path) { parseCube(path); }
void PhotoPostProcess::invalidateLutCache() { impl->cached = false; }
const char* PhotoPostProcess::focusPeakingDescription() { return "Approximate focus peaking (image edges; not depth focus)"; }

QRect PhotoPostProcess::cropRect(const QSize& size, double aspect_ratio)
{
	if(size.isEmpty() || !std::isfinite(aspect_ratio) || aspect_ratio<0) fail("invalid crop dimensions or aspect ratio");
	int w=size.width(), h=size.height();
	if(aspect_ratio>0)
	{
		if(double(w)/h>aspect_ratio) w=std::max(1,qRound(h*aspect_ratio));
		else h=std::max(1,qRound(w/aspect_ratio));
	}
	return QRect((size.width()-w)/2,(size.height()-h)/2,w,h);
}

QPointF PhotoPostProcess::sourceUV(const QPointF& displayUV, const QSize& size, const QVariantMap& settings)
{
	if(!std::isfinite(displayUV.x()) || !std::isfinite(displayUV.y())) fail("invalid display UV");
	const QRect crop=cropRect(size,value(settings,"aspect_ratio",0,0,10000));
	const float strength=value(settings,"filter_strength",1,0,1);
	const float distortion=value(settings,"distortion",0,-0.5f,0.5f) *
		(settings.value("compare_original",false).toBool()?0.0f:strength);
	const double px=(displayUV.x()*size.width()-crop.x())/crop.width()*2.0-1.0;
	const double py=(displayUV.y()*size.height()-crop.y())/crop.height()*2.0-1.0;
	const double k=(px*px+py*py)*distortion*0.25;
	const double u=displayUV.x()+px*k*crop.width()/size.width();
	const double v=displayUV.y()+py*k*crop.height()/size.height();
	return QPointF(std::max(0.5/size.width(),std::min(1.0-0.5/size.width(),u)),
		std::max(0.5/size.height(),std::min(1.0-0.5/size.height(),v)));
}

void PhotoPostProcess::render(unsigned int source_texture, const QSize& size, unsigned int target_fbo,
	const QVariantMap& settings, bool guides, double time_seconds)
{
	QOpenGLContext* context = QOpenGLContext::currentContext();
	if(!context || context->isOpenGLES()) fail("a current desktop OpenGL 3.2+ core context is required");
	if(size.width() <= 0 || size.height() <= 0 || !source_texture || !std::isfinite(time_seconds)) fail("invalid source, size or time");
	if(impl->context && impl->context != context) fail("use one adapter per context, or release before switching");
	if(!impl->context)
	{
		impl.reset(new Impl); // Drop stale names after context destruction.
		impl->gl.reset(new GL);
		if(!impl->gl->initializeOpenGLFunctions()) fail("OpenGL 3.2 core functions are unavailable");
		impl->context = context;
	}
	GL& gl = *impl->gl;
	State state(gl, context);
	if(!gl.glIsTexture(source_texture)) fail("source is not a texture");
	if(target_fbo && !gl.glIsFramebuffer(target_fbo)) fail("target is not a framebuffer");
	GLint maxViewport[2]; gl.glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maxViewport);
	if(size.width() > maxViewport[0] || size.height() > maxViewport[1]) fail("size exceeds GL viewport limits");
	state.bindSource(source_texture);
	GLint w=0, h=0, format=0;
	gl.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
	gl.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
	gl.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
	if(w != size.width() || h != size.height() || format != GL_RGBA8) fail("source must be a matching GL_RGBA8 2D texture containing encoded sRGB");
	state.bindTarget(target_fbo);
	const auto v = [&](const char* name, float fallback, float lo, float hi) { return value(settings,name,fallback,lo,hi); };
	const float requestedStrength = v("filter_strength",1,0,1);
	const float pixelScale = v("photo_pixel_scale",1,0.001f,1024);
	const float strength = settings.value("compare_original",false).toBool() ? 0.0f : requestedStrength;
	const QRect crop = cropRect(size,v("aspect_ratio",0,0,10000));
	const float kelvin = v("temperature_kelvin",6500,2000,12000);
	const float wbTint = v("wb_tint",0,-1,1);
	const QMatrix3x3 wb = whiteBalance(kelvin,wbTint);
	const QVector4D tonal(v("contrast",0,-1,1),v("shadows",0,-1,1),v("highlights",0,-1,1),v("blacks",0,-1,1));
	const QVector4D balance(v("whites",0,-1,1),0,0,v("warmth",0,-1,1));
	const QVector4D artistic(v("tint",0,-1,1),v("shadow_hue",0,0,360),v("highlight_hue",0,0,360),v("split_strength",0,0,1));
	const QVector4D optics(v("diffusion",0,0,1)*strength,v("halation",0,0,1)*strength,v("aberration",0,0,1)*strength,v("distortion",0,-0.5f,0.5f)*strength);
	const QVector4D vignette(v("vignette",0,0,1)*strength,v("vignette_radius",0.75f,0.2f,1.5f),v("vignette_softness",0.5f,0.05f,1),v("vignette_roundness",1,0,1));
	const QVector2D center(v("vignette_center_x",0,-0.5f,0.5f),v("vignette_center_y",0,-0.5f,0.5f));
	const QVector4D grain(v("grain",0,0,1),v("grain_size",1,0.5f,4)*pixelScale,settings.value("grain_colour",false).toBool()?1.0f:0.0f,float(std::fmod(time_seconds,3600.0)));
	const QString path=strength>0 ? settings.value("lut_path").toString().trimmed() : QString();
	const float lutStrength=v("lut_strength",1,0,1);
	if(!path.isEmpty()) impl->loadLut(path);
	const bool colourActive = strength>0 && !settings.value("compare_original",false).toBool() &&
		(!tonal.isNull() || !balance.isNull() || kelvin!=6500 || wbTint!=0 || artistic.x()!=0 || artistic.w()!=0 || grain.x()!=0 || (!path.isEmpty() && lutStrength>0));
	if(!impl->program)
	{
		std::unique_ptr<QOpenGLShaderProgram> program(new QOpenGLShaderProgram);
		if(!program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader) ||
			!program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader)) fail("shader compilation: " + program->log());
		gl.glBindFragDataLocation(program->programId(), 0, "colourOut");
		if(!program->link()) fail("shader link: " + program->log());
		impl->program = std::move(program);
	}
	if(!impl->vao) gl.glGenVertexArrays(1, &impl->vao);
	if(!impl->vao) fail("cannot allocate fullscreen VAO");
	if(!impl->program->bind()) fail("cannot bind postprocess shader");
	QOpenGLShaderProgram& program=*impl->program;
	program.setUniformValue("sourceTex",0); program.setUniformValue("lutTex",1);
	program.setUniformValue("imageSize",QVector2D(float(size.width()),float(size.height())));
	program.setUniformValue("crop",QVector4D(float(crop.x()),float(size.height()-crop.y()-crop.height()),float(crop.width()),float(crop.height())));
	program.setUniformValue("guides",guides);
	program.setUniformValue("showGrid",settings.value("show_grid",false).toBool());
	program.setUniformValue("tonal",tonal); program.setUniformValue("balance",balance);
	program.setUniformValue("whiteBalance",wb);
	program.setUniformValue("artistic",artistic); program.setUniformValue("optics",optics);
	program.setUniformValue("vignette",vignette); program.setUniformValue("vignetteCenter",center);
	program.setUniformValue("grain",grain); program.setUniformValue("strength",strength);
	program.setUniformValue("pixelScale",pixelScale);
	program.setUniformValue("lutStrength",lutStrength); program.setUniformValue("lutSize",path.isEmpty()?0:impl->lutSize);
	program.setUniformValue("colourActive",colourActive);
	program.setUniformValue("zebra",guides && settings.value("clipping_zebra",settings.value("zebra",false)).toBool());
	program.setUniformValue("peaking",guides && settings.value("focus_peaking",false).toBool());
	gl.glActiveTexture(GL_TEXTURE1); gl.glBindTexture(GL_TEXTURE_3D,impl->lutTexture);
	gl.glBindVertexArray(impl->vao);
	if(state.viewportIndexed) state.viewportIndexed(0,0,0,float(size.width()),float(size.height()));
	else gl.glViewport(0,0,size.width(),size.height());
	gl.glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
	gl.glColorMaski(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
	gl.glDisablei(GL_BLEND,0);
	for(GLenum cap : state.caps)
	{
		if(cap==GL_SCISSOR_TEST && state.viewportIndexed) gl.glDisablei(cap,0);
		else gl.glDisable(cap);
	}
	for(size_t i=0; i<state.clipEnabled.size(); ++i) gl.glDisable(GL_CLIP_DISTANCE0+GLenum(i));
	if(state.clipControl) state.clipControl(GL_LOWER_LEFT,0x935E /*GL_NEGATIVE_ONE_TO_ONE*/);
	gl.glDrawArrays(GL_TRIANGLES,0,3);
}
#endif
