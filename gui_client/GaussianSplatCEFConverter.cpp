/*=====================================================================
GaussianSplatCEFConverter.cpp
=====================================================================*/
#include "GaussianSplatCEFConverter.h"

#include "CEF.h"
#include "CEFInternal.h"

#include <utils/BufferInStream.h>
#include <utils/Exception.h>
#include <utils/FileInStream.h>
#include <utils/FileUtils.h>
#include <utils/Lock.h>
#include <utils/Mutex.h>
#include <utils/PlatformUtils.h>
#include <utils/RandomAccessInStream.h>
#include <utils/Reference.h>
#include <utils/StringUtils.h>
#include <utils/ThreadSafeRefCounted.h>
#include <utils/Timer.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

#if CEF_SUPPORT
#include <cef_app.h>
#include <cef_browser.h>
#include <cef_client.h>
#include <cef_download_handler.h>
#include <cef_parser.h>
#include <cef_render_handler.h>
#include <cef_request_handler.h>
#include <cef_resource_handler.h>
#include <wrapper/cef_helpers.h>
#endif

#if CEF_SUPPORT
namespace
{
static const char* const VIRTUAL_ORIGIN = "https://metasiberia-splat-converter";
static const char* const OUTPUT_FILENAME = "metasiberia-runtime.ply";


static bool isVirtualOriginURL(const std::string& url)
{
	const std::string origin(VIRTUAL_ORIGIN);
	return url == origin || hasPrefix(url, origin + "/");
}


static bool endsWithCaseInsensitive(const std::string& value, const std::string& suffix)
{
	if(value.size() < suffix.size())
		return false;
	const size_t offset = value.size() - suffix.size();
	for(size_t i=0; i<suffix.size(); ++i)
		if(std::tolower((unsigned char)value[offset + i]) != std::tolower((unsigned char)suffix[i]))
			return false;
	return true;
}


static std::string normaliseVirtualPath(const std::string& path)
{
	std::string result = path;
	std::replace(result.begin(), result.end(), '\\', '/');
	while(!result.empty() && result[0] == '/')
		result.erase(result.begin());

	if(result.empty() || !FileUtils::isPathSafe(result))
		throw glare::Exception("Unsafe Gaussian splat virtual resource path: '" + path + "'.");
	return result;
}


static bool pathHasDirectoryPrefix(const std::string& canonical_path, const std::string& canonical_dir)
{
	std::string path = FileUtils::toPlatformSlashes(canonical_path);
	std::string dir = FileUtils::toPlatformSlashes(canonical_dir);
	while(!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
		dir.pop_back();

#if defined(_WIN32)
	path = toLowerCase(path);
	dir = toLowerCase(dir);
#endif

	if(path.size() <= dir.size() || path.compare(0, dir.size(), dir) != 0)
		return false;

	const char separator = path[dir.size()];
	return separator == '/' || separator == '\\';
}


static std::string makeRootPage(const std::string& main_virtual_filename)
{
	// main_virtual_filename has already passed FileUtils::isPathSafe.  Pass it
	// through JSON escaping rather than interpolating it as markup.
	std::string escaped;
	escaped.reserve(main_virtual_filename.size() + 8);
	for(size_t i=0; i<main_virtual_filename.size(); ++i)
	{
		const unsigned char c = (unsigned char)main_virtual_filename[i];
		switch(c)
		{
		case '\\': escaped += "\\\\"; break;
		case '"':  escaped += "\\\""; break;
		case '\n': escaped += "\\n";  break;
		case '\r': escaped += "\\r";  break;
		case '\t': escaped += "\\t";  break;
		default:
			if(c < 0x20)
				escaped += '_';
			else
				escaped += (char)c;
			break;
		}
	}

	return
		"<!doctype html><meta charset=\"utf-8\"><title>METASIBERIA_GS_STARTING</title>"
		"<script src=\"/files/gaussian_splat_converter.js\"></script>"
		"<script>"
		"(async()=>{"
			"const filename=\"" + escaped + "\";"
			"const inputUrl=new URL('/input/'+encodeURIComponent(filename),location.href).href;"
			"const report=p=>{"
				"const stage=(p&&p.stage)||'working';"
				"const value=(p&&Number.isFinite(p.value))?p.value:0;"
				"document.title='METASIBERIA_GS_PROGRESS:'+stage+':'+value;"
			"};"
			"try{"
				"const c=window.MetasiberiaGaussianSplatConverter||window.MetasiberiaSplatConverter;"
				"if(!c)throw new Error('Bundled Gaussian converter did not initialise.');"
				"let converted;"
				"if(typeof c.convertUrlToPlyBytes==='function'){"
					"converted=await c.convertUrlToPlyBytes(inputUrl,report);"
				"}else if(typeof c.convertFileToPlyBytes==='function'){"
					"const response=await fetch(inputUrl);"
					"if(!response.ok)throw new Error('Input HTTP '+response.status);"
					"const bytes=await response.arrayBuffer();"
					"converted=await c.convertFileToPlyBytes(bytes,filename,report);"
				"}else if(typeof c.convertUrlToPlyUrl==='function'){"
					"converted=await c.convertUrlToPlyUrl(inputUrl,report);"
				"}else{"
					"throw new Error('Bundled converter lacks the native PLY API.');"
				"}"
				"let outputUrl;"
				"if(typeof converted==='string')outputUrl=converted;"
				"else if(converted&&converted.url)outputUrl=converted.url;"
				"else if(converted&&converted.bytes)"
					"outputUrl=URL.createObjectURL(new Blob([converted.bytes],{type:'application/octet-stream'}));"
				"else if(converted instanceof Uint8Array||converted instanceof ArrayBuffer)"
					"outputUrl=URL.createObjectURL(new Blob([converted],{type:'application/octet-stream'}));"
				"else throw new Error('Converter returned no PLY payload.');"
				"document.title='METASIBERIA_GS_DOWNLOADING';"
				"const link=document.createElement('a');"
				"link.href=outputUrl;"
				"link.download='" + std::string(OUTPUT_FILENAME) + "';"
				"link.style.display='none';"
				"document.body.appendChild(link);"
				"link.click();"
			"}catch(error){"
				"const message=(error&&error.message)?error.message:String(error);"
				"document.title='METASIBERIA_GS_ERROR:'+message.substring(0,1000);"
			"}"
		"})();"
		"</script>";
}
}
#endif


GaussianSplatCEFConverter::Config::Config()
:	allow_input_directory_sidecars(true),
	max_output_bytes(1024ULL * 1024ULL * 1024ULL),
	timeout_s(15.0 * 60.0)
{}


#if CEF_SUPPORT

namespace
{
struct ConverterState : public ThreadSafeRefCounted
{
	ConverterState()
	:	state(GaussianSplatCEFConverter::State_Idle), progress_value(0.0), download_started(false)
	{}

