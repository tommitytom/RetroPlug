#include "Lsdj.h"

#include <iostream>
#include <sstream>
#include <set>
#include "util/File.h"
#include "lsdj/rom.h"
#include "lsdj/kit.h"
#include "util/crc32.h"

const int LSDJ_SAV_SIZE = 131072; // FIXME: This is probably in liblsdj somewhere

std::string projectName(lsdj_project_t* project) {
	char name[9];
	std::fill_n(name, 9, '\0');
	lsdj_project_get_name(project, name, sizeof(name));
	return std::string(name);
}

int nextProjectIndex(lsdj_sav_t* sav, int startIdx) {
	for (int index = startIdx; index < lsdj_sav_get_project_count(sav); ++index) {
		lsdj_project_t* project = lsdj_sav_get_project(sav, index);
		lsdj_song_t* song = lsdj_project_get_song(project);
		if (!song) {
			return index;
		}
	}

	return -1;
}

Lsdj::Lsdj() {
	for (int i = 0; i < 0x33; ++i) {
		kitData.push_back(nullptr);
	}
}

void Lsdj::clearKits() {
	for (int i = 0; i < kitData.size(); ++i) {
		kitData[i] = nullptr;
	}
}

void Lsdj::loadRom(const std::vector<std::byte>& romData) {
	clearKits();
	std::string error;
	if (!loadRomKits(romData, true, error)) {

	}
}

bool Lsdj::importSongs(const std::vector<tstring>& paths, std::string& errorStr) {
	lsdj_error_t* error = nullptr;
	lsdj_sav_t* sav = lsdj_sav_read_from_memory((const unsigned char*)saveData.data(), saveData.size(), &error);
	if (sav == nullptr) {
		if (error) {
			errorStr = lsdj_error_get_c_str(error);
			consoleLogLine(errorStr);
		}

		return false;
	}

	int index = nextProjectIndex(sav, 0);

	std::vector<std::byte> fileData;
	for (auto& path : paths) {
		fileData.clear();
		if (readFile(path, fileData)) {
			lsdj_project_t* project = lsdj_project_read_lsdsng_from_memory((const unsigned char*)fileData.data(), fileData.size(), &error);
			if (error != nullptr) {
				consoleLogLine(lsdj_error_get_c_str(error));
				continue;
			}

			lsdj_sav_set_project(sav, index, project, &error);
			index = nextProjectIndex(sav, index);

			if (error != nullptr) {
				consoleLogLine("Project " + projectName(project) + ": " + std::string(lsdj_error_get_c_str(error)));
				lsdj_project_free(project);
				continue;
			}
		}
	}

	lsdj_sav_write_to_memory(sav, (unsigned char*)saveData.data(), saveData.size(), &error);
	if (error != nullptr) {
		errorStr = lsdj_error_get_c_str(error);
		consoleLogLine(errorStr);
		lsdj_sav_free(sav);
		return false;
	}

	lsdj_sav_free(sav);
	return true;
}

void Lsdj::loadSong(int idx) {
	if (saveData.size() == 0) {
		return;
	}

	lsdj_error_t* error = nullptr;
	lsdj_sav_t* sav = lsdj_sav_read_from_memory((const unsigned char*)saveData.data(), saveData.size(), &error);
	if (sav == nullptr) {
		if (error) {
			consoleLogLine(lsdj_error_get_c_str(error));
		}

		return;
	}

	lsdj_sav_set_working_memory_song_from_project(sav, idx, &error);
	if (error) {
		consoleLogLine(lsdj_error_get_c_str(error));
		lsdj_sav_free(sav);
		return;
	}

	saveData.resize(LSDJ_SAV_SIZE);
	lsdj_sav_write_to_memory(sav, (unsigned char*)saveData.data(), saveData.size(), &error);
	if (error) {
		consoleLogLine(lsdj_error_get_c_str(error));
	}

	lsdj_sav_free(sav);
}

void serializeSong(const lsdj_project_t* project, std::vector<std::byte>& target) {
	target.resize(LSDSNG_MAX_SIZE);

	lsdj_error_t* error = nullptr;
	size_t size = lsdj_project_write_lsdsng_to_memory(project, (unsigned char*)target.data(), target.size(), &error);
	if (error) {
		consoleLogLine(lsdj_error_get_c_str(error));
		target.resize(0);
	} else {
		target.resize(size);
	}
}

void Lsdj::exportSong(int idx, std::vector<std::byte>& target) {
	if (saveData.size() == 0) {
		return;
	}

	lsdj_error_t* error = nullptr;
	lsdj_sav_t* sav = lsdj_sav_read_from_memory((const unsigned char*)saveData.data(), saveData.size(), &error);
	if (sav == nullptr) {
		if (error) {
			consoleLogLine(lsdj_error_get_c_str(error));
		}

		return;
	}

	lsdj_project_t* project;	
	if (idx == -1) {
		project = lsdj_project_new_from_working_memory_song(sav, &error);
		if (error) {
			consoleLogLine(lsdj_error_get_c_str(error));
			lsdj_sav_free(sav);
		}
	} else {
		project = lsdj_sav_get_project(sav, idx);
	}

	serializeSong(project, target);

	lsdj_sav_free(sav);
}

