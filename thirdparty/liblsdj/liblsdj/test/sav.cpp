#include <lsdj/sav.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <catch2/catch.hpp>
#include <cstring>

#include "file.hpp"

using namespace Catch;

SCENARIO( "Saves", "[sav]" )
{
	std::array<uint8_t, LSDJ_SONG_BYTE_COUNT> zeroBuffer;
	zeroBuffer.fill(0);

	REQUIRE(LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX == 0xFF);

	// Sample project used in some tests
	lsdj_project_t* project = NULL;
    REQUIRE( lsdj_project_new(&project, nullptr) == LSDJ_SUCCESS );
    
	lsdj_project_set_name(project, "MYSONG");
	lsdj_project_set_version(project, 16);

	lsdj_song_t song;
	std::fill_n(song.bytes, LSDJ_SONG_BYTE_COUNT, 40);
	lsdj_project_set_song(project, &song);

	GIVEN( "A new sav is created" )
	{
        lsdj_sav_t* sav = NULL;
        REQUIRE( lsdj_sav_new(&sav, nullptr) == LSDJ_SUCCESS );
        REQUIRE( sav != NULL );

		WHEN( "The working memory song buffer is requested" )
		{
			auto song = lsdj_sav_get_working_memory_song_const(sav);
			REQUIRE( song != nullptr );

			THEN( "It should be a zeroed out array of bytes" )
			{
                REQUIRE( memcmp(song->bytes, LSDJ_SONG_NEW_BYTES, LSDJ_SONG_BYTE_COUNT) == 0 );
			}
		}

		WHEN( "Changing the working memory song buffer" )
		{
			lsdj_sav_set_working_memory_song(sav, &song);

			THEN( "Retrieving the song buffer should return the same data" )
			{
				auto result = lsdj_sav_get_working_memory_song_const(sav);
				REQUIRE( memcpy(&song.bytes, result->bytes, LSDJ_SONG_BYTE_COUNT) );
			}
		}

		WHEN( "Copying the working memory song from a project")
		{
            REQUIRE( lsdj_sav_set_working_memory_song_from_project(sav, 2) == LSDJ_NO_PROJECT_AT_INDEX );

			lsdj_sav_set_project_move(sav, 2, project);
            REQUIRE( lsdj_sav_set_working_memory_song_from_project(sav, 2) == LSDJ_SUCCESS );

			THEN( "The working memory and index should change" )
			{
				auto result = lsdj_sav_get_working_memory_song_const(sav);
				REQUIRE( memcpy(&song.bytes, result->bytes, LSDJ_SONG_BYTE_COUNT) );
			}
		}

		WHEN( "The active project slot index is requested" )
		{
			auto index = lsdj_sav_get_active_project_index(sav);

			THEN( "It should not refer to any project slot" )
			{
				REQUIRE( index == LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX );
			}
		}

		WHEN( "Changing the active project slot index" )
		{
			REQUIRE( lsdj_sav_get_active_project_index(sav) != 11 );
			lsdj_sav_set_active_project_index(sav, 11);

			THEN( "It should update accordingly" )
			{
				REQUIRE( lsdj_sav_get_active_project_index(sav) == 11 );
			}
		}

		WHEN( "Copying a project into a sav" )
		{
            REQUIRE( lsdj_sav_set_project_copy(sav, 7, project, nullptr) == LSDJ_SUCCESS );

			THEN( "The data in the project should be identical " )
			{
				auto copy = lsdj_sav_get_project_const(sav, 7);
				REQUIRE( copy != nullptr );

				std::array<char, LSDJ_PROJECT_NAME_LENGTH> name;
                name.fill('\0');
                strncpy(name.data(), lsdj_project_get_name(copy), name.size());

				REQUIRE_THAT( name.data(), Equals("MYSONG") );
				REQUIRE( lsdj_project_get_version(copy) == 16 );

				auto bufferCopy = lsdj_project_get_song_const(project);
				REQUIRE( memcmp(song.bytes, bufferCopy->bytes, LSDJ_SONG_BYTE_COUNT) == 0 );
			}
		}

		WHEN( "Moving a project into a sav" )
		{
			lsdj_sav_set_project_move(sav, 3, project);

			THEN( "The project in the sav should point to the same memory " )
			{
				REQUIRE(lsdj_sav_get_project_const(sav, 3) == project);

				WHEN( "Erasing a project from a sav" )
				{
					lsdj_sav_erase_project(sav, 3);

					THEN( "The slot should be empty" )
					{
						REQUIRE( lsdj_sav_get_project_const(sav, 3) == nullptr );
					}
				}
			}
		}

		WHEN( "Requesting a project" )
		{
			auto project = lsdj_sav_get_project_const(sav, 0);

			THEN( "The project should be NULL" )
			{
				REQUIRE( project == NULL );
			}
		}

		WHEN( "Copying a sav" )
		{
			lsdj_sav_set_working_memory_song(sav, &song);
			lsdj_sav_set_active_project_index(sav, 15);
			lsdj_sav_set_project_move(sav, 9, project);

			THEN( "The data should remain intact" )
			{
                lsdj_sav_t* copy = nullptr;
                REQUIRE( lsdj_sav_copy(sav, &copy, nullptr) == LSDJ_SUCCESS );
				REQUIRE( copy != nullptr );

				auto copyBuffer = lsdj_sav_get_working_memory_song_const(sav);
				REQUIRE( memcpy(&song.bytes, copyBuffer->bytes, LSDJ_SONG_BYTE_COUNT) );
				REQUIRE( lsdj_sav_get_active_project_index(sav) == 15 );
				REQUIRE( lsdj_sav_get_project_const(sav, 9) == project );
			}
		}
        
        WHEN( "Creating a project from the working memory song with no active project" )
        {
            lsdj_sav_set_active_project_index(sav, LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX);
            
            lsdj_project_t* project = nullptr;
            lsdj_project_new_from_working_memory_song(sav, &project, nullptr);
            auto wm = lsdj_sav_get_working_memory_song_const(sav);
            
            THEN( "It should contain the same data" )
            {
                REQUIRE( project != nullptr );
                
                std::array<char, LSDJ_PROJECT_NAME_LENGTH> name;
                name.fill('\0');
                strncpy(name.data(), lsdj_project_get_name(project), name.size());
                REQUIRE( strncmp(name.data(), "", LSDJ_PROJECT_NAME_LENGTH) == 0 );
                
                REQUIRE( lsdj_project_get_version(project) == 0 );
                
                auto song = lsdj_project_get_song_const(project);
                REQUIRE( memcmp(song->bytes, wm->bytes, LSDJ_SONG_BYTE_COUNT) == 0 );
            }
        }
        
        WHEN( "Creating a project from the working memory song with an active project" )
        {
            lsdj_sav_set_project_move(sav, 3, project);
            lsdj_sav_set_active_project_index(sav, 3);
            
            lsdj_project_t* project = nullptr;
            REQUIRE( lsdj_project_new_from_working_memory_song(sav, &project, nullptr) == LSDJ_SUCCESS);
            REQUIRE( project != nullptr );
            
            auto wm = lsdj_sav_get_working_memory_song_const(sav);
            
            THEN( "It should contain the same data" )
            {
                std::array<char, LSDJ_PROJECT_NAME_LENGTH> name;
                name.fill('\0');
                strncpy(name.data(), lsdj_project_get_name(project), name.size());
                REQUIRE( strncmp(name.data(), "MYSONG", LSDJ_PROJECT_NAME_LENGTH) == 0 );
                
                REQUIRE( lsdj_project_get_version(project) == 16 );
                
                auto song = lsdj_project_get_song_const(project);
                REQUIRE( memcmp(song->bytes, wm->bytes, LSDJ_SONG_BYTE_COUNT) == 0 );
            }
        }
	}
}

