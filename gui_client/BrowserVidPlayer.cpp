/*=====================================================================
BrowserVidPlayer.cpp
--------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "BrowserVidPlayer.h"


#include "GUIClient.h"
#include "EmbeddedBrowser.h"
#include "../shared/WorldObject.h"
#include "../shared/ResourceManager.h"
#include "../shared/FileTypes.h"
#include "CEF.h"
#include <opengl/OpenGLEngine.h>
#include <opengl/IncludeOpenGL.h>
#include <maths/vec2.h>
#include <webserver/Escaping.h>
#include <networking/URL.h>
#include <ui/UIEvents.h>
#include <utils/FileInStream.h>
#include <utils/PlatformUtils.h>
#include <utils/Base64.h>
#include <utils/Clock.h>
#include <utils/FileUtils.h>
#include <utils/StringUtils.h>
#include "superluminal/PerformanceAPI.h"
#if EMSCRIPTEN
#include <emscripten.h>
#endif
#ifdef _WIN32
#include "../video/WMFVideoReader.h"
#include "../video/VideoReader.h"
#endif


#define GL_BGRA                           0x80E1


#if EMSCRIPTEN
EM_JS(void, destroyHTMLViewJS, (int handle), {
	console.log("=================destroyHTMLViewJS()================");
	
	let div = html_view_elem_handle_to_div_map[handle];
	div.remove();
	delete html_view_elem_handle_to_div_map.handle; // Remove from html_view_elem_handle_to_div_map
});
#endif


BrowserVidPlayer::BrowserVidPlayer()
:	browser(NULL),
	previous_is_visible(true),
	state(State_Unloaded),
	m_gui_client(NULL),
	html_view_handle(-1),
	using_iframe(false)
#ifdef _WIN32
	, wmf_video_reader(NULL),
	last_video_frame_time(0),
	video_playback_time(0),
	is_paused(false),
	show_controls(false),
	controls_hide_time(0)
#endif
{}


BrowserVidPlayer::~BrowserVidPlayer()
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


// See https://webapps.stackexchange.com/questions/54443/format-for-id-of-youtube-video
static void checkYouTubeVideoID(const std::string& s)
{
	for(size_t i=0; i<s.size(); ++i)
		if(!(isAlphaNumeric(s[i]) || s[i] == '-' || s[i] == '_'))
			throw glare::Exception("invalid char in youtube vid id: '" + std::string(1, s[i]) + "'");
}


// For YouTube URLs, we want to transform the URL into an embedded webpage, and encode it into a data URL.
// Throws glare::Exception on failure.
static std::string makeEmbedHTMLForVideoURL(const std::string& video_url, int width, int height, WorldObject* ob, ResourceManager& resource_manager, const std::string& server_hostname)
{
	const bool is_http_URL = hasPrefix(video_url, "http://") || hasPrefix(video_url, "https://");

	if(is_http_URL)
	{
		const URL parsed_URL = URL::parseURL(video_url);

		if(parsed_URL.host == "www.youtube.com" || parsed_URL.host == "youtu.be")
		{
			std::string video_id;
			if(parsed_URL.host == "www.youtube.com")
			{
				// Parse query
				const std::map<std::string, std::string> params = URL::parseQuery(parsed_URL.query);
				const auto res = params.find("v"); // Find "v" param (video id)
				if(res != params.end())
					video_id = res->second;
				else
					throw glare::Exception("Could not find video id param (v) in YouTube URL.");
			}
			else
			{
				// Handle URLs of the form https://youtu.be/SaejB5NSzOs
				assert(parsed_URL.host == "youtu.be");
				video_id = eatSuffix(eatPrefix(parsed_URL.path, "/"), "/");
			}

			checkYouTubeVideoID(video_id); // Check video id so we don't need to worry about escaping it

			const bool autoplay = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_AUTOPLAY);
			const bool loop     = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_LOOP);
			const bool muted    = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_MUTED);

			// See https://developers.google.com/youtube/player_parameters
			const std::string player_params =
				"{												\n"
				"	height: '" + toString(height) + "',			\n"
				"	width: '" + toString(width) + "',			\n"
				"	videoId: '" + video_id + "',				\n"
				"	playerVars: {								\n"
				"		'playsinline': 1,						\n"
				"		'autoplay': " + toString(autoplay ? 1 : 0) + ",		\n"
				"		'loop': "     + toString(loop ? 1 : 0) + ",			\n"
				"		'playlist': '" + video_id + "'			\n"// Set the playlist to the video ID.  This is needed for looping to work.
				"	},											\n"
				"	events: {									\n"
				"		onReady: function(e) {					\n"
				"			" + (muted ? "e.target.mute();" : "") + "\n" // If video should be muted, insert a call to mute the video here.
				"		}										\n"
				"	}											\n"
				"}";

			// See https://developers.google.com/youtube/iframe_api_reference
			const std::string embed_html = 
				"<html>																			\n"
				"	<body style='margin:0px;'>													\n"
				"	<div id=\"player\"></div>													\n"
				"		<script>																\n"
				"			// Load the IFrame Player API code asynchronously.					\n"
				"			var tag = document.createElement('script');							\n"
				"			tag.src = \"https://www.youtube.com/iframe_api\";					\n"
				"			var firstScriptTag = document.getElementsByTagName('script')[0];	\n"
				"			firstScriptTag.parentNode.insertBefore(tag, firstScriptTag);		\n"
				"																				\n"
				"			// Replace the 'ytplayer' element with an <iframe> and				\n"
				"			// YouTube player after the API code downloads.						\n"
				"			var player;															\n"
				"			function onYouTubeIframeAPIReady() {								\n"
				"				player = new YT.Player('player', 								\n"
				+ player_params +
				"			)}																	\n"
				"		</script>																\n"
				"	</body>																		\n"
				"</html>																		\n";
				
			return embed_html;
		}
		else if(parsed_URL.host == "www.twitch.tv")
		{
			std::string channel_name = parsed_URL.path;
			channel_name = eatPrefix(channel_name, "/");

			const bool autoplay = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_AUTOPLAY);
			const bool muted    = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_MUTED);

			// See https://dev.twitch.tv/docs/embed/video-and-clips/

			const std::string iframe_src_url = "https://player.twitch.tv/?channel=" + web::Escaping::URLEscape(channel_name) + "&parent=localdomain&autoplay=" + boolToString(autoplay) + "&muted=" + boolToString(muted);

			const std::string embed_html = 
				"<html>																					\n"
				"	<body style='margin:0px;'>															\n"
				"		<iframe style='margin:0px;'													\n"
				"			src=\"" + iframe_src_url + "\"												\n"
				"			height=" + toString(height - 5) + "											\n"
				"			width=" + toString(width - 5) + "											\n"
				"		allowfullscreen>																\n"
				"		</iframe>																		\n"
				"	</body>																				\n"
				"</html>																				\n";

			return embed_html;
		}
		else
			throw glare::Exception("only YouTube and Twitch HTTP URLS accepted currently.");
	}
	else // Else non-http:
	{
		ResourceRef resource = resource_manager.getExistingResourceForURL(toURLString(video_url));

		// if the resource is downloaded already, read video off disk:
		std::string use_URL;
		if(resource.nonNull() && resource->getState() == Resource::State_Present)
		{
			use_URL = "https://resource/" + web::Escaping::URLEscape(video_url);// resource->getLocalPath();
		}
		else // Otherwise use streaming via HTTP
		{
			// Rewrite it to a substrata HTTP URL, so we can use streaming via HTTP.
			use_URL = "http://" + server_hostname + "/resource/" + web::Escaping::URLEscape(video_url);
		}

		const bool autoplay = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_AUTOPLAY);
		const bool loop     = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_LOOP);
		const bool muted    = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_MUTED);

		const std::string attributes = "controls " + std::string(autoplay ? "autoplay " : "") + std::string(loop ? "loop " : "") + std::string(muted ? "muted " : "");

		// NOTE: We will use a custom HTML page with the loop attribute set to true.  Can also add 'controls' attribute to debug stuff.
		const std::string html =
			"<html>"
			"	<head>"
			"	</head>"
			"	<body style=\"margin:0\">"
			"		<video " + attributes + " name=\"media\" id=\"thevid\" width=\"" + toString(width) + "px\" height=\"" + toString(height) + "px\">"
			"			<source src=\"" + web::Escaping::HTMLEscape(use_URL) + "\" type=\"video/mp4\" />"
			"		</video>"
			"	</body>"
			"</html>";

		return html;
	}
}


static void getVidTextureDimensions(const std::string& video_URL, WorldObject* ob, int& width_out, int& height_out)
{
	const bool is_http_URL = hasPrefix(video_URL, "http://") || hasPrefix(video_URL, "https://");
	if(!is_http_URL)
	{
		width_out = 1024;
		const float use_height_over_width = ob->scale.z / ob->scale.x; // Object scale should be based on video aspect ratio, see ModelLoading::makeImageCube().
		height_out = myClamp((int)(1024 * use_height_over_width), 16, 2048);
	}
	else
	{
		// Use standard YouTube embed aspect ratio.  See also MainWindow::on_actionAdd_Video_triggered().
		width_out = 1024;
		height_out = (int)(1024.f / 1920.f * 1080.f); // = 576
	}
}


void BrowserVidPlayer::createNewBrowserPlayer(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob)
{
	this->m_gui_client = gui_client; // Store reference for pause/play control
	
	if(ob->materials.empty())
		throw glare::Exception("materials were empty");

	const std::string video_URL = toStdString(ob->materials[0]->emission_texture_url);

	int width, height;
	getVidTextureDimensions(video_URL, ob, width, height);
	
	if(video_URL.empty())
		throw glare::Exception("video_URL was empty");

	if(ob->opengl_engine_ob.isNull())
		throw glare::Exception("ob->opengl_engine_ob.isNull");

	// Check if CEF is available
	bool cef_available = false;
#if CEF_SUPPORT
	if(!CEF::isInitialised())
		CEF::initialiseCEF(gui_client->base_dir_path);
	cef_available = CEF::isInitialised();
#endif

	// Check if this is an MP4 file and we should use WMFVideoReader fallback (Windows only, no CEF)
	const bool is_http_URL = hasPrefix(video_URL, "http://") || hasPrefix(video_URL, "https://");
	const bool is_mp4_file = !is_http_URL && hasExtension(video_URL, "mp4");

#ifdef _WIN32
	// Debug logging
	gui_client->logMessage("Video player creation: video_URL='" + video_URL + "', is_http_URL=" + toString(is_http_URL) + ", is_mp4_file=" + toString(is_mp4_file) + ", cef_available=" + toString(cef_available) + ", device_manager=" + toString(gui_client->device_manager != NULL));

	// Use WMFVideoReader fallback for MP4 files when CEF is not available
	if(is_mp4_file && !cef_available && gui_client->device_manager != NULL)
	{
		gui_client->logMessage("Creating WMFVideoReader for MP4 (CEF not available), video_URL: " + video_URL);

		// Get resource and local path
		ResourceRef resource = gui_client->resource_manager->getExistingResourceForURL(toURLString(video_URL));
		if(resource.isNull() || resource->getState() != Resource::State_Present)
		{
			std::string error_msg = "MP4 resource not found or not downloaded: " + video_URL;
			if(resource.isNull())
				error_msg += " (resource is null)";
			else
				error_msg += " (resource state: " + toString((int)resource->getState()) + ")";
			gui_client->logMessage("WMFVideoReader error: " + error_msg);
			throw glare::Exception(error_msg);
		}

		const std::string local_path = gui_client->resource_manager->getLocalAbsPathForResource(*resource);
		gui_client->logMessage("WMFVideoReader: local_path='" + local_path + "'");

		// Check if file exists
		if(!FileUtils::fileExists(local_path))
		{
			std::string error_msg = "MP4 file does not exist at path: " + local_path;
			gui_client->logMessage("WMFVideoReader error: " + error_msg);
			throw glare::Exception(error_msg);
		}

		// Get file size for diagnostics
		const uint64 file_size = FileUtils::getFileSize(local_path);
		gui_client->logMessage("WMFVideoReader: file_size=" + toString(file_size) + " B (" + getNiceByteSize(file_size) + ")");

		try
		{
			// Create WMFVideoReader
			gui_client->logMessage("WMFVideoReader: Creating WMFVideoReader instance...");
			wmf_video_reader = new WMFVideoReader(false, /*just_read_audio=*/false, local_path, /*reader callback=*/NULL, gui_client->device_manager, /*decode_to_d3d_tex=*/false);
			gui_client->logMessage("WMFVideoReader: WMFVideoReader created successfully");

			// Get first frame to determine dimensions
			gui_client->logMessage("WMFVideoReader: Reading first frame...");
			const SampleInfoRef frameinfo = wmf_video_reader->getAndLockNextSample(/*just_get_vid_sample=*/true);
			if(frameinfo.isNull())
			{
				std::string error_msg = "Could not read first frame from MP4 file: " + local_path;
				gui_client->logMessage("WMFVideoReader error: " + error_msg);
				throw glare::Exception(error_msg);
			}
			gui_client->logMessage("WMFVideoReader: First frame read successfully, dimensions: " + toString((int)frameinfo->width) + "x" + toString((int)frameinfo->height));

			// Use actual video dimensions
			width = (int)frameinfo->width;
			height = (int)frameinfo->height;

			gui_client->setGLWidgetContextAsCurrent();

			// Create texture
			std::vector<uint8> data(width * height * 4); // Use a zeroed buffer to clear the texture.
			ob->opengl_engine_ob->materials[0].emission_texture = new OpenGLTexture(width, height, opengl_engine, data, OpenGLTextureFormat::Format_SRGBA_Uint8,
				GL_SRGB8_ALPHA8, // GL internal format
				GL_BGRA, // GL format.
				OpenGLTexture::Filtering_Bilinear);

			opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob);

			// Initialize playback time from first frame
			last_video_frame_time = frameinfo->frame_time;
			video_playback_time = frameinfo->frame_time;
			is_paused = false; // Start playing by default

			this->loaded_video_url = video_URL;
			this->state = State_WMFVideoReaderCreated;
			gui_client->logMessage("WMFVideoReader: Video player initialized successfully");
			return;
		}
		catch(glare::Exception& e)
		{
			// Log detailed error information
			std::string error_msg = "WMFVideoReader creation failed: " + e.what();
			gui_client->logMessage("WMFVideoReader error: " + error_msg);
			// Clean up if partially created
			if(wmf_video_reader.nonNull())
			{
				wmf_video_reader = NULL;
			}
			throw glare::Exception(error_msg);
		}
	}
