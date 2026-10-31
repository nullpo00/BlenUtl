//----------------------------------------------------------------------------------
// Config IO header
//----------------------------------------------------------------------------------
#pragma once

#include <concepts>
#include <fstream>
#include <map>
#include <string>

namespace cfg {

// map[section, map[key, value]]
using Config = std::map<std::string, std::map<std::string, std::string>>;

// Trim
std::string trim(std::string str) {

	auto is_not_space = [](char c) {return !(c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\v'); };
	auto start = std::find_if(str.begin(), str.end(), is_not_space);
	if (start != str.end()) {
		str.erase(str.begin(), start);
	}
	auto end = std::find_if(str.rbegin(), str.rend(), is_not_space);
	str.erase(end.base(), str.end());

	return str;
}

// Read config file
template <typename T>
requires std::constructible_from<std::filesystem::path, T>
[[nodiscard]] bool read_config(const T& filepath, Config& cfg) {

	std::filesystem::path path(filepath);
	std::ifstream file(path);

	if (file.is_open()) {
		std::string str = "";
		std::map<std::string, std::string> sub{};

		// section
		while (std::getline(file, str)) {
			str = trim(str);
			if (str.starts_with('[') && str.ends_with(']')) {
				std::string section = str.substr(1, str.size() - 2);
				sub.clear();

				// key, value
				while (std::getline(file, str) && !str.empty()) {
					str = trim(str);
					std::erase(str, ' ');
					auto pos = str.find('=');
					if (pos != std::string::npos) {
						std::string key = str.substr(0, pos);
						std::string value = str.substr(pos + 1);
						sub.emplace(key, value);
					} else {
						break;
					}
				}
				if (!sub.empty()) {
					cfg.emplace(section, sub);
				}
			}
		}
		return true;

	} else {
		return false;
	}
}

// Write config file
template <typename T>
requires std::constructible_from<std::filesystem::path, T>
[[nodiscard]] bool write_config(const T& filepath, const Config& cfg) {

	std::filesystem::path path(filepath);
	std::ofstream file(path);

	if (file.is_open()) {
		for (const auto& [section, settings] : cfg) {
			file << "[" << section << "]\n";
			for (const auto& [key, value] : settings) {
				file << key << " = " << value << "\n";
			}
			file << "\n";
		}
		return true;

	} else {
		return false;
	}

}

}	// namespace cfg
