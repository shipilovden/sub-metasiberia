/*=====================================================================
URLString.h
------------
Minimal adapter for URLString type and helpers.
=====================================================================*/
#pragma once

#include <string>
#include <cstring>
#include <utils/STLArenaAllocator.h>

// URLString is a std::basic_string with glare arena allocator to match usage in codebase
using URLString = std::basic_string<char, std::char_traits<char>, glare::STLArenaAllocator<char>>;

inline URLString toURLString(const std::string& s)
{
    return URLString(s.begin(), s.end());
}

inline URLString toURLString(const char* s)
{
    return URLString(s, s + std::strlen(s));
}

inline std::string toStdString(const URLString& s)
{
    return std::string(s.begin(), s.end());
}


