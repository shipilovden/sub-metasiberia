/*=====================================================================
WebViewData.cpp
---------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "WebViewData.h"


#include "GUIClient.h"
#include "WorldState.h"
#include "EmbeddedBrowser.h"
#include "CEF.h"
#include "URLWhitelist.h"
#include "../shared/WorldObject.h"
#include "../shared/ResourceManager.h"
#include "../audio/AudioEngine.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/IncludeOpenGL.h>
#include <maths/vec2.h>
#include <webserver/Escaping.h>
#include <networking/URL.h>
#include <graphics/Drawing.h>
#include <ui/UIEvents.h>
#include <utils/FileUtils.h>
#include <utils/FileInStream.h>
#include <utils/PlatformUtils.h>
#include <utils/Base64.h>
#include <utils/StringUtils.h>
#include "superluminal/PerformanceAPI.h"
#include <cmath>
#include <vector>
#if EMSCRIPTEN
#include <emscripten.h>
#endif


// Defined in BrowserVidPlayer.cpp
extern "C" void destroyHTMLViewJS(int handle);
extern "C" void setHTMLElementCSSTransform(int handle, const char* matrix_string);
extern "C" int makeHTMLViewJS();


WebViewData::WebViewData()
:	browser(NULL),
	loaded_audio_player_state_key(),
	showing_click_to_load_text(false),
	user_clicked_to_load(false),
	previous_is_visible(true),
	html_view_handle(-1),
	m_gui_client(NULL)
{}


WebViewData::~WebViewData()
{
	browser = NULL;

#if EMSCRIPTEN
	// If there is a browser, destroy it.
	if(html_view_handle >= 0)
	{
		destroyHTMLViewJS(html_view_handle);
		html_view_handle = -1;
	}
#endif
}


static const int text_tex_W = 512;
static const int button_W = 200;
static const int button_left_x = text_tex_W/2 - button_W/2;
static const int button_top_y = (int)((1080.0 / 1920) * text_tex_W) - 120;
static const int button_H = 60;


[[maybe_unused]] static OpenGLTextureRef makeTextTexture(OpenGLEngine* opengl_engine, GLUI* glui, const std::string& URL)
{
	TextRendererFontFace* font = glui->getFont(/*font size px=*/16, /*emoji=*/false);

	const int W = text_tex_W;
	const int H = (int)((1080.0 / 1920) * W);

	ImageMapUInt8Ref map = new ImageMapUInt8(W, H, 3);
	map->set(255);

	{
		const std::string text = "Click below to load";
		const TextRenderer::SizeInfo size_info = font->getTextSize(text);
		font->drawText(*map, text, W/2 - size_info.glyphSize().x/2, H/2 - 23, Colour3f(0,0,0), /*render SDF=*/false);
	}
	{
		const TextRenderer::SizeInfo size_info = font->getTextSize(URL);
		font->drawText(*map, URL, W/2 - size_info.glyphSize().x/2, H/2, Colour3f(0,0,0), /*render SDF=*/false);
	}

	const uint8 col[4] = { 30, 30, 30, 255 };
	Drawing::drawRect(*map, /*x0=*/W/2 - button_W/2, /*y0=*/button_top_y, /*wdith=*/button_W, /*height=*/button_H, col);


	const TextRenderer::SizeInfo load_size_info = font->getTextSize("Load");
	font->drawText(*map, "Load", W/2 - load_size_info.glyphSize().x/2, button_top_y + button_H - 22, Colour3f(30.f / 255), /*render SDF=*/false);


	TextureParams params;
	params.use_mipmaps = false;
	params.filtering = OpenGLTexture::Filtering_Bilinear; // Disable trilinear filtering on the 'click to load' texture on webviews, to avoid stutters while drivers compute Mipmaps.
	params.allow_compression = false;
	OpenGLTextureRef tex = opengl_engine->getOrLoadOpenGLTextureForMap2D(OpenGLTextureKey("click_to_load_" + URL), *map, params);
	return tex;
}


static bool uvsAreOnLoadButton(float uv_x, float uv_y)
{
	const int W = text_tex_W;
	const int H = (int)((1080.0 / 1920) * W);
	const int x = (int)(uv_x * W);
	const int y = (int)((1.f - uv_y) * H);
	
	return
		x >= button_left_x && x <= (button_left_x + button_W) &&
		y >= button_top_y && y <= (button_top_y + button_H);
}


#if EMSCRIPTEN


EM_JS(int, makeWebViewIframe, (const char* URL), {
	console.log("=================makeWebViewIframe()================");
	console.log("URL: " + UTF8ToString(URL));

	let URL_str = UTF8ToString(URL);

	// Get handle
	let handle = next_html_view_elem_handle;
	next_html_view_elem_handle++;

	let new_div = document.createElement('div');
	new_div.className = 'transformable-html-view';
	new_div.width = '1024';
	new_div.height = '576';
	new_div.id = 'transformable-html-view-' + handle.toString(); // Make unique ID


	// Make the iframe ourself so we can set credentialless attribute.
	let youtube_iframe = document.createElement('iframe');
	youtube_iframe.src = URL_str;
	youtube_iframe.width = '1024';
	youtube_iframe.height = '576';
	youtube_iframe.id = 'transformable-html-view-webview-' + handle.toString(); // Make unique ID
	youtube_iframe.setAttribute('credentialless', "");
	
	new_div.appendChild(youtube_iframe);

	document.getElementById('iframe-container-camera').appendChild(new_div);

	// insert into global handle->div map
	html_view_elem_handle_to_div_map[handle] = new_div;
	
	// console.log("makeWebViewIframe() done, returning handle " + handle.toString());
	return handle;
});


#endif // end if EMSCRIPTEN