	Mutex mutex;
	GaussianSplatCEFConverter::Config config;
	GaussianSplatCEFConverter::State state;
	std::string error_message;
	std::string progress_stage;
	double progress_value;
	std::string canonical_input_path;
	std::string canonical_input_dir;
	std::string main_virtual_filename;
	std::map<std::string, std::string> explicit_files;
	std::string root_page;
	Timer timer;
	bool download_started;
};

typedef Reference<ConverterState> ConverterStateRef;


static void setFailed(const ConverterStateRef& state, const std::string& message)
{
	Lock lock(state->mutex);
	if(state->state == GaussianSplatCEFConverter::State_Running)
	{
		state->state = GaussianSplatCEFConverter::State_Failed;
		state->error_message = message;
	}
}


static std::string decodedRequestPath(CefRefPtr<CefRequest> request)
{
	CefURLParts parts;
	if(!CefParseURL(request->GetURL(), parts))
		return std::string();

	const std::string encoded_path = CefString(&parts.path).ToString();
	const cef_uri_unescape_rule_t rules = (cef_uri_unescape_rule_t)(
		UU_NORMAL | UU_SPACES | UU_URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS);
	return CefURIDecode(encoded_path, true, rules).ToString();
}


static std::string mimeTypeForPath(const std::string& path)
{
	if(endsWithCaseInsensitive(path, ".html")) return "text/html";
	if(endsWithCaseInsensitive(path, ".js"))   return "application/javascript";
	if(endsWithCaseInsensitive(path, ".wasm")) return "application/wasm";
	if(endsWithCaseInsensitive(path, ".json")) return "application/json";
	if(endsWithCaseInsensitive(path, ".webp")) return "image/webp";
	return "application/octet-stream";
}


class ConverterResourceHandler : public CefResourceHandler
{
public:
	ConverterResourceHandler(const ConverterStateRef& state_)
	:	state(state_), stream(nullptr), total_size(0), range_start(0), range_end(0),
		remaining(0), partial(false), invalid_range(false)
	{}

