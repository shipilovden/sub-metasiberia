/*=====================================================================
URLParser.cpp
-------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "URLParser.h"


#include <networking/URL.h>
#include <utils/Parser.h>
#include <utils/Exception.h>
#include <utils/ConPrint.h>
#include <webserver/Escaping.h>


URLParseResults::URLParseResults()
:	x(0), y(0), z(0),
	lat(0), lon(0),
	heading(90.0),
	parcel_uid(0),
	sun_vert_angle(45),
	sun_azimuth_angle(45),
	parsed_sun_vert_angle(false),
	parsed_sun_azimuth_angle(false),
	parsed_x(false),
	parsed_y(false),
	parsed_z(false),
	parsed_lat(false),
	parsed_lon(false),
	parsed_map_query(false),
	parsed_heading(false),
	parsed_parcel_uid(false)
{}


bool URLParser::hasSupportedSchemePrefix(const std::string& URL)
{
	return URL.compare(0, 6, "sub://") == 0 || URL.compare(0, 6, "sib://") == 0;
}


URLParseResults URLParser::parseURL(const std::string& URL)
{
	URLParseResults res;

	Parser parser(URL);

	// Parse protocol
	if(URL.find_first_of(':') != std::string::npos) // Allow user to skip protocol prefix: If there is no ':' character in string, assume protocol prefix is missing and skip it.
	{
		string_view protocol;
		parser.parseAlphaToken(protocol);
		if(protocol != "sub" && protocol != "sib")
			throw glare::Exception("Unhandled protocol scheme '" + toString(protocol) + "'.");
		if(!parser.parseString("://"))
			throw glare::Exception("Expected '://' after protocol scheme.");
	}

	// Parse hostname and worldname, parcel number
	while(!parser.eof())
	{
		if(parser.current() == '/') // End of hostname, start of worldname if there is one.
		{
			parser.consume('/');

			if(parser.parseCString("parcel/"))
			{
				if(!parser.parseInt(res.parcel_uid))
					throw glare::Exception("Failed to parse parcel number");
				res.parsed_parcel_uid = true;
			}

			// Parse worldname, if present
			while(parser.notEOF() && parser.current() != '?')
			{
				res.worldname.push_back(parser.current());
				parser.advance();
			}

			res.worldname = web::Escaping::URLUnescape(res.worldname);

			break;
		}
		else if(parser.current() == '?')
			break;

		res.hostname += parser.current();
		parser.advance();
	}

	if(parser.currentIsChar('?'))
	{
		parser.consume('?');

		const std::string query_string = URL.substr(parser.currentPos());
		std::map<std::string, std::string> query_keyvalues = URL::parseQuery(query_string);

		processQueryKeyValues(query_keyvalues, res);
	}
	
	return res;
}


void URLParser::processQueryKeyValues(const std::map<std::string, std::string>& query_keyvalues, URLParseResults& res)
{
	if(query_keyvalues.count("x"))
	{
		res.x = stringToDouble(query_keyvalues.find("x")->second);
		res.parsed_x = true;
	}
	if(query_keyvalues.count("y"))
	{
		res.y = stringToDouble(query_keyvalues.find("y")->second);
		res.parsed_y = true;
	}
	if(query_keyvalues.count("z"))
	{
		res.z = stringToDouble(query_keyvalues.find("z")->second);
		res.parsed_z = true;
	}

	if(query_keyvalues.count("lat"))
	{
		res.lat = stringToDouble(query_keyvalues.find("lat")->second);
		res.parsed_lat = true;
	}
	if(query_keyvalues.count("lon"))
	{
		res.lon = stringToDouble(query_keyvalues.find("lon")->second);
		res.parsed_lon = true;
	}
	if(query_keyvalues.count("q"))
	{
		res.map_query = query_keyvalues.find("q")->second;
		res.parsed_map_query = true;
	}

	if(query_keyvalues.count("world"))
		res.worldname = query_keyvalues.find("world")->second; // An alternative way of specifying the world/user name

	if(query_keyvalues.count("heading"))
	{
		res.heading = stringToDouble(query_keyvalues.find("heading")->second);
		res.parsed_heading = true;
	}

	if(query_keyvalues.count("parcel"))
	{
		res.parcel_uid = stringToInt(query_keyvalues.find("parcel")->second);
		res.parsed_parcel_uid = true;
	}

	if(query_keyvalues.count("sun_vert_angle"))
	{
		res.sun_vert_angle = stringToDouble(query_keyvalues.find("sun_vert_angle")->second);
		res.parsed_sun_vert_angle = true;
	}

	if(query_keyvalues.count("sun_azimuth_angle"))
	{
		res.sun_azimuth_angle = stringToDouble(query_keyvalues.find("sun_azimuth_angle")->second);
		res.parsed_sun_azimuth_angle = true;
	}
}


#if BUILD_TESTS


#include "../utils/TestUtils.h"
#include "../maths/PCG32.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/Timer.h"


static void testInvalidURL(const std::string& URL)
{
	try
	{
		URLParser::parseURL(URL);
		failTest("Exception expected.");
	}
	catch(glare::Exception&)
	{}
}


static void testURLParseResultsEqual(const URLParseResults& a, const URLParseResults& b)
{
	testAssert(a.hostname == b.hostname);
	testAssert(a.worldname == b.worldname);
	testAssert(a.map_query == b.map_query);
	testAssert(a.x == b.x);
	testAssert(a.y == b.y);
	testAssert(a.z == b.z);
	testAssert(a.lat == b.lat);
	testAssert(a.lon == b.lon);
	testAssert(a.heading == b.heading);
	testAssert(a.parcel_uid == b.parcel_uid);
	testAssert(a.sun_vert_angle == b.sun_vert_angle);
	testAssert(a.sun_azimuth_angle == b.sun_azimuth_angle);
	testAssert(a.parsed_sun_vert_angle == b.parsed_sun_vert_angle);
	testAssert(a.parsed_sun_azimuth_angle == b.parsed_sun_azimuth_angle);
	testAssert(a.parsed_x == b.parsed_x);
	testAssert(a.parsed_y == b.parsed_y);
	testAssert(a.parsed_z == b.parsed_z);
	testAssert(a.parsed_lat == b.parsed_lat);
	testAssert(a.parsed_lon == b.parsed_lon);
	testAssert(a.parsed_map_query == b.parsed_map_query);
	testAssert(a.parsed_heading == b.parsed_heading);
	testAssert(a.parsed_parcel_uid == b.parsed_parcel_uid);
}


void URLParser::test()
{
	testAssert(hasSupportedSchemePrefix("sub://vr.metasiberia.com"));
	testAssert(hasSupportedSchemePrefix("sib://vr.metasiberia.com"));
	testAssert(!hasSupportedSchemePrefix("https://vr.metasiberia.com"));
	testAssert(!hasSupportedSchemePrefix("vr.metasiberia.com"));
	testAssert(!hasSupportedSchemePrefix("sib:/vr.metasiberia.com"));

	//----------------------------------- Test processQueryKeyValues by itself -----------------------------------
	{
		std::map<std::string, std::string> query_keyvalues;
		URLParseResults res;
		processQueryKeyValues(query_keyvalues, res);
		testAssert(!res.parsed_x);
		testAssert(!res.parsed_y);
		testAssert(!res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	{
		std::map<std::string, std::string> query_keyvalues;
		query_keyvalues["x"] = "1.0";
		query_keyvalues["z"] = "3.0";
		URLParseResults res;
		processQueryKeyValues(query_keyvalues, res);
		testAssert(res.parsed_x);
		testAssert(res.x == 1.0);
		testAssert(!res.parsed_y);
		testAssert(res.parsed_z);
		testAssert(res.z == 3.0);
		testAssert(!res.parsed_parcel_uid);
	}

	//----------------------------------- Test parseURL -----------------------------------
	{
		URLParseResults res = parseURL("sub://substrata.info");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "");
		testAssert(res.x == 0);
		testAssert(res.y == 0);
		testAssert(res.z == 0);
		testAssert(!res.parsed_x);
		testAssert(!res.parsed_y);
		testAssert(!res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "");
		testAssert(res.x == 0);
		testAssert(res.y == 0);
		testAssert(res.z == 0);
		testAssert(!res.parsed_x);
		testAssert(!res.parsed_y);
		testAssert(!res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/bleh");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "bleh");
		testAssert(res.x == 0);
		testAssert(res.y == 0);
		testAssert(res.z == 0);
		testAssert(!res.parsed_x);
		testAssert(!res.parsed_y);
		testAssert(!res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	// Test URL unescaping with escape sequence in world name
	{
		URLParseResults res = parseURL("sub://substrata.info/nick/my+cool+world");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "nick/my cool world");
		testAssert(res.x == 0);
		testAssert(res.y == 0);
		testAssert(res.z == 0);
		testAssert(!res.parsed_x);
		testAssert(!res.parsed_y);
		testAssert(!res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	// Test URL unescaping with escape sequence in world name
	{
		URLParseResults res = parseURL("sub://substrata.info/nick/my%3Fcool%3Fworld");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "nick/my?cool?world");
		testAssert(res.x == 0);
		testAssert(res.y == 0);
		testAssert(res.z == 0);
		testAssert(!res.parsed_x);
		testAssert(!res.parsed_y);
		testAssert(!res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	// Test URL unescaping with escape sequence in world name and position params
	{
		URLParseResults res = parseURL("sub://substrata.info/nick/my%3Fcool%3Fworld?x=1.0&y=2.0&z=3.0");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "nick/my?cool?world");
		testAssert(res.x == 1.0);
		testAssert(res.y == 2.0);
		testAssert(res.z == 3.0);
		testAssert(res.parsed_x);
		testAssert(res.parsed_y);
		testAssert(res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/bleh?x=1.0&y=2.0&z=3.0");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "bleh");
		testAssert(res.x == 1.0);
		testAssert(res.y == 2.0);
		testAssert(res.z == 3.0);
		testAssert(res.parsed_x);
		testAssert(res.parsed_y);
		testAssert(res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	// The Metasiberia URI scheme and the legacy sub:// scheme must produce
	// exactly the same parsed server, world, and navigation parameters.
	{
		const std::string path_and_query = "/nick/my%3Fworld?x=-1.25&y=2.5&z=3.75&lat=47.5&lon=-122.3&q=central_market&heading=180&parcel=102343&sun_vert_angle=35&sun_azimuth_angle=280";
		const URLParseResults sub_res = parseURL("sub://metasiberia.example" + path_and_query);
		const URLParseResults sib_res = parseURL("sib://metasiberia.example" + path_and_query);
		testURLParseResultsEqual(sub_res, sib_res);
		testAssert(sib_res.hostname == "metasiberia.example");
		testAssert(sib_res.worldname == "nick/my?world");
		testAssert(sib_res.x == -1.25);
		testAssert(sib_res.y == 2.5);
		testAssert(sib_res.z == 3.75);
		testAssert(sib_res.lat == 47.5);
		testAssert(sib_res.lon == -122.3);
		testAssert(sib_res.map_query == "central_market");
		testAssert(sib_res.heading == 180.0);
		testAssert(sib_res.parcel_uid == 102343);
		testAssert(sib_res.sun_vert_angle == 35.0);
		testAssert(sib_res.sun_azimuth_angle == 280.0);
		testAssert(sib_res.parsed_x && sib_res.parsed_y && sib_res.parsed_z);
		testAssert(sib_res.parsed_lat && sib_res.parsed_lon && sib_res.parsed_map_query);
		testAssert(sib_res.parsed_heading && sib_res.parsed_parcel_uid);
		testAssert(sib_res.parsed_sun_vert_angle && sib_res.parsed_sun_azimuth_angle);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/bleh?x=-1.0&y=-2.0");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "bleh");
		testAssert(res.x == -1.0);
		testAssert(res.y == -2.0);
		testAssert(res.z == 0);
		testAssert(res.parsed_x);
		testAssert(res.parsed_y);
		testAssert(!res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/bleh?x=-1.0&y=-2.0");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.worldname == "bleh");
		testAssert(res.x == -1.0);
		testAssert(res.y == -2.0);
		testAssert(res.z == 0);
		testAssert(res.parsed_x);
		testAssert(res.parsed_y);
		testAssert(!res.parsed_z);
		testAssert(!res.parsed_parcel_uid);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/parcel/10");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.parsed_parcel_uid);
		testAssert(res.parcel_uid == 10);
		testAssert(res.x == 0);
		testAssert(res.y == 0);
		testAssert(res.z == 0);
		testAssert(!res.parsed_x);
		testAssert(!res.parsed_y);
		testAssert(!res.parsed_z);
	}

	{
		URLParseResults res = parseURL("sub://substrata.info/parcel/102343");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.parsed_parcel_uid);
		testAssert(res.parcel_uid == 102343);
	}

	// Test parcel URL with coords as well. not sure this should be supported.
	{
		URLParseResults res = parseURL("sub://substrata.info/parcel/102343?x=-1.0&y=-2.0");
		testAssert(res.hostname == "substrata.info");
		testAssert(res.parsed_parcel_uid);
		testAssert(res.parcel_uid == 102343);
		testAssert(res.x == -1.0);
		testAssert(res.y == -2.0);
		testAssert(res.z == 0);
	}

	testInvalidURL("ub://substrata.info");
	testInvalidURL("sib:/substrata.info/world");
	testInvalidURL("sib://substrata.info?x=");
	testInvalidURL("https://substrata.info/world");
	//testInvalidURL("sub//substrata.info"); // We handle missing ':' now.
	//testInvalidURL("sub/substrata.info");
	//testInvalidURL("subsubstrata.info");
//	testInvalidURL("sub://substrata.info?");
//	testInvalidURL("sub://substrata.info?x");
	testInvalidURL("sub://substrata.info?x=");
	testInvalidURL("sub://substrata.info?x=A");
//	testInvalidURL("sub://substrata.info?x=1.0&");
	//testInvalidURL("sub://substrata.info?x=1.0&y");
	testInvalidURL("sub://substrata.info?x=1.0&y=");
	testInvalidURL("sub://substrata.info?x=1.0&y=A");
	//testInvalidURL("sub://substrata.info?x=1.0&y=1.0A"); // We'll allow garbage at the end for now.
//	testInvalidURL("sub://substrata.info?x=1.0&y=1.0&");
//	testInvalidURL("sub://substrata.info?x=1.0&y=1.0&z");
	testInvalidURL("sub://substrata.info?x=1.0&y=1.0&z=");
	//testInvalidURL("sub://substrata.info?x=1.0&y=1.0&z=A");
	//testInvalidURL("sub://substrata.info/parcel");
	testInvalidURL("sub://substrata.info/parcel/");
	testInvalidURL("sub://substrata.info/parcel/a");
}


#endif // BUILD_TESTS
