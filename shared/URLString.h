/*=====================================================================
URLString.h
-----------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once

#include <string>
#include <utils/StringUtils.h>

/*=====================================================================
URLString
---------
A string that represents a URL. Inherits from std::string to provide
full compatibility with string operations.
=====================================================================*/
class URLString : public std::string
{
public:
	// Constructors
	URLString() : std::string() {}
	URLString(const std::string& s) : std::string(s) {}
	URLString(const char* s) : std::string(s ? s : "") {}
	URLString(const std::string& s, const std::allocator<char>& alloc) : std::string(s, alloc) {}
	URLString(const URLString& s, const std::allocator<char>& alloc) : std::string(s, alloc) {}
	
	// Constructor for iterators
	template<typename Iterator>
	URLString(Iterator begin, Iterator end) : std::string(begin, end) {}
	
	// Constructor for OpenGLTextureKey
	template<typename Alloc>
	URLString(const std::basic_string<char, std::char_traits<char>, Alloc>& s) : std::string(s.begin(), s.end()) {}
	
	// Assignment operators
	URLString& operator = (const std::string& s) { 
		std::string::operator=(s); 
		return *this; 
	}
	URLString& operator = (const char* s) { 
		std::string::operator=(s ? s : ""); 
		return *this; 
	}
	URLString& operator += (const std::string& s) {
		std::string::operator+=(s);
		return *this;
	}
	URLString& operator += (const char* s) {
		std::string::operator+=(s);
		return *this;
	}
	URLString& operator += (char c) {
		std::string::operator+=(c);
		return *this;
	}
	
	// Conversion operators
	operator const std::string& () const { return static_cast<const std::string&>(*this); }
	operator std::string_view () const { return static_cast<const std::string&>(*this); }
	
	// Hash support
	size_t hash() const { return std::hash<std::string>{}(*this); }
};

// Hash specialization for URLString
namespace std {
	template<>
	struct hash<URLString> {
		size_t operator()(const URLString& url) const {
			return url.hash();
		}
	};
}

// Utility functions
inline URLString toURLString(const std::string& str) {
	return URLString(str);
}

inline URLString toURLString(const char* str) {
	return URLString(str);
}

inline std::string toStdString(const URLString& url) {
	return static_cast<const std::string&>(url);
}
