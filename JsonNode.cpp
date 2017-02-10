#include <iostream>
#include <iomanip>
#include <sstream>
#include <math.h>
#include <cmath>
#include <cctype>
#include <string>


#include "JsonNode.h"



#include "mongoose/mongoose.h"
#ifdef ANDROID
#include "android-compat.h"
#endif

#include "frozen/frozen.h"

const JsonNode JsonNode::undefined;

JsonNode& JsonNode::operator[](const std::string &key) {
	if (t == Type::Undefined) {
		t = Type::Object;
		obj[key] = JsonNode();
		return obj[key];
	}
	else if (t == Type::Object) {
		auto v = obj.find(key);
		if (v != obj.end()) {
			return v->second;
		}
		else {
			obj[key] = JsonNode();
			return obj[key];
		}
	}
	else if (t == Type::Array) {
		// auto parse key if this is already an array
		return (*this)[std::stoul(key)];
	}
	
	throw std::runtime_error("operator[] only on objects and arrays");
}


JsonNode& JsonNode::operator[](std::vector<JsonNode>::size_type index) {
	if (t == Type::Undefined) {
		t = Type::Array;
		arr = std::vector<JsonNode>(index + 1U, JsonNode());
		return arr[index];
	}
	else if (t == Type::Array) {
		if (index >= arr.size()) {
			arr.resize(index + 1U, JsonNode());
		}
		return arr[index];
	}

	throw std::runtime_error("operator[int] only on arrays");
}

JsonNode const& JsonNode::operator[](const std::string &key) const {
	if (t == Type::Array) {
		return (*this)[std::stoul(key)];
	}

	if (t != Type::Object) {
		return undefined;
	}

	auto v = obj.find(key);
	if (v == obj.end()) {
		return undefined;
	}

	return v->second;
}


JsonNode const& JsonNode::operator[](std::vector<JsonNode>::size_type index) const {
	if (t != Type::Array || index >= arr.size()) {
		return undefined;
	}
	return arr[index];
}


bool JsonNode::tryParse(const std::string &str)
{
	try {
		JsonWalker walker(*this);
		return walker.parse(str) > 0;
	}
	catch (...) {
		return false;
	}
}


bool JsonNode::tryParse(const char *str, int len)
{
	try {
		JsonWalker walker(*this);
		return walker.parse(str, len) > 0;
	}
	catch (...) {
		return false;
	}
}




std::string url_encode(const std::string &value) {
    using namespace std;
	ostringstream escaped;
	escaped.fill('0');
	escaped << hex;

	for (string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
		string::value_type c = (*i);

		// Keep alphanumeric and other accepted characters intact
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			escaped << c;
			continue;
		}

		// Any other characters are percent-encoded
		escaped << uppercase;
		escaped << '%' << setw(2) << int((unsigned char)c);
		escaped << nouppercase;
	}

	return escaped.str();
}

// this only works on objects with strings
std::string JsonNode::toQueryString() {
	std::stringstream ss;
	
	if(t != Type::Object)
		throw std::runtime_error("JsonNode::toQueryString() does only work on string maps!");

	for (auto &a : obj) {
		ss << url_encode(a.first) << "=";
		switch (a.second.t)
		{
		case Type::String: ss << url_encode(a.second.str); break;
		case Type::Number: {
            double d = a.second.num;
            double r = round(d);
			if (std::abs(d - r) < 0.001) ss << (int)r;
			else ss << d;
		}
			break;
		default:
			throw std::runtime_error("JsonNode::toQueryString() does only work on string maps!");
			break;
		}
		ss << "&";	
	}
	auto str = ss.str();
	str.pop_back(); // remove &
	return str;
}

std::ostream& operator<< (std::ostream& stream, const JsonNode& jd) {
	static const std::string empty("");
	static const std::string sep(",");

	switch (jd.t) {
	case JsonNode::Type::Number: stream << jd.num; break;
	case JsonNode::Type::String: stream << "\"" << jd.str << "\""; break;
	case JsonNode::Type::Object: {
		stream << "{";
		auto l = jd.obj.end(); l--;
		for (auto it = jd.obj.begin(); it != jd.obj.end(); it++) {
			stream << "\"" << it->first << "\":" << it->second << (it == l ? empty : sep);
		}
		stream << "}"; }
		break;


	case JsonNode::Type::Array:
		stream << "[";
		for (auto it = jd.arr.begin(); it != jd.arr.end(); it++) {
			stream << *it << (it == (jd.arr.end()-1) ? empty : sep);
		}
		stream << "]";
		break;
	case JsonNode::Type::Undefined:
		stream << "null";
		break;
	}
	return stream;
}

int JsonWalker::parse(const std::string &jsonString) {
	return parse(jsonString.c_str(), jsonString.size());
}


int JsonWalker::parse(const char *jsonString, int len) {
	return json_walk(jsonString, len, &walking, this);
}

void JsonWalker::walking(void *callback_data, const char *name, size_t name_len, const char *path, const struct json_token *token)
{
	auto jwd = (JsonWalker*)callback_data;

	JsonNode &current(*jwd->stack.top());
	JsonNode &jn(name ? current[std::string(name, name + name_len)] : current);

	switch (token->type) {
	case JSON_TYPE_NUMBER:
		jn.t = JsonNode::Type::Number;
		jn.num = std::stod(std::string(token->ptr, token->len));
		break;

	case JSON_TYPE_STRING:
		jn.t = JsonNode::Type::String;
		jn.str = std::string(token->ptr, token->len);
		break;

	case JSON_TYPE_TRUE:
		jn.t = JsonNode::Type::Number;
		jn.num = 1;
		break;

	case JSON_TYPE_FALSE:
	case JSON_TYPE_NULL:
		jn.t = JsonNode::Type::Number;
		jn.num = 0;
		break;

	case JSON_TYPE_OBJECT_START:
		jn.t = JsonNode::Type::Object;
		jwd->stack.push(&jn);
		break;

	case JSON_TYPE_ARRAY_START:
		jn.t = JsonNode::Type::Object;
		jwd->stack.push(&jn);
		break;

	case JSON_TYPE_ARRAY_END:
	case JSON_TYPE_OBJECT_END:
		jwd->stack.pop();
		break;

	default:
		throw std::runtime_error("invalid JSON token type");
		break;
	}
}
