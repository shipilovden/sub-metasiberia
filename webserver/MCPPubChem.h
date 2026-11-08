/*=====================================================================
MCPPubChem.h
------------
PubChem-backed scientific molecule tools for the MCP modeling server.
=====================================================================*/
#pragma once

#include "MCPModeling.h"
#include "Escaping.h"
#include "../shared/ScientificBondData.h"
#include "../shared/ScientificElementPalette.h"
#include "../shared/ScientificAtomicMass.h"
#include "../shared/ScientificFormula.h"
#include "../shared/ScientificMoleculeGeometry.h"
#include "../shared/ScientificObjectTableCodec.h"
#include "../server/ServerWorldState.h"
#include "../server/User.h"
#include <JSONParser.h>
#include <networking/HTTPClient.h>
#include <SHA256.h>
#include <Exception.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace MCPScientific
{
struct PubChemAtom
{
	int id;
	std::string element;
	double x, y, z;
	int formal_charge;
	int isotope_mass_number = 0;
	int atom_stereo_parity = 0;
	std::string atom_stereo_source;
};

struct PubChemBond
{
	int atom_a, atom_b, order;
	int stereo = 0;
};

struct PubChemMolecule
{
	int cid;
	int connected_component_count = 0;
	int covalent_unit_count = 0;
	std::string name, formula, weight, iupac_name, inchi_key, smiles, property_table;
	std::vector<std::string> synonyms;
	std::string synonyms_error;
	std::string sdf, atom_table, bond_table, sdf_type;
	std::vector<PubChemAtom> atoms;
	std::vector<PubChemBond> bonds;
};

inline std::string quote(const std::string& value)
{
	return "\"" + web::Escaping::JSONEscape(value) + "\"";
}

inline std::string normalizedPubChemName(std::string value)
{
	auto is_ascii_space = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
	while(!value.empty() && is_ascii_space((unsigned char)value.front())) value.erase(value.begin());
	while(!value.empty() && is_ascii_space((unsigned char)value.back())) value.pop_back();
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return c < 0x80 ? (char)std::tolower(c) : (char)c;
	});
	return value;
}

inline size_t validatePubChemDisplayName(const std::string& value, bool& contains_non_whitespace)
{
	contains_non_whitespace = false;
	size_t code_point_count = 0;
	for(size_t i = 0; i < value.size(); )
	{
		const unsigned char first = (unsigned char)value[i];
		uint32 code_point = 0;
		size_t sequence_length = 0;
		if(first <= 0x7f) { code_point = first; sequence_length = 1; }
		else if(first >= 0xc2 && first <= 0xdf) { code_point = first & 0x1f; sequence_length = 2; }
		else if(first >= 0xe0 && first <= 0xef) { code_point = first & 0x0f; sequence_length = 3; }
		else if(first >= 0xf0 && first <= 0xf4) { code_point = first & 0x07; sequence_length = 4; }
		else throw glare::Exception("display_name must be valid UTF-8.");
		if(i + sequence_length > value.size())
			throw glare::Exception("display_name must be valid UTF-8.");
		for(size_t j = 1; j < sequence_length; ++j)
		{
			const unsigned char continuation = (unsigned char)value[i + j];
			if((continuation & 0xc0) != 0x80)
				throw glare::Exception("display_name must be valid UTF-8.");
			code_point = (code_point << 6) | (continuation & 0x3f);
		}
		if((sequence_length == 2 && code_point < 0x80) ||
			(sequence_length == 3 && code_point < 0x800) ||
			(sequence_length == 4 && (code_point < 0x10000 || code_point > 0x10ffff)) ||
			(code_point >= 0xd800 && code_point <= 0xdfff))
			throw glare::Exception("display_name must be valid UTF-8.");
		const bool bidi_or_invisible_control = code_point == 0x061c || code_point == 0x200e || code_point == 0x200f ||
			(code_point >= 0x202a && code_point <= 0x202e) || (code_point >= 0x2066 && code_point <= 0x2069) || code_point == 0xfeff;
		if(code_point <= 0x1f || (code_point >= 0x7f && code_point <= 0x9f) || code_point == 0x2028 || code_point == 0x2029 || bidi_or_invisible_control)
			throw glare::Exception("display_name must be a single line without control characters.");
		const bool unicode_whitespace = code_point == 0x20 || code_point == 0x00a0 || code_point == 0x1680 ||
			(code_point >= 0x2000 && code_point <= 0x200a) || code_point == 0x202f || code_point == 0x205f || code_point == 0x3000;
		if(!unicode_whitespace) contains_non_whitespace = true;
		if(++code_point_count > 128)
			throw glare::Exception("display_name must contain at most 128 Unicode characters (at most 512 UTF-8 bytes).");
		i += sequence_length;
	}
	return code_point_count;
}

inline std::string tableCell(const std::string& value)
{
	// PubChem text is external input. Keep tabs/newlines from creating forged
	// rows or columns in the editor's tab-separated scientific property table.
	std::string cleaned;
	cleaned.reserve(value.size());
	for(const unsigned char c : value)
		cleaned.push_back(c < 0x20 || c == 0x7f ? ' ' : (char)c);
	return cleaned;
}

inline std::string sha256Hex(const std::string& value)
{
	const std::vector<unsigned char> digest = SHA256::hash(value);
	if(digest.size() != 32) throw glare::Exception("Could not calculate the PubChem SDF SHA-256 checksum.");
	static const char hex_digits[] = "0123456789abcdef";
	std::string result;
	result.reserve(digest.size() * 2);
	for(const unsigned char byte : digest)
	{
		result.push_back(hex_digits[byte >> 4]);
		result.push_back(hex_digits[byte & 0x0f]);
	}
	return result;
}

inline std::string urlEscape(const std::string& value)
{
	return web::Escaping::URLEscape(value);
}

inline bool startsWithCaseInsensitive(const std::string& value, const char* prefix)
{
	const size_t prefix_size = std::strlen(prefix);
	if(value.size() < prefix_size) return false;
	for(size_t i=0; i<prefix_size; ++i)
		if(std::tolower((unsigned char)value[i]) != std::tolower((unsigned char)prefix[i])) return false;
	return true;
}

inline void waitForPubChemRequestSlot()
{
	// PubChem asks clients to stay at or below five requests per second. Keep
	// this server process at four requests per second, including concurrent MCP
	// calls, to leave headroom for scheduling jitter and other clients.
	static std::mutex request_mutex;
	static std::chrono::steady_clock::time_point next_request_time;
	std::unique_lock<std::mutex> lock(request_mutex);
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if(next_request_time.time_since_epoch().count() != 0 && now < next_request_time)
		std::this_thread::sleep_until(next_request_time);
	next_request_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
}

struct CachedResponse
{
	std::string body;
	std::chrono::steady_clock::time_point fetched_at;
	unsigned long long last_used;
};

struct ResponseCache
{
	std::mutex mutex;
	std::map<std::string, CachedResponse> entries;
	size_t bytes = 0;
	unsigned long long use_counter = 0;
};

inline ResponseCache& responseCache()
{
	static ResponseCache cache;
	return cache;
}

inline bool readCachedResponse(const std::string& path, size_t max_bytes, std::string& body_out)
{
	ResponseCache& cache = responseCache();
	std::lock_guard<std::mutex> lock(cache.mutex);
	const auto it = cache.entries.find(path);
	if(it == cache.entries.end()) return false;
	static const std::chrono::hours CACHE_TTL(1);
	if(std::chrono::steady_clock::now() - it->second.fetched_at >= CACHE_TTL)
	{
		cache.bytes -= it->second.body.size();
		cache.entries.erase(it);
		return false;
	}
	if(it->second.body.size() > max_bytes) return false;
	it->second.last_used = ++cache.use_counter;
	body_out = it->second.body;
	return true;
}

inline void cacheResponse(const std::string& path, const std::string& body)
{
	static const size_t MAX_CACHE_BYTES = 32 * 1024 * 1024;
	static const size_t MAX_CACHE_ENTRIES = 128;
	if(body.empty() || body.size() > MAX_CACHE_BYTES) return;
	ResponseCache& cache = responseCache();
	std::lock_guard<std::mutex> lock(cache.mutex);
	const auto existing = cache.entries.find(path);
	if(existing != cache.entries.end())
	{
		cache.bytes -= existing->second.body.size();
		cache.entries.erase(existing);
	}
	while(!cache.entries.empty() &&
		(cache.entries.size() >= MAX_CACHE_ENTRIES || cache.bytes > MAX_CACHE_BYTES - body.size()))
	{
		auto oldest = cache.entries.begin();
		for(auto it = cache.entries.begin(); it != cache.entries.end(); ++it)
			if(it->second.last_used < oldest->second.last_used) oldest = it;
		cache.bytes -= oldest->second.body.size();
		cache.entries.erase(oldest);
	}
	CachedResponse entry;
	entry.body = body;
	entry.fetched_at = std::chrono::steady_clock::now();
	entry.last_used = ++cache.use_counter;
	cache.bytes += entry.body.size();
	cache.entries.insert(std::make_pair(path, entry));
}

