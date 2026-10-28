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
#ifndef SETTING_MANAGER_DATASTORE_INCLUDED
#define SETTING_MANAGER_DATASTORE_INCLUDED
#include <sys/stat.h> //mkdir
#include "../lib/Trait/LoggerAware.hpp"
#include <sstream>
#include "../lib/Helper.hpp"
#include <map>
#include <algorithm>
#include <vector>
#include <list>
#include <dirent.h>
#include "Env.hpp"
#include "SettingsStore.hpp"

class Model_SettingsManagerData :
	public Model_SettingsStore,
	public Trait_LoggerAware,
	public Model_Env_Connection
{
	bool _reloadRequired;
public:
	bool color_helper_required;
	std::string grubFont, oldFontFile;
	int grubFontSize;
	Model_SettingsManagerData() : _reloadRequired(false), color_helper_required(false), grubFontSize(-1)
	{
	}

	bool reloadRequired() const {
		return this->_reloadRequired;
	}

	static std::map<std::string, std::string> parsePf2(std::string const& fileName) {
		std::map<std::string, std::string> result;
		FILE* file = fopen(fileName.c_str(), "rb");
		if (file) {
			while (true) {
				char nameBuf [5]; // stores 4 bytes of data (possible \0 at pos 5)
				if (fgets(nameBuf, 5, file) == NULL || nameBuf == std::string("CHIX") || nameBuf == std::string("DATA")) {
					break;
				}
				
				char* getsResult = nullptr;
	
				char sizeBuf [5];
				getsResult = fgets(sizeBuf, 5, file);
				if (getsResult == nullptr) {
					continue;
				}
				unsigned int size = sizeBuf[3] + (sizeBuf[2] << 8) + (sizeBuf[1] << 16) + (sizeBuf[0] << 24);
	
				char* contentBuf = new char[size + 1];
	
				getsResult = fgets(contentBuf, size + 1, file);
				if (getsResult == nullptr) {
					delete[] contentBuf;
					continue;
				}
	
				result[nameBuf] = contentBuf;
				delete[] contentBuf;
			}
			fclose(file);
		}
		return result;
	}

	static std::string readCommandOutput(std::string const& cmd) {
		std::string result;
		FILE* proc = popen(cmd.c_str(), "r");
		if (!proc) {
			return result;
		}
		int c;
		while ((c = getc(proc)) != EOF) {
			result += char(c);
		}
		pclose(proc);
		return result;
	}

	// escapes characters with a special meaning in fontconfig patterns
	static std::string escapeFontconfigValue(std::string const& value) {
		return Helper::str_escape(value, '\\', "\\-:,");
	}

	/**
	 * finds the font file for names like "DejaVu Sans Mono Book 16" (GTK font chooser)
	 * or "DejaVu Sans Mono Regular 16" (NAME of a pf2 file). The name doesn't say where the
	 * family name ends and the style begins, so every split is tried with an exact family match.
	 * Without this, any style except a few hardcoded ones made fc-match fall back to another font.
	 */
	/**
	 * splits a font name into words without the size. grub-mkfont appends its own style (Regular, Bold, Italic,
	 * Bold Italic) to the family name, so that suffix is removed when the name already contains the style:
	 * "DejaVu Sans Bold Bold 16" => "DejaVu Sans Bold", "DejaVu Sans Mono Regular 16" => "DejaVu Sans Mono"
	 */
	static std::vector<std::string> getNormalizedFontNameWords(std::string const& name) {
		std::vector<std::string> words;
		std::istringstream stream(name);
		std::string word;
		while (stream >> word) {
			words.push_back(word);
		}
		if (words.size() > 1 && words.back().find_first_not_of("0123456789.") == std::string::npos) {
			words.pop_back(); // font size
		}
		auto contains = [&words](size_t count, std::string const& part) {
			for (size_t i = 0; i < count; i++) {
				if (words[i].find(part) != std::string::npos) {
					return true;
				}
			}
			return false;
		};
		bool changed = true;
		while (changed && words.size() > 1) {
			changed = false;
			size_t n = words.size();
			if (words.back() == "Regular") {
				words.pop_back();
				changed = true;
			} else if (n > 2 && words[n - 2] == "Bold" && words[n - 1] == "Italic" && contains(n - 2, "Bold") && (contains(n - 2, "Italic") || contains(n - 2, "Oblique"))) {
				words.erase(words.end() - 2, words.end());
				changed = true;
			} else if (words.back() == "Bold" && contains(n - 1, "Bold")) {
				words.pop_back();
				changed = true;
			} else if (words.back() == "Italic" && (contains(n - 1, "Italic") || contains(n - 1, "Oblique"))) {
				words.pop_back();
				changed = true;
			}
		}
		return words;
	}

	static std::string getNormalizedFontName(std::string const& name) {
		std::string result;
		for (auto const& word : getNormalizedFontNameWords(name)) {
			result += (result == "" ? "" : " ") + word;
		}
		return result;
	}

	// name of a pf2 font as shown in the font chooser ("DejaVu Sans Bold Bold 16" => "DejaVu Sans Bold 16")
	static std::string getDisplayFontName(std::string const& pf2Name) {
		if (pf2Name == "") {
			return "";
		}
		std::string result = getNormalizedFontName(pf2Name);
		size_t lastSpace = pf2Name.find_last_of(' ');
		if (lastSpace != std::string::npos && pf2Name.substr(lastSpace + 1).find_first_not_of("0123456789.") == std::string::npos) {
			result += pf2Name.substr(lastSpace);
		}
		return result;
	}

	static std::string getFontFileByName(std::string const& name) {
		std::vector<std::string> words = getNormalizedFontNameWords(name);

		for (size_t familyWordCount = words.size(); familyWordCount >= 1; familyWordCount--) {
			std::string family, style;
			for (size_t i = 0; i < words.size(); i++) {
				std::string& target = i < familyWordCount ? family : style;
				target += (target == "" ? "" : " ") + words[i];
			}
			// the regular style has different names (DejaVu: "Book")
			std::list<std::string> wantedStyles;
			if (style == "" || style == "Regular" || style == "Book" || style == "Normal" || style == "Roman") {
				wantedStyles = {"Regular", "Book", "Normal", "Roman"};
			} else {
				wantedStyles = {style};
				// "ExtraBold Bold" (style added by grub-mkfont) => "ExtraBold"
				size_t lastSpace = style.find_last_of(' ');
				if (lastSpace != std::string::npos) {
					wantedStyles.push_back(style.substr(0, lastSpace));
				}
			}

			// one line per font: "<style list>|<file>"
			std::istringstream fonts(readCommandOutput("fc-list --format '%{style}|%{file}\\n' " + Helper::shellQuote(escapeFontconfigValue(family))));
			std::string line, bestFile, anyFile;
			int bestRank = 0;
			while (std::getline(fonts, line)) {
				size_t separatorPos = line.find('|');
				if (separatorPos == std::string::npos) {
					continue;
				}
				std::string file = line.substr(separatorPos + 1);
				std::list<std::string> styles;
				std::istringstream styleStream(line.substr(0, separatorPos));
				std::string currentStyle;
				while (std::getline(styleStream, currentStyle, ',')) {
					styles.push_back(currentStyle);
				}
				// the first style is the most specific one ("Black" vs. "Regular" for "Inter Black")
				// earlier wanted styles are better, a match of the first style is better than any match
				int rank = 0;
				int priority = wantedStyles.size() * 2;
				for (auto const& wantedStyle : wantedStyles) {
					if (styles.size() && styles.front() == wantedStyle) {
						rank = std::max(rank, priority);
					} else if (std::find(styles.begin(), styles.end(), wantedStyle) != styles.end()) {
						rank = std::max(rank, priority - 1);
					}
					priority -= 2;
				}
				if (rank > bestRank) {
					bestRank = rank;
					bestFile = file;
				}
				if (anyFile == "") {
					anyFile = file;
				}
			}
			if (bestFile != "") {
				return bestFile;
			}
			if (style == "" && anyFile != "") {
				return anyFile;
			}
		}

		// no exact match: let fontconfig choose the closest font
		std::string translatedName;
		for (auto const& w : words) {
			translatedName += (translatedName == "" ? "" : " ") + w;
		}
		return readCommandOutput("fc-match -f '%{file[0]}' " + Helper::shellQuote(escapeFontconfigValue(translatedName)));
	}


	std::string mkFont(std::string fontFile = "", std::string outputPath = "") {
		int fontSize = -1;
		if (fontFile == "") {
			fontFile = Model_SettingsManagerData::getFontFileByName(this->grubFont);
			fontSize = this->grubFontSize;
		}
		std::string sizeParam;
		if (fontSize != -1) {
			std::ostringstream stream;
			stream << fontSize;
			sizeParam = " --size=" + Helper::shellQuote(stream.str());
			if (fontSize > 72) {
				this->log("Error: font too large: " + stream.str() + "!", Logger::ERROR);
				return ""; // fehler
			}
		}
		outputPath = outputPath != "" ? outputPath : this->env->output_config_dir_noprefix + "/unicode.pf2";
		std::string nameParam;
		if (this->grubFont != "" && fontSize != -1) {
			// keep the chosen style (like "Light") in the font name - grub-mkfont would only add Regular/Bold/Italic
			nameParam = " --name=" + Helper::shellQuote(Model_SettingsManagerData::getNormalizedFontName(this->grubFont));
		}
		std::string cmd = this->env->mkfont_cmd + " --output=" + Helper::shellQuote(outputPath) + sizeParam + nameParam + " " + Helper::shellQuote(fontFile) + " 2>&1";
		this->log("running " + cmd, Logger::INFO);
		FILE* mkfont_proc = popen(cmd.c_str(), "r");
		int c;
	//	std::string row = "";
		while ((c = fgetc(mkfont_proc)) != EOF) {
	//		if (c == '\n') {
	//			this->log("MKFONT: " + row, Logger::INFO);
	//		} else {
	//			row += char(c);
	//		}
		}
		int result = pclose(mkfont_proc);
		if (result != 0) {
			this->log("error running " + this->env->mkfont_cmd, Logger::ERROR);
			return "";
		}
		this->setValue("GRUB_FONT", outputPath);
		return outputPath;
	}

	// returns the path of DejaVu Sans Mono inside of the target system (the location depends on the distribution)
	std::string findFallbackFont() {
		char const* candidates[] = {
			"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", // Debian, Ubuntu
			"/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf", // Fedora
			"/usr/share/fonts/TTF/DejaVuSansMono.ttf", // Arch
			"/usr/share/fonts/dejavu/DejaVuSansMono.ttf"
		};
		for (char const* candidate : candidates) {
			if (this->env->check_file(this->env->cfg_dir_prefix + candidate)) {
				return candidate;
			}
		}
		return candidates[0];
	}

	/**
	 * grub-mkconfig also reads the .cfg files in /etc/default/grub.d after /etc/default/grub.
	 * Returns the settings (name => file) which are assigned there and therefore
	 * override the values in /etc/default/grub. Settings which only extend
	 * the existing value (like X="$X foo") are ignored.
	 */
	std::map<std::string, std::string> getOverriddenSettings() {
		std::map<std::string, std::string> result;
		std::string dirPath = this->env->settings_file + ".d";
		DIR* dir = opendir(dirPath.c_str());
		if (!dir) {
			return result;
		}
		std::list<std::string> files;
		struct dirent* entry;
		while ((entry = readdir(dir))) {
			std::string fileName = entry->d_name;
			if (fileName.length() > 4 && fileName.substr(fileName.length() - 4) == ".cfg") {
				files.push_back(fileName);
			}
		}
		closedir(dir);
		files.sort();
		for (auto const& fileName : files) {
			FILE* file = fopen((dirPath + "/" + fileName).c_str(), "r");
			if (!file) {
				continue;
			}
			Model_SettingsStore store(file);
			fclose(file);
			for (auto iter = store.begin(); iter != store.end(); store.iter_to_next_setting(iter)) {
				if (!iter->isActive || iter->name == "") {
					continue;
				}
				if (iter->value.find("$" + iter->name) != std::string::npos || iter->value.find("${" + iter->name + "}") != std::string::npos) {
					continue;
				}
				result[iter->name] = dirPath.substr(this->env->cfg_dir_prefix.size()) + "/" + fileName;
			}
		}
		return result;
	}

	bool load() {
		settings.clear();
	
		FILE* file = fopen(this->env->settings_file.c_str(), "r");
		if (file){
			Model_SettingsStore::load(file);
			this->grubFontSize = -1;
			if (this->getValue("GRUB_FONT") != "") {
				this->oldFontFile = this->getValue("GRUB_FONT");
				this->log("parsing " + this->getValue("GRUB_FONT"), Logger::INFO);
				this->grubFont = Model_SettingsManagerData::getDisplayFontName(
					Model_SettingsManagerData::parsePf2(this->env->cfg_dir_prefix + this->getValue("GRUB_FONT"))["NAME"]
				);
				this->log("result " + this->grubFont, Logger::INFO);
				this->removeItem("GRUB_FONT");
			}
	
			fclose(file);
			return true;
		}
		else
			return false;
	}

	bool save() {
		const char* background_script = "\
	#! /bin/sh -e\n\
	# Name of this script: 'grub_background.sh'\n\
	\n\
	   WALLPAPER=\"/usr/share/images/desktop-base/background\"\n\
	   COLOR_NORMAL=\"light-gray/black\"\n\
	   COLOR_HIGHLIGHT=\"magenta/black\"\n\
	\n\
	if [ \"${GRUB_MENU_PICTURE}\" ] ; then\n\
	   echo \"using custom appearance settings\" >&2\n\
	   [ \"${GRUB_COLOR_NORMAL}\" ] && COLOR_NORMAL=\"${GRUB_COLOR_NORMAL}\"\n\
	   [ \"${GRUB_COLOR_HIGHLIGHT}\" ] && COLOR_HIGHLIGHT=\"${GRUB_COLOR_HIGHLIGHT}\"\n\
	   WALLPAPER=\"${GRUB_MENU_PICTURE}\"\n\
	fi\n";
	
		FILE* outFile = fopen(this->env->settings_file.c_str(), "w");
		if (outFile){
			std::string generatedFont;
			if (this->grubFont != "") {
				generatedFont = this->mkFont(); // overwrites the old font file generated by Grub Customizer
			} else if (this->oldFontFile == this->env->output_config_dir_noprefix + "/unicode.pf2") {
				// the custom font has been removed. Only delete the file Grub Customizer generated itself,
				// grub-mkconfig would pick it up otherwise
				remove((this->env->cfg_dir_prefix + this->oldFontFile).c_str());
			}
			this->oldFontFile = "";
			bool background_script_required = false;
			bool isGraphical = false;
			this->color_helper_required = false;
			for (std::list<Model_SettingsStore_Row>::iterator iter = this->begin(false); iter != this->end(); iter++){
				if (iter != this->begin(false)) {
					fputs("\n", outFile);
				}
				fputs((iter->getOutput()).c_str(), outFile);
	
				if (!background_script_required && !this->env->useDirectBackgroundProps && (iter->name == "GRUB_MENU_PICTURE" || iter->name == "GRUB_COLOR_NORMAL" || iter->name == "GRUB_COLOR_HIGHLIGHT")) {
					background_script_required = true;
					isGraphical = true;
				}
				if (this->env->useDirectBackgroundProps && (iter->name == "GRUB_COLOR_NORMAL" || iter->name == "GRUB_COLOR_HIGHLIGHT")) {
					this->color_helper_required = true;
					isGraphical = true;
				}
				if (iter->name == "GRUB_BACKGROUND" && this->env->useDirectBackgroundProps) {
					isGraphical = true;
				}
			}
			fclose(outFile);
			if (background_script_required){
				// don't overwrite the file of the desktop-base package - it already handles GRUB_MENU_PICTURE etc.
				std::string bgScriptPath = env->cfg_dir_prefix+"/usr/share/desktop-base/grub_background.sh";
				FILE* existingBgScript = fopen(bgScriptPath.c_str(), "r");
				if (existingBgScript) {
					fclose(existingBgScript);
					this->log("not overwriting existing " + bgScriptPath, Logger::INFO);
				} else {
					mkdir((env->cfg_dir_prefix+"/usr/share/desktop-base").c_str(), 0755);
					FILE* bgScriptFile = fopen(bgScriptPath.c_str(), "w");
					if (bgScriptFile) {
						chmod(bgScriptPath.c_str(), 0755);
						fputs(background_script, bgScriptFile);
						fclose(bgScriptFile);
					}
				}
			}
	
			if (isGraphical && generatedFont == "") {
				FILE* fontFile = fopen((this->env->output_config_dir + "/unicode.pf2").c_str(), "r");
				if (!fontFile) {
					fontFile = fopen((this->env->output_config_dir + "/fonts/unicode.pf2").c_str(), "r"); // installed by grub-install
				}
				if (fontFile) {
					this->log("font file exists", Logger::INFO);
					fclose(fontFile);
				} else {
					this->log("generating the font file", Logger::EVENT);
					this->mkFont(this->findFallbackFont());
				}
			}
			this->removeItem("GRUB_FONT");
	
			this->_reloadRequired = false;
			return true;
		}
		else
			return false;
	}

	bool setValue(std::string const& name, std::string const& value) {
		for (std::list<Model_SettingsStore_Row>::iterator iter = this->begin(); iter != this->end(); this->iter_to_next_setting(iter)){
			if (name == iter->name){
	
				if (iter->value != value){ //only set when the new value is really new
					iter->value = value;
					if (name == "GRUB_DISABLE_LINUX_RECOVERY" || name == "GRUB_DISABLE_OS_PROBER")
						_reloadRequired = true;
				}
	
				return true;
			}
		}
	
		settings.push_back(Model_SettingsStore_Row());
		settings.back().name = name;
		settings.back().value = value;
		settings.back().validate();
		if (name == "GRUB_DISABLE_LINUX_RECOVERY" || name == "GRUB_DISABLE_OS_PROBER")
			_reloadRequired = true;
		return false;
	}

	bool setIsActive(std::string const& name, bool value) {
		for (std::list<Model_SettingsStore_Row>::iterator iter = this->begin(); iter != this->end(); this->iter_to_next_setting(iter)){
			if (name == iter->name){
				if (iter->isActive != value){
					iter->isActive = value;
					if (name == "GRUB_DISABLE_LINUX_RECOVERY" || name == "GRUB_DISABLE_OS_PROBER")
						_reloadRequired = true;
				}
				return true;
			}
		}
		return false;
	}

};

class Model_SettingsManagerData_Connection
{
	protected: std::shared_ptr<Model_SettingsManagerData> settings;

	public: void setSettingsManager(std::shared_ptr<Model_SettingsManagerData> settings)
	{
		this->settings = settings;
	}
};
#endif
