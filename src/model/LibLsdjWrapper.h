#pragma once

#include <sol/sol.hpp>
#include "util/DataBuffer.h"
#include "sav.h"

void setupLsdj(sol::state& s) {
	s.create_named_table("liblsdj",
		"sav_new", []() {
			lsdj_sav_t* sav = nullptr;
			lsdj_error_t err = lsdj_sav_new(&sav, nullptr);
			return std::make_tuple((void*)sav, err);
		},
		"sav_read_from_memory", [](DataBuffer<char>* buffer) {
			lsdj_sav_t* sav = nullptr;
			lsdj_error_t err = lsdj_sav_read_from_memory((const uint8_t*)buffer->data(), buffer->size(), &sav, nullptr);
			return std::make_tuple((void*)sav, err);
		},
		"sav_set_working_memory_song_from_project", [](void* sav, uint8_t index) {
			return lsdj_sav_set_working_memory_song_from_project((lsdj_sav_t*)sav, index);
		},
		"sav_write_to_memory", [](const void* sav, DataBuffer<char>* target) {
			target->reserve(LSDJ_SAV_SIZE);
			size_t size = 0;
			lsdj_error_t err = lsdj_sav_write_to_memory((const lsdj_sav_t*)sav, (uint8_t*)target->data(), target->size(), &size);
			if (err == lsdj_error_t::LSDJ_SUCCESS) target->resize(size);
			return err;
		},
		"sav_get_project", [](void* sav, uint8_t index) {
			return (void*)lsdj_sav_get_project((lsdj_sav_t*)sav, index);
		},
		"project_read_lsdsng_from_memory", [](const DataBuffer<char>* buffer) {
			lsdj_project_t* project = nullptr;
			lsdj_error_t err = lsdj_project_read_lsdsng_from_memory((const uint8_t*)buffer->data(), buffer->size(), &project, nullptr);
			return std::make_tuple((void*)project, err);
		},
		"project_get_name", [](const void* project) {
			const lsdj_project_t* proj = (const lsdj_project_t*)project;
			const char* name = lsdj_project_get_name(proj);
			size_t length = lsdj_project_get_name_length(proj);
			return std::string(name, length);
		},
		"project_write_lsdsng_to_memory", [](const void* project, DataBuffer<char>* target) {
			target->reserve(LSDSNG_MAX_SIZE);
			size_t size = 0;
			lsdj_error_t err = lsdj_project_write_lsdsng_to_memory((const lsdj_project_t*)project, (uint8_t*)target->data(), &size);
			if (err == lsdj_error_t::LSDJ_SUCCESS) target->resize(size);
			return err;
		}
	);
}