void Lsdj::exportSongs(std::vector<NamedData>& target) {
	if (saveData.size() == 0) {
		return;
	}

	lsdj_error_t* error = nullptr;
	lsdj_sav_t* sav = lsdj_sav_read_from_memory((const unsigned char*)saveData.data(), saveData.size(), &error);
	if (sav == nullptr) {
		if (error) {
			consoleLogLine(lsdj_error_get_c_str(error));
		}

		return;
	}

	lsdj_project_t* project = lsdj_project_new_from_working_memory_song(sav, &error);
	if (error) {
		consoleLogLine(lsdj_error_get_c_str(error));
		lsdj_sav_free(sav);
		return;
	};

	char name[9];
	std::fill_n(name, 9, '\0');
	lsdj_project_get_name(project, name, sizeof(name));
	unsigned char version = lsdj_project_get_version(project);

	target.push_back(NamedData());
	target.back().name = std::string(name) + ".WM." + std::to_string(version);
	serializeSong(project, target.back().data);

	size_t count = lsdj_sav_get_project_count(sav);
	for (size_t i = 0; i < count; ++i) {
		lsdj_project_t* project = lsdj_sav_get_project(sav, i);
		lsdj_song_t* song = lsdj_project_get_song(project);
		if (song) {
			std::fill_n(name, 9, '\0');
			lsdj_project_get_name(project, name, sizeof(name));
			version = lsdj_project_get_version(project);

			target.push_back(NamedData());
			target.back().name = std::string(name) + "." + std::to_string(version);
			serializeSong(project, target.back().data);
		}
	}

	lsdj_sav_free(sav);
}

void Lsdj::deleteSong(int idx) {
	if (saveData.size() == 0) {
		return;
	}

	lsdj_error_t* error = nullptr;
	lsdj_sav_t* sav = lsdj_sav_read_from_memory((const unsigned char*)saveData.data(), saveData.size(), &error);
	if (sav == nullptr) {
		if (error) {
			consoleLogLine(lsdj_error_get_c_str(error));
		}

		return;
	}

	auto project = lsdj_sav_get_project(sav, idx);
	char name[9];
	std::fill_n(name, 9, '\0');
	lsdj_project_get_name(project, name, sizeof(name));
	std::cout << "Deleting " << std::string(name) << std::endl;

	lsdj_sav_erase_project(sav, idx, &error);
	if (error) {
		consoleLogLine(lsdj_error_get_c_str(error));
		lsdj_sav_free(sav);
		return;
	}

	saveData.resize(LSDJ_SAV_SIZE);
	lsdj_sav_write_to_memory(sav, (unsigned char*)saveData.data(), saveData.size(), &error);
	if (error) {
		consoleLogLine(lsdj_error_get_c_str(error));
	}

	lsdj_sav_free(sav);
}

void Lsdj::getSongNames(std::vector<LsdjSongName>& names) {
	if (saveData.size() == 0) {
		return;
	}

	lsdj_error_t* error = nullptr;
	lsdj_sav_t* sav = lsdj_sav_read_from_memory((const unsigned char*)saveData.data(), saveData.size(), &error);
	if (sav == nullptr) {
		consoleLogLine(lsdj_error_get_c_str(error));
		return;
	}

	char name[9];
	std::fill_n(name, 9, '\0');

	lsdj_project_t* current = lsdj_project_new_from_working_memory_song(sav, &error);
	if (!error) {
		lsdj_project_get_name(current, name, sizeof(name));
		names.push_back({ -1, std::string(name) + " (working)", lsdj_project_get_version(current) });
	}

	size_t count = lsdj_sav_get_project_count(sav);
	for (size_t i = 0; i < count; ++i) {
		lsdj_project_t* project = lsdj_sav_get_project(sav, i);
		if (lsdj_project_get_song(project) != NULL) {
			std::fill_n(name, 9, '\0');
			lsdj_project_get_name(project, name, sizeof(name));
			names.push_back({ (int)i, name, lsdj_project_get_version(project) });
		}
	}

	lsdj_sav_free(sav);
}

// Kit specific