#endif

	// Use CEF browser (original code)
	if(!cef_available)
	{
		std::string error_msg = "CEF could not be initialised and WMFVideoReader fallback is not available. ";
		error_msg += "is_mp4_file=" + toString(is_mp4_file) + ", device_manager=" + toString(gui_client->device_manager != NULL) + ", video_URL='" + video_URL + "'";
		throw glare::Exception(error_msg);
	}

	assert(browser.isNull());
	browser = NULL;

	gui_client->logMessage("Creating vid player browser, video_URL: " + video_URL);

	// Try and make page now, before we allocate a texture.
	const std::string root_page = makeEmbedHTMLForVideoURL(video_URL, width, height, ob, *gui_client->resource_manager, gui_client->server_hostname);

	gui_client->setGLWidgetContextAsCurrent(); // Make sure the correct context is current while making OpenGL calls.

	std::vector<uint8> data(width * height * 4); // Use a zeroed buffer to clear the texture.
	ob->opengl_engine_ob->materials[0].emission_texture = new OpenGLTexture(width, height, opengl_engine, data, OpenGLTextureFormat::Format_SRGBA_Uint8,
		GL_SRGB8_ALPHA8, // GL internal format
		GL_BGRA, // GL format.
		OpenGLTexture::Filtering_Bilinear);

	//ob->opengl_engine_ob->materials[0].fresnel_scale = 0; // Remove specular reflections, reduces washed-out look.

	opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob);

	browser = new EmbeddedBrowser();

	const std::string use_url = "https://localdomain/"; // URL to serve the root page

	browser->create(use_url, ob->opengl_engine_ob->materials[0].emission_texture, gui_client, ob, opengl_engine, root_page);

	this->loaded_video_url = video_URL;
	this->state = State_BrowserCreated;
}