	~ConverterResourceHandler() { delete stream; }

	bool Open(CefRefPtr<CefRequest> request, bool& handle_request, CefRefPtr<CefCallback>) override
	{
		handle_request = true;
		try
		{
			if(!isVirtualOriginURL(request->GetURL().ToString()))
				return false;

			const std::string path = decodedRequestPath(request);
			if(path == "/" || path == "/index.html")
			{
				stream = new BufferInStream(state->root_page);
				mime_type = "text/html";
			}
			else if(path == "/files/gaussian_splat_converter.js")
			{
				stream = new FileInStream(state->config.converter_script_path);
				mime_type = "application/javascript";
			}
			else if(path == "/files/webp.wasm")
			{
				stream = new FileInStream(state->config.webp_wasm_path);
				mime_type = "application/wasm";
			}
			else if(hasPrefix(path, "/input/"))
			{
				const std::string virtual_path = normaliseVirtualPath(path.substr(7));
				std::string resolved_path;

				const std::map<std::string, std::string>::const_iterator explicit_it =
					state->explicit_files.find(virtual_path);
				if(explicit_it != state->explicit_files.end())
					resolved_path = explicit_it->second;
				else if(state->config.allow_input_directory_sidecars)
				{
					const std::string candidate = FileUtils::join(state->canonical_input_dir, virtual_path);
					if(!FileUtils::fileExists(candidate) || FileUtils::isDirectory(candidate))
						return false;
					const std::string canonical_candidate = FileUtils::getCanonicalPath(candidate);
					if(!pathHasDirectoryPrefix(canonical_candidate, state->canonical_input_dir))
						return false;
					resolved_path = canonical_candidate;
				}
				else
					return false;

				stream = new FileInStream(resolved_path);
				mime_type = mimeTypeForPath(virtual_path);
			}
			else
				return false;

			initResponse();
			prepareRange(request);
			return true;
		}
		catch(glare::Exception& e)
		{
			setFailed(state, "Failed to serve Gaussian conversion resource: " + e.what());
			return false;
		}
	}

	void GetResponseHeaders(CefRefPtr<CefResponse> response, int64& response_length, CefString&) override
	{
		response->SetMimeType(mime_type);
		response->SetHeaderByName("Accept-Ranges", "bytes", true);
		response->SetHeaderByName("Cache-Control", "no-store", true);

		if(invalid_range)
		{
			response->SetStatus(416);
			response->SetHeaderByName("Content-Range", "bytes */" + toString(total_size), true);
			response_length = 0;
			return;
		}

		if(partial)
		{
			response->SetStatus(206);
			response->SetHeaderByName(
				"Content-Range",
				"bytes " + toString(range_start) + "-" + toString(range_end) + "/" + toString(total_size),
				true);
		}
		response_length = (int64)remaining;
	}

	bool Skip(int64 bytes_to_skip, int64& bytes_skipped, CefRefPtr<CefResourceSkipCallback>) override
	{
		if(!stream || invalid_range || bytes_to_skip < 0)
		{
			bytes_skipped = -2;
			return false;
		}

		const size_t amount = myMin((size_t)bytes_to_skip, remaining);
		try
		{
			stream->advanceReadIndex(amount);
			remaining -= amount;
			bytes_skipped = (int64)amount;
			return true;
		}
		catch(glare::Exception&)
		{
			bytes_skipped = -2;
			return false;
		}
	}

	bool Read(void* data_out, int bytes_to_read, int& bytes_read, CefRefPtr<CefResourceReadCallback>) override
	{
		if(!stream || invalid_range || remaining == 0)
		{
			bytes_read = 0;
			return false;
		}

		try
		{
			const size_t amount = myMin((size_t)bytes_to_read, remaining);
			stream->readData(data_out, amount);
			remaining -= amount;
			bytes_read = (int)amount;
			return true;
		}
		catch(glare::Exception&)
		{
			bytes_read = -2;
			return false;
		}
	}