inline std::string httpGet(const std::string& path, size_t max_bytes = 2 * 1024 * 1024)
{
	std::string cached_body;
	if(readCachedResponse(path, max_bytes, cached_body)) return cached_body;
	HTTPClient::ResponseInfo response;
	std::string body;
	std::string transport_error;
	for(int attempt = 0; attempt < 3; ++attempt)
	{
		waitForPubChemRequestSlot();
		HTTPClient client;
		client.verify_server_certificate = true;
		client.user_agent = "Metasiberia/1.0 (PubChem PUG REST scientific-object integration)";
		client.additional_headers.push_back("Accept: application/json, chemical/x-mdl-sdfile, */*;q=0.5");
		client.max_data_size = max_bytes;
		client.max_socket_buffer_size = max_bytes;
		body.clear();
		try
		{
			response = client.downloadFile("https://pubchem.ncbi.nlm.nih.gov" + path, body);
			transport_error.clear();
		}
		catch(const glare::Exception& e)
		{
			transport_error = e.what();
			if(attempt == 2)
				throw glare::Exception("PubChem PUG REST request failed after bounded retries: " + transport_error);
			std::this_thread::sleep_for(std::chrono::seconds(1u << attempt));
			continue;
		}
		if(response.response_code == 200) break;
		const bool transient = response.response_code == 429 ||
			(response.response_code >= 500 && response.response_code <= 599);
		if(!transient || attempt == 2) break;
		// Keep retries bounded; the next attempt still passes the rate gate.
		std::this_thread::sleep_for(std::chrono::seconds(1u << attempt));
	}
	if(response.response_code == 429)
		throw glare::Exception("PubChem rate limit persisted after bounded retries (HTTP 429). Wait before searching or creating another molecule.");
	if(response.response_code != 200)
		throw glare::Exception("PubChem PUG REST returned HTTP " + toString(response.response_code) + " " + response.response_message + ". Retry later or search by PubChem CID.");
	if(body.empty()) throw glare::Exception("PubChem PUG REST returned an empty response.");
	cacheResponse(path, body);
	return body;
}

inline const JSONNode& parseJSON(const std::string& body, JSONParser& parser)
{
	parser.parseBuffer(body.data(), body.size());
	if(parser.nodes.empty() || parser.nodes[0].type != JSONNode::Type_Object)
		throw glare::Exception("PubChem returned malformed JSON.");
	return parser.nodes[0];
}

inline std::vector<int> resolveCIDs(const std::string& query, bool& results_truncated_out)
{
	results_truncated_out = false;
	if(query.empty() || query.size() > 256) throw glare::Exception("query must contain 1..256 characters.");
	std::string namespace_name = "name";
	std::string identifier = query;
	const struct Prefix { const char* text; const char* namespace_name; } prefixes[] = {
		{"CID:", "cid"}, {"NAME:", "name"}, {"FORMULA:", "formula"}, {"SMILES:", "smiles"},
		{"INCHIKEY:", "inchikey"}, {"INCHI:", "inchi"}
	};
	for(const Prefix& prefix : prefixes)
		if(startsWithCaseInsensitive(query, prefix.text))
		{
			namespace_name = prefix.namespace_name;
			identifier = query.substr(std::strlen(prefix.text));
			break;
		}
	if(namespace_name == "name" && startsWithCaseInsensitive(identifier, "InChI="))
		namespace_name = "inchi";
	if(identifier.empty() || identifier.find_first_of("\r\n") != std::string::npos || identifier.find('\0') != std::string::npos)
		throw glare::Exception("query must contain a valid PubChem name or explicitly prefixed identifier.");
	if(namespace_name == "cid" || (namespace_name == "name" &&
		std::find_if(identifier.begin(), identifier.end(), [](char c){ return c < '0' || c > '9'; }) == identifier.end()))
	{
		if(std::find_if(identifier.begin(), identifier.end(), [](char c){ return c < '0' || c > '9'; }) != identifier.end())
			throw glare::Exception("CID: must be followed only by decimal digits.");
		const long cid = std::strtol(identifier.c_str(), NULL, 10);
		if(cid <= 0 || cid > 2147483647L) throw glare::Exception("PubChem CID is outside the supported range.");
		return std::vector<int>(1, (int)cid);
	}
	// The ordinary formula namespace starts an asynchronous PUG job (HTTP 202),
	// which this synchronous MCP tool cannot poll. Use PubChem's fastformula
	// endpoint and ask for one extra hit so the ten-result response can report
	// whether it was truncated.
	const std::string search_namespace = namespace_name == "formula" ? "fastformula" : namespace_name;
	const std::string search_limit = namespace_name == "formula" ? "?MaxRecords=11" : std::string();
	const std::string response = httpGet("/rest/pug/compound/" + search_namespace + "/" + urlEscape(identifier) + "/cids/JSON" + search_limit, 256 * 1024);
	JSONParser parser;
	const JSONNode& root = parseJSON(response, parser);
	if(!root.hasChild("IdentifierList")) throw glare::Exception("PubChem did not find an exact compound record for this query. Use a compound name, CID, or an explicit formula:, smiles:, inchi:, or inchikey: prefix.");
	const JSONNode& identifiers = root.getChildObject(parser, "IdentifierList");
	if(!identifiers.hasChild("CID")) throw glare::Exception("PubChem returned no compound identifiers for this query.");
	const JSONNode& cid_array = identifiers.getChildArray(parser, "CID");
	std::vector<int> cids;
	std::set<int> seen_cids;
	for(uint32 index : cid_array.child_indices)
	{
		const double raw = parser.nodes[index].getDoubleValue();
		if(std::isfinite(raw) && raw > 0 && raw <= 2147483647.0 && raw == std::floor(raw))
		{
			const int cid = (int)raw;
			if(seen_cids.insert(cid).second)
			{
				cids.push_back(cid);
				if(cids.size() > 10)
				{
					results_truncated_out = true;
					break;
				}
			}
		}
	}
	if(cids.empty()) throw glare::Exception("PubChem returned no valid CIDs for this query.");
	if(results_truncated_out) cids.pop_back();
	return cids;
}

inline std::map<std::string, std::string> propertiesFromRow(const JSONParser& parser, const JSONNode& row)
{
	std::map<std::string, std::string> out;
	const char* keys[] = {"IUPACName", "MolecularFormula", "MolecularFormulaNoCharge", "MolecularWeight", "ExactMass", "MonoisotopicMass", "Charge", "HeavyAtomCount", "CovalentUnitCount", "IsotopeAtomCount", "AtomStereoCount", "DefinedAtomStereoCount", "UndefinedAtomStereoCount", "BondStereoCount", "DefinedBondStereoCount", "UndefinedBondStereoCount", "HBondDonorCount", "HBondAcceptorCount", "RotatableBondCount", "XLogP", "TPSA", "Complexity", "SMILES", "ConnectivitySMILES", "InChI", "InChIKey"};
	for(size_t i=0; i<sizeof(keys)/sizeof(keys[0]); ++i)
		if(row.hasChild(keys[i]))
		{
			const JSONNode& value = row.getChildNode(parser, keys[i]);
			out[keys[i]] = value.type == JSONNode::Type_String ? value.getStringValue() : value.type == JSONNode::Type_Number ? doubleToString(value.getDoubleValue()) : std::string();
		}
	return out;
}


inline std::map<int, std::map<std::string, std::string>> propertiesForCIDs(const std::vector<int>& cids)
{
	if(cids.empty() || cids.size() > 10) throw glare::Exception("PubChem property batch must contain 1..10 CIDs.");
	std::string cid_list;
	std::set<int> requested_cids;
	for(size_t i=0; i<cids.size(); ++i)
	{
		if(cids[i] <= 0) throw glare::Exception("PubChem property batch contains an invalid CID.");
		if(!requested_cids.insert(cids[i]).second) throw glare::Exception("PubChem property batch contains a duplicate requested CID.");
		if(i) cid_list += ",";
		cid_list += toString(cids[i]);
	}
	const std::string props = httpGet("/rest/pug/compound/cid/" + cid_list + "/property/IUPACName,MolecularFormula,MolecularFormulaNoCharge,MolecularWeight,ExactMass,MonoisotopicMass,Charge,HeavyAtomCount,CovalentUnitCount,IsotopeAtomCount,AtomStereoCount,DefinedAtomStereoCount,UndefinedAtomStereoCount,BondStereoCount,DefinedBondStereoCount,UndefinedBondStereoCount,HBondDonorCount,HBondAcceptorCount,RotatableBondCount,XLogP,TPSA,Complexity,SMILES,ConnectivitySMILES,InChI,InChIKey/JSON", 512 * 1024);
	JSONParser parser;
	const JSONNode& root = parseJSON(props, parser);
	if(!root.hasChild("PropertyTable")) throw glare::Exception("PubChem returned no property table for the requested CIDs.");
	const JSONNode& table = root.getChildObject(parser, "PropertyTable");
	if(!table.hasChild("Properties")) throw glare::Exception("PubChem returned no properties for the requested CIDs.");
	const JSONNode& rows = table.getChildArray(parser, "Properties");
	std::map<int, std::map<std::string, std::string>> out;
	for(uint32 index : rows.child_indices)
	{
		const JSONNode& row = parser.nodes[index];
		if(!row.hasChild("CID")) throw glare::Exception("PubChem property row omitted its CID.");
		const double raw_cid = row.getChildDoubleValue(parser, "CID");
		if(!std::isfinite(raw_cid) || raw_cid < 1 || raw_cid > 2147483647.0 || raw_cid != std::floor(raw_cid))
			throw glare::Exception("PubChem property response contains an invalid CID.");
		const int cid = (int)raw_cid;
		if(requested_cids.count(cid) == 0)
			throw glare::Exception("PubChem property response included an unrequested CID " + toString(cid) + ".");
		if(out.count(cid) != 0)
			throw glare::Exception("PubChem property response repeated CID " + toString(cid) + ".");
		out[cid] = propertiesFromRow(parser, row);
	}
	if(out.size() != requested_cids.size()) throw glare::Exception("PubChem returned an incomplete property batch; retry the search before creating a molecule.");
	return out;
}


inline std::map<std::string, std::string> propertiesForCID(int cid)
{
	const std::map<int, std::map<std::string, std::string>> batch = propertiesForCIDs(std::vector<int>(1, cid));
	const auto it = batch.find(cid);
	if(it == batch.end()) throw glare::Exception("PubChem returned no properties for CID " + toString(cid) + ".");
	return it->second;
}

