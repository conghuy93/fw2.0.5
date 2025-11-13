#ifndef MUSIC_H
#define MUSIC_H

#include <string>
#include <cstdint>

class Music {
public:
    virtual ~Music() = default;
    
    // Pure virtual functions that derived classes must implement
    virtual bool Download(const std::string& song_name, const std::string& artist_name) = 0;
    virtual std::string GetDownloadResult() = 0;
    virtual bool StartStreaming(const std::string& music_url) = 0;
    virtual bool Stop() = 0;  // Renamed from StopStreaming
    virtual size_t GetBufferSize() const = 0;
    virtual bool IsDownloading() const = 0;
    virtual bool IsPlaying() const = 0;  // Added for MCP status
    virtual int16_t* GetAudioData() = 0;
};

// Alias for compatibility with existing code that uses MusicPlayer
using MusicPlayer = Music;

#endif // MUSIC_H