/*=====================================================================
MCPPolyfork.h
-------------
Polyfork catalogue search and direct CDN placement for free GLB assets.
Assets remain hosted by Polyfork; world objects reference their CDN URL.
=====================================================================*/
#pragma once

#include "MCPModeling.h"
#include "Escaping.h"
#include "../server/ServerWorldState.h"
#include "../server/User.h"
#include <JSONParser.h>
#include <networking/HTTPClient.h>
#include <Exception.h>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>

namespace MCPPolyfork
{
inline void waitForRequestSlot()
{
	// Keep concurrent world requests below the catalogue's public API limits.
	static std::mutex request_mutex;
	static std::chrono::steady_clock::time_point next_request_time;
	std::unique_lock<std::mutex> lock(request_mutex);
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if(next_request_time.time_since_epoch().count() != 0 && now < next_request_time)
		std::this_thread::sleep_until(next_request_time);
	next_request_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
}

inline std::string get(const std::string& path, size_t max_bytes = 2 * 1024 * 1024)
{
	HTTPClient::ResponseInfo response;
	std::string body;
	std::string transport_error;
	for(int attempt = 0; attempt < 3; ++attempt)
	{
		HTTPClient client;
		waitForRequestSlot();
		client.verify_server_certificate = true;
		client.user_agent = "Metasiberia/1.0 (Polyfork asset catalogue integration)";
		client.additional_headers.push_back("Accept: application/json");
		client.max_data_size = max_bytes;
		client.max_socket_buffer_size = max_bytes;
		body.clear();
		try
		{
			response = client.downloadFile("https://polyfork.dev" + path, body);
			transport_error.clear();
		}
		catch(const glare::Exception& e)
		{
			transport_error = e.what();
			if(attempt == 2)
				throw glare::Exception("Polyfork request failed after bounded retries: " + transport_error);
			std::this_thread::sleep_for(std::chrono::seconds(1u << attempt));
			continue;
		}
		if(response.response_code == 200) break;
		const bool transient = response.response_code == 429 ||
			(response.response_code >= 500 && response.response_code <= 599);
		if(!transient || attempt == 2) break;
		std::this_thread::sleep_for(std::chrono::seconds(1u << attempt));
	}
	if(response.response_code == 429)
		throw glare::Exception("Polyfork rate limit persisted after bounded retries (HTTP 429). Wait before searching or placing another model.");
	if(response.response_code != 200)
		throw glare::Exception("Polyfork returned HTTP " + toString(response.response_code) + " " + response.response_message + ".");
	if(body.empty()) throw glare::Exception("Polyfork returned an empty response.");
	return body;
}

inline bool validID(const std::string& id)
{
	if(id.empty() || id.size() > 128) return false;
	for(unsigned char c : id)
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
	return true;
}

inline std::string resolveID(const std::string& id_or_url)
{
	if(validID(id_or_url)) return id_or_url;
	// A page link can explicitly select the paid HD edition. Do not silently
	// turn that request into the separate free low-poly edition with the same ID.
	const size_t query_pos = id_or_url.find('?');
	if(query_pos != std::string::npos)
	{
		const std::string query = id_or_url.substr(query_pos + 1);
		if(query.find("edition=hd") != std::string::npos)
			throw glare::Exception("The supplied Polyfork link selects the HD edition. Metasiberia can place the free low-poly edition from the public CDN; provide its standard asset page or GLB link if that is what you want.");
	}
	const std::string prefixes[] = {
		"https://polyfork.dev/cdn/", "https://polyfork.dev/assets/", "https://polyfork.dev/asset/"
	};
	for(const std::string& prefix : prefixes)
		if(id_or_url.compare(0, prefix.size(), prefix) == 0)
		{
			std::string id = id_or_url.substr(prefix.size());
			const size_t suffix = id.find_first_of("?#");
			if(suffix != std::string::npos) id.erase(suffix);
			while(!id.empty() && id.back() == '/') id.pop_back();
			if(prefix.find("/cdn/") != std::string::npos)
			{
				if(id.size() > 4 && id.substr(id.size() - 4) == ".glb")
					id.erase(id.size() - 4);
				else if(id.size() > 4 && id.substr(id.size() - 4) == ".mjs")
					throw glare::Exception("The supplied Polyfork URL is an .mjs runtime module, not a model file. Provide the asset page URL or its official .glb URL to place the model.");
			}
			if(id.size() > 11 && id.substr(id.size() - 11) == "-hd-preview")
				throw glare::Exception("The supplied Polyfork link is an HD preview. Metasiberia can load the free low-poly model, but cannot serve the HD edition to world users.");
			if(id.size() > 3 && id.substr(id.size() - 3) == "-hd")
				throw glare::Exception("The supplied Polyfork link is an HD edition. Metasiberia can load the free low-poly model, but cannot serve the HD edition to world users.");
			// The public catalogue also exposes a low-poly preview GLB with the
			// same asset ID plus "-preview". Resolve that link to the canonical
			// catalogue record so placement uses its authoritative dimensions.
			if(prefix.find("/cdn/") != std::string::npos && id.size() > 8 && id.substr(id.size() - 8) == "-preview")
			{
				const std::string canonical_id = id.substr(0, id.size() - 8);
				if(validID(canonical_id)) return canonical_id;
			}
			if(validID(id)) return id;
		}
	throw glare::Exception("Use an exact Polyfork asset ID, asset page URL, or https://polyfork.dev/cdn/{id}.glb URL.");
}

inline double number(const JSONNode& node, const JSONParser& parser, const std::string& key, double fallback)
{
	return node.getChildDoubleValueWithDefaultVal(parser, key, fallback);
}

inline Vec3d dimensions(const JSONParser& parser, const JSONNode& root)
{
	const JSONNode* asset = &root;
	if(root.hasChild("asset")) asset = &root.getChildObject(parser, "asset");
	if(!asset->hasChild("size_m")) throw glare::Exception("Polyfork asset metadata has no size_m; refusing placement with an invented bounding box.");
	const JSONNode& size = asset->getChildNode(parser, "size_m");
	Vec3d result(0,0,0);
	if(size.type == JSONNode::Type_Object)
	{
		result = Vec3d(number(size, parser, "x", 0), number(size, parser, "y", 0), number(size, parser, "z", 0));
	}
	else if(size.type == JSONNode::Type_Array)
	{
		double values[3];
		size.parseDoubleArrayValues(parser, 3, values);
		result = Vec3d(values[0], values[1], values[2]);
	}
	else throw glare::Exception("Polyfork size_m must contain three dimensions in metres.");
	if(!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z) ||
		result.x <= 0 || result.y <= 0 || result.z <= 0 || result.x > 1000 || result.y > 1000 || result.z > 1000)
		throw glare::Exception("Polyfork returned invalid model dimensions; refusing placement.");
	return result;
}