inline std::map<int, std::vector<std::string>> synonymsForCIDs(const std::vector<int>& cids)
{
	if(cids.empty() || cids.size() > 10) throw glare::Exception("PubChem synonym batch must contain 1..10 CIDs.");
	std::string cid_list;
	std::set<int> requested_cids;
	for(size_t i=0; i<cids.size(); ++i)
	{
		if(cids[i] <= 0) throw glare::Exception("PubChem synonym batch contains an invalid CID.");
		if(!requested_cids.insert(cids[i]).second) throw glare::Exception("PubChem synonym batch contains a duplicate requested CID.");
		if(i) cid_list += ",";
		cid_list += toString(cids[i]);
	}
	const std::string body = httpGet("/rest/pug/compound/cid/" + cid_list + "/synonyms/JSON", 512 * 1024);
	JSONParser parser;
	const JSONNode& root = parseJSON(body, parser);
	if(!root.hasChild("InformationList")) throw glare::Exception("PubChem returned no synonym information for the requested CIDs.");
	const JSONNode& information_list = root.getChildObject(parser, "InformationList");
	if(!information_list.hasChild("Information")) throw glare::Exception("PubChem returned an empty synonym response.");
	const JSONNode& information = information_list.getChildArray(parser, "Information");
	std::map<int, std::vector<std::string>> result;
	std::set<int> seen_cids;
	for(uint32 index : information.child_indices)
	{
		const JSONNode& entry = parser.nodes[index];
		if(!entry.hasChild("CID") || !entry.hasChild("Synonym")) continue;
		const double raw_cid = entry.getChildDoubleValue(parser, "CID");
		if(!std::isfinite(raw_cid) || raw_cid < 1 || raw_cid > 2147483647.0 || raw_cid != std::floor(raw_cid))
			throw glare::Exception("PubChem synonym response contains an invalid CID.");
		const int cid = (int)raw_cid;
		if(requested_cids.count(cid) == 0)
			throw glare::Exception("PubChem synonym response included an unrequested CID " + toString(cid) + ".");
		if(!seen_cids.insert(cid).second)
			throw glare::Exception("PubChem synonym response repeated CID " + toString(cid) + ".");
		const JSONNode& synonym_list = entry.getChildArray(parser, "Synonym");
		std::set<std::string> unique;
		std::vector<std::string>& values = result[cid];
		for(uint32 synonym_index : synonym_list.child_indices)
		{
			const JSONNode& synonym = parser.nodes[synonym_index];
			if(synonym.type != JSONNode::Type_String) continue;
			const std::string value = synonym.getStringValue();
			// Avoid long catalogue/product descriptions while retaining common names
			// and identifiers from the leading part of PubChem's synonym list.
			if(value.empty() || value.size() > 160 ||
				std::find_if(value.begin(), value.end(), [](unsigned char c){ return c < 0x20 || c == 0x7f; }) != value.end() ||
				!unique.insert(value).second) continue;
			values.push_back(value);
			if(values.size() >= 8) break;
		}
	}
	return result;
}

inline std::string getCompound(const JSONParser& parser, const JSONNode& args)
{
	const double raw_cid = args.getChildDoubleValue(parser, "cid");
	if(!std::isfinite(raw_cid) || raw_cid < 1 || raw_cid > 2147483647.0 || raw_cid != std::floor(raw_cid))
		throw glare::Exception("cid must be a positive integer PubChem CID.");
	const int cid = (int)raw_cid;
	const std::map<std::string, std::string> props = propertiesForCID(cid);
	std::vector<std::string> synonyms;
	std::string synonyms_error;
	try
	{
		const std::string synonyms_body = httpGet("/rest/pug/compound/cid/" + toString(cid) + "/synonyms/JSON", 256 * 1024);
		JSONParser synonyms_parser;
		const JSONNode& synonyms_root = parseJSON(synonyms_body, synonyms_parser);
		if(!synonyms_root.hasChild("InformationList")) throw glare::Exception("PubChem returned no synonym list for CID " + toString(cid) + ".");
		const JSONNode& info_list = synonyms_root.getChildObject(synonyms_parser, "InformationList");
		if(!info_list.hasChild("Information")) throw glare::Exception("PubChem returned no compound information for CID " + toString(cid) + ".");
		const JSONNode& info_array = info_list.getChildArray(synonyms_parser, "Information");
		if(info_array.child_indices.empty()) throw glare::Exception("PubChem returned no compound information for CID " + toString(cid) + ".");
		const JSONNode& info = synonyms_parser.nodes[info_array.child_indices[0]];
		if(!info.hasChild("CID")) throw glare::Exception("PubChem synonym record omitted its CID.");
		const double returned_cid = info.getChildDoubleValue(synonyms_parser, "CID");
		if(!std::isfinite(returned_cid) || returned_cid != cid)
			throw glare::Exception("PubChem synonym response CID did not match the requested CID " + toString(cid) + ".");
		if(info.hasChild("Synonym"))
		{
			const JSONNode& synonym_array = info.getChildArray(synonyms_parser, "Synonym");
			const size_t limit = std::min<size_t>(100, synonym_array.child_indices.size());
			for(size_t i=0; i<limit; ++i)
			{
				const JSONNode& synonym = synonyms_parser.nodes[synonym_array.child_indices[i]];
				if(synonym.type == JSONNode::Type_String) synonyms.push_back(synonym.getStringValue());
			}
		}
	}
	catch(const glare::Exception& e) { synonyms_error = e.what(); }
	catch(const std::exception& e) { synonyms_error = e.what(); }
	std::string result = "{\"source\":\"PubChem PUG REST\",\"cid\":" + toString(cid) + ",\"name\":" + quote(props.count("IUPACName") ? props.find("IUPACName")->second : "") + ",\"properties\":{";
	bool first = true;
	for(const auto& entry : props)
	{
		if(!first) result += ",";
		first = false;
		result += quote(entry.first) + ":" + quote(entry.second);
	}
	result += "},\"synonyms\":[";
	for(size_t i=0; i<synonyms.size(); ++i)
	{
		if(i) result += ",";
		result += quote(synonyms[i]);
	}
	result += "],\"synonyms_status\":" + quote(synonyms_error.empty() ? (synonyms.empty() ? "not_returned" : "available") : "unavailable") +
		(synonyms_error.empty() ? std::string() : ",\"synonyms_error\":" + quote(synonyms_error)) +
		",\"source_url\":" + quote("https://pubchem.ncbi.nlm.nih.gov/compound/" + toString(cid)) +
		",\"note\":\"Properties and synonyms describe the PubChem compound record. They do not by themselves mean a substance is a solution, mixture, or physical sample.\"}";
	return result;
}

inline std::string getRecord(const JSONParser& parser, const JSONNode& args)
{
	const double raw_cid = args.getChildDoubleValue(parser, "cid");
	if(!std::isfinite(raw_cid) || raw_cid < 1 || raw_cid > 2147483647.0 || raw_cid != std::floor(raw_cid))
		throw glare::Exception("cid must be a positive integer PubChem CID.");
	const int cid = (int)raw_cid;
	const std::string mode = args.getChildStringValue(parser, "mode");
	std::string path = "/rest/pug_view/";
	size_t max_bytes = 1024 * 1024;
	if(mode == "index")
		path += "index/compound/" + toString(cid) + "/JSON";
	else if(mode == "full")
	{
		path += "data/compound/" + toString(cid) + "/JSON";
		max_bytes = 4 * 1024 * 1024;
	}
	else if(mode == "section")
	{
		if(!args.hasChild("heading")) throw glare::Exception("mode=section requires a PubChem heading from get_pubchem_record(mode=index).");
		const std::string heading = args.getChildStringValue(parser, "heading");
		if(heading.empty() || heading.size() > 160 || heading.find_first_of("\r\n") != std::string::npos || heading.find('\0') != std::string::npos)
			throw glare::Exception("heading must contain 1..160 characters without line breaks.");
		path += "data/compound/" + toString(cid) + "/JSON?heading=" + urlEscape(heading);
		max_bytes = 2 * 1024 * 1024;
	}
	else throw glare::Exception("mode must be index, section, or full.");
	const std::string body = httpGet(path, max_bytes);
	JSONParser record_parser;
	parseJSON(body, record_parser);
	return "{\"source\":\"PubChem PUG View\",\"cid\":" + toString(cid) + ",\"mode\":" + quote(mode) +
		(mode == "section" ? ",\"heading\":" + quote(args.getChildStringValue(parser, "heading")) : std::string()) +
		",\"record\":" + body + ",\"source_url\":" + quote("https://pubchem.ncbi.nlm.nih.gov/compound/" + toString(cid)) +
		",\"note\":\"PUG View includes contributed annotations and may omit information; inspect the index and retrieve relevant sections. Molecular structure and coordinates come from PUG REST SDF.\"}";
}

inline int v2000Int(const std::string& line, size_t offset, size_t length, const char* field)
{
	if(line.size() < offset + length) throw glare::Exception(std::string("PubChem SDF has an incomplete ") + field + ".");
	const std::string value = line.substr(offset, length);
	char* end = NULL;
	const long parsed = std::strtol(value.c_str(), &end, 10);
	if(end == value.c_str() || parsed < 0 || parsed > 1000000) throw glare::Exception(std::string("PubChem SDF has an invalid ") + field + ".");
	return (int)parsed;
}

inline int v2000SignedInt(const std::string& line, size_t offset, size_t length, int minimum, int maximum, const char* field)
{
	if(line.size() < offset + length) throw glare::Exception(std::string("PubChem SDF has an incomplete ") + field + ".");
	const std::string value = line.substr(offset, length);
	char* end = NULL;
	const long parsed = std::strtol(value.c_str(), &end, 10);
	if(end == value.c_str() || end == NULL || *end != '\0' || parsed < minimum || parsed > maximum)
		throw glare::Exception(std::string("PubChem SDF has an invalid ") + field + ".");
	return (int)parsed;
}