#if EMSCRIPTEN


EM_JS(int, makeHTMLViewJS, (), {
	console.log("=================makeHTMLViewJS()================");
	let new_div = document.createElement('div');
	new_div.className = 'transformable-html-view';
	document.getElementById('iframe-container-camera').appendChild(new_div);

	// Make handle, insert into global handle->div map
	let handle = next_html_view_elem_handle;
	next_html_view_elem_handle++;
	html_view_elem_handle_to_div_map[handle] = new_div;

	return handle;
});


EM_JS(void, startLoadingYouTubeIFrameAPI, (), {
	// Load the YouTube IFrame Player API code asynchronously.  See https://developers.google.com/youtube/iframe_api_reference
	var tag = document.createElement('script');
	tag.src = "https://www.youtube.com/iframe_api";
	var firstScriptTag = document.getElementsByTagName('script')[0];
	firstScriptTag.parentNode.insertBefore(tag, firstScriptTag);
});


EM_JS(int, isYouTubeIFrameAPILoaded, (), {
	return youtube_iframe_API_ready;
});


EM_JS(int, makeYouTubeHTMLView, (const char* video_id, int autoplay, int loop, int muted), {
	console.log("=================makeYouTubeHTMLView()================");
	// console.log("video_id: " + UTF8ToString(video_id));
	// console.log("autoplay: " + autoplay);
	// console.log("loop: " + loop);
	// console.log("muted: " + muted);

	if(!youtube_iframe_API_ready)
		return -2;

	let video_id_str = UTF8ToString(video_id);

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
	youtube_iframe.src = "https://www.youtube.com/embed/" + video_id_str + "?enablejsapi=1&origin=" + window.location.protocol + "//" + window.location.host;
	youtube_iframe.width = '1024';
	youtube_iframe.height = '576';
	//youtube_iframe.frameborder = '0';
	youtube_iframe.id = 'transformable-html-view-youtube-' + handle.toString(); // Make unique ID
	youtube_iframe.setAttribute('credentialless', "");
	youtube_iframe.setAttribute('allow', "autoplay");
	new_div.appendChild(youtube_iframe);

	document.getElementById('iframe-container-camera').appendChild(new_div);

	// insert into global handle->div map
	html_view_elem_handle_to_div_map[handle] = new_div;
	
	let height = 1024;
	let width = 576;
	if(autoplay != 0)
		muted = 1; // Force muted=true if autoplaying, otherwise autoplay doesn't work.

	let player_params = {
		height: height,
		width: width,
		videoId: video_id_str,
		playerVars: {
			'playsinline': 1,
			'autoplay': autoplay,
			'loop':  loop,
			'playlist': video_id_str, // Set the playlist to the video ID.  This is needed for looping to work.
			'mute': (muted ? 1 : 0)
		},
		events: {
			'onReady': function(e) {
				//console.log("****************player onReady()****************");
				if(muted) { e.target.mute(); }  // If video should be muted, insert a call to mute the video here.
				e.target.playVideo();
			}
		}
	};

	//console.log("player_params: ");
	//console.log(player_params);

	var player = new YT.Player(youtube_iframe, player_params);

	//console.log("makeYouTubeHTMLView() done, returning handle " + handle.toString());
	return handle;
});


