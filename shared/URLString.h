/*=====================================================================
URLString.h
------------
Minimal adapter for URLString type and helpers.
=====================================================================*/
#pragma once

#include <string>
#include <cstring>

// URLString is just a std::string for compatibility
using URLString = std::string;

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


