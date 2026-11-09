#pragma once

#include <jni.h>

// Plays an audio file (MP3, WAV, OGG, AAC...) through android.media.MediaPlayer, so no decoder ships in the mod.
// Game thread only. Tracks that are playing pause while the game is in the background and continue when it returns.
// Unload before the game exits (e.g. on Events::shutdownRwEvent); JNI is not safe during static destruction.
class MusicPlayer {
  public:
    MusicPlayer() = default;
    ~MusicPlayer() { Unload(); }
    MusicPlayer(const MusicPlayer &) = delete;
    MusicPlayer &operator=(const MusicPlayer &) = delete;

    // Replaces the current track. Returns false if the file is missing or can't be decoded.
    bool Load(const char *path);
    void Unload();

    void Play();
    void Pause();
    void Stop(); // Pauses and rewinds to the start.
    void SetLooping(bool looping);
    void SetVolume(float volume); // 0.0 to 1.0

    bool IsLoaded() const { return player_ != nullptr; }
    bool IsPlaying() const;

  private:
    jobject player_ = nullptr; // Global reference to the android.media.MediaPlayer.
};