	void Cancel() override {}

private:
	static std::string headerValue(CefRefPtr<CefRequest> request, const char* name)
	{
		CefRequest::HeaderMap headers;
		request->GetHeaderMap(headers);
		for(CefRequest::HeaderMap::const_iterator it=headers.begin(); it!=headers.end(); ++it)
			if(toLowerCase(it->first.ToString()) == toLowerCase(std::string(name)))
				return it->second.ToString();
		return std::string();
	}

	void initResponse()
	{
		total_size = stream ? stream->size() : 0;
		range_start = 0;
		range_end = total_size > 0 ? total_size - 1 : 0;
		remaining = total_size;
	}

	void prepareRange(CefRefPtr<CefRequest> request)
	{
		const std::string range = headerValue(request, "Range");
		if(range.empty() || !hasPrefix(toLowerCase(range), "bytes="))
			return;

		const std::string spec = range.substr(6);
		const size_t comma = spec.find(',');
		const std::string first_spec = spec.substr(0, comma);
		const size_t dash = first_spec.find('-');
		if(dash == std::string::npos || total_size == 0)
		{
			invalid_range = true;
			remaining = 0;
			return;
		}

		try
		{
			const std::string start_text = first_spec.substr(0, dash);
			const std::string end_text = first_spec.substr(dash + 1);
			uint64 start = 0;
			uint64 end = total_size - 1;

			if(start_text.empty())
			{
				const uint64 suffix = stringToUInt64(end_text);
				if(suffix == 0)
					throw glare::Exception("Invalid suffix range.");
				start = suffix >= total_size ? 0 : total_size - suffix;
			}
			else
			{
				start = stringToUInt64(start_text);
				if(!end_text.empty())
					end = stringToUInt64(end_text);
			}

			if(start >= total_size || end < start)
				throw glare::Exception("Invalid byte range.");
			end = myMin(end, (uint64)total_size - 1);

			stream->setReadIndex((size_t)start);
			range_start = (size_t)start;
			range_end = (size_t)end;
			remaining = (size_t)(end - start + 1);
			partial = true;
		}
		catch(glare::Exception&)
		{
			invalid_range = true;
			remaining = 0;
		}
	}

	ConverterStateRef state;
	RandomAccessInStream* stream;
	std::string mime_type;
	size_t total_size;
	size_t range_start;
	size_t range_end;
	size_t remaining;
	bool partial;
	bool invalid_range;

	IMPLEMENT_REFCOUNTING(ConverterResourceHandler);
};


class NullRenderHandler : public CefRenderHandler
{
public:
	void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override
	{
		rect = CefRect(0, 0, 1, 1);
	}

	void OnPaint(CefRefPtr<CefBrowser>, PaintElementType, const RectList&, const void*, int, int) override {}

	IMPLEMENT_REFCOUNTING(NullRenderHandler);
};


class ConverterClient : public CefClient,
	public CefRequestHandler,
	public CefResourceRequestHandler,
	public CefDisplayHandler,
	public CefLoadHandler,
	public CefDownloadHandler
{
public:
	ConverterClient(const ConverterStateRef& state_)
	:	state(state_), render_handler(new NullRenderHandler())
	{}

	CefRefPtr<CefRenderHandler> GetRenderHandler() override { return render_handler; }
	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return CEF::getLifespanHandler(); }
	CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
	CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
	CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
	CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }

	CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
		CefRefPtr<CefBrowser>,
		CefRefPtr<CefFrame>,
		CefRefPtr<CefRequest> request,
		bool,
		bool,
		const CefString&,
		bool& disable_default_handling) override
	{
		const std::string url = request->GetURL().ToString();
		if(isVirtualOriginURL(url))
		{
			disable_default_handling = true;
			return this;
		}
		if(hasPrefix(url, "blob:"))
			return nullptr;

		// Fail every other network/subresource request closed.  The converter
		// is self-hosted and should never need ambient internet access.
		disable_default_handling = true;
		return this;
	}

	CefRefPtr<CefResourceHandler> GetResourceHandler(
		CefRefPtr<CefBrowser>,
		CefRefPtr<CefFrame>,
		CefRefPtr<CefRequest>) override
	{
		return new ConverterResourceHandler(state);
	}

	bool OnBeforeBrowse(
		CefRefPtr<CefBrowser>,
		CefRefPtr<CefFrame>,
		CefRefPtr<CefRequest> request,
		bool,
		bool) override
	{
		const std::string url = request->GetURL().ToString();
		// Conversion should never navigate off its closed virtual origin.
		return !isVirtualOriginURL(url) && !hasPrefix(url, "blob:");
	}

	void OnLoadError(
		CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		ErrorCode error_code,
		const CefString& error_text,
		const CefString&) override
	{
		if(frame->IsMain() && error_code != ERR_ABORTED)
		{
			setFailed(state, "Hidden Gaussian converter failed to load: " + error_text.ToString());
			closeBrowser(browser);
		}
	}

	void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override
	{
		const std::string value = title.ToString();
		const std::string error_prefix = "METASIBERIA_GS_ERROR:";
		const std::string progress_prefix = "METASIBERIA_GS_PROGRESS:";
		if(hasPrefix(value, error_prefix))
		{
			setFailed(state, "Gaussian conversion failed: " + value.substr(error_prefix.size()));
			closeBrowser(browser);
		}
		else if(hasPrefix(value, progress_prefix))
		{
			const std::string progress = value.substr(progress_prefix.size());
			const size_t separator = progress.rfind(':');
			if(separator != std::string::npos)
			{
				try
				{
					const double parsed_value = stringToDouble(progress.substr(separator + 1));
					Lock lock(state->mutex);
					state->progress_stage = progress.substr(0, separator);
					state->progress_value = myClamp(parsed_value, 0.0, 1.0);
				}
				catch(glare::Exception&)
				{}
			}
		}
	}

	bool CanDownload(CefRefPtr<CefBrowser>, const CefString& url, const CefString&) override
	{
		return hasPrefix(url.ToString(), "blob:");
	}

	bool OnBeforeDownload(
		CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefDownloadItem>,
		const CefString& suggested_name,
		CefRefPtr<CefBeforeDownloadCallback> callback) override
	{
		CEF_REQUIRE_UI_THREAD();
		if(suggested_name.ToString() != OUTPUT_FILENAME)
		{
			setFailed(state, "Hidden Gaussian converter attempted an unexpected download.");
			closeBrowser(browser);
			return false;
		}

		{
			Lock lock(state->mutex);
			state->download_started = true;
		}
		callback->Continue(state->config.output_path, false);
		return true;
	}

	void OnDownloadUpdated(
		CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefDownloadItem> item,
		CefRefPtr<CefDownloadItemCallback>) override
	{
		CEF_REQUIRE_UI_THREAD();
		if(!item || !item->IsValid())
			return;

		if(item->IsComplete())
		{
			try
			{
				const uint64 file_size = FileUtils::getFileSize(state->config.output_path);
				if(file_size == 0 || file_size > state->config.max_output_bytes)
					throw glare::Exception(
						"Converted PLY size " + toString(file_size) +
						" bytes is outside the configured limit.");

				FileInStream converted_file(state->config.output_path);
				const size_t header_size = myMin((size_t)file_size, (size_t)(1024 * 1024));
				std::vector<unsigned char> header_bytes(header_size);
				converted_file.readData(header_bytes.data(), header_bytes.size());
				const std::string header((const char*)header_bytes.data(), header_bytes.size());
				if(!hasPrefix(header, "ply") ||
					header.find("format binary_little_endian 1.0") == std::string::npos ||
					header.find("element vertex ") == std::string::npos ||
					header.find("end_header") == std::string::npos)
					throw glare::Exception("Converter output is not a standard binary little-endian PLY.");

				{
					Lock lock(state->mutex);
					if(state->state == GaussianSplatCEFConverter::State_Running)
						state->state = GaussianSplatCEFConverter::State_Succeeded;
				}
			}
			catch(glare::Exception& e)
			{
				setFailed(state, "Failed to read converted Gaussian PLY: " + e.what());
			}
			closeBrowser(browser);
		}
		else if(item->IsCanceled() || item->IsInterrupted())
		{
			setFailed(state, "Gaussian PLY download was cancelled or interrupted (reason " +
				toString((int)item->GetInterruptReason()) + ").");
			closeBrowser(browser);
		}
	}

