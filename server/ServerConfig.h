/*=====================================================================
ServerConfig.h
--------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once

#include <string>
#include <map>


class ServerConfig
{
public:
	ServerConfig() : allow_light_mapper_bot_full_perms(false), update_parcel_sales(false), do_lua_http_request_rate_limiting(true), enable_LOD_chunking(true), enable_registration(true) {}
	
	std::string webserver_fragments_dir; // empty string = use default.
	std::string webserver_public_files_dir; // empty string = use default.
	std::string webclient_dir; // empty string = use default.

	std::string tls_certificate_path; // empty string = use default.
	std::string tls_private_key_path; // empty string = use default.

	// If set, webserver will redirect requests to the canonical hostname (preserving path + query).
	// This allows keeping IP access working while promoting a stable domain-based URL.
	std::string canonical_web_hostname; // e.g. "vr.metasiberia.com"

	// If set, webserver will serve ACME http-01 challenge files from this directory.
	// (Used for Let's Encrypt without stopping the server.)
	// Challenge files are expected at: <letsencrypt_webroot_dir>/.well-known/acme-challenge/<token>
	std::string letsencrypt_webroot_dir;
	
	bool allow_light_mapper_bot_full_perms; // Allow lightmapper bot (User account with name "lightmapperbot" to have full write permissions.

	bool update_parcel_sales; // Should we run auctions?

	bool do_lua_http_request_rate_limiting; // Should we rate-limit HTTP requests made by Lua scripts?

	bool enable_LOD_chunking; // Should we generate LOD chunks?

	bool enable_registration; // Should we allow new users to register?

	std::string AI_model_id; // Default value = "xai/grok-4-1-fast-non-reasoning"
	std::string shared_LLM_prompt_part; // Default value = "You are a helpful bot in the Substrata Metaverse." etc..
};


struct ServerCredentials
{
	std::map<std::string, std::string> creds;
};