bool Lsdj::loadRomKits(const std::vector<std::byte>& romData, bool absolute, std::string& error) {
	int kitIdx = 0;

	std::set<uint32_t> hashes;
	for (size_t i = 0; i < kitData.size(); ++i) {
		if (kitData[i]) {
			hashes.insert(kitData[i]->hash);
		}
	}

	const char* data = (const char*)romData.data();
	for (size_t bankIdx = 0; bankIdx < BANK_COUNT; ++bankIdx) {
		size_t offset = bankIdx * BANK_SIZE;
		if (bank_is_kit(data + offset) || bank_is_empty_kit(data + offset)) {
			if (bank_is_kit(data + offset)) {
				int targetIdx = absolute ? kitIdx : findEmptyKit();
				if (targetIdx != -1) {
					if (!absolute) {
						// Filter out duplicate kits
						uint32_t hash = crc32::update(data + offset, BANK_SIZE);
						if (hashes.find(hash) != hashes.end()) {
							continue;
						}
					}
					
					loadKitAt(data + offset, BANK_SIZE, targetIdx);
				} else {
					error = "Unable to import kit - not enough kit banks available";
					return false;
				}
			}

			kitIdx++;
		}
	}

	return true;
}

void Lsdj::getKitNames(std::vector<std::string>& names) {
	for (size_t i = 0; i < kitData.size(); ++i) {
		if (kitData[i]) {
			names.push_back(kitData[i]->name);
		} else {
			names.push_back("Empty");
		}
	}
}

void Lsdj::patchKit(std::vector<std::byte>& romData, const std::vector<std::byte>& kitData, int index) {
	int kitCount = 0;

	const char* data = (const char*)romData.data();
	for (size_t bankIdx = 0; bankIdx < BANK_COUNT; ++bankIdx) {
		size_t offset = bankIdx * BANK_SIZE;
		if (bank_is_kit(data + offset) || bank_is_empty_kit(data + offset)) {
			if (index == kitCount || (index == -1 && bank_is_empty_kit(data + offset))) {
				memcpy((void*)(data + offset), kitData.data(), kitData.size());
				break;
			}

			kitCount++; 
		}
	}
}

void Lsdj::readKit(const std::vector<std::byte>& romData, std::vector<std::byte>& target, int index) {
	int kitCount = 0;
	
	const char* data = (const char*)romData.data();
	for (size_t bankIdx = 0; bankIdx < BANK_COUNT; ++bankIdx) {
		size_t offset = bankIdx * BANK_SIZE;
		if (bank_is_kit(data + offset) || bank_is_empty_kit(data + offset)) {
			if (index == kitCount || (index == -1 && bank_is_empty_kit(data + offset))) {
				target.resize(BANK_SIZE);
				memcpy((void*)target.data(), (void*)(data + offset), BANK_SIZE);
				break;
			}

			kitCount++;
		}
	}
}

void Lsdj::patchKits(std::vector<std::byte>& romData) {
	int kitIdx = 0;

	char* data = (char*)romData.data();
	for (size_t bankIdx = 0; bankIdx < BANK_COUNT; ++bankIdx) {
		size_t offset = bankIdx * BANK_SIZE;
		if (bank_is_kit(data + offset) || bank_is_empty_kit(data + offset)) {
			auto kit = kitData[kitIdx];
			if (kit) {
				memcpy((void*)(data + offset), (void*)kit->data.data(), kit->data.size());
			} else {
				// Clear kit!
				memset((void*)(data + offset), 0, BANK_SIZE);
				data[offset + 0] = -1;
				data[offset + 1] = -1;
			}

			kitIdx++;
		}
	}
}

void Lsdj::loadKitAt(const char* data, size_t size, int idx) {
	char name[KIT_NAME_SIZE + 1];
	memset(name, '\0', KIT_NAME_SIZE + 1);
	memcpy(name, data + KIT_NAME_OFFSET, KIT_NAME_SIZE);

	auto kit = std::make_shared<NamedHashedData>(NamedHashedData { 
		std::string(name),
		std::vector<std::byte>(),
		0
	});

	rtrim(kit->name);

	kit->data.resize(size);
	memcpy(kit->data.data(), data, size);

	kit->hash = crc32::update(kit->data);

	kitData[idx] = kit;
}

bool Lsdj::loadKit(const tstring& path, int idx, std::string& error) {
	if (idx == -1) {
		idx = findEmptyKit();
	}

	if (idx != -1) {
		std::vector<std::byte> f;
		if (readFile(path, f)) {
			loadKitAt((const char*)f.data(), f.size(), idx);
		} else {
			error = "Failed to load kit from file";
			return false;
		}
	} else {
		error = "Unable to import kit - not enough kit banks available";
		return false;
	}

	return true;
}

void Lsdj::exportKit(const std::vector<std::byte>& romData, int index, std::vector<std::byte>& target) {
	int kitCount = 0;

	const char* data = (const char*)romData.data();
	for (size_t bankIdx = 0; bankIdx < BANK_COUNT; ++bankIdx) {
		size_t offset = bankIdx * BANK_SIZE;
		if (bank_is_kit(data + offset) || bank_is_empty_kit(data + offset)) {
			if (index == kitCount) {
				target.resize(BANK_SIZE);
				memcpy(target.data(), data + offset, BANK_SIZE);
				break;
			}

			kitCount++;
		}
	}
}

void Lsdj::deleteKit(std::vector<std::byte>& romData, int index) {
	if (kitData[index]) {
		kitData[index] = nullptr;
		patchKits(romData);
	}
}