private:
	static void closeBrowser(CefRefPtr<CefBrowser> browser)
	{
		if(browser && browser->GetHost())
			browser->GetHost()->CloseBrowser(true);
	}

	ConverterStateRef state;
	CefRefPtr<NullRenderHandler> render_handler;

	IMPLEMENT_REFCOUNTING(ConverterClient);
};
}


class GaussianSplatCEFConverter::Impl
{
public:
	~Impl()
	{
		close();
	}

	void close()
	{
		if(browser && browser->GetHost())
			browser->GetHost()->CloseBrowser(true);
		browser = nullptr;
		client = nullptr;
	}

	ConverterStateRef state;
	CefRefPtr<ConverterClient> client;
	CefRefPtr<CefBrowser> browser;
};

#else

class GaussianSplatCEFConverter::Impl
{
public:
	Impl() : state(GaussianSplatCEFConverter::State_Idle) {}

	State state;
	std::string error_message;
	std::string progress_stage;
	double progress_value = 0.0;
	std::string output_path;
};

#endif


GaussianSplatCEFConverter::GaussianSplatCEFConverter()
:	impl(new Impl())
{}


GaussianSplatCEFConverter::~GaussianSplatCEFConverter()
{
	delete impl;
}


void GaussianSplatCEFConverter::start(const Config& config)
{
#if CEF_SUPPORT
	if(impl->state && impl->state->state == State_Running)
		throw glare::Exception("Gaussian splat conversion is already running.");
	if(!CEF::isInitialised())
		throw glare::Exception("Gaussian splat conversion requires an initialised embedded CEF runtime.");
	if(config.input_path.empty() || !FileUtils::fileExists(config.input_path) || FileUtils::isDirectory(config.input_path))
		throw glare::Exception("Gaussian splat input file does not exist.");
	if(config.converter_script_path.empty() || !FileUtils::fileExists(config.converter_script_path))
		throw glare::Exception("Bundled Gaussian converter JavaScript is missing.");
	if(config.webp_wasm_path.empty() || !FileUtils::fileExists(config.webp_wasm_path))
		throw glare::Exception("Bundled Gaussian WebP codec is missing.");
	if(config.output_path.empty() || !endsWithCaseInsensitive(config.output_path, ".ply"))
		throw glare::Exception("Gaussian conversion output path must end in .ply.");
	if(FileUtils::fileExists(config.output_path))
		throw glare::Exception("Gaussian conversion output path already exists; use a new temporary path.");
	if(config.max_output_bytes == 0)
		throw glare::Exception("Gaussian conversion output byte limit must be non-zero.");
	if(config.timeout_s <= 0)
		throw glare::Exception("Gaussian conversion timeout must be positive.");

	ConverterStateRef state = new ConverterState();
	state->config = config;
	state->state = State_Running;
	state->canonical_input_path = FileUtils::getCanonicalPath(config.input_path);
	state->canonical_input_dir = FileUtils::getCanonicalPath(FileUtils::getDirectory(state->canonical_input_path));
	state->main_virtual_filename = normaliseVirtualPath(FileUtils::getFilename(state->canonical_input_path));

	state->explicit_files[state->main_virtual_filename] = state->canonical_input_path;
	for(std::map<std::string, std::string>::const_iterator it=config.related_files.begin(); it!=config.related_files.end(); ++it)
	{
		const std::string virtual_path = normaliseVirtualPath(it->first);
		if(!FileUtils::fileExists(it->second) || FileUtils::isDirectory(it->second))
			throw glare::Exception("Gaussian related file does not exist: '" + it->second + "'.");
		const std::string canonical_path = FileUtils::getCanonicalPath(it->second);
		if(!pathHasDirectoryPrefix(canonical_path, state->canonical_input_dir) &&
			canonical_path != state->canonical_input_path)
			throw glare::Exception("Gaussian related file escapes the selected input directory.");
		state->explicit_files[virtual_path] = canonical_path;
	}

	state->root_page = makeRootPage(state->main_virtual_filename);
	state->timer.reset();

	impl->state = state;
	impl->client = new ConverterClient(state);

	CefWindowInfo window_info;
	window_info.windowless_rendering_enabled = true;
	window_info.shared_texture_enabled = false;

	CefBrowserSettings browser_settings;
	browser_settings.windowless_frame_rate = 1;
	browser_settings.background_color = CefColorSetARGB(0, 0, 0, 0);

	impl->browser = CefBrowserHost::CreateBrowserSync(
		window_info,
		impl->client,
		CefString(std::string(VIRTUAL_ORIGIN) + "/index.html"),
		browser_settings,
		nullptr,
		nullptr);
	if(!impl->browser)
	{
		setFailed(state, "Failed to create hidden CEF Gaussian converter.");
		throw glare::Exception(state->error_message);
	}
	impl->browser->GetHost()->SetAudioMuted(true);
#else
	impl->state = State_Failed;
	impl->error_message = "This client was built without CEF support.";
	impl->output_path = config.output_path;
#endif
}


