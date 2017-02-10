#pragma once
#include <string>
#include <vector>
#include <map>
#include <stack>

#include <math.h>

#ifndef NAN
#define NAN NaN
#endif

struct json_token;



struct JsonNode {
	enum class Type { Undefined, Object, Array, String, Number };
	
	const static JsonNode undefined;
	
	Type t;
	std::map<std::string, JsonNode> obj;
	std::vector<JsonNode> arr;
	std::string str;
	double num;

	inline void clear() {
		t = Type::Undefined;
		obj.clear();
		arr.clear();
		str.erase();
		num = NAN;
	}
	inline JsonNode() { clear(); }

	/*
	template<typename ...Args>
	JsonNode(Args... args) {
		const int size = sizeof...(args);
		for (int i = 0; i < size; i += 2) {
			const std::string k(const_cast<const std::string>(args[i]));
			(*this)[k] = args[i+1];
		}
	}*/

	JsonNode(std::initializer_list<std::string> keyValues) {
		t = Type::Undefined;

		for (auto it = keyValues.begin(); it != keyValues.end(); it++) {
			std::string k(*it);
			(*this)[k] = *(++it);
		}
	}

	JsonNode(const std::map<std::string, std::string> &map) {
		t = Type::Undefined;

		for (auto m : map) {
			(*this)[m.first] = m.second;
		}
	}

	JsonNode& operator[](const std::string &key);
	JsonNode& operator[](std::vector<JsonNode>::size_type index);
	const JsonNode& operator[](const std::string &key) const;
	const JsonNode& operator[](std::vector<JsonNode>::size_type index) const;

	// assign strings
	inline const std::string& operator=(const std::string &val) { t = Type::String; return str = val; }

	// assign generic number types
	template<typename T, typename = typename std::enable_if<std::is_arithmetic<T>::value, T>::type>
	inline const T& operator=(const T &val) { t = Type::Number; 	num = (double)val; return val; }

	// assign std::vectors
	template<typename T>
	inline const std::vector<T>& operator=(const std::vector<T> &val) {
		t = Type::Array;
		auto &ref(*this);
		for (size_t i = 0; i < val.size(); i++) {
			ref[i] = val[i];
		}
		return val;
	}



	friend std::ostream& operator<< (std::ostream& stream, const JsonNode& jd);
	bool tryParse(const std::string &str);
	bool tryParse(const char *str, int len);

	std::string toQueryString();


	inline bool isUndefined() const {
		return t == Type::Undefined;
	}

	inline bool isEmpty() const {
		switch (t) {
		case Type::Array:return arr.size() > 0;
		case Type::Number: return false;
		case Type::Object: return obj.size() > 0;
		case Type::String: return str.length() > 0;
		case Type::Undefined: return true;
		}
		throw std::runtime_error("Invalid JsonNode type in isEmpty()");
	}
};

struct JsonWalker {
	JsonNode &root;
	JsonWalker(JsonNode &root) : root(root) {
		stack.push(&root);
	}
	int parse(const std::string &jsonString);
	int parse(const char *jsonString, int len);
private:
	static void walking(void *callback_data, const char *name, size_t name_len, const char *path, const struct json_token *token);
	std::stack<JsonNode*> stack;
};