EM_JS(int, makeTwitchHTMLView, (const char* iframe_src_url), {
	console.log("=================makeTwitchHTMLView()================");
	console.log("video_id: " + UTF8ToString(iframe_src_url));

	
	let iframe_src_url_str = UTF8ToString(iframe_src_url);

	// Get handle
	let handle = next_html_view_elem_handle;
	next_html_view_elem_handle++;

	let new_div = document.createElement('div');
	new_div.className = 'transformable-html-view';
	new_div.id = 'transformable-html-view-' + handle.toString(); // Make unique ID

	// Make the iframe ourself so we can set credentialless attribute.
	let twitch_iframe = document.createElement('iframe');
	twitch_iframe.src = iframe_src_url_str;
	twitch_iframe.width = '1024';
	twitch_iframe.height = '576';
	twitch_iframe.frameborder = '0';
	twitch_iframe.id = 'transformable-html-view-twitch-' + handle.toString(); // Make unique ID
	twitch_iframe.setAttribute('credentialless', "");
	new_div.appendChild(twitch_iframe);

	document.getElementById('iframe-container-camera').appendChild(new_div);

	// insert into global handle->div map
	html_view_elem_handle_to_div_map[handle] = new_div;
	
	// console.log("makeTwitchHTMLView() done, returning handle " + handle.toString());
	return handle;
});


