#include <lsdj/song.h>

#include <array>
#include <catch2/catch.hpp>
#include <cstring>

#include <lsdj/chain.h>
#include <lsdj/command.h>
#include <lsdj/groove.h>
#include <lsdj/instrument.h>
#include <lsdj/panning.h>
#include <lsdj/phrase.h>
#include <lsdj/sav.h>
#include <lsdj/speech.h>
#include <lsdj/synth.h>
#include <lsdj/table.h>
#include <lsdj/wave.h>

using namespace Catch;

TEST_CASE( "Song", "[song]" )
{
    lsdj_sav_t* sav = nullptr;
	lsdj_sav_read_from_file(RESOURCES_FOLDER "sav/all.sav", &sav, nullptr);
//    auto sav = lsdj_sav_read_from_file("/Users/stijn/Google Drive/lsdj/lsdj/old/lsdj_571.sav", nullptr);
//    auto sav = lsdj_sav_read_from_file("/Users/stijn/Google Drive/lsdj/lsdj/lsdj_822.sav", nullptr);
	REQUIRE( sav != nullptr );

	REQUIRE( LSDJ_SONG_BYTE_COUNT == 0x8000);

	SECTION( "Happy Birthday" )
	{
		auto song0 = lsdj_project_get_song_const(lsdj_sav_get_project_const(sav, 0));
        assert(song0 != nullptr);
        
        auto song1 = lsdj_project_get_song_const(lsdj_sav_get_project_const(sav, 1));
        assert(song1 != nullptr);
        
		SECTION( "Song settings" )
		{
			REQUIRE( lsdj_song_get_format_version(song0) == 7 );
			REQUIRE( lsdj_song_has_changed(song0) == false );
			REQUIRE( lsdj_song_get_tempo(song0) == 88 );
			REQUIRE( lsdj_song_get_transposition(song0) == 0 );
			REQUIRE( lsdj_song_get_sync_mode(song0) == LSDJ_SYNC_NONE );
			REQUIRE( lsdj_song_get_drum_max(song0) == 0x00 );
		}

		SECTION( "Editor settings" )
		{
			REQUIRE( lsdj_song_get_clone_mode(song0) == LSDJ_CLONE_DEEP );
			REQUIRE( lsdj_song_get_font(song0) == 0 );
			REQUIRE( lsdj_song_get_color_palette(song0) == 0 );
			REQUIRE( lsdj_song_get_key_delay(song0) == 7 );
			REQUIRE( lsdj_song_get_key_repeat(song0) == 2 );
			REQUIRE( lsdj_song_get_prelisten(song0) == true );
		}

		SECTION( "Clock" )
		{
			REQUIRE( lsdj_song_get_total_days(song0) == 0);
			REQUIRE( lsdj_song_get_total_hours(song0) == 18);
			REQUIRE( lsdj_song_get_total_minutes(song0) == 10);

			REQUIRE( lsdj_song_get_work_hours(song0) == 2);
			REQUIRE( lsdj_song_get_work_minutes(song0) == 4);
		}

		SECTION( "Rows" )
		{
			REQUIRE( lsdj_row_get_chain(song0, 0, LSDJ_CHANNEL_PULSE1) == 1 );
			REQUIRE( lsdj_row_get_chain(song0, 0, LSDJ_CHANNEL_PULSE2) == 2 );
			REQUIRE( lsdj_row_get_chain(song0, 0, LSDJ_CHANNEL_WAVE) == 3 );
			REQUIRE( lsdj_row_get_chain(song0, 0, LSDJ_CHANNEL_NOISE) == 4 );
			REQUIRE( lsdj_row_get_chain(song0, 1, LSDJ_CHANNEL_PULSE1) == LSDJ_SONG_NO_CHAIN );
            
            REQUIRE( lsdj_song_is_row_bookmarked(song0, 0, LSDJ_CHANNEL_PULSE1) == false );
            REQUIRE( lsdj_song_is_row_bookmarked(song0, 2, LSDJ_CHANNEL_PULSE2) == false );
            REQUIRE( lsdj_song_is_row_bookmarked(song0, 1, LSDJ_CHANNEL_WAVE) == false );
            REQUIRE( lsdj_song_is_row_bookmarked(song0, 3, LSDJ_CHANNEL_NOISE) == false );
            REQUIRE( lsdj_song_is_row_bookmarked(song0, 5, LSDJ_CHANNEL_PULSE1) == false );
		}

		SECTION( "Chains" )
		{
			REQUIRE( lsdj_chain_is_allocated(song0, 0x00) == false );
			REQUIRE( lsdj_chain_is_allocated(song0, 0x01) == true );
			REQUIRE( lsdj_chain_is_allocated(song0, 0x02) == true );
			REQUIRE( lsdj_chain_is_allocated(song0, 0x03) == true );
			REQUIRE( lsdj_chain_is_allocated(song0, 0x04) == true );
			REQUIRE( lsdj_chain_is_allocated(song0, 0x05) == false );

			REQUIRE( lsdj_chain_get_phrase(song0, 0x01, 6) == 0x07 );
			REQUIRE( lsdj_chain_get_phrase(song0, 0x02, 5) == 0x1B );
			REQUIRE( lsdj_chain_get_phrase(song0, 0x03, 2) == 0x0E );
			REQUIRE( lsdj_chain_get_phrase(song0, 0x04, 9) == 0x25 );
			REQUIRE( lsdj_chain_get_phrase(song0, 0x05, 0) == LSDJ_CHAIN_NO_PHRASE );

			REQUIRE( lsdj_chain_get_transposition(song0, 0x03, 15) == 0 );
		}

		SECTION( "Phrases" )
		{
			REQUIRE( lsdj_phrase_is_allocated(song0, 0x00) == false );
			REQUIRE( lsdj_phrase_is_allocated(song0, 0x01) == true );
			REQUIRE( lsdj_phrase_is_allocated(song0, 0x0A) == true );
			REQUIRE( lsdj_phrase_is_allocated(song0, 0x1A) == true );
			REQUIRE( lsdj_phrase_is_allocated(song0, 0x24) == true );
			REQUIRE( lsdj_phrase_is_allocated(song0, 0x26) == false );

			REQUIRE( lsdj_phrase_get_note(song0, 0x05, 0x3) == 49 );
			REQUIRE( lsdj_phrase_get_note(song0, 0x18, 0xE) == 19 );
			REQUIRE( lsdj_phrase_get_note(song0, 0x1C, 0x0) == 16 );
			REQUIRE( lsdj_phrase_get_note(song0, 0x08, 0x0B) == LSDJ_PHRASE_NO_NOTE );

			REQUIRE( lsdj_phrase_get_instrument(song0, 0x0C, 0x6) == 0x02 );
			REQUIRE( lsdj_phrase_get_instrument(song0, 0x16, 0x0) == 0x03 );
			REQUIRE( lsdj_phrase_get_instrument(song0, 0x16, 0x4) == 0x06 );
			REQUIRE( lsdj_phrase_get_instrument(song0, 0x16, 0x6) == LSDJ_PHRASE_NO_INSTRUMENT );

			REQUIRE( lsdj_phrase_get_command(song0, 0x12, 0x2) == LSDJ_COMMAND_L );
			REQUIRE( lsdj_phrase_get_command_value(song0, 0x12, 0x2) == 0x20 );
			REQUIRE( lsdj_phrase_get_command(song0, 0x0E, 0xA) == LSDJ_COMMAND_P );
			REQUIRE( lsdj_phrase_get_command_value(song0, 0x0E, 0xA) == 0x0F );
			REQUIRE( lsdj_phrase_get_command(song0, 0x05, 0x3) == LSDJ_COMMAND_E );
			REQUIRE( lsdj_phrase_get_command_value(song0, 0x05, 0x3) == 0xA3 );
			REQUIRE( lsdj_phrase_get_command(song0, 0x16, 0x0) == LSDJ_COMMAND_NONE );
		}

		SECTION( "Instruments" )
		{
			SECTION ("Check allocations" )
			{
				for (uint8_t i = 0; i < 10; i += 1)
					REQUIRE( lsdj_instrument_is_allocated(song0, i) );
				REQUIRE( lsdj_instrument_is_allocated(song0, 10) != true );
			}

			SECTION( "Names" )
			{
				REQUIRE( strncmp(lsdj_instrument_get_name(song0, 0), "LEAD1", LSDJ_INSTRUMENT_NAME_LENGTH) == 0 );
				REQUIRE( strncmp(lsdj_instrument_get_name(song0, 1), "LEAD2", LSDJ_INSTRUMENT_NAME_LENGTH) == 0 );
				REQUIRE( strncmp(lsdj_instrument_get_name(song0, 2), "SIDE", LSDJ_INSTRUMENT_NAME_LENGTH) == 0 );
				REQUIRE( strncmp(lsdj_instrument_get_name(song0, 3), "KICK", LSDJ_INSTRUMENT_NAME_LENGTH) == 0 );
				REQUIRE( strncmp(lsdj_instrument_get_name(song0, 4), "HATC", LSDJ_INSTRUMENT_NAME_LENGTH) == 0 );
				REQUIRE( strncmp(lsdj_instrument_get_name(song0, 5), "SNARE", LSDJ_INSTRUMENT_NAME_LENGTH) == 0 );
				REQUIRE( strncmp(lsdj_instrument_get_name(song0, 6), "BASS", LSDJ_INSTRUMENT_NAME_LENGTH) == 0 );
				REQUIRE( strncmp(lsdj_instrument_get_name(song0, 7), "ARP", LSDJ_INSTRUMENT_NAME_LENGTH) == 0 );
				REQUIRE( strncmp(lsdj_instrument_get_name(song0, 8), "SIDE", LSDJ_INSTRUMENT_NAME_LENGTH) == 0 );
				REQUIRE( strncmp(lsdj_instrument_get_name(song0, 9), "ARP2", LSDJ_INSTRUMENT_NAME_LENGTH) == 0 );
			}

			SECTION( "Generic" )
			{
				REQUIRE( lsdj_instrument_get_type(song0, 0) == LSDJ_INSTRUMENT_TYPE_PULSE );
				REQUIRE( lsdj_instrument_get_type(song0, 3) == LSDJ_INSTRUMENT_TYPE_WAVE );
				REQUIRE( lsdj_instrument_get_envelope(song0, 0) == 0xA6 );
				REQUIRE( lsdj_instrument_get_envelope(song0, 2) == 0x93 );
				REQUIRE( lsdj_instrument_get_transpose(song0, 0) == true );

				REQUIRE( lsdj_instrument_is_table_enabled(song0, 0) == false );
				REQUIRE( lsdj_instrument_is_table_enabled(song0, 3) == true );
				REQUIRE( lsdj_instrument_get_table(song0, 3) == 0x00 );
				REQUIRE( lsdj_instrument_get_table_mode(song0, 0) == LSDJ_INSTRUMENT_TABLE_PLAY );
				REQUIRE( lsdj_instrument_get_table_mode(song0, 3) == LSDJ_INSTRUMENT_TABLE_PLAY );

				REQUIRE( lsdj_instrument_get_vibrato_direction(song0, 0) == LSDJ_INSTRUMENT_VIBRATO_DOWN );
				REQUIRE( lsdj_instrument_get_vibrato_shape(song0, 0) == LSDJ_INSTRUMENT_VIBRATO_TRIANGLE );
				REQUIRE( lsdj_instrument_get_plv_speed(song0, 0) == LSDJ_INSTRUMENT_PLV_FAST );
				REQUIRE( lsdj_instrument_get_command_rate(song0, 0) == 0x00 );
			}

			SECTION( "Pulse" )
			{
				REQUIRE( lsdj_instrument_pulse_get_pulse_width(song0, 0) == LSDJ_INSTRUMENT_PULSE_WIDTH_25 );
				REQUIRE( lsdj_instrument_pulse_get_pulse_width(song0, 1) == LSDJ_INSTRUMENT_PULSE_WIDTH_50 );
				REQUIRE( lsdj_instrument_get_panning(song0, 0) == LSDJ_PAN_LEFT_RIGHT );
				REQUIRE( lsdj_instrument_pulse_get_length(song0, 0) == LSDJ_INSTRUMENT_PULSE_LENGTH_INFINITE );
				REQUIRE( lsdj_instrument_pulse_get_sweep(song0, 0) == 0xFF );
				REQUIRE( lsdj_instrument_pulse_get_pulse2_tune(song0, 0) == 0x00 );
				REQUIRE( lsdj_instrument_pulse_get_finetune(song0, 0) == 0x0 );
			}

			SECTION( "Wave" )
			{
                REQUIRE( lsdj_instrument_wave_get_volume(song0, 3) == LSDJ_INSTRUMENT_WAVE_VOLUME_3 );
				REQUIRE( lsdj_instrument_wave_get_synth(song0, 3) == 0x0 );
				REQUIRE( lsdj_instrument_wave_get_synth(song0, 6) == 0x1 );
				REQUIRE( lsdj_instrument_wave_get_play_mode(song0, 3) == LSDJ_INSTRUMENT_WAVE_PLAY_MANUAL );
				REQUIRE( lsdj_instrument_wave_get_length(song0, 3) == 0xF );
				REQUIRE( lsdj_instrument_wave_get_repeat(song0, 3) == 0x0 );
                REQUIRE( lsdj_instrument_wave_get_loop_pos(song0, 3) == 0xF );
				REQUIRE( lsdj_instrument_wave_get_speed(song0, 3) == 0x04 );
			}
            
            SECTION( "Kit" )
            {
                REQUIRE( lsdj_instrument_kit_get_volume(song0, 3) == LSDJ_INSTRUMENT_WAVE_VOLUME_3 );
                REQUIRE( lsdj_instrument_kit_get_pitch(song1, 0x0A) == 0x00 );
                REQUIRE( lsdj_instrument_kit_get_half_speed(song1, 0x0A) == false );
                REQUIRE( lsdj_instrument_kit_get_distortion_mode(song1, 0x0A) == LSDJ_INSTRUMENT_KIT_DISTORTION_CLIP );
                
                REQUIRE( lsdj_instrument_kit_get_kit1(song1, 0) == 0x0 );
                REQUIRE( lsdj_instrument_kit_get_kit2(song1, 0) == 0x0 );
                REQUIRE( lsdj_instrument_kit_get_offset1(song1, 0) == 0x00 );
                REQUIRE( lsdj_instrument_kit_get_offset2(song1, 0) == 0x00 );
                REQUIRE( lsdj_instrument_kit_get_length1(song1, 0) == LSDJ_INSTRUMENT_KIT_LENGTH_AUTO );
                REQUIRE( lsdj_instrument_kit_get_length2(song1, 0) == LSDJ_INSTRUMENT_KIT_LENGTH_AUTO );
                REQUIRE( lsdj_instrument_kit_get_loop1(song1, 0) == LSDJ_INSTRUMENT_KIT_LOOP_OFF );
                REQUIRE( lsdj_instrument_kit_get_loop2(song1, 0) == LSDJ_INSTRUMENT_KIT_LOOP_OFF );
            }

            SECTION( "Noise" )
			{
				REQUIRE( lsdj_instrument_noise_get_length(song0, 4) == LSDJ_INSTRUMENT_NOISE_LENGTH_INFINITE );
				REQUIRE( lsdj_instrument_noise_get_shape(song0, 4) == 0xFF );
				REQUIRE( lsdj_instrument_noise_get_stability(song0, 4) == LSDJ_INSTRUMENT_NOISE_FREE );
			}
		}

		SECTION( "Synths" )
		{
			REQUIRE( lsdj_synth_is_wave_overwritten(song0, 0x0) == false );
			REQUIRE( lsdj_synth_is_wave_overwritten(song0, 0x1) == false );

			REQUIRE( lsdj_synth_get_waveform(song0, 0x0) == LSDJ_SYNTH_WAVEFORM_TRIANGLE );
			REQUIRE( lsdj_synth_get_waveform(song0, 0x1) == LSDJ_SYNTH_WAVEFORM_SAW );
			REQUIRE( lsdj_synth_get_distortion(song0, 0x0) == LSDJ_SYNTH_DISTORTION_CLIP );
			REQUIRE( lsdj_synth_get_phase_compression(song0, 0x0) == LSDJ_SYNTH_PHASE_NORMAL );

			REQUIRE( lsdj_synth_get_volume_start(song0, 0x0) == 0x30 );
			REQUIRE( lsdj_synth_get_volume_end(song0, 0x0) == 0x10 );
			REQUIRE( lsdj_synth_get_resonance_start(song0, 0x0) == 0x0 );
			REQUIRE( lsdj_synth_get_resonance_end(song0, 0x0) == 0x0 );
			REQUIRE( lsdj_synth_get_cutoff_start(song0, 0x0) == 0xFF );
			REQUIRE( lsdj_synth_get_cutoff_end(song0, 0x0) == 0xFF );
			REQUIRE( lsdj_synth_get_vshift_start(song0, 0x0) == 0x0 );
			REQUIRE( lsdj_synth_get_vshift_end(song0, 0x0) == 0x0 );
			REQUIRE( lsdj_synth_get_limit_start(song0, 0x0) == 0xF );
			REQUIRE( lsdj_synth_get_limit_end(song0, 0x0) == 0xF );
			REQUIRE( lsdj_synth_get_phase_start(song0, 0x0) == 0x00 );
			REQUIRE( lsdj_synth_get_phase_end(song0, 0x0) == 0x00 );
		}

		SECTION( "Waves" )
		{
			std::array<std::uint8_t, LSDJ_WAVE_BYTE_COUNT> wave00 = {
				0x89, 0xBD, 0xFF, 0xDF, 0xFF, 0xFF, 0xFD, 0xB9, 0x86, 0x42, 0x00, 0x00, 0x00, 0x00, 0x02, 0x46
			};

			REQUIRE( memcmp(lsdj_wave_get_bytes_const(song0, 0x00), wave00.data(), wave00.size()) == 0 );

			std::array<std::uint8_t, LSDJ_WAVE_BYTE_COUNT> wave31 = {
				0x8E, 0xCD, 0xCC, 0xBB, 0xAA, 0xA9, 0x99, 0x88, 0x87, 0x76, 0x66, 0x55, 0x54, 0x43, 0x32, 0x31
			};

			REQUIRE( memcmp(lsdj_wave_get_bytes_const(song0, 0x31), wave31.data(), wave31.size()) == 0 );
		}

		SECTION( "Tables" )
		{
			for (uint8_t i = 0; i < 4; i += 1)
				REQUIRE( lsdj_table_is_allocated(song0, i) );
			REQUIRE( lsdj_table_is_allocated(song0, 5) != true );

			REQUIRE( lsdj_table_get_envelope(song0, 2, 0) == 0x00 );

			REQUIRE( lsdj_table_get_transposition(song0, 2, 0) == 0x00 );
			REQUIRE( lsdj_table_get_transposition(song0, 2, 1) == 0x02 );
			REQUIRE( lsdj_table_get_transposition(song0, 2, 2) == 0x0C );
			REQUIRE( lsdj_table_get_transposition(song0, 2, 3) == 0x0E );
			REQUIRE( lsdj_table_get_transposition(song0, 2, 4) == 0x10 );

			REQUIRE( lsdj_table_get_command1(song0, 0, 0) == LSDJ_COMMAND_P );
			REQUIRE( lsdj_table_get_command1_value(song0, 0, 0) == 0xDB );
			REQUIRE( lsdj_table_get_command1(song0, 0, 1) == LSDJ_COMMAND_NONE );
			REQUIRE( lsdj_table_get_command1(song0, 0, 3) == LSDJ_COMMAND_K );
			REQUIRE( lsdj_table_get_command1_value(song0, 0, 3) == 0x00 );

			REQUIRE( lsdj_table_get_command2(song0, 1, 0) == LSDJ_COMMAND_O );
			REQUIRE( lsdj_table_get_command2_value(song0, 1, 0) == LSDJ_PAN_LEFT );
			REQUIRE( lsdj_table_get_command2(song0, 1, 1) == LSDJ_COMMAND_O );
			REQUIRE( lsdj_table_get_command2_value(song0, 1, 1) == LSDJ_PAN_LEFT_RIGHT );
			REQUIRE( lsdj_table_get_command2(song0, 1, 2) == LSDJ_COMMAND_NONE );
		}

		SECTION(" Grooves" )
		{
			REQUIRE( lsdj_groove_get_step(song0, 0, 0) == 6 );
			REQUIRE( lsdj_groove_get_step(song0, 0, 1) == 6 );
			REQUIRE( lsdj_groove_get_step(song0, 0, 2) == LSDJ_GROOVE_NO_VALUE );
		}

		SECTION( "Speech" )
		{
			REQUIRE( strncmp(lsdj_speech_get_word_name(song0, 0), "C 2 ", LSDJ_SPEECH_WORD_NAME_LENGTH) == 0 );
			REQUIRE( strncmp(lsdj_speech_get_word_name(song0, 3), "D#2 ", LSDJ_SPEECH_WORD_NAME_LENGTH) == 0 );
			REQUIRE( strncmp(lsdj_speech_get_word_name(song0, 32), "G#4 ", LSDJ_SPEECH_WORD_NAME_LENGTH) == 0 );

			REQUIRE( lsdj_speech_get_word_allophone(song0, 0, 0) == LSDJ_SPEECH_WORD_NO_ALLOPHONE_VALUE );
			REQUIRE( lsdj_speech_get_word_allophone_duration(song0, 0, 0) == 0x00 );
		}
	}
}
