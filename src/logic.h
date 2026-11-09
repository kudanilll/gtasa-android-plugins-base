#pragma once

#include <initializer_list>
#include <string>

class MusicPlayer;

// Game-side actions behind the example UI, with no ImGui code. Game thread only.
namespace Logic {
// First of `names` found in the game's Android data folder, then the AML config folder; empty if none exist.
std::string FindAsset(std::initializer_list<const char *> names);

bool IsPlayerReady();

void SetTime(int hour, int minute);

inline constexpr int weatherPresetCount = 8;
extern const char *const weatherPresetNames[weatherPresetCount];
void SetWeatherPreset(int index);
void RestoreWeather();

// Returns the message to show the player.
const char *TeleportToWaypoint();

MusicPlayer &Music();
// Loads music.mp3 or music.wav on first use, then plays. Returns nullptr on success, otherwise why it can't play.
const char *PlayMusic(float volume, bool looping);

// Releases the music player. Must run before the game exits.
void Shutdown();
} // namespace Logic