inline PubChemMolecule parseSDF(int cid, const std::string& sdf, const std::map<std::string, std::string>& properties, const std::string& conformer)
{
	std::vector<std::string> lines;
	std::istringstream input(sdf);
	for(std::string line; std::getline(input, line); )
	{
		if(!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
		lines.push_back(line);
	}
	if(lines.size() < 4) throw glare::Exception("PubChem SDF is too short to contain a molecule.");
	// Cross-check the record identity embedded in the provider response. Formula
	// and atom-count checks alone cannot distinguish same-formula compounds.
	bool found_matching_cid = false;
	for(size_t i=0; i<lines.size(); ++i)
		if(lines[i] == "> <PUBCHEM_COMPOUND_CID>")
		{
			if(found_matching_cid)
				throw glare::Exception("PubChem SDF contains a duplicate PUBCHEM_COMPOUND_CID field; no molecule was created.");
			bool found_value = false;
			for(size_t value_i=i+1; value_i<lines.size() && !lines[value_i].empty(); ++value_i)
			{
				char* end = NULL;
				const long record_cid = std::strtol(lines[value_i].c_str(), &end, 10);
				if(end == lines[value_i].c_str() || *end != '\0' || record_cid < 1 || record_cid > 2147483647L)
					throw glare::Exception("PubChem SDF contains an invalid PUBCHEM_COMPOUND_CID field.");
				if(record_cid != cid)
					throw glare::Exception("PubChem SDF record CID does not match the requested CID; no molecule was created.");
				found_matching_cid = true;
				found_value = true;
				break;
			}
			if(!found_value)
				throw glare::Exception("PubChem SDF contains a PUBCHEM_COMPOUND_CID field without a value.");
		}
	if(!found_matching_cid)
		throw glare::Exception("PubChem SDF omitted PUBCHEM_COMPOUND_CID; refusing to create an unverified molecule.");
	const bool v3000 = sdf.find("V3000") != std::string::npos;
	int atom_count = 0, bond_count = 0;
	PubChemMolecule molecule;
	molecule.cid = cid;
	molecule.sdf = sdf;
	molecule.sdf_type = conformer;
	molecule.formula = properties.count("MolecularFormula") ? properties.find("MolecularFormula")->second : "";
	molecule.weight = properties.count("MolecularWeight") ? properties.find("MolecularWeight")->second : "";
	molecule.iupac_name = properties.count("IUPACName") ? properties.find("IUPACName")->second : "";
	molecule.inchi_key = properties.count("InChIKey") ? properties.find("InChIKey")->second : "";
	molecule.smiles = properties.count("SMILES") ? properties.find("SMILES")->second : properties.count("ConnectivitySMILES") ? properties.find("ConnectivitySMILES")->second : "";
	molecule.name = molecule.iupac_name.empty() ? ("PubChem CID " + toString(cid)) : molecule.iupac_name;
	std::ostringstream all_properties;
	all_properties << "provider\tPubChem\nCID\t" << cid << "\n";
	for(const auto& entry : properties) all_properties << tableCell(entry.first) << "\t" << tableCell(entry.second) << "\n";
	all_properties << "source_url\thttps://pubchem.ncbi.nlm.nih.gov/compound/" << cid << "\n";
	molecule.property_table = all_properties.str();
	if(v3000)
	{
		std::vector<std::string> records;
		std::string pending;
		for(const std::string& line : lines)
		{
			if(line.compare(0, 7, "M  V30 ") != 0) continue;
			std::string record = line.substr(7);
			if(!pending.empty()) pending += record; else pending = record;
			if(!pending.empty() && pending.back() == '-') { pending.pop_back(); continue; }
			records.push_back(pending);
			pending.clear();
		}
		if(!pending.empty()) throw glare::Exception("PubChem V3000 SDF ends with an incomplete continuation line.");
		bool in_atoms = false, in_bonds = false, found_counts = false;
		std::map<int, size_t> atom_indices;
		std::vector<PubChemBond> unresolved_bonds;
		auto parse_v30_integer = [](const std::string& value, const char* field) -> int {
			char* end = NULL;
			const long parsed = std::strtol(value.c_str(), &end, 10);
			if(end == value.c_str() || *end != '\0' || parsed < -1000000 || parsed > 1000000)
				throw glare::Exception(std::string("PubChem V3000 SDF has an invalid ") + field + ".");
			return (int)parsed;
		};
		for(const std::string& record : records)
		{
			std::istringstream row(record);
			std::string kind;
			row >> kind;
			if(kind == "COUNTS")
			{
				int atom_groups=0, chiral_centers=0, sgroups=0;
				if(found_counts || !(row >> atom_count >> bond_count >> atom_groups >> chiral_centers >> sgroups) || atom_count < 1 || bond_count < 0)
					throw glare::Exception("PubChem V3000 SDF has invalid COUNTS.");
				if(atom_count > 5000 || bond_count > 5000)
					throw glare::Exception("PubChem V3000 record exceeds the parser input budget (5,000 atoms or bonds).");
				(void)atom_groups; (void)chiral_centers; (void)sgroups;
				found_counts = true;
			}
			else if(kind == "BEGIN")
			{
				std::string section; row >> section;
				in_atoms = section == "ATOM"; in_bonds = section == "BOND";
			}
			else if(kind == "END")
			{
				std::string section; row >> section;
				if(section == "ATOM") in_atoms = false;
				if(section == "BOND") in_bonds = false;
			}
			else if(in_atoms)
			{
				PubChemAtom atom;
				int atom_map = 0;
				if(!(row >> atom.id >> atom.element >> atom.x >> atom.y >> atom.z >> atom_map) || atom.id < 1 || atom.id > 1000000 ||
					atom.element.empty() || atom.element.size() > 2 || !std::isfinite(atom.x) || !std::isfinite(atom.y) || !std::isfinite(atom.z) || atom_indices.count(atom.id))
					throw glare::Exception("PubChem V3000 SDF has an invalid or duplicate atom row.");
				(void)atom_map;
				atom.formal_charge = 0;
				atom.isotope_mass_number = 0;
				atom.atom_stereo_parity = 0;
				atom.atom_stereo_source.clear();
				bool atom_stereo_seen = false;
				for(std::string attribute; row >> attribute; )
				{
					if(attribute.compare(0, 4, "CHG=") == 0) atom.formal_charge = parse_v30_integer(attribute.substr(4), "atom charge");
					else if(attribute.compare(0, 5, "MASS=") == 0) atom.isotope_mass_number = parse_v30_integer(attribute.substr(5), "isotope mass");
					else if(attribute.compare(0, 4, "CFG=") == 0)
					{
						if(atom_stereo_seen) throw glare::Exception("PubChem V3000 SDF contains duplicate atom stereo CFG fields.");
						atom_stereo_seen = true;
						atom.atom_stereo_parity = parse_v30_integer(attribute.substr(4), "atom stereo configuration");
						if(atom.atom_stereo_parity < 0 || atom.atom_stereo_parity > 3)
							throw glare::Exception("PubChem V3000 SDF has an invalid atom stereo configuration.");
						if(atom.atom_stereo_parity != 0) atom.atom_stereo_source = "V3000_CFG";
					}
				}
				if(atom.formal_charge < -8 || atom.formal_charge > 8 || atom.isotope_mass_number < 0 || atom.isotope_mass_number > 400)
					throw glare::Exception("PubChem V3000 SDF has an invalid atom charge or isotope mass.");
				const int source_atom_id = atom.id;
				atom_indices[source_atom_id] = molecule.atoms.size();
				// The editor's atom/bond tables use one-based contiguous indices.
				// V3000 source IDs can be sparse, so normalize after recording the
				// source-ID mapping used to resolve its bond rows.
				atom.id = (int)molecule.atoms.size() + 1;
				molecule.atoms.push_back(atom);
			}
			else if(in_bonds)
			{
				int bond_id = 0;
				PubChemBond bond;
				if(!(row >> bond_id >> bond.order >> bond.atom_a >> bond.atom_b) || bond_id < 1 || bond.order < 1 || bond.order > 4)
					throw glare::Exception("PubChem V3000 SDF has an invalid bond row.");
				for(std::string attribute; row >> attribute; )
					if(attribute.compare(0, 4, "CFG=") == 0)
					{
						const int config = parse_v30_integer(attribute.substr(4), "bond stereo configuration");
						if(config < 1 || config > 3) throw glare::Exception("PubChem V3000 SDF has an invalid bond stereo configuration.");
						// V3000's "either" maps differently for single and double bonds
						// in the V2000 table consumed by the molecule editor.
						if(bond.order == 1) bond.stereo = config == 1 ? 1 : config == 2 ? 4 : 6;
						else if(bond.order == 2 && config == 2) bond.stereo = 3;
						else throw glare::Exception("PubChem V3000 SDF has a bond CFG unsupported for its bond order.");
					}
				if(!ScientificBondData::isValidSDFStereo(bond.order, bond.stereo))
					throw glare::Exception("PubChem V3000 SDF has stereochemistry that is invalid for its bond order.");
				unresolved_bonds.push_back(bond);
			}
		}
		if(!found_counts || atom_count != (int)molecule.atoms.size() || bond_count != (int)unresolved_bonds.size())
			throw glare::Exception("PubChem V3000 SDF counts do not match its atom and bond blocks.");
		std::set<std::pair<int, int>> seen_bonds;
		for(const PubChemBond& raw_bond : unresolved_bonds)
		{
			const auto a = atom_indices.find(raw_bond.atom_a), b = atom_indices.find(raw_bond.atom_b);
			if(a == atom_indices.end() || b == atom_indices.end() || a->second == b->second)
				throw glare::Exception("PubChem V3000 SDF bond references an invalid atom index.");
			if(!seen_bonds.insert(std::minmax(raw_bond.atom_a, raw_bond.atom_b)).second)
				throw glare::Exception("PubChem V3000 SDF contains a duplicate bond between the same atoms.");
			PubChemBond bond = raw_bond;
			bond.atom_a = (int)a->second + 1; bond.atom_b = (int)b->second + 1;
			molecule.bonds.push_back(bond);
		}
		for(size_t i=0; i<molecule.atoms.size(); ++i) molecule.atoms[i].id = (int)i + 1;
	}
	else
	{
		atom_count = v2000Int(lines[3], 0, 3, "atom count");
		bond_count = v2000Int(lines[3], 3, 3, "bond count");
		if(atom_count < 1 || atom_count > 5000 || bond_count > 5000 || lines.size() < (size_t)(4 + atom_count + bond_count))
			throw glare::Exception("PubChem V2000 molecule exceeds parser input limits or has an incomplete SDF record (maximum 5,000 atoms or bonds).");
		for(int i=0; i<atom_count; ++i)
		{
			const std::string& line = lines[(size_t)(4+i)];
			if(line.size() < 34) throw glare::Exception("PubChem SDF contains a truncated atom row.");
			PubChemAtom atom;
			char* end = NULL;
			const char* start = line.c_str();
			atom.x = std::strtod(start, &end); if(end == start || !std::isfinite(atom.x)) throw glare::Exception("PubChem SDF has an invalid atom X coordinate.");
			start = line.c_str() + 10; atom.y = std::strtod(start, &end); if(end == start || !std::isfinite(atom.y)) throw glare::Exception("PubChem SDF has an invalid atom Y coordinate.");
			start = line.c_str() + 20; atom.z = std::strtod(start, &end); if(end == start || !std::isfinite(atom.z)) throw glare::Exception("PubChem SDF has an invalid atom Z coordinate.");
			atom.element = line.substr(31, 3);
			atom.element.erase(0, atom.element.find_first_not_of(" \t"));
			while(!atom.element.empty() && (atom.element.back() == ' ' || atom.element.back() == '\t')) atom.element.pop_back();
			if(atom.element.empty() || atom.element.size() > 2) throw glare::Exception("PubChem SDF has an invalid element symbol.");
			atom.id = i + 1;
			atom.formal_charge = 0;
			atom.isotope_mass_number = 0;
			atom.atom_stereo_parity = 0;
			atom.atom_stereo_source.clear();
			const std::string charge_field = line.size() >= 39 ? line.substr(36, 3) : std::string();
			const bool has_charge_code = charge_field.find_first_not_of(" \t") != std::string::npos;
			const int charge_code = has_charge_code ? v2000Int(line, 36, 3, "atom charge") : 0;
			if(charge_code >= 1 && charge_code <= 3) atom.formal_charge = 4 - charge_code;
			else if(charge_code >= 5 && charge_code <= 7) atom.formal_charge = 4 - charge_code;
			if(line.size() >= 42 && line.substr(39, 3).find_first_not_of(" \t") != std::string::npos)
			{
				atom.atom_stereo_parity = v2000Int(line, 39, 3, "atom stereo parity");
				if(atom.atom_stereo_parity < 0 || atom.atom_stereo_parity > 3)
					throw glare::Exception("PubChem V2000 SDF has an unsupported atom stereo parity.");
				if(atom.atom_stereo_parity != 0) atom.atom_stereo_source = "V2000_parity";
			}
			molecule.atoms.push_back(atom);
		}
		std::set<int> charge_table_atoms, isotope_table_atoms;
		std::vector<int> mass_differences((size_t)atom_count, 0);
		bool has_iso_property_table = false;
		for(size_t line_i=(size_t)(4+atom_count+bond_count); line_i<lines.size(); ++line_i)
		{
			std::istringstream property(lines[line_i]);
			std::string marker, code;
			if(property >> marker >> code && marker == "M" && code == "ISO") has_iso_property_table = true;
		}
		for(int i=0; i<atom_count; ++i)
		{
			const std::string& line = lines[(size_t)(4+i)];
			if(line.size() < 36 || line.substr(34, 2).find_first_not_of(" \t") == std::string::npos) continue;
			const int mass_difference = v2000SignedInt(line, 34, 2, -3, 4, "atom mass difference (valid range -3..4)");
			mass_differences[(size_t)i] = mass_difference;
		}
		for(size_t line_i=(size_t)(4+atom_count+bond_count); line_i<lines.size(); ++line_i)
		{
			std::istringstream property(lines[line_i]);
			std::string marker, code;
			int count = 0;
			if(!(property >> marker >> code >> count) || marker != "M") continue;
			if(code != "CHG" && code != "ISO") continue;
			if(count < 0 || count > atom_count) throw glare::Exception("PubChem SDF has an invalid charge/isotope table.");
			for(int j=0; j<count; ++j)
			{
				int atom_id=0, value=0;
				if(!(property >> atom_id >> value) || atom_id < 1 || atom_id > atom_count)
					throw glare::Exception("PubChem SDF has an invalid charge/isotope entry.");
				if(code == "CHG")
				{
					if(value < -8 || value > 8 || !charge_table_atoms.insert(atom_id).second)
						throw glare::Exception("PubChem SDF has an invalid, out-of-range, or duplicate formal-charge entry.");
					molecule.atoms[(size_t)(atom_id-1)].formal_charge = value;
				}
				else
				{
					if(value < 1 || value > 400 || !isotope_table_atoms.insert(atom_id).second)
						throw glare::Exception("PubChem SDF has an invalid, out-of-range, or duplicate isotope-mass entry.");
					molecule.atoms[(size_t)(atom_id-1)].isotope_mass_number = value;
				}
			}
		}
		if(!has_iso_property_table)
			for(int i=0; i<atom_count; ++i)
			{
				const int mass_difference = mass_differences[(size_t)i];
				if(mass_difference == 0) continue;
				double average_mass = 0.0;
				PubChemAtom& atom = molecule.atoms[(size_t)i];
				if(!ScientificAtomicMass::averageMass(atom.element.c_str(), average_mass))
					throw glare::Exception("PubChem V2000 SDF uses an atom mass difference for an element without a periodic-table mass entry.");
				const int mass_number = (int)std::floor(average_mass + 0.5) + mass_difference;
				if(mass_number < 1 || mass_number > 400)
					throw glare::Exception("PubChem V2000 SDF atom mass difference resolves outside the supported isotope-mass range.");
				atom.isotope_mass_number = mass_number;
			}
		std::set<std::pair<int, int>> seen_bonds;
		for(int i=0; i<bond_count; ++i)
		{
			const std::string& line = lines[(size_t)(4+atom_count+i)];
			if(line.size() < 9) throw glare::Exception("PubChem SDF contains a truncated bond row.");
			PubChemBond bond;
			bond.atom_a = v2000Int(line, 0, 3, "bond atom index");
			bond.atom_b = v2000Int(line, 3, 3, "bond atom index");
			bond.order = v2000Int(line, 6, 3, "bond order");
			bond.stereo = line.size() >= 12 && line.substr(9, 3).find_first_not_of(" \t") != std::string::npos ? v2000Int(line, 9, 3, "bond stereo") : 0;
			if(bond.atom_a < 1 || bond.atom_b < 1 || bond.atom_a > atom_count || bond.atom_b > atom_count || bond.atom_a == bond.atom_b || bond.order < 1 || bond.order > 4)
				throw glare::Exception("PubChem SDF has an invalid atom index or bond order.");
			if(!ScientificBondData::isValidSDFStereo(bond.order, bond.stereo))
				throw glare::Exception("PubChem SDF has stereochemistry that is invalid for its bond order.");
			if(!seen_bonds.insert(std::minmax(bond.atom_a, bond.atom_b)).second)
				throw glare::Exception("PubChem SDF contains a duplicate bond between the same atoms.");
			molecule.bonds.push_back(bond);
		}
	}
	// Preserve disconnected PubChem records (for example, salts) but make their
	// component count explicit so callers do not mistake a multi-part structure
	// for one connected molecule.
	std::vector<std::vector<size_t>> atom_neighbours(molecule.atoms.size());
	for(const PubChemBond& bond : molecule.bonds)
	{
		const size_t atom_a = (size_t)(bond.atom_a - 1);
		const size_t atom_b = (size_t)(bond.atom_b - 1);
		atom_neighbours[atom_a].push_back(atom_b);
		atom_neighbours[atom_b].push_back(atom_a);
	}
	std::vector<bool> visited(molecule.atoms.size(), false);
	std::vector<size_t> stack;
	for(size_t atom_i = 0; atom_i < molecule.atoms.size(); ++atom_i)
	{
		if(visited[atom_i]) continue;
		++molecule.connected_component_count;
		visited[atom_i] = true;
		stack.push_back(atom_i);
		while(!stack.empty())
		{
			const size_t current = stack.back();
			stack.pop_back();
			for(const size_t neighbour : atom_neighbours[current])
				if(!visited[neighbour])
				{
					visited[neighbour] = true;
					stack.push_back(neighbour);
				}
		}
	}
	molecule.property_table += "connected_component_count\t" + toString(molecule.connected_component_count) + "\n";
	std::ostringstream atom_table, bond_table;
	atom_table << std::fixed << std::setprecision(6);
	for(const PubChemAtom& atom : molecule.atoms)
	{
		atom_table << atom.id << " " << atom.element << " " << atom.x << " " << atom.y << " " << atom.z << " " << atom.formal_charge << " " << atom.isotope_mass_number;
		if(atom.atom_stereo_parity != 0) atom_table << " sdf_atom_stereo=" << atom.atom_stereo_source << ":" << atom.atom_stereo_parity;
		atom_table << "\n";
	}
	for(const PubChemBond& bond : molecule.bonds)
	{
		bond_table << bond.atom_a << " " << bond.atom_b << " " << (bond.order == 2 ? "double" : bond.order == 3 ? "triple" : bond.order == 4 ? "aromatic" : "single");
		if(bond.stereo != 0) bond_table << " stereo:" << bond.stereo;
		bond_table << "\n";
	}
	molecule.atom_table = atom_table.str();
	molecule.bond_table = bond_table.str();
	return molecule;
}

inline PubChemMolecule fetchMolecule(int cid)
{
	if(cid <= 0) throw glare::Exception("cid must be a positive PubChem CID.");
	const std::map<std::string, std::string> properties = propertiesForCID(cid);
	std::string sdf;
	std::string conformer;
	try
	{
		sdf = httpGet("/rest/pug/compound/cid/" + toString(cid) + "/SDF?record_type=3d", 4 * 1024 * 1024);
		conformer = "3D SDF";
	}
	catch(const glare::Exception& e)
	{
		// Fall back only when PubChem says that this compound has no 3D record.
		// Rate limits, transient service errors and network failures must be
		// surfaced directly instead of triggering an unnecessary second request.
		const std::string error = e.what();
		if(error.find("HTTP 400") == std::string::npos && error.find("HTTP 404") == std::string::npos &&
			error.find("HTTP 204") == std::string::npos)
			throw;
		sdf = httpGet("/rest/pug/compound/cid/" + toString(cid) + "/SDF?record_type=2d", 4 * 1024 * 1024);
		conformer = "2D SDF (PubChem has no available 3D conformer)";
	}
	PubChemMolecule molecule = parseSDF(cid, sdf, properties, conformer);
	const std::map<std::string, std::string>::const_iterator covalent_units_it = properties.find("CovalentUnitCount");
	if(covalent_units_it == properties.end())
		throw glare::Exception("PubChem omitted CovalentUnitCount; no molecule was created because the SDF graph cannot be cross-checked.");
	char* covalent_units_end = NULL;
	const long parsed_covalent_units = std::strtol(covalent_units_it->second.c_str(), &covalent_units_end, 10);
	if(covalent_units_end == covalent_units_it->second.c_str() || *covalent_units_end != '\0' ||
		parsed_covalent_units < 1 || parsed_covalent_units > 5000)
		throw glare::Exception("PubChem returned an invalid CovalentUnitCount property; no molecule was created because the SDF graph cannot be cross-checked.");
	molecule.covalent_unit_count = (int)parsed_covalent_units;
	if(parsed_covalent_units != molecule.connected_component_count)
		throw glare::Exception("PubChem property/SDF covalent-unit mismatch for CID " + toString(cid) + " (property " + covalent_units_it->second + ", SDF graph " + toString(molecule.connected_component_count) + "); no molecule was created.");
	const std::map<std::string, std::string>::const_iterator formula_it = properties.find("MolecularFormula");
	const std::map<std::string, std::string>::const_iterator formula_no_charge_it = properties.find("MolecularFormulaNoCharge");
	if(formula_it == properties.end())
		throw glare::Exception("PubChem omitted MolecularFormula; no molecule was created because the downloaded structure cannot be cross-checked.");
	std::map<std::string, int> expected_elements, observed_elements;
	const std::map<std::string, std::string>::const_iterator charge_it = properties.find("Charge");
	if(charge_it == properties.end())
		throw glare::Exception("PubChem omitted the net Charge property; no molecule was created because the structure charge cannot be cross-checked.");
	char* charge_end = NULL;
	const double expected_charge = std::strtod(charge_it->second.c_str(), &charge_end);
	if(charge_end == charge_it->second.c_str() || *charge_end != '\0' || !std::isfinite(expected_charge) ||
		expected_charge != std::floor(expected_charge) || std::abs(expected_charge) > 40000)
		throw glare::Exception("PubChem returned an invalid net Charge property; no molecule was created.");
	const std::string formula_no_charge = formula_no_charge_it == properties.end() ? std::string() : formula_no_charge_it->second;
	if(!ScientificFormula::parsePubChemCounts(formula_it->second, formula_no_charge, (int)expected_charge, expected_elements))
		throw glare::Exception("PubChem returned inconsistent or unsupported molecular formula/charge data; no molecule was created.");
	for(const PubChemAtom& atom : molecule.atoms) observed_elements[atom.element]++;
	if(expected_elements != observed_elements)
		throw glare::Exception("PubChem property/SDF element counts disagree for CID " + toString(cid) + " (formula " + formula_it->second + "); no molecule was created.");
	int observed_charge = 0;
	for(const PubChemAtom& atom : molecule.atoms) observed_charge += atom.formal_charge;
	if(observed_charge != (int)expected_charge)
		throw glare::Exception("PubChem property/SDF formal-charge mismatch for CID " + toString(cid) + " (property " + charge_it->second + ", SDF " + toString(observed_charge) + "); no molecule was created.");
	const std::map<std::string, std::string>::const_iterator heavy = properties.find("HeavyAtomCount");
	if(heavy == properties.end())
		throw glare::Exception("PubChem omitted HeavyAtomCount; no molecule was created because the downloaded structure cannot be fully cross-checked.");
	char* heavy_end = NULL;
	const long parsed_heavy_count = std::strtol(heavy->second.c_str(), &heavy_end, 10);
	if(heavy_end == heavy->second.c_str() || *heavy_end != '\0' || parsed_heavy_count < 0 || parsed_heavy_count > 5000)
		throw glare::Exception("PubChem returned an invalid HeavyAtomCount property; no molecule was created because the SDF cannot be cross-checked.");
	int observed_heavy_atoms = 0;
	for(const PubChemAtom& atom : molecule.atoms) if(atom.element != "H") observed_heavy_atoms++;
	if((int)parsed_heavy_count != observed_heavy_atoms) throw glare::Exception("PubChem property/SDF atom-count mismatch; no molecule was created.");
	const std::map<std::string, std::string>::const_iterator isotope_count_it = properties.find("IsotopeAtomCount");
	if(isotope_count_it != properties.end())
	{
		char* isotope_count_end = NULL;
		const long parsed_isotope_count = std::strtol(isotope_count_it->second.c_str(), &isotope_count_end, 10);
		if(isotope_count_end == isotope_count_it->second.c_str() || *isotope_count_end != '\0' || parsed_isotope_count < 0 || parsed_isotope_count > (long)molecule.atoms.size())
			throw glare::Exception("PubChem returned an invalid IsotopeAtomCount property; no molecule was created because isotope data cannot be cross-checked.");
		int observed_isotope_count = 0;
		for(const PubChemAtom& atom : molecule.atoms) if(atom.isotope_mass_number > 0) ++observed_isotope_count;
		if(parsed_isotope_count != observed_isotope_count)
			throw glare::Exception("PubChem property/SDF isotope-atom count mismatch for CID " + toString(cid) + " (property " + isotope_count_it->second + ", SDF " + toString(observed_isotope_count) + "); no molecule was created.");
	}
	try
	{
		const std::map<int, std::vector<std::string>> synonyms_by_cid = synonymsForCIDs(std::vector<int>(1, cid));
		const auto synonyms_it = synonyms_by_cid.find(cid);
		if(synonyms_it != synonyms_by_cid.end()) molecule.synonyms = synonyms_it->second;
	}
	catch(const glare::Exception& e)
	{
		// Synonyms improve the editor label, but they are not required to validate
		// or construct the PubChem structure. Retain the provider failure in the
		// imported metadata instead of discarding an otherwise verified molecule.
		molecule.synonyms_error = e.what();
	}
	catch(const std::exception& e)
	{
		// Synonyms improve the editor label, but they are not required to validate
		// or construct the PubChem structure. Retain the provider failure in the
		// imported metadata instead of discarding an otherwise verified molecule.
		molecule.synonyms_error = e.what();
	}
	if(molecule.synonyms.empty() && molecule.synonyms_error.empty())
		molecule.synonyms_error = "PubChem returned no synonyms.";
	// CID 313's IUPACName is the technically valid but obscure "chlorane".
	// Prefer PubChem's explicit hydrogen-chloride synonym for the displayed
	// molecular structure, while keeping the IUPACName in the scientific record.
	if(cid == 313)
		for(const std::string& synonym : molecule.synonyms)
		{
			std::string lower = synonym;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return (char)std::tolower(c); });
			if(lower == "hydrogen chloride")
			{
				molecule.name = synonym;
				break;
			}
		}
	for(const std::string& synonym : molecule.synonyms)
		molecule.property_table += "synonym\t" + tableCell(synonym) + "\n";
	if(!molecule.synonyms_error.empty())
		molecule.property_table += std::string("synonyms_status\t") + (molecule.synonyms_error == "PubChem returned no synonyms." ? "none_returned\n" : "unavailable\n") +
			(molecule.synonyms_error == "PubChem returned no synonyms." ? std::string() : "synonyms_error\t" + tableCell(molecule.synonyms_error) + "\n");
	return molecule;
}

