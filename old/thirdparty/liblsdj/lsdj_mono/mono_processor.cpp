#include "mono_processor.hpp"

#include <cassert>
#include <lsdj/instrument.h>
#include <lsdj/phrase.h>
#include <lsdj/table.h>

namespace lsdj
{
    void convertInstrument(lsdj_song_t* song, uint8_t instrument)
    {
        if (lsdj_instrument_get_panning(song, instrument) != LSDJ_PAN_NONE)
            lsdj_instrument_set_panning(song, instrument, LSDJ_PAN_LEFT_RIGHT);
    }

    void convertTable(lsdj_song_t* song, uint8_t table)
    {
        for (int step = 0; step < LSDJ_TABLE_LENGTH; ++step)
        {
            if (lsdj_table_get_command1(song, table, step) == LSDJ_COMMAND_O &&
                lsdj_table_get_command1_value(song, table, step) != LSDJ_PAN_NONE)
            {
                lsdj_table_set_command1_value(song, table, step, LSDJ_PAN_LEFT_RIGHT);
            }
            
            if (lsdj_table_get_command2(song, table, step) == LSDJ_COMMAND_O &&
                lsdj_table_get_command2_value(song, table, step) != LSDJ_PAN_NONE)
            {
                lsdj_table_set_command2_value(song, table, step, LSDJ_PAN_LEFT_RIGHT);
            }
        }
    }

    void convertPhrase(lsdj_song_t* song, uint8_t phrase)
    {
        for (int step = 0; step < LSDJ_PHRASE_LENGTH; ++step)
        {
            if (lsdj_phrase_get_command(song, phrase, step) == LSDJ_COMMAND_O &&
                lsdj_phrase_get_command_value(song, phrase, step) != LSDJ_PAN_NONE)
            {
                lsdj_phrase_set_command_value(song, phrase, step, LSDJ_PAN_LEFT_RIGHT);
            }
        }
    }

    [[nodiscard]] bool alreadyEndsWithMono(const ghc::filesystem::path& path)
    {
        const auto stem = path.stem().string();
        return stem.size() >= 5 && stem.substr(stem.size() - 5) == ".MONO";
    }

    [[nodiscard]] ghc::filesystem::path addMonoSuffix(const ghc::filesystem::path& path)
    {
        return path.parent_path() / (path.stem().string() + ".MONO" + path.extension().string());
    }

    bool MonoProcessor::shouldProcessSav(const ghc::filesystem::path& path) const
    {
        return alreadyEndsWithMono(path);
    }

    bool MonoProcessor::shouldProcessLsdsng(const ghc::filesystem::path& path) const
    {
        return alreadyEndsWithMono(path);
    }

    ghc::filesystem::path MonoProcessor::constructSavDestinationPath(const ghc::filesystem::path& path)
    {
        return addMonoSuffix(path);
    }

    ghc::filesystem::path MonoProcessor::constructLsdsngDestinationPath(const ghc::filesystem::path& path)
    {
        return addMonoSuffix(path);
    }

    bool MonoProcessor::processSong(lsdj_song_t* song)
    {
        assert(song != nullptr);
        
        if (processInstruments)
        {
            for (int i = 0; i < LSDJ_INSTRUMENT_COUNT; ++i)
                convertInstrument(song, i);
        }

        if (processTables)
        {
            for (int i = 0; i < LSDJ_TABLE_COUNT; ++i)
                convertTable(song, i);
        }

        if (processPhrases)
        {
            for (int i = 0; i < LSDJ_PHRASE_COUNT; ++i)
                convertPhrase(song, i);
        }
        
        return true;
    }
}
