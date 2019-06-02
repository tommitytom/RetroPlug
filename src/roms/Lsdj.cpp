#include "Lsdj.h"

#include <iostream>
#include "util/File.h"

const int LSDJ_SAV_SIZE = 131072; // FIXME: This is probably in liblsdj somewhere

void Lsdj::importSongs(const std::vector<std::wstring>& paths) {
	lsdj_error_t* error = nullptr;
	lsdj_sav_t* sav = lsdj_sav_read_from_memory((const unsigned char*)saveData.data(), saveData.size(), &error);
	if (sav == nullptr) {
		if (error) {
			consoleLogLine(lsdj_error_get_c_str(error));
		}

		return;
	}

	int index;
	for (index = 0; index < lsdj_sav_get_project_count(sav); ++index) {
		lsdj_project_t* project = lsdj_sav_get_project(sav, index);
		lsdj_song_t* song = lsdj_project_get_song(project);
		if (!song) {
			break;
		}
	}

	std::vector<std::byte> fileData;
	for (auto& path : paths) {
		fileData.clear();
		readFile(path, fileData);

		lsdj_project_t* project = lsdj_project_read_lsdsng_from_memory((const unsigned char*)fileData.data(), fileData.size(), &error);
		if (error != nullptr) {
			consoleLogLine(lsdj_error_get_c_str(error));
			continue;
		}

		lsdj_sav_set_project(sav, index++, project, &error);
		if (error != nullptr) {
			consoleLogLine(lsdj_error_get_c_str(error));
			lsdj_project_free(project);
			continue;
		}
	}

	lsdj_sav_write_to_memory(sav, (unsigned char*)saveData.data(), saveData.size(), &error);
	if (error != nullptr) {
		consoleLogLine(lsdj_error_get_c_str(error));
	}

	lsdj_sav_free(sav);
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

	target.resize(LSDSNG_MAX_SIZE);

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

	size_t size = lsdj_project_write_lsdsng_to_memory(project, (unsigned char*)target.data(), target.size(), &error);
	if (error) {
		consoleLogLine(lsdj_error_get_c_str(error));
		target.resize(0);
	} else {
		target.resize(size);
	}

	lsdj_sav_free(sav);
}

void Lsdj::exportSongs(const std::vector<LsdjSongName>& names, std::vector<std::vector<std::byte>>& target) {
	
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
