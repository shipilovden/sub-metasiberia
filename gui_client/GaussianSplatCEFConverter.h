/*=====================================================================
GaussianSplatCEFConverter.h
---------------------------
Hidden Chromium conversion bridge for Gaussian splat containers.
=====================================================================*/
#pragma once

#include <utils/Platform.h>
#include <utils/RefCounted.h>

#include <map>
#include <string>


/*=====================================================================
GaussianSplatCEFConverter
-------------------------
Runs the bundled JavaScript Gaussian converter in an off-screen CEF browser.
No browser window or world WebView is created.

All methods must be called on the same thread that pumps
CEF::doMessageLoopWork().  start() is asynchronous; call think() once per
frame and inspect state().
=====================================================================*/
class GaussianSplatCEFConverter : public RefCounted
{
public:
	enum State
	{
		State_Idle,
		State_Running,
		State_Succeeded,
		State_Failed
	};

	struct Config
	{
		Config();

		// Selected source asset.  Its semantic filename/extension is preserved
		// on the converter's virtual origin.
		std::string input_path;

		// Bundled @playcanvas/splat-transform browser build and its WebP codec.
		std::string converter_script_path;
		std::string webp_wasm_path;

		// Must be a new, caller-owned temporary path ending in ".ply".
		std::string output_path;

		// Optional explicit related files, keyed by their relative virtual path
		// (for example "means.webp").  They take precedence over directory
		// lookup and are still canonicalised before use.
		std::map<std::string, std::string> related_files;

		// Allow SOG/LCC metadata to resolve related files beside input_path.
		// Resolution is canonical and cannot escape this directory.
		bool allow_input_directory_sidecars;

		uint64 max_output_bytes;
		double timeout_s;
	};

	GaussianSplatCEFConverter();
	~GaussianSplatCEFConverter();

	// Begins conversion.  Configuration errors throw glare::Exception.
	void start(const Config& config);

	// Enforces timeout and advances terminal cleanup.  CEF itself is pumped by
	// the existing application message loop.
	void think();
	void cancel();

	State state() const;
	bool isFinished() const;
	const std::string& errorMessage() const;
	const std::string& progressStage() const;
	double progressValue() const;

	// Valid only after State_Succeeded. The caller memory-maps this temporary
	// file, avoiding a second full-size native copy of large splat payloads.
	const std::string& outputPath() const;

	// Runtime paths installed by scripts/copy_files_to_output.rb.
	static std::string packagedConverterScriptPath(const std::string& resources_dir_path);
	static std::string packagedWebPWasmPath(const std::string& resources_dir_path);
	static std::string makeTemporaryOutputPath();

private:
	class Impl;
	Impl* impl;
};