EM_JS(void, setHTMLElementCSSTransform, (int handle, const char* matrix_string), {
	//console.log("=================setHTMLElementCSSTransform()================");
	//console.log("handle: " + handle);
	//console.log("matrix_string: " + UTF8ToString(matrix_string));

	// Lookup div for handle
	let div = html_view_elem_handle_to_div_map[handle];
	console.assert(div, "div for handle is null");

	div.style.transform = UTF8ToString(matrix_string);

	let vw = document.getElementById('canvas').offsetWidth;
	// console.log("JS vw: " + vw.toString());
	let pers_str = ((0.025 / 0.035) * document.getElementById('canvas').offsetWidth).toString() + "px";
	// console.log("Setting perspective to " + pers_str);
	document.getElementById('iframe-container-camera').style.perspective = pers_str;
});


// Define getLocationHost2() function
EM_JS(char*, getLocationHost2, (), {
	return stringToNewUTF8(window.location.host);
});


EM_JS(int, makeHTMLVideoElement, (const char* http_URL, int autoplay, int loop, int muted), {
	console.log("=================makeHTMLVideoElement()================");
	console.log("http_URL: " + UTF8ToString(http_URL));

	let handle = next_html_view_elem_handle;
	next_html_view_elem_handle++;

	let video_elem = document.createElement('video');
	video_elem.src = UTF8ToString(http_URL);
	if(autoplay)
		video_elem.setAttribute('autoplay', ""); 
	if(loop)
		video_elem.setAttribute('loop', "");
	if(muted)
		video_elem.setAttribute('muted', "");

	// There's a bug in Chrome where the muted attribute doesn't work.  Work around it:  (see https://stackoverflow.com/questions/47638344/muted-autoplay-in-chrome-still-not-working)
	if(muted)
		video_elem.addEventListener("canplay", (event) => { video_elem.muted = true; });
	
	document.getElementById('background-div').appendChild(video_elem);

	html_view_elem_handle_to_div_map[handle] = video_elem;

	return handle;
});


// Should return 0 if the video metadata has not been loaded yet.
EM_JS(int, getVideoWidth, (int handle), {
	//console.log("=================getVideoWidth()================");

	console.assert(html_view_elem_handle_to_div_map[handle]);
	return html_view_elem_handle_to_div_map[handle].videoWidth;
});


// Should return 0 if the video metadata has not been loaded yet.
EM_JS(int, getVideoHeight, (int handle), {
	//console.log("=================getVideoHeight()================");

	console.assert(html_view_elem_handle_to_div_map[handle]);
	return html_view_elem_handle_to_div_map[handle].videoHeight;
});


EM_JS(void, updateOpenGLTexWithVideoElementFrame, (int tex_handle, int video_elem_handle), {
	//console.log("=================updateOpenGLTexWithVideoElementFrame()================");
	//console.log("tex_handle: " + tex_handle);
	//console.log("video_elem_handle: " + video_elem_handle);

	console.assert(html_view_elem_handle_to_div_map[video_elem_handle]);
	console.assert(GL.textures[tex_handle]);

	let video_elem = html_view_elem_handle_to_div_map[video_elem_handle];

	let width  = video_elem.videoWidth;
	let height = video_elem.videoHeight;

	let gl_tex = GL.textures[tex_handle];

	let gl = GLctx;

	gl.bindTexture(gl.TEXTURE_2D, gl_tex);

	gl.texSubImage2D(gl.TEXTURE_2D, /*level=*/0, /*xoffset=*/0, /*yoffset=*/0, width, height, /*format=*/gl.RGB, /*type=*/gl.UNSIGNED_BYTE, /*source=*/video_elem);	
	
	gl.bindTexture(gl.TEXTURE_2D, null); // unbind
});


#endif // end if EMSCRIPTEN


static bool started_loading_youtube_api = false;