inline std::string search(const JSONParser& parser, const JSONNode& args)
{
	const std::string query = args.getChildStringValue(parser, "q");
	if(query.empty() || query.size() > 200 || query.find_first_of("\r\n") != std::string::npos || query.find('\0') != std::string::npos)
		throw glare::Exception("q must contain 1..200 characters.");
	const double per_page_value = number(args, parser, "per_page", 10);
	if(!std::isfinite(per_page_value) || per_page_value < 1 || per_page_value > 100 || std::floor(per_page_value) != per_page_value)
		throw glare::Exception("per_page must be an integer from 1 to 100.");
	const int per_page = (int)per_page_value;
	const double page_value = number(args, parser, "page", 1);
	if(!std::isfinite(page_value) || page_value < 1 || page_value > 10000 || std::floor(page_value) != page_value)
		throw glare::Exception("page must be an integer from 1 to 10000.");
	const int page = (int)page_value;
	if(args.hasChild("min_triangles") && args.hasChild("max_triangles") &&
		number(args, parser, "min_triangles", 0) > number(args, parser, "max_triangles", 0))
		throw glare::Exception("min_triangles must not exceed max_triangles.");
	std::string path = "/api/assets?q=" + web::Escaping::URLEscape(query) + "&free=1&per_page=" +
		toString(per_page) + "&page=" + toString(page) + "&fields=compact";
	if(args.hasChild("class"))
	{
		const std::string asset_class = args.getChildStringValue(parser, "class");
		static const char* allowed[] = {"prop","building","vehicle","character","animal","ultra","attachment","terrain","hand"};
		bool valid = false;
		for(const char* candidate : allowed) valid = valid || asset_class == candidate;
		if(!valid) throw glare::Exception("Unsupported Polyfork class filter.");
		path += "&class=" + web::Escaping::URLEscape(asset_class);
	}
	if(args.hasChild("max_triangles"))
	{
		const double triangle_value = number(args, parser, "max_triangles", 0);
		if(!std::isfinite(triangle_value) || triangle_value < 1 || triangle_value > 1000000 || std::floor(triangle_value) != triangle_value)
			throw glare::Exception("max_triangles must be an integer from 1 to 1000000.");
		const int triangles = (int)triangle_value;
		path += "&max_triangles=" + toString(triangles);
	}
	if(args.hasChild("min_triangles"))
	{
		const double triangle_value = number(args, parser, "min_triangles", 0);
		if(!std::isfinite(triangle_value) || triangle_value < 0 || triangle_value > 1000000 || std::floor(triangle_value) != triangle_value)
			throw glare::Exception("min_triangles must be an integer from 0 to 1000000.");
		const int triangles = (int)triangle_value;
		path += "&min_triangles=" + toString(triangles);
	}
	if(args.hasChild("ik"))
	{
		const std::string ik = args.getChildStringValue(parser, "ik");
		if(ik != "1" && ik != "legs" && ik != "arm")
			throw glare::Exception("ik must be 1, legs, or arm.");
		path += "&ik=" + web::Escaping::URLEscape(ik);
	}
	if(args.hasChild("sort"))
	{
		const std::string sort = args.getChildStringValue(parser, "sort");
		if(sort != "latest" && sort != "tris") throw glare::Exception("sort must be latest or tris.");
		path += "&sort=" + web::Escaping::URLEscape(sort);
	}
	return get(path, 512 * 1024);
}

