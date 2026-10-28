/*
 * Copyright (C) 2010-2011 Daniel Richter <danielrichter2007@web.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 * 
 * Additional permission under GNU GPL version 3 section 7
 *
 * If you modify this program, or any covered work, by linking or combining
 * it with the OpenSSL library (or a modified version of that library),
 * containing parts covered by the terms of the OpenSSL license, the licensors
 * of this program grant you additional permission to convey the resulting work.
 * Corresponding source for a non-source form of such a combination shall include
 * the source code for the parts of the OpenSSL library used as well as that of
 * the covered work.
 */
#ifndef HELPER_H_INCLUDED
#define HELPER_H_INCLUDED

#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <string>
#include <map>
#include <vector>
#include "Exception.hpp"

# define ASSERT_VOID_CAST static_cast<void>

# define assert(expr)							\
  ((expr)								\
   ? ASSERT_VOID_CAST (0)						\
   : Helper::assert_fail (__STRING(expr), __FILE__, __LINE__, __func__))

class Helper {
	public: static void assert_fail(std::string const& expr, std::string const& file, int line, std::string const& func) {
		throw AssertException("Assertion `" + expr + "' failed. Function: " + func, file, line);
	}

	public: static void assert_filepath_empty(std::string const& filepath, std::string const& sourceCodeFile, int line) {
		FILE* file = fopen(filepath.c_str(), "r");
		if (file) {
			fclose(file);
			throw AssertException("found unexpected file on path: " + filepath, sourceCodeFile, line);
		}
	}

	public: static std::string md5(std::string const& input) {
		unsigned char buf[EVP_MAX_MD_SIZE];
		unsigned int bufLength = 0;
		if (EVP_Digest(input.data(), input.length(), buf, &bufLength, EVP_md5(), nullptr) != 1 || bufLength != 16) {
			throw LogicException("md5 calculation failed", __FILE__, __LINE__);
		}

		std::string result;
		for (int i = 0; i < 16; i++) {
			unsigned int a = (buf[i] - (buf[i] % 16)) / 16;
			if (a <= 9) {
				result += '0' + a;
			} else {
				result += 'a' + a - 10;
			}
			unsigned int b = (buf[i] % 16);
			if (b <= 9) {
				result += '0' + b;
			} else {
				result += 'a' + b - 10;
			}
		}
		assert(result.length() == 32);
		return result;
	}

	public: static std::string str_replace(const std::string &search, const std::string &replace, std::string subject) {
		size_t pos = 0;
		while (pos < subject.length() && (pos = subject.find(search, pos)) != -1){
			subject.replace(pos, search.length(), replace);
			pos += replace.length();
		}
		return subject;
	}

	/**
	 * creates a new private (0700) directory with a random name.
	 * Must be used instead of fixed file names in /tmp because Grub Customizer runs as root.
	 */
	public: static std::string createTempDir(std::string const& baseDir = "/tmp") {
		std::string pattern = baseDir + "/grub-customizer-XXXXXX";
		std::vector<char> buffer(pattern.begin(), pattern.end());
		buffer.push_back('\0');
		if (mkdtemp(buffer.data()) == nullptr) {
			throw FileSaveException("cannot create temporary directory in " + baseDir, __FILE__, __LINE__);
		}
		return std::string(buffer.data());
	}

	// creates the parent directory of the given file (mode 0700) if it doesn't exist yet
	public: static void createPrivateParentDir(std::string const& filePath) {
		size_t slashPos = filePath.find_last_of('/');
		if (slashPos == std::string::npos || slashPos == 0) {
			return;
		}
		std::string dir = filePath.substr(0, slashPos);
		struct stat dirInfo;
		if (lstat(dir.c_str(), &dirInfo) != 0) {
			mkdir(dir.c_str(), 0700);
		}
	}

	// wraps the given string into single quotes so it can be passed to /bin/sh as one word
	public: static std::string shellQuote(std::string const& input) {
		return "'" + str_replace("'", "'\\''", input) + "'";
	}