void BrowserVidPlayer::process(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt)
{
	PERFORMANCEAPI_INSTRUMENT_FUNCTION();

	m_gui_client = gui_client;


	const double ob_dist_from_cam = ob->pos.getDist(gui_client->cam_controller.getPosition());
	const double max_play_dist = maxBrowserDist();
	[[maybe_unused]] const double unload_dist = maxBrowserDist() * 1.5;
	[[maybe_unused]] const bool in_process_dist = ob_dist_from_cam < max_play_dist;

	
#if EMSCRIPTEN
	if(in_process_dist)
	{
		if(html_view_handle < 0) // If HTML view not created yet:
		{
			const std::string& video_url = ob->materials[0]->emission_texture_url;

			const bool is_http_URL = hasPrefix(video_url, "http://") || hasPrefix(video_url, "https://");

			if(is_http_URL)
			{
				const URL parsed_URL = URL::parseURL(video_url);

				if(parsed_URL.host == "www.youtube.com" || parsed_URL.host == "youtu.be")
				{
					if(!started_loading_youtube_api)
					{
						startLoadingYouTubeIFrameAPI();
						started_loading_youtube_api = true;
					}

					if(isYouTubeIFrameAPILoaded())
					{
						std::string video_id;
						if(parsed_URL.host == "www.youtube.com")
						{
							// Parse query
							const std::map<std::string, std::string> params = URL::parseQuery(parsed_URL.query);
							const auto res = params.find("v"); // Find "v" param (video id)
							if(res != params.end())
								video_id = res->second;
							else
								throw glare::Exception("Could not find video id param (v) in YouTube URL.");
						}
						else
						{
							// Handle URLs of the form https://youtu.be/SaejB5NSzOs
							assert(parsed_URL.host == "youtu.be");
							video_id = eatSuffix(eatPrefix(parsed_URL.path, "/"), "/");
						}

						checkYouTubeVideoID(video_id); // Check video id so we don't need to worry about escaping it

						const bool autoplay = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_AUTOPLAY);
						const bool loop     = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_LOOP);
						const bool muted    = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_MUTED);

						conPrint("BrowserVidPlayer: Making new YouTube HTML View");
						html_view_handle = makeYouTubeHTMLView(video_id.c_str(), autoplay ? 1 : 0, loop ? 1 : 0, muted ? 1 : 0);
					}
					else
					{
						conPrint("waiting for YouTubeIFrameAPI ...");
					}
				}
				else if(parsed_URL.host == "www.twitch.tv")
				{
					std::string channel_name = parsed_URL.path;
					channel_name = eatPrefix(channel_name, "/");

					// Extract URL details to connect to from from webpage URL
					char* location_host_str = getLocationHost2();
					const std::string location_host(location_host_str);
					free(location_host_str);

					const bool autoplay = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_AUTOPLAY);
					const bool muted    = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_MUTED);
					const std::string iframe_src_url = "https://player.twitch.tv/?channel=" + web::Escaping::URLEscape(channel_name) + "&parent=" + location_host + "&autoplay=" + boolToString(autoplay) + "&muted=" + boolToString(muted);

					conPrint("BrowserVidPlayer: Making new Twitch HTML View");
					html_view_handle = makeTwitchHTMLView(iframe_src_url.c_str());
				}

				if(html_view_handle >= 0) // If loaded:
				{
					// Now that we are loading the iframe, draw this object with alpha zero.
					ob->opengl_engine_ob->materials[0].alpha = 0.f; // Set alpha to zero for alpha-cutout technique, to show web views in iframe under the opengl canvas.
					opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob);
				}

				using_iframe = true;
			}
			else // Else non-http: (e.g. mp4 resource)
			{
				const std::string video_http_URL = "/resource/" + ob->materials[0]->emission_texture_url;

				const bool autoplay = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_AUTOPLAY);
				const bool loop     = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_LOOP);
				const bool muted    = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_MUTED);

				// Make <video> element, append to page
				html_view_handle = makeHTMLVideoElement(video_http_URL.c_str(), autoplay ? 1 : 0, loop ? 1 : 0, muted ? 1 : 0);

				using_iframe = false;
			}
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
		if(using_iframe)
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
		else
		{
			// if we haven't created the video texture yet:
			if(!ob->opengl_engine_ob->materials[0].emission_texture)
			{
				const int video_width = getVideoWidth(html_view_handle);
				if(video_width > 0) // if the video metadata has been loaded:
				{
					const int video_height = getVideoHeight(html_view_handle);

					// conPrint("Creating OpenGL texture for vid with dimensions " + toString(video_width) + " x " + toString(video_height) + "...");

					// Although using sRGB internal formats does work, it's very slow.  Likely the nonlinear to linear conversion is done single-threaded on the CPU in the driver or ANGLE.
					// So just use a RGB format and do the conversion in the phong shader with convert_albedo_from_srgb = true.
					//
					// Using a with-alpha (Format_RGBA_Linear_Uint8, GL_RGBA8, GL_RGBA) format here also works and seems just as fast.
					ob->opengl_engine_ob->materials[0].emission_texture = new OpenGLTexture(video_width, video_height, opengl_engine, ArrayRef<uint8>(), OpenGLTextureFormat::Format_RGB_Linear_Uint8,
						GL_RGB8, // GL internal format
						GL_RGB, // GL format.
						OpenGLTexture::Filtering_Bilinear);

					ob->opengl_engine_ob->materials[0].convert_albedo_from_srgb = true;
					opengl_engine->objectMaterialsUpdated(*ob->opengl_engine_ob);
				}
			}

			if(ob->opengl_engine_ob->materials[0].emission_texture)
			{
				const uint32 tex_handle = ob->opengl_engine_ob->materials[0].emission_texture->texture_handle;
				updateOpenGLTexWithVideoElementFrame(tex_handle, html_view_handle);
			}
		}
	}