inline std::string search(const JSONParser& parser, const JSONNode& args)
{
	const std::string query = args.getChildStringValue(parser, "query");
	bool results_truncated = false;
	const std::vector<int> cids = resolveCIDs(query, results_truncated);
	const std::map<int, std::map<std::string, std::string>> properties = propertiesForCIDs(cids);
	std::map<int, std::vector<std::string>> synonyms;
	std::string synonyms_error;
	try
	{
		synonyms = synonymsForCIDs(cids);
	}
	catch(const glare::Exception& e)
	{
		// The CID and property response is sufficient to identify candidates.
		// A transient failure of the optional synonym endpoint must not make
		// name searches unusable or tempt the caller to invent a molecule.
		synonyms_error = e.what();
	}
	catch(const std::exception& e)
	{
		synonyms_error = e.what();
	}
	std::string result = "{\"source\":\"PubChem PUG REST\",\"query\":" + quote(query) + ",\"candidates\":[";
	for(size_t i=0; i<cids.size(); ++i)
	{
		const auto props_it = properties.find(cids[i]);
		if(props_it == properties.end()) throw glare::Exception("PubChem search result is missing properties for CID " + toString(cids[i]) + ".");
		const std::map<std::string, std::string>& props = props_it->second;
		if(i) result += ",";
		result += "{\"cid\":" + toString(cids[i]) + ",\"name\":" + quote(props.count("IUPACName") ? props.find("IUPACName")->second : "") +
			",\"formula\":" + quote(props.count("MolecularFormula") ? props.find("MolecularFormula")->second : "") +
			",\"molecular_weight\":" + quote(props.count("MolecularWeight") ? props.find("MolecularWeight")->second : "") +
			",\"exact_mass\":" + quote(props.count("ExactMass") ? props.find("ExactMass")->second : "") +
			",\"charge\":" + quote(props.count("Charge") ? props.find("Charge")->second : "") +
			",\"heavy_atom_count\":" + quote(props.count("HeavyAtomCount") ? props.find("HeavyAtomCount")->second : "") +
			",\"covalent_unit_count\":" + quote(props.count("CovalentUnitCount") ? props.find("CovalentUnitCount")->second : "") +
			",\"isotope_atom_count\":" + quote(props.count("IsotopeAtomCount") ? props.find("IsotopeAtomCount")->second : "") +
			",\"atom_stereo_count\":" + quote(props.count("AtomStereoCount") ? props.find("AtomStereoCount")->second : "") +
			",\"defined_atom_stereo_count\":" + quote(props.count("DefinedAtomStereoCount") ? props.find("DefinedAtomStereoCount")->second : "") +
			",\"undefined_atom_stereo_count\":" + quote(props.count("UndefinedAtomStereoCount") ? props.find("UndefinedAtomStereoCount")->second : "") +
			",\"bond_stereo_count\":" + quote(props.count("BondStereoCount") ? props.find("BondStereoCount")->second : "") +
			",\"defined_bond_stereo_count\":" + quote(props.count("DefinedBondStereoCount") ? props.find("DefinedBondStereoCount")->second : "") +
			",\"undefined_bond_stereo_count\":" + quote(props.count("UndefinedBondStereoCount") ? props.find("UndefinedBondStereoCount")->second : "") +
			",\"inchi_key\":" + quote(props.count("InChIKey") ? props.find("InChIKey")->second : "") +
			",\"smiles\":" + quote(props.count("SMILES") ? props.find("SMILES")->second : props.count("ConnectivitySMILES") ? props.find("ConnectivitySMILES")->second : "") +
			",\"synonyms\":[";
		const auto synonym_it = synonyms.find(cids[i]);
		if(synonym_it != synonyms.end())
			for(size_t synonym_i=0; synonym_i<synonym_it->second.size(); ++synonym_i)
			{
				if(synonym_i) result += ",";
				result += quote(synonym_it->second[synonym_i]);
			}
		result += "],\"synonyms_status\":" + quote(synonyms_error.empty() ?
			(synonym_it != synonyms.end() && !synonym_it->second.empty() ? "available" : "not_returned") : "unavailable") +
			",\"source_url\":" + quote("https://pubchem.ncbi.nlm.nih.gov/compound/" + toString(cids[i])) + "}";
	}
	result += "],\"results_truncated\":" + std::string(results_truncated ? "true" : "false");
	result += ",\"instruction\":\"If results_truncated is true, the candidate list is incomplete; refine the search or ask the user for an exact identifier before selecting a CID. If multiple candidates still match the requested compound, ask which CID they mean. Use create_pubchem_molecule only with the selected CID; do not invent atom coordinates or bonds.\"";
	if(!synonyms_error.empty()) result += ",\"synonyms_error\":" + quote(synonyms_error);
	return result + "}";
}