	/**
	 * calculates how a line of shell / grub script changes the nesting level of
	 * if/fi, for/while/until/done and case/esac blocks
	 */
	public: static int shellBlockDepthDelta(std::string const& line) {
		int delta = 0;
		bool commandPosition = true;
		std::string word;
		char quote = 0;
		for (size_t i = 0; i <= line.length(); i++) {
			char c = i < line.length() ? line[i] : '\n';
			if (quote) {
				if (c == quote) {
					quote = 0;
				} else if (c == '\\' && quote == '"') {
					i++;
				}
				continue;
			}
			if (c == '\'' || c == '"') {
				quote = c;
				word += c;
				continue;
			}
			if (c == '\\') {
				word += c;
				i++;
				continue;
			}
			if (c == '#' && word == "") {
				break; // comment
			}
			bool isSeparator = c == ';' || c == '&' || c == '|' || c == '(' || c == ')' || c == '\n';
			if (c == ' ' || c == '\t' || isSeparator) {
				if (word != "") {
					if (commandPosition) {
						if (word == "if" || word == "for" || word == "while" || word == "until" || word == "case") {
							delta++;
						} else if (word == "fi" || word == "done" || word == "esac") {
							delta--;
						}
					}
					commandPosition = word == "then" || word == "do" || word == "else" || word == "elif" || word == "!"
						|| word == "if" || word == "while" || word == "until";
					word = "";
				}
				if (isSeparator) {
					commandPosition = true;
				}
				continue;
			}
			word += c;
		}
		return delta;
	}

	public: static int shellBlockDepth(std::string const& script) {
		int depth = 0;
		size_t start = 0;
		while (start < script.length()) {
			size_t end = script.find('\n', start);
			if (end == std::string::npos) {
				end = script.length();
			}
			depth += shellBlockDepthDelta(script.substr(start, end - start));
			start = end + 1;
		}
		return depth < 0 ? 0 : depth;
	}

	public: static std::string escapeXml(std::string const& input) {
		return str_replace("<", "&lt;", str_replace("&", "&amp;", input));
	}

	public: static std::string ltrim(std::string string, std::string const& chars = " \t\n\r") {
		int first = string.find_first_not_of(chars);
		if (first != -1) {
			return string.substr(first);
		} else {
			return "";
		}
	}

	public: static std::string rtrim(std::string string, std::string const& chars = " \t\n\r") {
		string = std::string(string.rbegin(), string.rend());
		string = ltrim(string, chars);
		string = std::string(string.rbegin(), string.rend());
		return string;
	}

	public: static std::string trim(std::string string, std::string const& chars = " \t\n\r") {
		return rtrim(ltrim(string, chars));
	}

	public: static std::string str_replace_escape(std::string subject, char const& escapeCharacter, char const& replaceCharacter) {
		std::map<int, std::string> extractedEscapeSequences;

		size_t pos = 0;
		while (pos < subject.length() && (pos = subject.find(escapeCharacter, pos)) != -1){
			subject[pos] = replaceCharacter;
			subject[pos + 1] = replaceCharacter;
			pos += 2;
		}

		return subject;
	}

	public: static std::string str_escape(std::string subject, char const& escapeCharacter, std::string const& charactersToBeEscaped) {
		size_t pos = 0;
		while (pos < subject.length() && (pos = subject.find_first_of(charactersToBeEscaped, pos)) != -1){
			subject.replace(pos, 1, std::string(1, escapeCharacter) + subject[pos]);
			pos += 2;
		}
		return subject;
	}

	public: static std::string str_unescape(std::string subject, char const& escapeCharacter) {
		size_t pos = 0;
		while (pos < subject.length() && (pos = subject.find_first_of(escapeCharacter, pos)) != -1){
			subject.replace(pos, 2, subject.substr(pos + 1, 1));
			pos += 1;
		}
		return subject;
	}
};


#endif