#endif // end if EMSCRIPTEN

	// Handle WMFVideoReader and CEF browser (both need in_process_dist check)
	if(in_process_dist)
	{
		// Check if URL has changed and we need to recreate the browser/player
		if(!ob->materials.empty())
		{
			const std::string current_video_URL = toStdString(ob->materials[0]->emission_texture_url);
			if(!current_video_URL.empty() && current_video_URL != this->loaded_video_url)
			{
				// URL has changed, need to recreate
#if CEF_SUPPORT
				if(browser.nonNull())
				{
					browser = NULL;
				}
#endif
#ifdef _WIN32
				if(wmf_video_reader.nonNull())
				{
					wmf_video_reader = NULL;
				}
#endif
				this->state = State_Unloaded;
				this->loaded_video_url = ""; // Reset to force recreation
			}
		}

		if(state == State_Unloaded)
		{
			// Only try to create browser/player if URL is not empty
			if(!ob->materials.empty())
			{
				const std::string video_URL = toStdString(ob->materials[0]->emission_texture_url);
				if(!video_URL.empty())
				{
					try
					{
						createNewBrowserPlayer(gui_client, opengl_engine, ob);
						// State is set in createNewBrowserPlayer (State_BrowserCreated or State_WMFVideoReaderCreated)
					}
					catch(glare::Exception& e)
					{
						conPrint("Error occured while created browser video player: " + e.what());
						gui_client->print("Error occured while created browser video player: " + e.what());
						this->state = State_ErrorOccurred;
					}
				}
			}
		}
		else if(state == State_ErrorOccurred)
		{
			// If we're in error state, but URL is now valid, try again
			if(!ob->materials.empty())
			{
				const std::string video_URL = toStdString(ob->materials[0]->emission_texture_url);
				if(!video_URL.empty() && video_URL != this->loaded_video_url)
				{
					this->state = State_Unloaded; // Reset state to try again
				}
			}
		}
		
#if CEF_SUPPORT
		// While the webview is not visible by the camera, we discard any dirty-rect updates.  
		// When the webview becomes visible again, if there were any discarded dirty rects, then we know our buffer is out of date.
		// So invalidate the whole buffer.
		// Note that CEF only allows us to invalidate the whole buffer - see https://magpcss.org/ceforum/viewtopic.php?f=6&t=15079
		// If we could invalidate part of it, then we can maintain the actual discarded dirty rectangles (or a bounding rectangle around them)
		// and use that to just invalidate the dirty part.
		if(browser.nonNull())
		{
			const bool ob_visible = opengl_engine->isObjectInCameraFrustum(*ob->opengl_engine_ob);
			if(ob_visible && !previous_is_visible) // If webview just became visible:
			{
				//conPrint("Browser became visible!");
				browser->browserBecameVisible();
			}

			previous_is_visible = ob_visible;
		}
#endif

#ifdef _WIN32
		// Handle WMFVideoReader playback (fallback for MP4 when CEF is not available)
		if(state == State_WMFVideoReaderCreated && wmf_video_reader.nonNull())
		{
			// Update playback time only if not paused
			if(!is_paused)
				video_playback_time += dt;

			// Get next frame if available (using synchronous mode) - only if not paused
			if(!is_paused && wmf_video_reader.nonNull() && ob->opengl_engine_ob.nonNull() && ob->opengl_engine_ob->materials[0].emission_texture.nonNull())
			{
				try
				{
					const SampleInfoRef frameinfo = wmf_video_reader->getAndLockNextSample(/*just_get_vid_sample=*/true);
					if(frameinfo.nonNull() && frameinfo->frame_buffer != NULL)
					{
						// Check if we should display this frame (based on frame time)
						// Always display frame if it's the first one or if playback time has advanced
						if(last_video_frame_time == 0 || frameinfo->frame_time <= video_playback_time)
						{
							// Update texture with frame data
							gui_client->setGLWidgetContextAsCurrent();

							const uint8* frame_data = frameinfo->frame_buffer;
							const int frame_width = (int)frameinfo->width;
							const int frame_height = (int)frameinfo->height;
							const int stride = (int)frameinfo->stride_B;

							// WMFVideoReader provides frames in BGRA format (MFVideoFormat_RGB32)
							if(stride > 0 && frame_data != NULL && frame_width > 0 && frame_height > 0)
							{
								// Copy frame data to texture
								const size_t data_size = frame_height * stride;
								ArrayRef<uint8> frame_array(const_cast<uint8*>(frame_data), data_size);

								ob->opengl_engine_ob->materials[0].emission_texture->loadIntoExistingTexture(
									/*mip level=*/0,
									frame_width,
									frame_height,
									stride,
									frame_array,
									/*bind_needed=*/true
								);

								last_video_frame_time = frameinfo->frame_time;
							}
						}
					}
					else if(frameinfo.nonNull() && frameinfo->frame_buffer == NULL)
					{
						// End of stream - loop if needed
						const bool loop = BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_LOOP);
						if(loop)
						{
							video_playback_time = 0;
							last_video_frame_time = 0;
							wmf_video_reader->seek(0);
						}
						else
						{
							// Video finished, keep showing last frame
						}
					}
				}
				catch(glare::Exception& e)
				{
					// Log error but don't crash - video might be corrupted or still loading
					static Timer error_timer;
					if(error_timer.elapsed() > 1.0) // Log at most once per second
					{
						gui_client->logMessage("WMFVideoReader error: " + e.what());
						error_timer.reset();
					}
				}
			}
		}