TEST_CASE( ".sav save/load", "[sav]" )
{
	const auto raw = readFileContents(RESOURCES_FOLDER "raw/happy_birthday.raw");
    assert(raw.size() == LSDJ_SONG_BYTE_COUNT);

    const auto save = readFileContents(RESOURCES_FOLDER "sav/happy_birthday.sav");
    assert(save.size() == 131072);

	SECTION( "Reading a .sav from file" )
	{
        lsdj_sav_t* sav = nullptr;
        REQUIRE( lsdj_sav_read_from_file(RESOURCES_FOLDER "sav/all.sav", &sav, nullptr) == LSDJ_SUCCESS );
		REQUIRE( sav != nullptr );

		auto wm = lsdj_sav_get_working_memory_song_const(sav);
		REQUIRE( memcmp(wm->bytes, raw.data(), LSDJ_SONG_BYTE_COUNT) == 0 );

		REQUIRE( lsdj_sav_get_active_project_index(sav) == LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX );

		auto project = lsdj_sav_get_project_const(sav, 0);
		REQUIRE( project != nullptr );

		std::array<char, LSDJ_PROJECT_NAME_LENGTH> name;
        name.fill('\0');
        strncpy(name.data(), lsdj_project_get_name(project), name.size());
		REQUIRE( strncmp(name.data(), "HAPPY BD", LSDJ_PROJECT_NAME_LENGTH) == 0 );

		REQUIRE( lsdj_project_get_version(project) == 4 );

		auto song = lsdj_project_get_song_const(project);
		REQUIRE( memcmp(song->bytes, raw.data(), raw.size()) == 0 );

		lsdj_sav_free(sav);
	}

	SECTION( "Reading a .sav from memory" )
	{
        lsdj_sav_t* sav = nullptr;
        REQUIRE( lsdj_sav_read_from_memory(save.data(), save.size(), &sav, nullptr) == LSDJ_SUCCESS );
		REQUIRE( sav != nullptr );

		// Working memory
		auto wm = lsdj_sav_get_working_memory_song_const(sav);
		REQUIRE( memcmp(wm->bytes, raw.data(), LSDJ_SONG_BYTE_COUNT) == 0 );

		// Active project
		REQUIRE( lsdj_sav_get_active_project_index(sav) == LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX );

		auto project = lsdj_sav_get_project_const(sav, 0);
		REQUIRE( project != nullptr );

		std::array<char, LSDJ_PROJECT_NAME_LENGTH> name;
        name.fill('\0');
        strncpy(name.data(), lsdj_project_get_name(project), name.size());
		REQUIRE( strncmp(name.data(), "HAPPY BD", LSDJ_PROJECT_NAME_LENGTH) == 0 );

		REQUIRE( lsdj_project_get_version(project) == 4 );

		auto song = lsdj_project_get_song_const(project);
		REQUIRE( memcmp(song->bytes, raw.data(), raw.size()) == 0 );

		lsdj_sav_free(sav);
	}

	SECTION( "Writing a .sav to memory" )
	{
        lsdj_sav_t* sav = nullptr;
        REQUIRE( lsdj_sav_new(&sav, nullptr) == LSDJ_SUCCESS );
		REQUIRE( sav != nullptr );

		// Working memory
		lsdj_song_t wm;
		memcpy(wm.bytes, raw.data(), raw.size());
		lsdj_sav_set_working_memory_song(sav, &wm);

		// Active project
		lsdj_sav_set_active_project_index(sav, LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX);

        lsdj_project_t* project = nullptr;
		lsdj_project_read_lsdsng_from_file(RESOURCES_FOLDER "lsdsng/happy_birthday.lsdsng", &project, nullptr);
		assert(project != nullptr);

		lsdj_sav_set_project_move(sav, 0, project);

		std::array<uint8_t, 131072> memory;
        size_t writeCount = 0;
		REQUIRE( lsdj_sav_write_to_memory(sav, memory.data(), memory.size(), &writeCount) == LSDJ_SUCCESS );
		REQUIRE( writeCount == LSDJ_SAV_SIZE );

		lsdj_sav_free(sav);

        lsdj_sav_t* compSav = nullptr;
        REQUIRE( lsdj_sav_read_from_memory(memory.data(), writeCount, &compSav, nullptr) == LSDJ_SUCCESS );
		REQUIRE( compSav != nullptr );
        
        // --- Comparison --- //
        
        // Working memory
        auto compWm = lsdj_sav_get_working_memory_song_const(compSav);
        REQUIRE( memcmp(compWm->bytes, raw.data(), LSDJ_SONG_BYTE_COUNT) == 0 );

        // Active project
        REQUIRE( lsdj_sav_get_active_project_index(compSav) == LSDJ_SAV_NO_ACTIVE_PROJECT_INDEX );

        auto compProject = lsdj_sav_get_project_const(compSav, 0);
        REQUIRE( compProject != nullptr );

        std::array<char, LSDJ_PROJECT_NAME_LENGTH> name;
        name.fill('\0');
        strncpy(name.data(), lsdj_project_get_name(compProject), name.size());
        REQUIRE( strncmp(name.data(), "HAPPY BD", LSDJ_PROJECT_NAME_LENGTH) == 0 );

        REQUIRE( lsdj_project_get_version(compProject) == 4 );

        auto song = lsdj_project_get_song_const(compProject);
        REQUIRE( memcmp(song->bytes, raw.data(), raw.size()) == 0 );

		lsdj_sav_free(compSav);
	}

	SECTION( "Checking sav likelihood" )
	{
        const auto lsdsng = readFileContents(RESOURCES_FOLDER "lsdsng/happy_birthday.lsdsng");
        assert(lsdsng.size() == 3081);
        
		REQUIRE( lsdj_sav_is_likely_valid_memory(save.data(), save.size()) == true );
        REQUIRE( lsdj_sav_is_likely_valid_memory(lsdsng.data(), lsdsng.size()) == false );
        REQUIRE( lsdj_sav_is_likely_valid_memory(raw.data(), raw.size()) == false );
        
        REQUIRE( lsdj_sav_is_likely_valid_file(RESOURCES_FOLDER "sav/happy_birthday.sav") == true );
        REQUIRE( lsdj_sav_is_likely_valid_file(RESOURCES_FOLDER "lsdsng/happy_birthday.lsdsng") == false );
        REQUIRE( lsdj_sav_is_likely_valid_file(RESOURCES_FOLDER "raw/happy_birthday.raw") == false );
	}
}