namespace
{
struct AudioPlayerTrack
{
	std::string title;
	std::string resolved_url;
};


const WorldMaterial* getAudioPlayerBodyMaterial(const WorldObject& ob)
{
	if(ob.materials.size() > 1 && ob.materials[1].nonNull())
		return ob.materials[1].ptr();
	else if(!ob.materials.empty() && ob.materials[0].nonNull())
		return ob.materials[0].ptr();
	else
		return NULL;
}


Colour3f getAudioPlayerBodyColour(const WorldObject& ob)
{
	const WorldMaterial* body_mat = getAudioPlayerBodyMaterial(ob);
	if(body_mat)
		return body_mat->colour_rgb;
	else
		return Colour3f(0.85f);
}


Colour3f getAudioPlayerBodyEmissionColour(const WorldObject& ob)
{
	const WorldMaterial* body_mat = getAudioPlayerBodyMaterial(ob);
	if(body_mat)
		return body_mat->emission_rgb;
	else
		return Colour3f(0.f);
}


float getAudioPlayerBodyLuminance(const WorldObject& ob)
{
	const WorldMaterial* body_mat = getAudioPlayerBodyMaterial(ob);
	if(body_mat)
		return body_mat->emission_lum_flux_or_lum;
	else
		return 0.f;
}


URLString getAudioPlayerBodyTextureURL(const WorldObject& ob)
{
	const WorldMaterial* body_mat = getAudioPlayerBodyMaterial(ob);
	if(body_mat)
		return body_mat->colour_texture_url;
	else
		return URLString();
}


URLString getAudioPlayerBodyEmissionTextureURL(const WorldObject& ob)
{
	const WorldMaterial* body_mat = getAudioPlayerBodyMaterial(ob);
	if(body_mat)
		return body_mat->emission_texture_url;
	else
		return URLString();
}


Vec2f getAudioPlayerBodyTextureScale(const WorldObject& ob)
{
	const WorldMaterial* body_mat = getAudioPlayerBodyMaterial(ob);
	if(body_mat)
		return Vec2f(body_mat->tex_matrix.elem(0, 0), body_mat->tex_matrix.elem(1, 1));
	else
		return Vec2f(1.f);
}


int toCSSChannel(float v)
{
	if(v < 0.f) v = 0.f;
	if(v > 1.f) v = 1.f;
	return (int)(v * 255.f + 0.5f);
}


void getAudioPlayerTrackURLs(const WorldObject& ob, std::vector<std::string>& URLs_out)
{
	if(!ob.isAudioPlayerWebView())
		return;

	size_t line_start = 0;
	while(line_start <= ob.content.size())
	{
		const size_t line_end = ob.content.find('\n', line_start);
		const size_t line_size = (line_end == std::string::npos) ? (ob.content.size() - line_start) : (line_end - line_start);

		const std::string line = stripHeadWhitespace(stripTailWhitespace(ob.content.substr(line_start, line_size)));
		if(!line.empty())
			URLs_out.push_back(line);

		if(line_end == std::string::npos)
			break;

		line_start = line_end + 1;
	}
}


std::string getAudioPlayerTrackTitle(const std::string& playlist_url)
{
	std::string filename = FileUtils::getFilename(playlist_url);
	if(filename.empty())
		filename = playlist_url;

	std::string title = removeDotAndExtension(filename);
	if(title.empty())
		title = filename;

	for(size_t i=0; i<title.size(); ++i)
		if(title[i] == '_')
			title[i] = ' ';

	return title;
}


std::string resolveAudioPlayerTrackURL(const std::string& playlist_url, ResourceManager& resource_manager, const std::string& server_hostname)
{
	if(hasPrefix(playlist_url, "http://") || hasPrefix(playlist_url, "https://"))
		return playlist_url;

	ResourceRef resource = resource_manager.getExistingResourceForURL(toURLString(playlist_url));
	if(resource.nonNull() && resource->getState() == Resource::State_Present)
		return "https://resource/" + web::Escaping::URLEscape(playlist_url);
	else
		return "http://" + server_hostname + "/resource/" + web::Escaping::URLEscape(playlist_url);
}


std::string resolveAudioPlayerAssetURL(const URLString& asset_url, ResourceManager& resource_manager, const std::string& server_hostname)
{
	if(asset_url.empty())
		return "";

	const std::string asset_url_std = toStdString(asset_url);
	if(hasPrefix(asset_url_std, "http://") || hasPrefix(asset_url_std, "https://"))
		return asset_url_std;

	ResourceRef resource = resource_manager.getExistingResourceForURL(asset_url);
	if(resource.nonNull() && resource->getState() == Resource::State_Present)
		return "https://resource/" + web::Escaping::URLEscape(asset_url_std);
	else
		return "http://" + server_hostname + "/resource/" + web::Escaping::URLEscape(asset_url_std);
}


std::string makeAudioPlayerStateKey(const WorldObject& ob)
{
	const Colour3f body_col = getAudioPlayerBodyColour(ob);
	const Colour3f emission_col = getAudioPlayerBodyEmissionColour(ob);
	const float luminance = getAudioPlayerBodyLuminance(ob);
	const URLString body_tex_url = getAudioPlayerBodyTextureURL(ob);
	const URLString emission_tex_url = getAudioPlayerBodyEmissionTextureURL(ob);
	const Vec2f body_tex_scale = getAudioPlayerBodyTextureScale(ob);
	return ob.target_url + "\n" + toString(ob.flags) + "\n" + toString(ob.audio_volume) + "\n" + ob.content + "\n" +
		toString(body_col.r) + "," + toString(body_col.g) + "," + toString(body_col.b) + "\n" +
		toStdString(body_tex_url) + "\n" + toString(body_tex_scale.x) + "," + toString(body_tex_scale.y) + "\n" +
		toString(emission_col.r) + "," + toString(emission_col.g) + "," + toString(emission_col.b) + "\n" +
		toString(luminance) + "\n" + toStdString(emission_tex_url);
}


std::string makeAudioPlayerRootPage(const WorldObject& ob, ResourceManager& resource_manager, const std::string& server_hostname)
{
	std::vector<std::string> playlist_urls;
	getAudioPlayerTrackURLs(ob, playlist_urls);

	std::vector<AudioPlayerTrack> tracks;
	tracks.reserve(playlist_urls.size());
	for(size_t i=0; i<playlist_urls.size(); ++i)
	{
		AudioPlayerTrack track;
		track.title = getAudioPlayerTrackTitle(playlist_urls[i]);
		track.resolved_url = resolveAudioPlayerTrackURL(playlist_urls[i], resource_manager, server_hostname);
		tracks.push_back(track);
	}

	std::string playlist_js = "[";
	for(size_t i=0; i<tracks.size(); ++i)
	{
		std::string title_base64;
		Base64::encode(tracks[i].title.data(), tracks[i].title.size(), title_base64);

		std::string url_base64;
		Base64::encode(tracks[i].resolved_url.data(), tracks[i].resolved_url.size(), url_base64);

		if(i > 0)
			playlist_js += ",";
		playlist_js += "{title:atob('" + title_base64 + "'),url:atob('" + url_base64 + "')}";
	}
	playlist_js += "]";

	const bool autoplay = BitUtils::isBitSet(ob.flags, WorldObject::AUDIO_AUTOPLAY);
	const bool loop = BitUtils::isBitSet(ob.flags, WorldObject::AUDIO_LOOP);
	const bool shuffle = BitUtils::isBitSet(ob.flags, WorldObject::AUDIO_SHUFFLE);
	float initial_volume = ob.audio_volume;
	if(initial_volume < 0.f)
		initial_volume = 0.f;
	else if(initial_volume > 1.f)
		initial_volume = 1.f;

	const Colour3f body_col = getAudioPlayerBodyColour(ob);
	const std::string body_col_css = "rgb(" +
		toString(toCSSChannel(body_col.r)) + "," +
		toString(toCSSChannel(body_col.g)) + "," +
		toString(toCSSChannel(body_col.b)) + ")";

	const Colour3f emission_col = getAudioPlayerBodyEmissionColour(ob);
	const int emission_col_r = toCSSChannel(emission_col.r);
	const int emission_col_g = toCSSChannel(emission_col.g);
	const int emission_col_b = toCSSChannel(emission_col.b);
	const float body_luminance = getAudioPlayerBodyLuminance(ob);
	const float luminance = (body_luminance > 0.f) ? body_luminance : 0.f;

	const std::string resolved_body_texture_url = resolveAudioPlayerAssetURL(getAudioPlayerBodyTextureURL(ob), resource_manager, server_hostname);
	std::string body_texture_url_base64;
	Base64::encode(resolved_body_texture_url.data(), resolved_body_texture_url.size(), body_texture_url_base64);

	const std::string resolved_emission_texture_url = resolveAudioPlayerAssetURL(getAudioPlayerBodyEmissionTextureURL(ob), resource_manager, server_hostname);
	std::string emission_texture_url_base64;
	Base64::encode(resolved_emission_texture_url.data(), resolved_emission_texture_url.size(), emission_texture_url_base64);

	const Vec2f body_tex_scale = getAudioPlayerBodyTextureScale(ob);
	const float body_tex_scale_x = (std::fabs(body_tex_scale.x) > 1.0e-4f) ? body_tex_scale.x : 1.f;
	const float body_tex_scale_y = (std::fabs(body_tex_scale.y) > 1.0e-4f) ? body_tex_scale.y : 1.f;

	return std::string(
		R"PLAYER(<html>
<head>
<meta charset="utf-8">
<style>
html,body{margin:0;width:100%;height:100%;background:)PLAYER") + body_col_css + std::string(R"PLAYER(;color:#111111;font-family:Segoe UI,Arial,sans-serif;overflow:hidden;-webkit-user-select:none;}
body{display:flex;align-items:center;justify-content:center;}
.player{position:relative;width:100%;height:100%;box-sizing:border-box;overflow:hidden;}
.player-base,.player-emission{position:absolute;left:0;top:0;right:0;bottom:0;pointer-events:none;}
.player-base{background:)PLAYER") + body_col_css + std::string(R"PLAYER(;}
.player-emission{opacity:0;mix-blend-mode:screen;}
.player-content{position:relative;z-index:1;width:100%;height:100%;box-sizing:border-box;padding:18px 28px 20px;display:flex;flex-direction:column;justify-content:center;gap:14px;}
.progress-shell{height:20px;display:flex;align-items:center;}
.progress-track{position:relative;width:100%;height:5px;border-radius:999px;background:#000000;cursor:pointer;}
.progress-track.disabled{cursor:default;opacity:0.55;}
.progress-fill{position:absolute;left:0;top:0;height:100%;width:0%;border-radius:999px;background:#000000;pointer-events:none;}
.progress-thumb{position:absolute;left:0%;top:50%;width:18px;height:18px;border-radius:50%;background:#000000;transform:translate(-50%,-50%);box-shadow:0 3px 10px rgba(0,0,0,0.16);pointer-events:none;}
.controls{display:flex;align-items:center;justify-content:center;gap:14px;}
.icon-button,.transport-button,.play-button{border:0;background:transparent;display:flex;align-items:center;justify-content:center;padding:0;cursor:pointer;transition:transform 0.12s ease,color 0.12s ease,opacity 0.12s ease;}
.icon-button,.transport-button{color:#000000;}
.icon-button{width:32px;height:32px;}
.transport-button{width:44px;height:44px;}
.play-button{width:74px;height:74px;border-radius:50%;background:#000000;color:#ffffff;box-shadow:0 9px 20px rgba(0,0,0,0.16);}
.icon-button.active,.transport-button:hover,.icon-button:hover{color:#000000;}
.play-button:hover{transform:translateY(-1px) scale(1.01);}
.icon-button:disabled,.transport-button:disabled,.play-button:disabled{cursor:default;opacity:0.28;transform:none;box-shadow:none;}
.icon-button svg,.transport-button svg{width:20px;height:20px;fill:currentColor;stroke:currentColor;stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round;}
.play-button svg{width:28px;height:28px;fill:currentColor;}
.empty{display:none;text-align:center;font-size:13px;letter-spacing:0.04em;text-transform:uppercase;color:rgba(17,17,17,0.48);}
audio{display:none;}
</style>
</head>
<body>
<div class="player">
<div class="player-base" id="playerBase"></div>
<div class="player-emission" id="playerEmission"></div>
<div class="player-content">
<div class="progress-shell">
<div class="progress-track" id="progressTrack" aria-label="Playback progress">
<div class="progress-fill" id="progressFill"></div>
<div class="progress-thumb" id="progressThumb"></div>
</div>
</div>
<div class="controls">
<button class="icon-button" id="shuffleButton" aria-label="Shuffle playlist"></button>
<button class="transport-button" id="prevButton" aria-label="Previous track"></button>
<button class="play-button" id="playButton" aria-label="Play"></button>
<button class="transport-button" id="nextButton" aria-label="Next track"></button>
<button class="icon-button" id="repeatButton" aria-label="Toggle repeat"></button>
</div>
<div class="empty" id="emptyState">Playlist is empty</div>
<audio id="playerAudio" preload="metadata"></audio>
</div>
</div>
<script>
const playlist=)PLAYER") + playlist_js + std::string(R"PLAYER(;
const autoplay=)PLAYER") + std::string(autoplay ? "true" : "false") + std::string(R"PLAYER(;
const loopPlaylistDefault=)PLAYER") + std::string(loop ? "true" : "false") + std::string(R"PLAYER(;
const shufflePlaylistDefault=)PLAYER") + std::string(shuffle ? "true" : "false") + std::string(R"PLAYER(;
const initialVolume=)PLAYER") + toString(initial_volume) + std::string(R"PLAYER(;
const bodyTextureURL=atob(')PLAYER") + body_texture_url_base64 + std::string(R"PLAYER(');
const bodyTextureScaleX=)PLAYER") + toString(body_tex_scale_x) + std::string(R"PLAYER(;
const bodyTextureScaleY=)PLAYER") + toString(body_tex_scale_y) + std::string(R"PLAYER(;
const emissionTextureURL=atob(')PLAYER") + emission_texture_url_base64 + std::string(R"PLAYER(');
const emissionColourR=)PLAYER") + toString(emission_col_r) + std::string(R"PLAYER(;
const emissionColourG=)PLAYER") + toString(emission_col_g) + std::string(R"PLAYER(;
const emissionColourB=)PLAYER") + toString(emission_col_b) + std::string(R"PLAYER(;
const emissionLuminance=)PLAYER") + toString(luminance) + std::string(R"PLAYER(;
const audio=document.getElementById('playerAudio');
const playerBase=document.getElementById('playerBase');
const playerEmission=document.getElementById('playerEmission');
const progressTrack=document.getElementById('progressTrack');
const progressFill=document.getElementById('progressFill');
const progressThumb=document.getElementById('progressThumb');
const emptyElem=document.getElementById('emptyState');
const shuffleButton=document.getElementById('shuffleButton');
const repeatButton=document.getElementById('repeatButton');
const playButton=document.getElementById('playButton');
const prevButton=document.getElementById('prevButton');
const nextButton=document.getElementById('nextButton');
audio.volume=initialVolume;
let currentIndex=0;
let shuffleEnabled=shufflePlaylistDefault;
let loopEnabled=loopPlaylistDefault;
let userIsSeeking=false;
let pendingSeekProgress=null;
let pendingSeekAttemptCount=0;
let pendingSeekLastAttemptMs=0;
const pendingSeekRetryDelayMs=180;
const pendingSeekMaxAttempts=12;
const shuffleSvg=`<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M4 7h4c3.5 0 4.5 5 8 5h4' fill='none'/><path d='M17 4l3 3-3 3' fill='none'/><path d='M4 17h4c3.5 0 4.5-5 8-5h4' fill='none'/><path d='M17 14l3 3-3 3' fill='none'/></svg>`;
const repeatSvg=`<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M7 7h10a3 3 0 0 1 3 3v1' fill='none'/><path d='M17 4l3 3-3 3' fill='none'/><path d='M17 17H7a3 3 0 0 1-3-3v-1' fill='none'/><path d='M7 20l-3-3 3-3' fill='none'/></svg>`;
const prevSvg=`<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M6 5h3v14H6z'/><path d='M18 6v12l-8-6 8-6z'/></svg>`;
const nextSvg=`<svg viewBox='0 0 24 24' aria-hidden='true'><path d='M15 5h3v14h-3z'/><path d='M6 6v12l8-6-8-6z'/></svg>`;
const playSvg=`<svg viewBox='0 0 64 64' aria-hidden='true'><path d='M24 18l22 14-22 14z'/></svg>`;
const pauseSvg=`<svg viewBox='0 0 64 64' aria-hidden='true'><rect x='21' y='18' width='8' height='28' rx='2'/><rect x='35' y='18' width='8' height='28' rx='2'/></svg>`;
function clamp01(value){return Math.max(0, Math.min(1, value));}
function textureSizeForScale()
{
	const safeScaleX=Math.max(Math.abs(bodyTextureScaleX), 0.0001);
	const safeScaleY=Math.max(Math.abs(bodyTextureScaleY), 0.0001);
	return (100 / safeScaleX) + '% ' + (100 / safeScaleY) + '%';
}
function applyBodyTextureStyles()
{
	if(playerBase)
	{
		if(bodyTextureURL)
		{
			playerBase.style.backgroundImage='url(' + JSON.stringify(bodyTextureURL) + ')';
			playerBase.style.backgroundRepeat='repeat';
			playerBase.style.backgroundSize=textureSizeForScale();
			playerBase.style.backgroundPosition='0 0';
		}
		else
		{
			playerBase.style.backgroundImage='none';
		}
	}

	if(bodyTextureURL)
	{
		document.body.style.backgroundImage='url(' + JSON.stringify(bodyTextureURL) + ')';
		document.body.style.backgroundRepeat='repeat';
		document.body.style.backgroundSize=textureSizeForScale();
		document.body.style.backgroundPosition='0 0';
	}
}
function luminanceToStrength(luminance)
{
	if(luminance <= 0)
		return 0;

	const mapped=Math.log10(1 + luminance) / 5.0;
	return clamp01(mapped);
}
function applyEmissionStyles()
{
	if(!playerEmission)
		return;

	const glowStrength=luminanceToStrength(emissionLuminance);
	const hasEmissionTexture=!!emissionTextureURL;
	if(glowStrength <= 0 && !hasEmissionTexture)
	{
		playerEmission.style.opacity='0';
		playerEmission.style.backgroundImage='none';
		playerEmission.style.boxShadow='none';
		return;
	}

	const colourSum=emissionColourR + emissionColourG + emissionColourB;
	const glowR=(colourSum > 0) ? emissionColourR : 255;
	const glowG=(colourSum > 0) ? emissionColourG : 255;
	const glowB=(colourSum > 0) ? emissionColourB : 255;

	const tintAlpha=0.10 + glowStrength * 0.55;
	const innerAlpha=0.12 + glowStrength * 0.50;
	const outerAlpha=0.16 + glowStrength * 0.58;

	playerEmission.style.opacity='1';
	playerEmission.style.backgroundColor='rgba(' + glowR + ',' + glowG + ',' + glowB + ',' + tintAlpha + ')';
	if(hasEmissionTexture)
	{
		playerEmission.style.backgroundImage='url(' + JSON.stringify(emissionTextureURL) + ')';
		playerEmission.style.backgroundRepeat='repeat';
		playerEmission.style.backgroundSize=textureSizeForScale();
		playerEmission.style.backgroundPosition='0 0';
	}
	else
	{
		playerEmission.style.backgroundImage='none';
	}

	const innerBlur=Math.round(20 + 70 * glowStrength);
	const outerBlur=Math.round(46 + 150 * glowStrength);
	const innerGlow='rgba(' + glowR + ',' + glowG + ',' + glowB + ',' + innerAlpha + ')';
	const outerGlow='rgba(' + glowR + ',' + glowG + ',' + glowB + ',' + outerAlpha + ')';
	playerEmission.style.boxShadow='inset 0 0 ' + innerBlur + 'px ' + innerGlow + ', 0 0 ' + outerBlur + 'px ' + outerGlow;
	playerEmission.style.mixBlendMode='screen';
	playerEmission.style.filter='saturate(1.08)';
	playerEmission.style.backgroundBlendMode='screen';
}
function hasUsableDuration(){return Number.isFinite(audio.duration) && audio.duration > 0;}
function clearPendingSeek()
{
	pendingSeekProgress=null;
	pendingSeekAttemptCount=0;
	pendingSeekLastAttemptMs=0;
}
function pendingSeekTargetTime(){return clamp01(pendingSeekProgress) * audio.duration;}
function pendingSeekReachedTarget()
{
	const tolerance=Math.max(0.08, audio.duration * 0.001);
	return Math.abs(audio.currentTime - pendingSeekTargetTime()) <= tolerance;
}
function applyPendingSeekIfPossible(force)
{
	if(pendingSeekProgress === null)
		return;

	if(!hasUsableDuration())
		return;

	if(pendingSeekReachedTarget())
	{
		clearPendingSeek();
		return;
	}

	const now=performance.now();
	if(!force)
	{
		if(audio.seeking)
			return;

		if((pendingSeekAttemptCount > 0) && ((now - pendingSeekLastAttemptMs) < pendingSeekRetryDelayMs))
			return;
	}

	if(pendingSeekAttemptCount >= pendingSeekMaxAttempts)
	{
		clearPendingSeek();
		return;
	}

	audio.currentTime=pendingSeekTargetTime();
	pendingSeekAttemptCount++;
	pendingSeekLastAttemptMs=now;
}
function updateEmptyState(){emptyElem.style.display=playlist.length ? 'none' : 'block';}
function updateProgressVisual(progress)
{
	const clamped=clamp01(progress);
	const percent=(Math.round(clamped * 1000) / 10) + '%';
	progressFill.style.width=percent;
	progressThumb.style.left=percent;
	progressTrack.classList.toggle('disabled', !playlist.length);
}
function updateButtons()
{
	const hasTracks=playlist.length > 0;
	shuffleButton.innerHTML=shuffleSvg;
	repeatButton.innerHTML=repeatSvg;
	prevButton.innerHTML=prevSvg;
	nextButton.innerHTML=nextSvg;
	playButton.innerHTML=audio.paused ? playSvg : pauseSvg;
	shuffleButton.classList.toggle('active', shuffleEnabled);
	repeatButton.classList.toggle('active', loopEnabled);
	shuffleButton.disabled=!hasTracks;
	repeatButton.disabled=!hasTracks;
	prevButton.disabled=!hasTracks;
	nextButton.disabled=!hasTracks;
	playButton.disabled=!hasTracks;
	playButton.setAttribute('aria-label', audio.paused ? 'Play' : 'Pause');
}
function syncProgressFromAudio()
{
	applyPendingSeekIfPossible(false);

	if(!userIsSeeking)
	{
		if(pendingSeekProgress !== null)
			updateProgressVisual(pendingSeekProgress);
		else if(hasUsableDuration())
			updateProgressVisual(audio.currentTime / audio.duration);
		else
			updateProgressVisual(0);
	}
	updateButtons();
}
function playCurrent()
{
	if(!playlist.length)
		return;
	const playPromise=audio.play();
	if(playPromise && playPromise.catch)
		playPromise.catch(function(){updateButtons();});
	updateButtons();
}
function chooseRandomIndex()
{
	if(playlist.length <= 1)
		return currentIndex;
	let nextIndex=currentIndex;
	while(nextIndex === currentIndex)
		nextIndex=Math.floor(Math.random() * playlist.length);
	return nextIndex;
}
function selectTrack(index, shouldPlay)
{
	if(!playlist.length)
	{
		clearPendingSeek();
		audio.pause();
		audio.removeAttribute('src');
		audio.load();
		updateEmptyState();
		updateProgressVisual(0);
		updateButtons();
		return;
	}

	updateEmptyState();
	currentIndex=((index % playlist.length) + playlist.length) % playlist.length;
	clearPendingSeek();
	audio.src=playlist[currentIndex].url;
	audio.load();
	updateProgressVisual(0);
	if(shouldPlay)
		playCurrent();
	else
		updateButtons();
}
function selectNext(shouldPlay)
{
	if(!playlist.length)
		return;

	if(shuffleEnabled)
		selectTrack(chooseRandomIndex(), shouldPlay);
	else if(currentIndex + 1 < playlist.length)
		selectTrack(currentIndex + 1, shouldPlay);
	else if(loopEnabled)
		selectTrack(0, shouldPlay);
	else
	{
		audio.pause();
		if(hasUsableDuration())
			audio.currentTime=audio.duration;
		updateProgressVisual(1);
		updateButtons();
	}
}
function selectPrev()
{
	if(!playlist.length)
		return;

	if(hasUsableDuration() && audio.currentTime > 2)
	{
		audio.currentTime=0;
		syncProgressFromAudio();
		return;
	}

	if(shuffleEnabled)
		selectTrack(chooseRandomIndex(), true);
	else if(currentIndex > 0)
		selectTrack(currentIndex - 1, true);
	else if(loopEnabled)
		selectTrack(playlist.length - 1, true);
	else
	{
		audio.currentTime=0;
		syncProgressFromAudio();
	}
}
function progressForEvent(event)
{
	const rect=progressTrack.getBoundingClientRect();
	if(rect.width <= 0)
		return 0;

	let clientX=0;
	if(event.touches && event.touches.length)
		clientX=event.touches[0].clientX;
	else if(event.changedTouches && event.changedTouches.length)
		clientX=event.changedTouches[0].clientX;
	else
		clientX=event.clientX;

	return clamp01((clientX - rect.left) / rect.width);
}
function setPlaybackProgress(progress)
{
	const clamped=clamp01(progress);
	pendingSeekProgress=clamped;
	pendingSeekAttemptCount=0;
	pendingSeekLastAttemptMs=0;
	updateProgressVisual(clamped);
	applyPendingSeekIfPossible(true);
}
function beginSeek(event)
{
	if(!playlist.length)
		return;
	userIsSeeking=true;
	if(event.cancelable)
		event.preventDefault();
	setPlaybackProgress(progressForEvent(event));
}
function continueSeek(event)
{
	if(!userIsSeeking)
		return;
	if(event.cancelable)
		event.preventDefault();
	setPlaybackProgress(progressForEvent(event));
}
function endSeek(event)
{
	if(!userIsSeeking)
		return;
	if(event && event.cancelable)
		event.preventDefault();
	if(event)
		setPlaybackProgress(progressForEvent(event));
	userIsSeeking=false;
	updateButtons();
}
shuffleButton.onclick=function(){if(!playlist.length)return;shuffleEnabled=!shuffleEnabled;updateButtons();};
repeatButton.onclick=function(){if(!playlist.length)return;loopEnabled=!loopEnabled;updateButtons();};
playButton.onclick=function(){if(!playlist.length)return;if(!audio.src)selectTrack(currentIndex, false);if(audio.paused)playCurrent();else{audio.pause();updateButtons();}};
prevButton.onclick=function(){selectPrev();};
nextButton.onclick=function(){selectNext(true);};
progressTrack.addEventListener('mousedown', beginSeek);
document.addEventListener('mousemove', continueSeek);
document.addEventListener('mouseup', endSeek);
progressTrack.addEventListener('click', function(event){if(userIsSeeking || !playlist.length)return;setPlaybackProgress(progressForEvent(event));});
audio.addEventListener('play', updateButtons);
audio.addEventListener('pause', updateButtons);
audio.addEventListener('timeupdate', syncProgressFromAudio);
audio.addEventListener('seeking', syncProgressFromAudio);
audio.addEventListener('seeked', function(){applyPendingSeekIfPossible(true);syncProgressFromAudio();});
audio.addEventListener('progress', syncProgressFromAudio);
audio.addEventListener('loadedmetadata', function(){applyPendingSeekIfPossible(true);syncProgressFromAudio();});
audio.addEventListener('durationchange', function(){applyPendingSeekIfPossible(true);syncProgressFromAudio();});
audio.addEventListener('canplay', function(){applyPendingSeekIfPossible(true);syncProgressFromAudio();});
audio.addEventListener('ended', function(){if(loopEnabled || shuffleEnabled || (currentIndex + 1 < playlist.length)){selectNext(true);}else{updateProgressVisual(1);updateButtons();}});
applyBodyTextureStyles();
applyEmissionStyles();
updateEmptyState();
updateProgressVisual(0);
updateButtons();
if(playlist.length)
	selectTrack(0, autoplay);
</script>
</body>
</html>)PLAYER");
}
}


void WebViewData::process(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt)
{
	PERFORMANCEAPI_INSTRUMENT_FUNCTION();

	m_gui_client = gui_client;

	const double ob_dist_from_cam = ob->pos.getDist(gui_client->cam_controller.getPosition());
	const double max_play_dist = maxBrowserDist();
	[[maybe_unused]]const double unload_dist = maxBrowserDist() * 1.3;
	const bool in_process_dist = ob_dist_from_cam < max_play_dist;
	
	// Debug output - always log for webview objects
	static int debug_counter = 0;
	if(++debug_counter % 60 == 0) // Log every 60 frames (~1 second)
	{
		conPrint("WebViewData::process: ob_dist=" + toString(ob_dist_from_cam) + ", max_dist=" + toString(max_play_dist) + ", in_process=" + toString(in_process_dist) + ", target_url='" + ob->target_url + "', opengl_engine_ob=" + toString(ob->opengl_engine_ob.nonNull()) + ", browser=" + toString(browser.nonNull()));
	}

#if EMSCRIPTEN
	if(in_process_dist)
	{
		if((html_view_handle < 0) && !ob->target_url.empty() && ob->opengl_engine_ob)
		{
			const bool URL_in_whitelist = gui_client->url_whitelist.isURLPrefixInWhitelist(ob->target_url);

			// If the user is logged in to their personal world, and the user created the object, consider the URL to be safe.
			const bool webview_is_safe = gui_client->logged_in_user_id.valid() && 
				(!gui_client->server_worldname.empty() && (gui_client->server_worldname == gui_client->logged_in_user_name)) && // If this is the personal world of the user:
				(gui_client->logged_in_user_id == ob->creator_id);

			if(user_clicked_to_load || URL_in_whitelist || webview_is_safe)
			{
				gui_client->logMessage("WebViewData: Making new web view iframe, target_url: " + ob->target_url);
				conPrint("WebViewData: Making new web view iframe, target_url: " + ob->target_url);

				html_view_handle = makeWebViewIframe(ob->target_url.c_str());

				conPrint("makeWebViewIframe() done, got handle " +  toString(html_view_handle));

				// Now that we are loading the iframe, draw this object with alpha zero.
				ob->opengl_engine_ob->materials[0].alpha = 0.f; // Set alpha to zero for alpha-cutout technique, to show web views in iframe under the opengl canvas.
				opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob);
			}
			else
			{
				if(ob->opengl_engine_ob->materials[0].emission_texture.isNull())
				{
					gui_client->setGLWidgetContextAsCurrent(); // Make sure the correct context is current while making OpenGL calls.

					//assert(!showing_click_to_load_text);
					ob->opengl_engine_ob->materials[0].emission_texture = makeTextTexture(opengl_engine, gui_client->gl_ui.ptr(), ob->target_url);
					opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob);
					showing_click_to_load_text = true;
				}
			}

			// TODO: handle target_url changing

			this->loaded_target_url = ob->target_url;
		}
	}
	else // else if !in_process_dist:
	{
		if(ob_dist_from_cam >= unload_dist)
		{
			// If there is a browser, destroy it.
			if(html_view_handle >= 0)
			{
				destroyHTMLViewJS(html_view_handle);
				html_view_handle = -1;
			}
		}
	}


	// Set transform on the browser iframe div, if it exists.
	if(html_view_handle >= 0)
	{
		const float vw = (float)opengl_engine->getMainViewPortWidth();
		const float vh = (float)opengl_engine->getMainViewPortHeight();
		const float cam_dist = (opengl_engine->getCurrentScene()->lens_sensor_dist / opengl_engine->getCurrentScene()->use_sensor_width) * vw;

		const float elem_w_px = 1024;
		const float elem_h_px = 576;

		const Matrix4f transform = Matrix4f::translationMatrix(vw/2, vh/2 - elem_w_px, cam_dist) * 
			Matrix4f::scaleMatrix(1, -1, 1) * Matrix4f::translationMatrix(0, -elem_w_px, 0) * Matrix4f::uniformScaleMatrix(elem_w_px) * 
			opengl_engine->getCurrentScene()->last_view_matrix * ob->obToWorldMatrix() * 
			/*rot from x-y plane to x-z plane=*/Matrix4f::rotationAroundXAxis(Maths::pi_2<float>()) * Matrix4f::translationMatrix(0, 1, 0) * Matrix4f::scaleMatrix(1, -1, 1) * 
			Matrix4f::scaleMatrix(1 / elem_w_px, 1 / elem_h_px, 1 / elem_w_px);
			
		std::string matrix_string = "matrix3d(";
		for(int i=0; i<16; ++i)
		{
			matrix_string += ::toString(transform.e[i]);
			if(i + 1 < 16)
				matrix_string += ", ";
		}
		matrix_string += ")";
		setHTMLElementCSSTransform(html_view_handle, matrix_string.c_str());
	}
#endif // end if EMSCRIPTEN


#if CEF_SUPPORT

	const bool is_audio_player = ob->isAudioPlayerWebView();
	const int viewport_width  = is_audio_player ? 1200 : 1920;
	const int viewport_height = is_audio_player ? 320  : 1080;
	const std::string audio_player_state_key = is_audio_player ? makeAudioPlayerStateKey(*ob) : std::string();
	const std::string effective_target_url = is_audio_player ? WorldObject::audioPlayerTargetURL() : ob->target_url;

	if(is_audio_player && ob->target_url.empty())
		ob->target_url = effective_target_url;

	if(in_process_dist)
	{
		const bool cef_initialized = CEF::isInitialised();
		if(!cef_initialized)
		{
			static int cef_warn_counter = 0;
			if(++cef_warn_counter % 300 == 0) // Warn every 5 seconds
			{
				conPrint("WebViewData::process: CEF not initialized! Cannot load webview.");
			}
		}
		if(cef_initialized)
		{
			const bool has_url = !effective_target_url.empty();
			const bool has_opengl_ob = ob->opengl_engine_ob.nonNull();
			const bool browser_is_null = browser.isNull();
			
			if(browser_is_null && has_url && has_opengl_ob)
			{
				// Auto-load web view when player approaches (in_process_dist is true).
				// Local audio players should load immediately, normal web views still follow the existing interaction rules.
				const bool should_auto_load = is_audio_player || in_process_dist || user_clicked_to_load;
				
				conPrint("WebViewData::process: should_auto_load=" + toString(should_auto_load) + ", in_process_dist=" + toString(in_process_dist) + ", user_clicked=" + toString(user_clicked_to_load) + ", target_url='" + ob->target_url + "'");

				if(should_auto_load)
				{
					conPrint("WebViewData::process: Creating browser, target_url: " + effective_target_url);
					gui_client->logMessage("Creating browser, target_url: " + effective_target_url);

					if(ob->opengl_engine_ob.nonNull())
					{
						gui_client->setGLWidgetContextAsCurrent(); // Make sure the correct context is current while making OpenGL calls.

						if(!is_audio_player)
							ob->opengl_engine_ob->materials[0].fresnel_scale = 0; // Remove specular reflections, reduces washed-out look.
					}

					browser = new EmbeddedBrowser();
					conPrint("WebViewData::process: Calling browser->create()...");
					if(is_audio_player)
					{
						const std::string root_page = makeAudioPlayerRootPage(*ob, *gui_client->resource_manager, gui_client->server_hostname);
						browser->create(WorldObject::audioPlayerTargetURL(), viewport_width, viewport_height, gui_client, ob, /*mat index=*/0, /*apply_to_emission_texture=*/false, OpenGLTexture::Wrapping_Clamp, opengl_engine, root_page);
						this->loaded_audio_player_state_key = audio_player_state_key;
					}
					else
					{
						browser->create(effective_target_url, viewport_width, viewport_height, gui_client, ob, /*mat index=*/0, /*apply_to_emission_texture=*/true, OpenGLTexture::Wrapping_Clamp, opengl_engine);
					}
					this->loaded_target_url = effective_target_url;
					conPrint("WebViewData::process: browser->create() completed");
				}
				else
				{
					static int skip_counter = 0;
					if(++skip_counter % 300 == 0)
					{
						conPrint("WebViewData::process: Skipping auto-load, should_auto_load=false");
					}
				}
			}
			else
			{
				static int cond_counter = 0;
				if(++cond_counter % 300 == 0)
				{
					conPrint("WebViewData::process: Conditions not met - browser_null=" + toString(browser_is_null) + ", has_url=" + toString(has_url) + ", has_opengl_ob=" + toString(has_opengl_ob));
				}
			}

			if(browser.nonNull() && is_audio_player && (audio_player_state_key != this->loaded_audio_player_state_key))
			{
				const std::string root_page = makeAudioPlayerRootPage(*ob, *gui_client->resource_manager, gui_client->server_hostname);
				browser->updateRootPage(root_page);
				browser->navigate(WorldObject::audioPlayerTargetURL());
				this->loaded_audio_player_state_key = audio_player_state_key;
				this->loaded_target_url = effective_target_url;
			}
			else if(browser.nonNull() && (effective_target_url != this->loaded_target_url))
			{
				// Always navigate to new URL, no security checks needed
				browser->navigate(effective_target_url);
				this->loaded_target_url = effective_target_url;
			}

			// While the webview is not visible by the camera, we discard any dirty-rect updates.  
			// When the webview becomes visible again, if there were any discarded dirty rects, then we know our buffer is out of date.
			// So invalidate the whole buffer.
			// Note that CEF only allows us to invalidate the whole buffer - see https://magpcss.org/ceforum/viewtopic.php?f=6&t=15079
			// If we could invalidate part of it, then we can maintain the actual discarded dirty rectangles (or a bounding rectangle around them)
			// and use that to just invalidate the dirty part.
			if(browser && ob->opengl_engine_ob)
			{
				browser->think();

				const bool ob_visible = opengl_engine->isObjectInCameraFrustum(*ob->opengl_engine_ob);
				if(ob_visible && !previous_is_visible) // If webview just became visible:
				{
					//conPrint("Browser became visible!");
					browser->browserBecameVisible();
				}

				previous_is_visible = ob_visible;
			}
		}
	}
	else // else if !in_process_dist:
	{
		if(browser.nonNull())
		{
			conPrint("WebViewData::process: Closing browser (out of view distance), target_url: " + ob->target_url);
			gui_client->logMessage("Closing browser (out of view distance), target_url: " + ob->target_url);
			browser = NULL;
			loaded_audio_player_state_key.clear();

			// Remove audio source
			if(ob->audio_source.nonNull())
			{
				gui_client->audio_engine.removeSource(ob->audio_source);
				ob->audio_source = NULL;
			}
		}
	}
#endif // CEF_SUPPORT
}


void WebViewData::mouseReleased(MouseEvent* e, const Vec2f& uv_coords)
{
	if(browser.nonNull())
		browser->mouseReleased(e, uv_coords);
}


void WebViewData::mousePressed(MouseEvent* e, const Vec2f& uv_coords)
{
	if(showing_click_to_load_text && uvsAreOnLoadButton(uv_coords.x, uv_coords.y))
		user_clicked_to_load = true;

	if(browser.nonNull())
		browser->mousePressed(e, uv_coords);

	startInteractingWithWebViewEmscripten();
}


void WebViewData::mouseDoubleClicked(MouseEvent* e, const Vec2f& uv_coords)
{
}


void WebViewData::mouseMoved(MouseEvent* e, const Vec2f& uv_coords)
{
	if(browser.nonNull())
		browser->mouseMoved(e, uv_coords);
}


void WebViewData::wheelEvent(MouseWheelEvent* e, const Vec2f& uv_coords)
{
	if(browser.nonNull())
		browser->wheelEvent(e, uv_coords);

	startInteractingWithWebViewEmscripten();
}


void WebViewData::keyPressed(KeyEvent* e)
{
	if(browser.nonNull())
		browser->keyPressed(e);
}


void WebViewData::keyReleased(KeyEvent* e)
{
	if(browser.nonNull())
		browser->keyReleased(e);
}


void WebViewData::handleTextInputEvent(TextInputEvent& e)
{
	if(browser.nonNull())
		browser->handleTextInputEvent(e);
}


void WebViewData::startInteractingWithWebViewEmscripten()
{
#if EMSCRIPTEN
	if(html_view_handle >= 0)
	{
		if(m_gui_client)
			m_gui_client->showInfoNotification("Interacting with web view");

		EM_ASM({
			document.getElementById('background-div').style.zIndex = 100; // move background to front

			move_background_timer_id = setTimeout(function() {
				document.getElementById('background-div').style.zIndex = -1; // move background to back
			}, 5000);
		});
	}
#endif
}
