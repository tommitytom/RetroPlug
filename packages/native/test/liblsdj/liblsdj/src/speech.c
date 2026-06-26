#include "speech.h"

#include <string.h>

#include "song_offsets.h"

#define WORD_LENGTH (32)

void lsdj_speech_set_word_name(lsdj_song_t* song, uint8_t word, const char* name)
{
	strncpy((char*)&song->bytes[WORD_NAMES_OFFSET + word * LSDJ_SPEECH_WORD_NAME_LENGTH], name, LSDJ_SPEECH_WORD_NAME_LENGTH);
}

const char* lsdj_speech_get_word_name(const lsdj_song_t* song, uint8_t word)
{
	return (const char*)&song->bytes[WORD_NAMES_OFFSET + word * LSDJ_SPEECH_WORD_NAME_LENGTH];
}

void lsdj_speech_set_word_allophone(lsdj_song_t* song, uint8_t word, uint8_t allophone, uint8_t value)
{
	song->bytes[WORDS_OFFSET + word * WORD_LENGTH + allophone] = value;
}

uint8_t lsdj_speech_get_word_allophone(const lsdj_song_t* song, uint8_t word, uint8_t allophone)
{
	return song->bytes[WORDS_OFFSET + word * WORD_LENGTH + allophone];
}

void lsdj_speech_set_word_allophone_duration(lsdj_song_t* song, uint8_t word, uint8_t allophone, uint8_t duration)
{
	song->bytes[WORDS_OFFSET + word * WORD_LENGTH + allophone + 16] = duration;
}

uint8_t lsdj_speech_get_word_allophone_duration(const lsdj_song_t* song, uint8_t word, uint8_t allophone)
{
	return song->bytes[WORDS_OFFSET + word * WORD_LENGTH + allophone + 16];
}