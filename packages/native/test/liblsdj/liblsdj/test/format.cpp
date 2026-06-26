#include <lsdj/instrument.h>
#include <lsdj/project.h>
#include <lsdj/sav.h>
#include <lsdj/song.h>
#include <lsdj/synth.h>
#include <lsdj/table.h>

#include <cassert>
#include <catch2/catch.hpp>

using namespace Catch;

TEST_CASE( "Format version changes", "[sav]" )
{
	SECTION( "4.9.9" )
	{
        lsdj_sav_t* sav = nullptr;
        REQUIRE( lsdj_sav_read_from_file(RESOURCES_FOLDER "sav/lsdj499.sav", &sav, nullptr) == LSDJ_SUCCESS);
        REQUIRE( sav != nullptr );
        
        lsdj_project_t* project = lsdj_sav_get_project(sav, 0);
        REQUIRE( project != nullptr );
        
        lsdj_song_t* song = lsdj_project_get_song(project);
        assert(song != nullptr);
        
        REQUIRE( lsdj_song_get_format_version(song) == 3 );
        
        REQUIRE( lsdj_instrument_is_allocated(song, 0) == true );
        REQUIRE( lsdj_instrument_get_plv_speed(song, 0) == LSDJ_INSTRUMENT_PLV_FAST );
        REQUIRE( lsdj_instrument_get_vibrato_shape(song, 0) == LSDJ_INSTRUMENT_VIBRATO_TRIANGLE );
        REQUIRE( lsdj_instrument_get_vibrato_direction(song, 0) == LSDJ_INSTRUMENT_VIBRATO_DOWN );
	}
    
    SECTION( "6.2.0" )
    {
        lsdj_sav_t* sav = nullptr;
        REQUIRE( lsdj_sav_read_from_file(RESOURCES_FOLDER "sav/lsdj620.sav", &sav, nullptr) == LSDJ_SUCCESS);
        REQUIRE( sav != nullptr );
        
        lsdj_project_t* project = lsdj_sav_get_project(sav, 0);
        REQUIRE( project != nullptr );
        
        lsdj_song_t* song = lsdj_project_get_song(project);
        assert(song != nullptr);
        
        REQUIRE( lsdj_song_get_format_version(song) == 4 );
        
        REQUIRE( lsdj_instrument_is_allocated(song, 0) == true );
        REQUIRE( lsdj_instrument_get_plv_speed(song, 0) == LSDJ_INSTRUMENT_PLV_FAST );
        REQUIRE( lsdj_instrument_get_vibrato_shape(song, 0) == LSDJ_INSTRUMENT_VIBRATO_TRIANGLE );
        REQUIRE( lsdj_instrument_get_vibrato_direction(song, 0) == LSDJ_INSTRUMENT_VIBRATO_DOWN );
        
        REQUIRE( lsdj_instrument_is_allocated(song, 1) == true );
        REQUIRE( lsdj_instrument_get_plv_speed(song, 1) == LSDJ_INSTRUMENT_PLV_FAST );
        REQUIRE( lsdj_instrument_get_vibrato_shape(song, 1) == LSDJ_INSTRUMENT_VIBRATO_TRIANGLE );
        REQUIRE( lsdj_instrument_get_vibrato_direction(song, 1) == LSDJ_INSTRUMENT_VIBRATO_UP );
        
        REQUIRE( lsdj_instrument_is_allocated(song, 8) == true );
        REQUIRE( lsdj_instrument_get_plv_speed(song, 8) == LSDJ_INSTRUMENT_PLV_TICK );
        REQUIRE( lsdj_instrument_get_vibrato_shape(song, 8) == LSDJ_INSTRUMENT_VIBRATO_SQUARE );
        REQUIRE( lsdj_instrument_get_vibrato_direction(song, 8) == LSDJ_INSTRUMENT_VIBRATO_DOWN );
        
        REQUIRE( lsdj_instrument_is_allocated(song, 0x0A) == true );
        REQUIRE( lsdj_instrument_get_plv_speed(song, 0x0A) == LSDJ_INSTRUMENT_PLV_TICK );
        REQUIRE( lsdj_instrument_get_vibrato_shape(song, 0x0A) == LSDJ_INSTRUMENT_VIBRATO_SAWTOOTH );
        REQUIRE( lsdj_instrument_get_vibrato_direction(song, 0x0A) == LSDJ_INSTRUMENT_VIBRATO_DOWN );
        
        REQUIRE( lsdj_instrument_is_allocated(song, 0x10) == true );
        REQUIRE( lsdj_instrument_get_plv_speed(song, 0x10) == LSDJ_INSTRUMENT_PLV_STEP );
        REQUIRE( lsdj_instrument_get_vibrato_shape(song, 0x10) == LSDJ_INSTRUMENT_VIBRATO_TRIANGLE );
        REQUIRE( lsdj_instrument_get_vibrato_direction(song, 0x10) == LSDJ_INSTRUMENT_VIBRATO_UP );
        
        REQUIRE( lsdj_synth_get_resonance_start(song, 0) == 0 );
        REQUIRE( lsdj_synth_get_resonance_end(song, 0) == 0 );
        
        REQUIRE( lsdj_synth_get_resonance_start(song, 2) == 0xA );
        REQUIRE( lsdj_synth_get_resonance_end(song, 2) == 0xA );
    }
    
    SECTION( "6.6.8" )
    {
        lsdj_sav_t* sav = nullptr;
        REQUIRE( lsdj_sav_read_from_file(RESOURCES_FOLDER "sav/lsdj668.sav", &sav, nullptr) == LSDJ_SUCCESS);
        REQUIRE( sav != nullptr );
        
        lsdj_project_t* project = lsdj_sav_get_project(sav, 0);
        REQUIRE( project != nullptr );
        
        lsdj_song_t* song = lsdj_project_get_song(project);
        assert(song != nullptr);
         
        REQUIRE( lsdj_song_get_format_version(song) == 5 );
        
        REQUIRE( lsdj_synth_get_resonance_start(song, 0) == 0 );
        REQUIRE( lsdj_synth_get_resonance_end(song, 0) == 0 );
        
        REQUIRE( lsdj_synth_get_resonance_start(song, 2) == 0xA );
        REQUIRE( lsdj_synth_get_resonance_end(song, 2) == 0xA );
        
        REQUIRE( lsdj_instrument_wave_get_length(song, 0x0B) == 0xF );
        REQUIRE( lsdj_instrument_wave_get_speed(song, 0x0B) == 0x4 );
        REQUIRE( lsdj_instrument_wave_get_speed(song, 0x0F) == 0x3 );
    }
    
    SECTION( "6.7.1" )
    {
        lsdj_sav_t* sav = nullptr;
        REQUIRE( lsdj_sav_read_from_file(RESOURCES_FOLDER "sav/lsdj671.sav", &sav, nullptr) == LSDJ_SUCCESS);
        REQUIRE( sav != nullptr );
        
        lsdj_project_t* project = lsdj_sav_get_project(sav, 0);
        REQUIRE( project != nullptr );
        
        lsdj_song_t* song = lsdj_project_get_song(project);
        assert(song != nullptr);
         
        REQUIRE( lsdj_song_get_format_version(song) == 6 );
        
        REQUIRE( lsdj_instrument_wave_get_length(song, 0x0B) == 0xF );
        REQUIRE( lsdj_instrument_wave_get_speed(song, 0x0B) == 0x4 );
        REQUIRE( lsdj_instrument_wave_get_speed(song, 0x0F) == 0x3 );
    }
    
    SECTION( "6.9.0" )
    {
        lsdj_sav_t* sav = nullptr;
        REQUIRE( lsdj_sav_read_from_file(RESOURCES_FOLDER "sav/lsdj690.sav", &sav, nullptr) == LSDJ_SUCCESS);
        REQUIRE( sav != nullptr );
        
        lsdj_project_t* project = lsdj_sav_get_project(sav, 0);
        REQUIRE( project != nullptr );
        
        lsdj_song_t* song = lsdj_project_get_song(project);
        assert(song != nullptr);
         
        REQUIRE( lsdj_song_get_format_version(song) == 7 );
        
        REQUIRE( lsdj_instrument_wave_get_length(song, 0x0B) == 0xF );
        REQUIRE( lsdj_instrument_wave_get_speed(song, 0x0B) == 0x4 );
        REQUIRE( lsdj_instrument_wave_get_speed(song, 0x0F) == 0x3 );
        
        REQUIRE( lsdj_table_get_command1(song, 0x00, 0) == LSDJ_COMMAND_S );
    }
    
    SECTION( "7.3.2" )
    {
        lsdj_sav_t* sav = nullptr;
        REQUIRE( lsdj_sav_read_from_file(RESOURCES_FOLDER "sav/lsdj732.sav", &sav, nullptr) == LSDJ_SUCCESS);
        REQUIRE( sav != nullptr );
        
        lsdj_project_t* project = lsdj_sav_get_project(sav, 0);
        REQUIRE( project != nullptr );
        
        lsdj_song_t* song = lsdj_project_get_song(project);
        assert(song != nullptr);
         
        REQUIRE( lsdj_song_get_format_version(song) == 8 );
        
        REQUIRE( lsdj_table_get_command1(song, 0x00, 0) == LSDJ_COMMAND_S );
        
        REQUIRE( lsdj_instrument_wave_get_repeat(song, 0x0B) == 0x0 );
        REQUIRE( lsdj_instrument_wave_get_repeat(song, 0x0F) == 0xF );
        REQUIRE( lsdj_instrument_wave_get_loop_pos(song, 0x0B) == 0xF );
        REQUIRE( lsdj_instrument_wave_get_loop_pos(song, 0x0F) == 0x0 );
    }
    
    SECTION( "7.9.0" )
    {
        lsdj_sav_t* sav = nullptr;
        REQUIRE( lsdj_sav_read_from_file(RESOURCES_FOLDER "sav/lsdj790.sav", &sav, nullptr) == LSDJ_SUCCESS);
        REQUIRE( sav != nullptr );
        
        lsdj_project_t* project = lsdj_sav_get_project(sav, 0);
        REQUIRE( project != nullptr );
        
        lsdj_song_t* song = lsdj_project_get_song(project);
        assert(song != nullptr);
         
        REQUIRE( lsdj_song_get_format_version(song) == 9 );
        
        REQUIRE( lsdj_instrument_wave_get_repeat(song, 0x0B) == 0x0 );
        REQUIRE( lsdj_instrument_wave_get_repeat(song, 0x0F) == 0xF );
        REQUIRE( lsdj_instrument_wave_get_loop_pos(song, 0x0B) == 0xF );
        REQUIRE( lsdj_instrument_wave_get_loop_pos(song, 0x0F) == 0x0 );
        
        REQUIRE( lsdj_instrument_wave_get_play_mode(song, 0x0B) == LSDJ_INSTRUMENT_WAVE_PLAY_MANUAL );
    }
    
    SECTION( "7.9.8" )
    {
        lsdj_sav_t* sav = nullptr;
        REQUIRE( lsdj_sav_read_from_file(RESOURCES_FOLDER "sav/lsdj798.sav", &sav, nullptr) == LSDJ_SUCCESS);
        REQUIRE( sav != nullptr );
        
        lsdj_project_t* project = lsdj_sav_get_project(sav, 0);
        REQUIRE( project != nullptr );
        
        lsdj_song_t* song = lsdj_project_get_song(project);
        assert(song != nullptr);
         
        REQUIRE( lsdj_song_get_format_version(song) == 10 );
        
        REQUIRE( lsdj_instrument_wave_get_play_mode(song, 0x0B) == LSDJ_INSTRUMENT_WAVE_PLAY_MANUAL );
        
        REQUIRE( lsdj_instrument_get_envelope(song, 0) == 0x88 );
        REQUIRE( lsdj_instrument_wave_get_volume(song, 1) == LSDJ_INSTRUMENT_WAVE_VOLUME_3 );
        REQUIRE( lsdj_instrument_get_envelope(song, 6) == 0x42 );
    }
    
    SECTION( "8.3.4" )
    {
        lsdj_sav_t* sav = nullptr;
        REQUIRE( lsdj_sav_read_from_file(RESOURCES_FOLDER "sav/lsdj834.sav", &sav, nullptr) == LSDJ_SUCCESS);
        REQUIRE( sav != nullptr );
        
        lsdj_project_t* project = lsdj_sav_get_project(sav, 0);
        REQUIRE( project != nullptr );
        
        lsdj_song_t* song = lsdj_project_get_song(project);
        assert(song != nullptr);
         
        REQUIRE( lsdj_song_get_format_version(song) == 11 );
        
        // Check for new ADSR values
        
        REQUIRE( lsdj_instrument_adsr_get_initial_level(song, 0) == 0x8 );
        REQUIRE( lsdj_instrument_adsr_get_attack_speed(song, 0) == 0x0 );

        REQUIRE( lsdj_instrument_adsr_get_initial_level(song, 6) == 0x4 );
        REQUIRE( lsdj_instrument_adsr_get_attack_speed(song, 6) == 0x2 );
        REQUIRE( lsdj_instrument_adsr_get_attack_level(song, 6) == 0x0 );

        REQUIRE( lsdj_instrument_adsr_get_attack_speed(song, 7) == 0x3 );
    }
    
    SECTION( "8.8.8" )
    {
        lsdj_sav_t* sav = nullptr;
        REQUIRE( lsdj_sav_read_from_file(RESOURCES_FOLDER "sav/lsdj888.sav", &sav, nullptr) == LSDJ_SUCCESS);
        REQUIRE( sav != nullptr );
        
        lsdj_project_t* project = lsdj_sav_get_project(sav, 0);
        REQUIRE( project != nullptr );
        
        lsdj_song_t* song = lsdj_project_get_song(project);
        assert(song != nullptr);
         
        REQUIRE( lsdj_song_get_format_version(song) == 16 );
        
        // Check for new ADSR values
        
        REQUIRE( lsdj_instrument_wave_get_wave(song, 0x0B) == 0 );
        REQUIRE( lsdj_instrument_wave_get_wave(song, 0x0F) == 0x10 );
    }
}