#endif // _WIN32
	} // end if(in_process_dist)
	else // else if !in_process_dist:
	{
#if CEF_SUPPORT
		// If there is a browser, and it is past the unload distance, destroy it.
		if((state == State_BrowserCreated) && (ob_dist_from_cam >= unload_dist))
		{
			if(browser.nonNull())
			{
				if(!ob->materials.empty())
				{
					const auto& video_URL = ob->materials[0]->emission_texture_url;
					gui_client->logMessage("Closing vid player browser (out of view distance), video_URL: " + std::string(video_URL));
				}
				browser = NULL;

				// Remove audio source
				if(ob->audio_source.nonNull())
				{
					gui_client->audio_engine.removeSource(ob->audio_source);
					ob->audio_source = NULL;
				}
			}
			this->state = State_Unloaded;
		}
#endif
#ifdef _WIN32
		// If using WMFVideoReader, and it is past the unload distance, destroy it.
		if((state == State_WMFVideoReaderCreated) && (ob_dist_from_cam >= unload_dist))
		{
			if(wmf_video_reader.nonNull())
			{
				if(!ob->materials.empty())
				{
					const auto& video_URL = ob->materials[0]->emission_texture_url;
					gui_client->logMessage("Closing WMFVideoReader (out of view distance), video_URL: " + std::string(video_URL));
				}
				wmf_video_reader = NULL;
			}
			this->state = State_Unloaded;
		}
#endif
	}
}


void BrowserVidPlayer::videoURLMayHaveChanged(GUIClient* gui_client, OpenGLEngine* opengl_engine, WorldObject* ob)
{
	if(ob->materials.empty())
		return;

	const std::string video_URL = toStdString(ob->materials[0]->emission_texture_url);
	if(video_URL != this->loaded_video_url)
	{
		if(browser.isNull())
		{
			try
			{
				createNewBrowserPlayer(gui_client, opengl_engine, ob);
				this->state = State_BrowserCreated;
			}
			catch(glare::Exception& e)
			{
				conPrint("Error occured while created browser video player: " + e.what());
				gui_client->print("Error occured while created browser video player: " + e.what());
				this->state = State_ErrorOccurred;
			}
		}
		else
		{
			int width, height;
			getVidTextureDimensions(video_URL, ob, width, height);

			try
			{
				const std::string root_page = makeEmbedHTMLForVideoURL(video_URL, width, height, ob, *gui_client->resource_manager, gui_client->server_hostname);

				browser->updateRootPage(root_page);

				const std::string use_url = "https://localdomain/"; // URL to serve the root page

				browser->navigate(use_url);

				this->loaded_video_url = video_URL;
			}
			catch(glare::Exception& e)
			{
				conPrint("Error occured while created browser video player: " + e.what());
				gui_client->print("Error occured while created browser video player: " + e.what());
				this->state = State_ErrorOccurred;
			}
		}
	}
}


void BrowserVidPlayer::mouseReleased(MouseEvent* e, const Vec2f& uv_coords)
{
	//conPrint("BrowserVidPlayer mouseReleased()");

	if(browser.nonNull())
		browser->mouseReleased(e, uv_coords);
}


void BrowserVidPlayer::mousePressed(MouseEvent* e, const Vec2f& uv_coords)
{
	//conPrint("BrowserVidPlayer mousePressed(), uv_coords: " + uv_coords.toString());

#ifdef _WIN32
	// Handle pause/play toggle for WMFVideoReader
	if(state == State_WMFVideoReaderCreated && wmf_video_reader.nonNull())
	{
		// Toggle pause state
		is_paused = !is_paused;
		if(m_gui_client)
		{
			m_gui_client->logMessage("Video " + std::string(is_paused ? "paused" : "resumed"));
		}
		return; // Don't pass to browser
	}
#endif

	if(browser.nonNull())
		browser->mousePressed(e, uv_coords);

#if EMSCRIPTEN
	if(using_iframe)
	{
		if(m_gui_client)
			m_gui_client->showInfoNotification("Interacting with video player");

		EM_ASM({
			document.getElementById('background-div').style.zIndex = 100; // move background to front

			//console.log("Clearing previous timer:");
			//console.log(move_background_timer_id);

			//clearTimeout(move_background_timer_id);

			move_background_timer_id = setTimeout(function() {
				//canvas.style.pointerEvents = "auto";
				document.getElementById('background-div').style.zIndex = -1; // move background to back
			}, 5000);
		});
	}
#endif
}


void BrowserVidPlayer::mouseDoubleClicked(MouseEvent* e, const Vec2f& uv_coords)
{
	//conPrint("BrowserVidPlayer mouseDoubleClicked()");
}


void BrowserVidPlayer::mouseMoved(MouseEvent* e, const Vec2f& uv_coords)
{
	//conPrint("BrowserVidPlayer mouseMoved(), uv_coords: " + uv_coords.toString());

#ifdef _WIN32
	// Show controls when mouse is over video (for WMFVideoReader)
	if(state == State_WMFVideoReaderCreated && wmf_video_reader.nonNull())
	{
		show_controls = true;
		controls_hide_time = Clock::getCurTimeRealSec() + 3.0; // Hide controls after 3 seconds of no mouse movement
		return; // Don't pass to browser
	}
#endif

	if(browser.nonNull())
		browser->mouseMoved(e, uv_coords);
}


void BrowserVidPlayer::wheelEvent(MouseWheelEvent* e, const Vec2f& uv_coords)
{
	if(browser.nonNull())
		browser->wheelEvent(e, uv_coords);
}


void BrowserVidPlayer::keyPressed(KeyEvent* e)
{
	if(browser.nonNull())
		browser->keyPressed(e);
}


void BrowserVidPlayer::keyReleased(KeyEvent* e)
{
	if(browser.nonNull())
		browser->keyReleased(e);
}