void GaussianSplatCEFConverter::think()
{
#if CEF_SUPPORT
	if(!impl->state)
		return;

	bool timed_out = false;
	bool failed = false;
	{
		Lock lock(impl->state->mutex);
		timed_out =
			impl->state->state == State_Running &&
			impl->state->timer.elapsed() > impl->state->config.timeout_s;
		failed = impl->state->state == State_Failed;
	}
	if(timed_out)
		setFailed(impl->state, "Gaussian conversion timed out.");
	if(timed_out || failed)
		impl->close();
#endif
}


void GaussianSplatCEFConverter::cancel()
{
#if CEF_SUPPORT
	if(impl->state)
		setFailed(impl->state, "Gaussian conversion was cancelled.");
	impl->close();
#else
	if(impl->state == State_Running)
	{
		impl->state = State_Failed;
		impl->error_message = "Gaussian conversion was cancelled.";
	}
#endif
}


GaussianSplatCEFConverter::State GaussianSplatCEFConverter::state() const
{
#if CEF_SUPPORT
	if(!impl->state)
		return State_Idle;
	Lock lock(impl->state->mutex);
	return impl->state->state;
#else
	return impl->state;
#endif
}


bool GaussianSplatCEFConverter::isFinished() const
{
	const State s = state();
	return s == State_Succeeded || s == State_Failed;
}


const std::string& GaussianSplatCEFConverter::errorMessage() const
{
#if CEF_SUPPORT
	static const std::string empty;
	return impl->state ? impl->state->error_message : empty;
#else
	return impl->error_message;
#endif
}


const std::string& GaussianSplatCEFConverter::progressStage() const
{
#if CEF_SUPPORT
	static const std::string empty;
	return impl->state ? impl->state->progress_stage : empty;
#else
	return impl->progress_stage;
#endif
}


double GaussianSplatCEFConverter::progressValue() const
{
#if CEF_SUPPORT
	if(!impl->state)
		return 0.0;
	Lock lock(impl->state->mutex);
	return impl->state->progress_value;
#else
	return impl->progress_value;
#endif
}


const std::string& GaussianSplatCEFConverter::outputPath() const
{
#if CEF_SUPPORT
	static const std::string empty;
	return impl->state ? impl->state->config.output_path : empty;
#else
	return impl->output_path;
#endif
}


std::string GaussianSplatCEFConverter::packagedConverterScriptPath(const std::string& resources_dir_path)
{
	return FileUtils::join(resources_dir_path, "gaussian_splat/gaussian_splat_converter.js");
}


std::string GaussianSplatCEFConverter::packagedWebPWasmPath(const std::string& resources_dir_path)
{
	return FileUtils::join(resources_dir_path, "gaussian_splat/webp.wasm");
}


std::string GaussianSplatCEFConverter::makeTemporaryOutputPath()
{
	static uint64 counter = 0; // UI-thread API; no cross-thread synchronisation needed.
	const std::string temp_dir = PlatformUtils::getTempDirPath();
	for(size_t attempt=0; attempt<1000; ++attempt)
	{
		const std::string path = FileUtils::join(
			temp_dir,
			"metasiberia_gaussian_runtime_" +
				toString(PlatformUtils::getProcessID()) + "_" +
				toString(++counter) + ".ply"
		);
		if(!FileUtils::fileExists(path))
			return path;
	}
	throw glare::Exception("Could not allocate a unique temporary Gaussian PLY path.");
}
