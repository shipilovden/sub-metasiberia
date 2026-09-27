// Standalone Windows CPU + hidden-context shader checks. Does not start gui_client.
// clang++ -std=c++17 scripts/test_water_surf.cpp -o <temp>/test_water_surf.exe -lopengl32 -lgdi32 -luser32
// test_water_surf.exe <glare-core>/opengl/shaders
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include "../gui_client/WaterWaveUtils.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

static void require(bool ok, const char* message) { if(!ok) throw std::runtime_error(message); }
static std::string read(const std::string& path)
{
	std::ifstream in(path);
	require(bool(in), path.c_str());
	return std::string(std::istreambuf_iterator<char>(in), {});
}

int main(int argc, char** argv)
{
	try
	{
		require(argc == 2, "Pass the glare-core shader source directory");
		for(int i = 0; i < 300; ++i)
		{
			const double t = i * 0.013;
			const auto sample = [&](double time, float a, float speed) {
				return WaterWaveUtils::sample(-213.f, -408.f, time, a, 22.f, speed, 1.1f, 0.4f, 0.22f);
			};
			require(sample(t, 0, 1).height == 0 && sample(t, 0, 1).vertical_speed == 0, "Zero amplitude");
			require(sample(t, 1.2f, 0).height == sample(t + 1, 1.2f, 0).height, "Paused waves moved");
			require(sample(t, 1.2f, 0).vertical_speed == 0, "Paused waves generated impact velocity");
			const auto p = sample(t, 1.2f, 1);
			const float derivative = (sample(t + 0.002, 1.2f, 1).height - sample(t - 0.002, 1.2f, 1).height) / 0.004f;
			require(std::abs(derivative - p.vertical_speed) < 0.004f, "Impact velocity does not match wave derivative");
			require(std::isfinite(p.height) && std::abs(p.height) <= 1.2f * 0.35f * (1.f + 0.45f * 0.22f), "Wave bounds");
		}
		std::cout << "CPU wave checks passed\n";
		bool incoming = false, returning = false;
		for(int i=1; i<500; ++i)
		{
			const auto coast = [](float x, double t, float speed) {
				return WaterWaveUtils::sampleCoastal(x,0,t,1.2f,22, speed,0,0.4f,0.22f,x*0.1f,0.1f,0);
			};
			const double t = i*0.013;
			const auto s = coast(0,t,1);
			incoming |= s.vertical_speed > 0.1f;
			returning |= s.vertical_speed < -0.1f;
			require(std::abs(s.height-coast(1,t,1).height)<0.0001f, "Shore phase split across beach slope");
			require(std::abs(s.vertical_speed-(coast(0,t+0.001,1).height-coast(0,t-0.001,1).height)/0.002f)<0.004f, "Swash impact velocity");
			require(coast(0,t,0).vertical_speed==0 && coast(0,t,0).height==coast(0,t+1,0).height, "Paused swash");
		}
		require(incoming && returning, "Missing run-up or backwash");
		std::cout << "CPU run-up, backwash, shore phase and impact checks passed\n";

		// No WS_VISIBLE / ShowWindow: a shader compiler context, not an application launch.
		WNDCLASSA wc = {};
		wc.style = CS_OWNDC;
		wc.lpfnWndProc = DefWindowProcA;
		wc.hInstance = GetModuleHandle(nullptr);
		wc.lpszClassName = "WaterShaderCheck";
		require(RegisterClassA(&wc) != 0, "RegisterClass");
		HWND window = CreateWindowA(wc.lpszClassName, "", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
		require(window != nullptr, "Hidden window");
		HDC dc = GetDC(window);
		PIXELFORMATDESCRIPTOR pf = {};
		pf.nSize = sizeof(pf); pf.nVersion = 1;
		pf.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
		pf.iPixelType = PFD_TYPE_RGBA; pf.cColorBits = 24;
		require(SetPixelFormat(dc, ChoosePixelFormat(dc, &pf), &pf) != 0, "Pixel format");
		HGLRC context = wglCreateContext(dc);
		require(context && wglMakeCurrent(dc, context), "OpenGL context");
		auto createShader = reinterpret_cast<GLuint(APIENTRY*)(GLenum)>(wglGetProcAddress("glCreateShader"));
		auto shaderSource = reinterpret_cast<void(APIENTRY*)(GLuint, GLsizei, const char* const*, const GLint*)>(wglGetProcAddress("glShaderSource"));
		auto compileShader = reinterpret_cast<void(APIENTRY*)(GLuint)>(wglGetProcAddress("glCompileShader"));
		auto getShaderiv = reinterpret_cast<void(APIENTRY*)(GLuint, GLenum, GLint*)>(wglGetProcAddress("glGetShaderiv"));
		auto getLog = reinterpret_cast<void(APIENTRY*)(GLuint, GLsizei, GLsizei*, char*)>(wglGetProcAddress("glGetShaderInfoLog"));
		auto deleteShader = reinterpret_cast<void(APIENTRY*)(GLuint)>(wglGetProcAddress("glDeleteShader"));
		require(createShader && shaderSource && compileShader && getShaderiv && getLog && deleteShader, "Shader compiler unavailable");
		const std::string root = argv[1];
		const auto compile = [&](const char* name, bool vertex, const std::string& defines) {
			const std::string prefix = "#version 460\n#define NUM_DEPTH_TEXTURES 0\n#define NUM_DYNAMIC_DEPTH_TEXTURES 0\n#define NUM_STATIC_DEPTH_TEXTURES 0\n#define DEPTH_TEXTURE_SCALE_MULT 1.0\n#define MAIN_BUFFER_MSAA_SAMPLES 1\n#define DO_POST_PROCESSING 1\n#define DEPTH_FOG 1\n" + defines;
			std::string source = prefix + read(root + "/water_surface.glsl") + read(root + (vertex ? "/common_vert_structures.glsl" : "/common_frag_structures.glsl")) +
				read(root + (vertex ? "/vert_utils.glsl" : "/frag_utils.glsl")) + read(root + "/" + name);
			const char* ptr = source.c_str();
			GLuint shader = createShader(vertex ? 0x8B31 : 0x8B30);
			shaderSource(shader, 1, &ptr, nullptr); compileShader(shader);
			GLint ok = 0; getShaderiv(shader, 0x8B81, &ok);
			char log[16000] = {}; getLog(shader, sizeof(log), nullptr, log);
			deleteShader(shader);
			if(!ok) { std::cerr << name << "\n" << log; throw std::runtime_error("Shader compilation failed"); }
			std::cout << name << " passed\n";
		};
		for(int screenspace = 0; screenspace < 2; ++screenspace)
		{
			const std::string defines = "#define WATER_DO_SCREENSPACE_REFL_AND_REFR " + std::to_string(screenspace) + "\n#define USE_REVERSE_Z 1\n";
			compile("water_vert_shader.glsl", true, defines);
			compile("water_frag_shader.glsl", false, defines);
			compile("phong_frag_shader.glsl", false, defines + "#define TERRAIN 1\n");
		}
		const std::string gpu_defines = "#extension GL_ARB_bindless_texture : require\n#define USE_BINDLESS_TEXTURES 1\n#define OB_AND_MAT_DATA_GPU_RESIDENT 1\n#define USE_MULTIDRAW_ELEMENTS_INDIRECT 1\n#define USE_SSBOS 1\n#define NORMAL_TEXTURE_IS_UINT 1\n#define WATER_DO_SCREENSPACE_REFL_AND_REFR 1\n#define USE_REVERSE_Z 1\n";
		compile("water_vert_shader.glsl", true, gpu_defines);
		compile("water_frag_shader.glsl", false, gpu_defines);
		compile("phong_frag_shader.glsl", false, gpu_defines + "#define TERRAIN 1\n");

		// Execute the actual shared GLSL math, without rendering a client/world.
		// A one-pixel FBO reports regression failures in its red channel.
		auto createProgram = reinterpret_cast<GLuint(APIENTRY*)()>(wglGetProcAddress("glCreateProgram"));
		auto attachShader = reinterpret_cast<void(APIENTRY*)(GLuint, GLuint)>(wglGetProcAddress("glAttachShader"));
		auto linkProgram = reinterpret_cast<void(APIENTRY*)(GLuint)>(wglGetProcAddress("glLinkProgram"));
		auto getProgramiv = reinterpret_cast<void(APIENTRY*)(GLuint, GLenum, GLint*)>(wglGetProcAddress("glGetProgramiv"));
		auto useProgram = reinterpret_cast<void(APIENTRY*)(GLuint)>(wglGetProcAddress("glUseProgram"));
		auto activeTexture = reinterpret_cast<void(APIENTRY*)(GLenum)>(wglGetProcAddress("glActiveTexture"));
		auto uniform1i = reinterpret_cast<void(APIENTRY*)(GLint, GLint)>(wglGetProcAddress("glUniform1i"));
		auto getUniformLocation = reinterpret_cast<GLint(APIENTRY*)(GLuint,const char*)>(wglGetProcAddress("glGetUniformLocation"));
		auto deleteProgram = reinterpret_cast<void(APIENTRY*)(GLuint)>(wglGetProcAddress("glDeleteProgram"));
		auto genFramebuffers = reinterpret_cast<void(APIENTRY*)(GLsizei, GLuint*)>(wglGetProcAddress("glGenFramebuffers"));
		auto bindFramebuffer = reinterpret_cast<void(APIENTRY*)(GLenum, GLuint)>(wglGetProcAddress("glBindFramebuffer"));
		auto framebufferTexture2D = reinterpret_cast<void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint)>(wglGetProcAddress("glFramebufferTexture2D"));
		auto checkFramebuffer = reinterpret_cast<GLenum(APIENTRY*)(GLenum)>(wglGetProcAddress("glCheckFramebufferStatus"));
		auto deleteFramebuffers = reinterpret_cast<void(APIENTRY*)(GLsizei, const GLuint*)>(wglGetProcAddress("glDeleteFramebuffers"));
		require(createProgram && attachShader && linkProgram && getProgramiv && useProgram && deleteProgram &&
			genFramebuffers && bindFramebuffer && framebufferTexture2D && checkFramebuffer && deleteFramebuffers, "FBO test support");
		GLuint program = createProgram();
		const std::string vertex = "#version 460\nvoid main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2); gl_Position=vec4(p*2.0-1.0,0,1); }";
		const std::string fragment = "#version 460\n" + read(root + "/water_surface.glsl") + read(root + "/common_frag_structures.glsl") + R"GLSL(
layout(location=0) out vec4 result;
void main() {
    float error = 0.0;
    vec4 waves = vec4(1.2, 22.0, 0.45, 1.0);
    vec4 dirs = vec4(cos(1.1), sin(1.1), 0.49, 0.22);
    for(int i=0; i<64; ++i) {
        float t = float(i) * 0.31;
        vec2 p = vec2(-213.0 + float(i) * 1.9, -408.0);
        vec3 d = waterWaveDisplacement(p, waves, dirs, vec2(-213,-408), t);
        float recovered = waterSurfaceHeight(p + d.xy, waves, dirs, vec2(-213,-408), t);
        error = max(error, abs(recovered - d.z));
        float width = 0.1 + float(i) * 0.2;
        error = max(error, abs(waterContactFoam(0.0, width, 0.025) - 1.0));
        error = max(error, waterContactFoam(width + 1.0, width, 0.025));
        error = max(error, waterContactFoam(-width - 1.0, width, 0.025));
        error = max(error, abs(waterSurfaceHeight(p, vec4(waves.xyz,0), dirs, p, t) -
            waterSurfaceHeight(p, vec4(waves.xyz,0), dirs, p, t+1.0)));
    }
    // Synthetic sloping beach: the waterline must advance and retreat as one sheet.
    float omega = sqrt(9.8 * 6.28318530718 / waves.y);
    for(int i=0; i<64; ++i) {
        float t = float(i)*0.1;
        float h = waterSurfaceHeight(vec2(0),waves,vec4(1,0,0.4,0.22),vec2(0),t);
        error = max(error,abs(h - waves.x*0.35*waterSwash(-t*omega)));
        error = max(error,abs(h-waterSurfaceHeight(vec2(1,0),waves,vec4(1,0,0.4,0.22),vec2(0),t)));
    }
    error = max(error,waterSwash(-0.1*6.28318530718)-waterSwash(-0.11*6.28318530718));
    error = max(error,waterSwash(-0.61*6.28318530718)-waterSwash(-0.6*6.28318530718));
    error = max(error,waterSeabedFoamVisibility(-0.2,-0.8)); // submerged shin/rock, not beach
    error = max(error,abs(1.0-waterSeabedFoamVisibility(-0.8,-0.8)));
    float least_foam = 1.0, most_foam = 0.0;
    for(int i=0;i<128;++i) {
        float noise_value = float(i)/127.0;
        error = max(error,waterShoreCoverage(0.0,0.1,2.4,noise_value));
        error = max(error,waterShoreCoverage(-0.1,0.1,2.4,noise_value));
        error = max(error,abs(1.0-waterShoreCoverage(0.2,0.1,2.4,noise_value)));
        float edge = waterShoreCoverage(0.015,0.1,2.4,noise_value);
        if(edge<=0.0 || edge>=1.0) error=1.0; // soft transition, not a binary cut-out
        vec2 p = vec2(float(i)*0.137,float(i%13)*0.219);
        float fresh = surfFoamLace(p,0.0,0.015);
        float old = surfFoamLace(p,0.9,0.015);
        error = max(error,old-fresh); // aged foam erodes, never grows on dry land
        if(isnan(fresh) || isnan(old) || fresh<0.0 || fresh>1.0) error=1.0;
        least_foam=min(least_foam,fresh); most_foam=max(most_foam,fresh);
    }
    if(least_foam>0.1 || most_foam<0.6) error=1.0; // both holes and dense clusters
    result = error < 0.002 ? vec4(0,1,0,1) : vec4(1,0,0,1);
})GLSL";
		for(int stage = 0; stage < 2; ++stage)
		{
			GLuint shader = createShader(stage == 0 ? 0x8B31 : 0x8B30);
			const char* source = stage == 0 ? vertex.c_str() : fragment.c_str();
			shaderSource(shader, 1, &source, nullptr); compileShader(shader);
			GLint ok = 0; getShaderiv(shader, 0x8B81, &ok);
			require(ok != 0, "GPU regression shader compile");
			attachShader(program, shader); deleteShader(shader);
		}
		linkProgram(program);
		GLint linked = 0; getProgramiv(program, 0x8B82, &linked);
		require(linked != 0, "GPU regression program link");
		require(activeTexture && uniform1i && getUniformLocation, "Texture test support");
		GLuint bathymetry;
		glGenTextures(1,&bathymetry); activeTexture(0x84C1); glBindTexture(GL_TEXTURE_2D,bathymetry);
		float coast_pixels[3*4*4] = {};
		coast_pixels[0]=-1; coast_pixels[1]=-1; coast_pixels[2]=0.5f;
		for(int y=1;y<4;++y) for(int x=0;x<3;++x) {
			const int offset=(y*3+x)*4;
			coast_pixels[offset]=(x-1)*0.1f; coast_pixels[offset+1]=0.1f; coast_pixels[offset+3]=1;
		}
		glTexImage2D(GL_TEXTURE_2D,0,0x8814,3,4,0,GL_RGBA,GL_FLOAT,coast_pixels);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
		useProgram(program); uniform1i(getUniformLocation(program,"water_coast_tex"),1);
		activeTexture(0x84C0);
		GLuint fbo, tex;
		genFramebuffers(1, &fbo); bindFramebuffer(0x8D40, fbo);
		glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		framebufferTexture2D(0x8D40, 0x8CE0, GL_TEXTURE_2D, tex, 0);
		require(checkFramebuffer(0x8D40) == 0x8CD5, "Incomplete test FBO");
		glViewport(0,0,1,1); useProgram(program); glDrawArrays(GL_TRIANGLES,0,3);
		unsigned char pixel[4] = {};
		glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
		require(pixel[0] == 0 && pixel[1] == 255, "Water/foam synchronisation GPU regression");
		std::cout << "GPU contact-edge, displaced-height, swash/backwash, submerged-object mask, porous foam erosion, soft shoreline and pause checks passed\n";
		useProgram(0); deleteProgram(program); bindFramebuffer(0x8D40,0);
		deleteFramebuffers(1,&fbo); glDeleteTextures(1,&tex); glDeleteTextures(1,&bathymetry);
		wglMakeCurrent(nullptr, nullptr); wglDeleteContext(context); ReleaseDC(window, dc); DestroyWindow(window);
		return 0;
	}
	catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
