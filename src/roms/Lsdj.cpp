#include "Lsdj.h"

#include "util/File.h"

void Lsdj::importSongs(const std::vector<std::wstring>& paths) {
	lsdj_error_t* error = nullptr;
	lsdj_sav_t* sav = lsdj_sav_read_from_memory((const unsigned char*)saveData.data(), saveData.size(), &error);
	if (sav == nullptr) {
		if (error) {
			consoleLogLine(lsdj_error_get_c_str(error));
		}
		return;
	}

	int index = 0;
	for (; index < lsdj_sav_get_project_count(sav); ++index)
	{
		lsdj_project_t* project = lsdj_sav_get_project(sav, index);
		lsdj_song_t* song = lsdj_project_get_song(project);
		if (!song)
			break;
	}

	std::vector<char> fileData;
	for (auto& path : paths) {
		fileData.clear();
		readFile(path, fileData);

		lsdj_project_t* project = lsdj_project_read_lsdsng_from_memory((const unsigned char*)fileData.data(), fileData.size(), &error);
		if (error != nullptr)
			return;

		lsdj_sav_set_project(sav, index++, project, &error);
		if (error != nullptr)
			return lsdj_project_free(project);
	}

	lsdj_sav_write_to_memory(sav, (unsigned char*)saveData.data(), saveData.size(), &error);
	lsdj_sav_free(sav);
}

void Lsdj::removeSong(int idx) {

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
	}

	saveData.resize(131072);
	lsdj_sav_write_to_memory(sav, (unsigned char*)saveData.data(), saveData.size(), &error);
	if (error) {
		consoleLogLine(lsdj_error_get_c_str(error));
	}

	lsdj_sav_free(sav);
}

void Lsdj::exportSong(int idx, const std::string& target) {
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

	//target.resize(LSDJ_SONG_DECOMPRESSED_SIZE);

	lsdj_project_t* project = lsdj_sav_get_project(sav, idx);
	//lsdj_project_write_lsdsng_to_memory(project, (unsigned char*)target.data(), target.size(), &error);

	lsdj_project_write_lsdsng_to_file(project, target.c_str(), &error);
	if (error) {
		consoleLogLine(lsdj_error_get_c_str(error));
	}

	lsdj_sav_free(sav);
}

void Lsdj::getSongNames(std::vector<std::string>& names) {
	if (saveData.size() == 0) {
		return;
	}

	lsdj_error_t* error = nullptr;
	lsdj_sav_t* sav = lsdj_sav_read_from_memory((const unsigned char*)saveData.data(), saveData.size(), &error);
	if (sav == nullptr) {
		return;
	}

	char name[9];
	std::fill_n(name, 9, '\0');

	lsdj_project_t* current = lsdj_project_new_from_working_memory_song(sav, &error);
	lsdj_project_get_name(current, name, sizeof(name));
	names.push_back(std::string(name) + " (working)");

	size_t count = lsdj_sav_get_project_count(sav);
	for (size_t i = 0; i < count; ++i) {
		lsdj_project_t* project = lsdj_sav_get_project(sav, i);
		if (lsdj_project_get_song(project) != NULL) {
			std::fill_n(name, 9, '\0');
			lsdj_project_get_name(project, name, sizeof(name));
			names.push_back(name);
		}
	}

	lsdj_sav_free(sav);
}