inline std::string create(const JSONParser& parser, const JSONNode& args, ServerAllWorldsState& all, ServerWorldState& world, const UserID& user, const std::string& username)
{
	const double raw_cid = args.getChildDoubleValue(parser, "cid");
	if(!std::isfinite(raw_cid) || raw_cid < 1 || raw_cid > 2147483647.0 || raw_cid != std::floor(raw_cid)) throw glare::Exception("cid must be an integer PubChem CID returned by search_pubchem_compounds.");
	const PubChemMolecule molecule = fetchMolecule((int)raw_cid);
	std::string display_name = molecule.name;
	const bool user_supplied_display_name = args.hasChild("display_name");
	if(user_supplied_display_name)
	{
		display_name = args.getChildStringValue(parser, "display_name");
		if(display_name.empty() || display_name.size() > 512)
			throw glare::Exception("display_name must contain 1..128 Unicode characters (at most 512 UTF-8 bytes).");
		bool contains_non_whitespace = false;
		validatePubChemDisplayName(display_name, contains_non_whitespace);
		while(!display_name.empty() && display_name.front() == ' ') display_name.erase(display_name.begin());
		while(!display_name.empty() && display_name.back() == ' ') display_name.pop_back();
		if(display_name.empty() || !contains_non_whitespace)
			throw glare::Exception("display_name cannot contain only whitespace.");
		const std::string normalized_display_name = normalizedPubChemName(display_name);
		bool is_confirmed_pubchem_name = normalized_display_name == normalizedPubChemName(molecule.iupac_name);
		for(const std::string& synonym : molecule.synonyms)
			is_confirmed_pubchem_name = is_confirmed_pubchem_name || normalized_display_name == normalizedPubChemName(synonym);
		if(!is_confirmed_pubchem_name)
			throw glare::Exception("display_name must match this PubChem CID's IUPACName or one of its returned synonyms; omit it to use the provider name.");
		// PubChem also lists solution/product synonyms under CID 313. The record
		// geometry is one isolated HCl molecule, so those aliases must not relabel
		// this single molecule as hydrochloric acid or a commercial solution.
		if(molecule.cid == 313 && normalized_display_name != "hydrogen chloride" && normalized_display_name != "chlorane")
			throw glare::Exception("CID 313 is one hydrogen chloride molecule, not hydrochloric acid solution; use display_name 'hydrogen chloride' or 'chlorane', or omit display_name.");
	}
	Vec3d pos(0,0,0);
	const bool has_pos = args.hasChild("pos");
	if(has_pos)
	{
		const JSONNode& position = args.getChildObject(parser, "pos");
		pos = Vec3d(position.getChildDoubleValue(parser,"x"),position.getChildDoubleValue(parser,"y"),position.getChildDoubleValue(parser,"z"));
		if(!std::isfinite(pos.x)||!std::isfinite(pos.y)||!std::isfinite(pos.z)||std::abs(pos.x)>1.0e7||std::abs(pos.y)>1.0e7||std::abs(pos.z)>1.0e7)
			throw glare::Exception("pos coordinates must be finite and within +/-10,000,000 metres.");
	}
	const double model_scale = args.hasChild("scale") ? args.getChildDoubleValue(parser, "scale") : 0.35;
	if(!std::isfinite(model_scale) || model_scale < 0.1875 || model_scale > 10.0)
		throw glare::Exception("scale must be finite and between 0.1875 and 10; it scales the molecule's coordinates, atoms and bonds together and remains editable without changing their proportions.");
	std::map<std::string, int> element_material;
	std::map<std::string, std::array<double,3>> element_colour;
	auto cpk = [](const std::string& e) -> std::array<double,3> {
		const char* hex = ScientificElementPalette::displayHexColour(e.c_str());
		if(!hex) throw glare::Exception("PubChem SDF contains an unknown element symbol: " + e);
		const unsigned long rgb = std::strtoul(hex, NULL, 16);
		return {{((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0}};
	};
	std::set<std::string> unique_elements;
	for(const PubChemAtom& atom : molecule.atoms) unique_elements.insert(atom.element);
	bool has_neutral_colour_fallback = false;
	for(const std::string& element : unique_elements)
	{
		element_material[element] = (int)element_material.size() + 1;
		element_colour[element] = cpk(element);
		if(!ScientificElementPalette::cpkHexColour(element.c_str())) has_neutral_colour_fallback = true;
	}
	size_t bond_part_count=0;
	for(const PubChemBond& bond : molecule.bonds)
	{
		if(bond.order==1 && bond.stereo==1) bond_part_count += 1; // closed tapered wedge primitive
		else if(bond.order==1 && (bond.stereo==4 || bond.stereo==6)) bond_part_count += 5; // hashed/unknown wedge
		else bond_part_count += bond.order==3 ? 3 : (bond.order==2 || bond.order==4 ? 2 : 1);
	}
	const size_t part_count = molecule.atoms.size() + bond_part_count;
	const size_t estimated_triangles = molecule.atoms.size() * 144 + bond_part_count * 32;
	if(part_count > 2048 || estimated_triangles > 100000)
		throw glare::Exception("This molecule exceeds the native mesh budget (2048 primitive parts or approximately 100,000 triangles). No object was created.");
	Vec3d center(0,0,0);
	for(const PubChemAtom& atom : molecule.atoms) center += Vec3d(atom.x,atom.y,atom.z);
	center = center / (double)molecule.atoms.size();
	auto modelPos = [&center, model_scale](const PubChemAtom& a)
	{
		const std::array<double, 3> p = ScientificMoleculeGeometry::sdfPositionToObjectSpace(
			a.x, a.y, a.z, center.x, center.y, center.z, 0.75 * model_scale);
		return Vec3d(p[0], p[1], p[2]);
	};
	std::ostringstream request;
	request << std::fixed << std::setprecision(6) << "{";
	if(has_pos) request << "\"pos\":{\"x\":" << pos.x << ",\"y\":" << pos.y << ",\"z\":" << pos.z << "},";
	request << "\"materials\":[{\"color\":[0.62,0.62,0.58],\"roughness\":0.5}";
	for(const auto& entry : element_colour) request << ",{\"color\":[" << entry.second[0] << "," << entry.second[1] << "," << entry.second[2] << "],\"roughness\":0.35}";
	request << "],\"parts\":[";
	bool first = true;
	for(const PubChemBond& bond : molecule.bonds)
	{
		const Vec3d a=modelPos(molecule.atoms[(size_t)(bond.atom_a-1)]), b=modelPos(molecule.atoms[(size_t)(bond.atom_b-1)]);
		const double dx=b.x-a.x, dy=b.y-a.y, dz=b.z-a.z;
		const double length=std::sqrt(dx*dx+dy*dy+dz*dz);
		if(length < 1.0e-8) throw glare::Exception("PubChem SDF contains a zero-length bond; no molecule was created.");
		const double hx=std::abs(dx/length)<0.8 ? 1.0 : 0.0, hy=hx==0.0 ? 1.0 : 0.0, hz=0.0;
		double ox=dy*hz-dz*hy, oy=dz*hx-dx*hz, oz=dx*hy-dy*hx;
		const double olen=std::sqrt(ox*ox+oy*oy+oz*oz);
		if(olen < 1.0e-8) throw glare::Exception("Could not construct a stable offset for a PubChem bond.");
		const double bond_radius=ScientificMoleculeGeometry::bondRadius(0.10 * model_scale);
		const bool solid_wedge=bond.order==1 && bond.stereo==1;
		const bool hashed_wedge=bond.order==1 && (bond.stereo==4 || bond.stereo==6);
		if(solid_wedge)
		{
			// Use the same centered triangular prism as the scientific editor:
			// narrow at atom A, widening toward atom B, with thickness normal to
			// the molecular plane.
			double wx=dx/length, wy=dy/length, wz=dz/length;
			double nx=0.0, ny=1.0, nz=0.0;
			double sx=wy*nz-wz*ny, sy=wz*nx-wx*nz, sz=wx*ny-wy*nx;
			double slen=std::sqrt(sx*sx+sy*sy+sz*sz);
			if(slen<1.0e-8)
			{
				nx=0.0; ny=0.0; nz=1.0;
				sx=wy*nz-wz*ny; sy=wz*nx-wx*nz; sz=wx*ny-wy*nx;
				slen=std::sqrt(sx*sx+sy*sy+sz*sz);
			}
			sx/=slen; sy/=slen; sz/=slen;
			// MeshBuilder applies XYZ Euler angles as Rz * Ry * Rx. Its local
			// axes map to (side, -plane_normal, bond_direction).
			const double ry=std::asin(std::max(-1.0,std::min(1.0,-sz)));
			const double cy=std::cos(ry);
			double rx=0.0, rz=0.0;
			if(std::abs(cy)>1.0e-8)
			{
				rx=std::atan2(-nz,wz);
				rz=std::atan2(sy,sx);
			}
			else
			{
				rx=ry>0.0 ? std::atan2(-nx,-ny) : std::atan2(nx,-ny);
			}
			const double degrees=180.0/3.14159265358979323846;
			request << "{\"shape\":\"tapered_wedge\",\"center\":[" << (a.x+b.x)*0.5 << "," << (a.y+b.y)*0.5 << "," << (a.z+b.z)*0.5
				<< "],\"size\":[" << ScientificMoleculeGeometry::wedgeHalfWidth(bond_radius)*2.0 << ","
				<< ScientificMoleculeGeometry::wedgeHalfThickness(bond_radius)*2.0 << "," << length
				<< "],\"rotation\":[" << rx*degrees << "," << ry*degrees << "," << rz*degrees << "],\"material\":0}";
			first=false;
			continue;
		}
		const int line_count=hashed_wedge ? 5 : bond.order==3 ? 3 : (bond.order==2 || bond.order==4 ? 2 : 1);
		const double spacing=ScientificMoleculeGeometry::bondParallelSpacing(bond_radius)/olen;
		for(int line=0; line<line_count; ++line)
		{
			if(!first) request << ","; first=false;
			if(hashed_wedge)
			{
				const double step=1.0/(double)line_count;
				const double start_t=(double)line*step + (hashed_wedge ? step*0.19 : 0.0);
				const double end_t=(double)(line+1)*step - (hashed_wedge ? step*0.19 : 0.0);
				const double radius=ScientificMoleculeGeometry::hashedBondRadius(bond_radius, ((double)line+0.5)/(double)line_count);
				request << "{\"shape\":\"tube\",\"path\":[[" << a.x+dx*start_t << "," << a.y+dy*start_t << "," << a.z+dz*start_t << "],[" << a.x+dx*end_t << "," << a.y+dy*end_t << "," << a.z+dz*end_t << "]],\"tube_radius\":" << radius << ",\"segments\":8,\"material\":0}";
				continue;
			}
			const double offset=line_count==1 ? 0.0 : ((double)line-(line_count-1)*0.5)*spacing;
			request << "{\"shape\":\"tube\",\"path\":[[" << a.x+ox*offset << "," << a.y+oy*offset << "," << a.z+oz*offset << "],[" << b.x+ox*offset << "," << b.y+oy*offset << "," << b.z+oz*offset << "]],\"tube_radius\":" << bond_radius << ",\"segments\":8,\"material\":0}";
		}
	}
	for(const PubChemAtom& atom : molecule.atoms)
	{
		if(!first) request << ","; first=false;
		const Vec3d p=modelPos(atom);
		const double radius=ScientificMoleculeGeometry::atomBaseRadius(false)*model_scale*ScientificMoleculeGeometry::cpkRadiusScale(atom.element);
		request << "{\"shape\":\"sphere\",\"center\":[" << p.x << "," << p.y << "," << p.z << "],\"size\":[" << radius*2 << "," << radius*2 << "," << radius*2 << "],\"segments\":12,\"rings\":6,\"material\":" << element_material[atom.element] << "}";
	}
	const std::string source_url = "https://pubchem.ncbi.nlm.nih.gov/compound/" + toString(molecule.cid);
	const std::string image_url = "https://pubchem.ncbi.nlm.nih.gov/rest/pug/compound/cid/" + toString(molecule.cid) + "/PNG?image_size=large";
	const std::string molecule_property_table = molecule.property_table + "image_url\t" + image_url + "\n";
	std::ostringstream summary;
	summary << "PubChem CID " << molecule.cid << "; formula " << molecule.formula << "; molecular weight " << molecule.weight << " Da; " << molecule.atoms.size() << " atoms; " << molecule.bonds.size() << " bonds; " << molecule.connected_component_count << " SDF graph component(s), matching PubChem CovalentUnitCount " << molecule.covalent_unit_count << "; coordinates from " << molecule.sdf_type << ". Source: " << source_url;
	if(!display_name.empty()) summary << " Display name: " << display_name << (user_supplied_display_name ? " (user-language label; chemical identity remains PubChem CID " + toString(molecule.cid) + ")." : " (PubChem record name).");
	if(!molecule.synonyms_error.empty()) summary << (molecule.synonyms_error == "PubChem returned no synonyms." ? " PubChem returned no synonyms." : " PubChem synonyms could not be loaded.");
	if(has_neutral_colour_fallback) summary << " Elements without a defined CPK colour use the neutral viewer fallback.";
	const std::string sdf_checksum = sha256Hex(molecule.sdf);
	const std::string property_table = molecule_property_table + "sdf_checksum_sha256\t" + sdf_checksum + "\n";
	const std::string packed_tables = ScientificObjectTableCodec::encode(molecule.atom_table, molecule.bond_table, "", "", property_table);
	std::ostringstream content_json;
	content_json << "{\"schema_version\":1,\"name\":" << quote(display_name) << ",\"iupac_name\":" << quote(molecule.iupac_name) << ",\"display_name_source\":" << quote(user_supplied_display_name ? "user" : "PubChem") << ",\"scientific_type\":\"molecule\",\"description\":" << quote(summary.str())
		<< ",\"source\":\"PubChem\",\"author\":\"PubChem record; contributor attribution may vary\",\"source_mode\":\"online\",\"source_url\":" << quote(source_url)
		<< ",\"online_database\":\"PubChem\",\"online_query\":" << quote(toString(molecule.cid))
		<< ",\"online_result_id\":" << quote(toString(molecule.cid)) << ",\"load_status\":\"ready\",\"load_status_message\":" << quote("Imported from PubChem PUG REST using " + molecule.sdf_type)
		<< ",\"data_origin\":\"provider\",\"provenance_source\":\"PubChem\",\"provenance_identifier\":" << quote("CID:" + toString(molecule.cid))
		<< ",\"provenance_url\":" << quote(source_url) << ",\"provenance_author\":\"PubChem record; contributor attribution may vary\",\"provenance_checksum\":" << quote(sdf_checksum) << ",\"provenance_format\":" << quote(molecule.sdf_type)
		<< ",\"provenance_version\":\"PubChem PUG REST\",\"provenance_license\":\"License not asserted; consult PubChem and the contributing source terms.\",\"" << ScientificObjectTableCodec::FIELD_NAME << "\":" << quote(packed_tables)
		<< ",\"data_summary\":" << quote(summary.str())
		<< ",\"image_url\":" << quote(image_url) << ",\"atom_count\":" << molecule.atoms.size() << ",\"bond_count\":" << molecule.bonds.size()
		<< ",\"connected_component_count\":" << molecule.connected_component_count << ",\"covalent_unit_count\":" << molecule.covalent_unit_count
		<< ",\"visualization_mode\":\"ball_and_stick\",\"colour_scheme\":\"CPK\",\"object_scale\":" << model_scale
		<< ",\"atom_radius\":" << model_scale << ",\"bond_thickness\":" << (0.1 * model_scale) << ",\"show_hydrogen\":true}";
	const std::string content = "metasiberia_scientific_object_v1\n" + content_json.str();
	if(content.size() > WorldObject::MAX_CONTENT_SIZE) throw glare::Exception("PubChem molecule metadata exceeds the 10 KB world-object content limit. No object was created; use a smaller compound or ask for a reduced scientific record.");
	request << "],\"content\":" << quote(content) << "}";
	const std::string request_json=request.str();
	JSONParser mesh_parser;
	mesh_parser.parseBuffer(request_json.data(), request_json.size());
	if(mesh_parser.nodes.empty() || mesh_parser.nodes[0].type!=JSONNode::Type_Object) throw glare::Exception("Internal PubChem mesh recipe was invalid; no object was created.");
	return MCPHandlers::Modeling::meshObject(all,world,mesh_parser,mesh_parser.nodes[0],user,username,/*allow_large_primitive_parts=*/true);
}
} // namespace MCPScientific
