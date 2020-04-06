#pragma once

#include "../common/song_processor.hpp"

namespace lsdj
{
    class CleanProcessor :
        public SongProcessor
    {
    public:
        bool processTables = false;
        bool processInstruments = false;
        bool processChains = false;
        bool processPhrases = false;
        
    private:
        bool processSong(lsdj_song_t& song) final;
    };
}
