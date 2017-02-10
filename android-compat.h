#pragma once
#ifdef ANDROID

#include <string>
#include <sstream>
#include <cstdlib>


namespace std
{
template <typename T>
	inline std::string to_string(T value)
	{
		std::ostringstream os;
		os << value;
		return os.str() ;
	}
	
	inline int stoi(const std::string& str)
	{
		return atoi(str.c_str());
	}
	
	
	inline double stod(const std::string& str)
	{
		return atof(str.c_str());
	}
}



inline std::basic_ostream<char> & operator<<(std::ostream &os, const char *v)
{
	os << std::string(v);
	return os;
}

#endif