inline std::string getAsset(const JSONParser& parser, const JSONNode& args)
{
	const std::string id = resolveID(args.getChildStringValue(parser, "asset_id"));
	const std::string metadata = get("/api/assets/" + web::Escaping::URLEscape(id));
	JSONParser asset_parser;
	asset_parser.parseBuffer(metadata.data(), metadata.size());
	if(asset_parser.nodes.empty() || asset_parser.nodes[0].type != JSONNode::Type_Object)
		throw glare::Exception("Polyfork returned malformed asset metadata.");
	const JSONNode& root = asset_parser.nodes[0];
	const JSONNode* asset = root.hasChild("asset") ? &root.getChildObject(asset_parser, "asset") : &root;
	if(!asset->hasChild("id") || asset->getChildStringValue(asset_parser, "id") != id)
		throw glare::Exception("Polyfork asset metadata ID did not match the requested asset.");
	if(!asset->hasChild("title") || asset->getChildStringValue(asset_parser, "title").empty())
		throw glare::Exception("Polyfork asset metadata omitted its title.");
	return metadata;
}

inline std::string place(ServerAllWorldsState& all, ServerWorldState& world, const JSONParser& parser,
	const JSONNode& args, const UserID& user, const std::string& username)
{
	const std::string id = resolveID(args.getChildStringValue(parser, "asset_id"));
	const std::string metadata = get("/api/assets/" + web::Escaping::URLEscape(id));
	JSONParser asset_parser;
	asset_parser.parseBuffer(metadata.data(), metadata.size());
	if(asset_parser.nodes.empty() || asset_parser.nodes[0].type != JSONNode::Type_Object)
		throw glare::Exception("Polyfork returned malformed asset metadata.");
	const JSONNode& root = asset_parser.nodes[0];
	const JSONNode* asset = root.hasChild("asset") ? &root.getChildObject(asset_parser, "asset") : &root;
	if(!asset->hasChild("id") || asset->getChildStringValue(asset_parser, "id") != id)
		throw glare::Exception("Polyfork asset metadata ID did not match the requested asset; refusing to place a model with unverified bounds.");
	if(!asset->hasChild("title"))
		throw glare::Exception("Polyfork asset metadata omitted its title; refusing to place an unidentified model.");
	const std::string asset_title = asset->getChildStringValue(asset_parser, "title");
	if(asset_title.empty() || asset_title.size() > 256)
		throw glare::Exception("Polyfork asset metadata returned an invalid title; refusing to place an unidentified model.");
	if(!asset->hasChild("download"))
		throw glare::Exception("Polyfork asset metadata omitted its official download URLs; refusing to guess a CDN path.");
	const JSONNode& downloads = asset->getChildObject(asset_parser, "download");
	if(!downloads.hasChild("glb"))
		throw glare::Exception("Polyfork asset metadata has no GLB download URL; refusing to place an unsupported asset format.");
	const std::string glb_url = downloads.getChildStringValue(asset_parser, "glb");
	const std::string expected_glb_url = "https://polyfork.dev/cdn/" + id + ".glb";
	if(glb_url != expected_glb_url)
		throw glare::Exception("Polyfork GLB URL did not match the verified asset ID and official CDN host.");
	const Vec3d size = dimensions(asset_parser, root);
	if(!asset->hasChild("free") || !asset->getChildBoolValueWithDefaultVal(asset_parser, "free", false))
		throw glare::Exception("Polyfork did not confirm that this asset is free; refusing to place it through the public low-poly CDN.");
	if(!asset->hasChild("origin_y"))
		throw glare::Exception("Polyfork asset metadata omitted its ground origin; refusing to place the model with an assumed origin.");
	const double origin_y = asset->getChildDoubleValue(asset_parser, "origin_y");
	if(!std::isfinite(origin_y) || std::abs(origin_y) > size.y * 2.0)
		throw glare::Exception("Polyfork returned an invalid origin_y.");
	Vec3d scale(1,1,1);
	if(args.hasChild("scale"))
	{
		double values[3];
		args.getChildArray(parser, "scale").parseDoubleArrayValues(parser, 3, values);
		for(double value : values)
			if(!std::isfinite(value) || value < 0.1 || value > 10.0)
				throw glare::Exception("scale components must be finite and between 0.1 and 10.");
		scale = Vec3d(values[0], values[1], values[2]);
		if(size.x * scale.x > 1000 || size.y * scale.y > 1000 || size.z * scale.z > 1000)
			throw glare::Exception("The scaled Polyfork model exceeds the 1,000 metre placement bound.");
	}

	WorldObjectRef object = new WorldObject();
	object->object_type = WorldObject::ObjectType_Generic;
	object->model_url = glb_url;
	object->content = "{\"metasiberia_asset_source\":\"polyfork\",\"asset_id\":\"" + web::Escaping::JSONEscape(id) +
		"\",\"asset_title\":\"" + web::Escaping::JSONEscape(asset_title) + "\",\"asset_page_url\":\"https://polyfork.dev/asset/" +
		web::Escaping::JSONEscape(id) + "\",\"size_m\":" + MCPHandlers::Modeling::vecJSON(size) +
		",\"origin_y\":" + doubleToString(origin_y) + "}";
	// GLB meshes retain glTF's Y-up basis in this renderer. Match the existing
	// importer transform that rotates imported glTF content into +Z-up.
	object->axis = Vec3f(1,0,0);
	object->angle = 1.5707963267948966f;
	object->scale = Vec3f((float)scale.x, (float)scale.y, (float)scale.z);
	// Keep the object-space bounds in the GLB's Y-up basis. The importer axis
	// transform maps the ground plane to Metasiberia's X/Y plane and height to +Z.
	object->setAABBOS(js::AABBox(Vec4f((float)-size.x/2, (float)origin_y, (float)-size.z/2, 1),
		Vec4f((float)size.x/2, (float)(origin_y + size.y), (float)size.z/2, 1)));
	const std::string result = MCPHandlers::Modeling::publish(all, world, object, parser, args, user, username, Reference<BatchedMesh>());
	return result;
}
}
