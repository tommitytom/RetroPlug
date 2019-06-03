#include "Lsdj.h"

#include <iostream>
#include "util/File.h"

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

bool Lsdj::importSongs(const std::vector<std::wstring>& paths, std::string& errorStr) {
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

void Lsdj::exportSongs(std::vector<LsdjSongData>& target) {
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

	target.push_back(LsdjSongData());
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

			target.push_back(LsdjSongData());
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
