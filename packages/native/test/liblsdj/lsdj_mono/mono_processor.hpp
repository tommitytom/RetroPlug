#pragma once

#include "../common/song_processor.hpp"

namespace lsdj
{
    class MonoProcessor :
        public SongProcessor
    {
    public:
        bool processInstruments = false;
        bool processTables = false;
        bool processPhrases = false;
        
    private:
        [[nodiscard]] bool shouldProcessSav(const ghc::filesystem::path& path) const final;
        [[nodiscard]] bool shouldProcessLsdsng(const ghc::filesystem::path& path) const final;
        
        [[nodiscard]] ghc::filesystem::path constructSavDestinationPath(const ghc::filesystem::path& path) final;
        [[nodiscard]] ghc::filesystem::path constructLsdsngDestinationPath(const ghc::filesystem::path& path) final;
        
        bool processSong(lsdj_song_t* song) final;
    };
}
