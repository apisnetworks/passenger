/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
#include "Exceptions.h"
#include "StrIntUtils.h"
#include <cstdio>
#include <cstdlib>

namespace Passenger {

string
fillInMiddle(unsigned int max, const string &prefix, const string &middle, const string &postfix) {
	if (max <= prefix.size() + postfix.size()) {
		throw ArgumentException("Impossible to build string with the given size constraint.");
	}
	
	unsigned int fillSize = max - (prefix.size() + postfix.size());
	if (fillSize > middle.size()) {
		return prefix + middle + postfix;
	} else {
		return prefix + middle.substr(0, fillSize) + postfix;
	}
}

void
split(const string &str, char sep, vector<string> &output) {
	string::size_type start, pos;
	start = 0;
	output.clear();
	while ((pos = str.find(sep, start)) != string::npos) {
		output.push_back(str.substr(start, pos - start));
		start = pos + 1;
	}
	output.push_back(str.substr(start));
}

string toString(const vector<string> &vec) {
	vector<StaticString> vec2;
	vec2.reserve(vec.size());
	for (vector<string>::const_iterator it = vec.begin(); it != vec.end(); it++) {
		vec2.push_back(*it);
	}
	return toString(vec2);
}

string toString(const vector<StaticString> &vec) {
	string result = "[";
	vector<StaticString>::const_iterator it;
	unsigned int i;
	for (it = vec.begin(), i = 0; it != vec.end(); it++, i++) {
		result.append("'");
		result.append(it->data(), it->size());
		if (i == vec.size() - 1) {
			result.append("'");
		} else {
			result.append("', ");
		}
	}
	result.append("]");
	return result;
}

string
pointerToIntString(void *pointer) {
	// Use wierd union construction to avoid compiler warnings.
	if (sizeof(void *) == sizeof(unsigned int)) {
		union {
			void *pointer;
			unsigned int value;
		} u;
		u.pointer = pointer;
		return toString(u.value);
	} else if (sizeof(void *) == sizeof(unsigned long long)) {
		union {
			void *pointer;
			unsigned long long value;
		} u;
		u.pointer = pointer;
		return toString(u.value);
	} else {
		fprintf(stderr, "Pointer size unsupported...");
		abort();
	}
}

unsigned long long
stringToULL(const StaticString &str) {
	unsigned long long result = 0;
	string::size_type i = 0;
	const char *data = str.data();
	
	while (data[i] == ' ' && i < str.size()) {
		i++;
	}
	while (data[i] >= '0' && data[i] <= '9' && i < str.size()) {
		result *= 10;
		result += data[i] - '0';
		i++;
	}
	return result;
}

unsigned long long
hexToULL(const StaticString &hex) {
	unsigned long long result = 0;
	string::size_type i = 0;
	bool done = false;
	
	while (i < hex.size() && !done) {
		char c = hex[i];
		if (c >= '0' && c <= '9') {
			result *= 16;
			result += c - '0';
		} else if (c >= 'a' && c <= 'f') {
			result *= 16;
			result += 10 + (c - 'a');
		} else if (c >= 'A' && c <= 'Z') {
			result *= 16;
			result += 10 + (c - 'A');
		} else {
			done = true;
		}
		i++;
	}
	return result;
}

string
toHex(const StaticString &data) {
	string result(data.size() * 2, '\0');
	toHex(data, (char *) result.data());
	return result;
}

static const char hex_chars[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
	'a', 'b', 'c', 'd', 'e', 'f'
};

void
toHex(const StaticString &data, char *output) {
	const char *data_buf = data.c_str();
	string::size_type i;
	
	for (i = 0; i < data.size(); i++) {
		output[i * 2] = hex_chars[(unsigned char) data_buf[i] / 16];
		output[i * 2 + 1] = hex_chars[(unsigned char) data_buf[i] % 16];
	}
}

string
integerToHex(long long value) {
	char buf[sizeof(long long) * 2 + 1];
	integerToHex(value, buf);
	return string(buf);
}

int
atoi(const string &s) {
	return ::atoi(s.c_str());
}

long
atol(const string &s) {
	return ::atol(s.c_str());
}

} // namespace Passenger
