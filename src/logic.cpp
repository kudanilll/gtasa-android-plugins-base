#include "logic.h"

#include <mod/amlmod.h>
#include <mod/music_player.h>
#include <plugin-sdk/game_sa/engine/Radar.h>
#include <plugin-sdk/game_sa/engine/Streaming.h>
#include <plugin-sdk/game_sa/engine/World.h>
#include <plugin-sdk/game_sa/entity/PlayerPed.h>
#include <plugin-sdk/game_sa/entity/Vehicle.h>
#include <plugin-sdk/game_sa/other/Clock.h>
#include <plugin-sdk/game_sa/other/Weather.h>
#include <plugin-sdk/game_sa/plugin.h>

#include <cmath>
#include <unistd.h>

namespace {
MusicPlayer g_Music;

CPlayerPed *Player() { return FindPlayerPed ? FindPlayerPed(-1) : nullptr; }

const i16 weatherPresetIds[Logic::weatherPresetCount] = {
    WEATHER_SUNNY_LA, WEATHER_EXTRASUNNY_LA, WEATHER_CLOUDY_LA,    WEATHER_SUNNY_SF,
    WEATHER_RAINY_SF, WEATHER_FOGGY_SF,      WEATHER_SUNNY_DESERT, WEATHER_SANDSTORM_DESERT};
} // namespace

namespace Logic {
std::string FindAsset(std::initializer_list<const char *> names) {
    if (!aml)
        return {};
    for (const char *dir : {aml->GetAndroidDataPath(), aml->GetConfigPath()}) {
        if (!dir)
            continue;
        for (const char *name : names) {
            std::string path = std::string(dir) + '/' + name;
            if (access(path.c_str(), R_OK) == 0)
                return path;
        }
    }
    return {};
}

bool IsPlayerReady() { return Player() != nullptr; }

void SetTime(int hour, int minute) { CClock::SetGameClock(static_cast<u8>(hour), static_cast<u8>(minute), 0); }

const char *const weatherPresetNames[weatherPresetCount] = {
    "Sunny (LA)", "Extra Sunny (LA)", "Cloudy (LA)", "Sunny (SF)", "Rainy (SF)", "Foggy (SF)", "Desert", "Sandstorm"};

void SetWeatherPreset(int index) {
    if (index >= 0 && index < weatherPresetCount)
        CWeather::ForceWeatherNow(weatherPresetIds[index]);
}

void RestoreWeather() { CWeather::ReleaseWeather(); }

const char *TeleportToWaypoint() {
    CPlayerPed *player = Player();
    if (!player)
        return "Player not loaded";

    // Scan radar blips for the player's map waypoint
    tRadarTrace *traces = CRadar::ms_RadarTrace;
    tRadarTrace *waypoint = nullptr;
    if (traces) {
        for (int i = 0; i < 250; ++i) {
            if (traces[i].m_bInUse && traces[i].m_nBlipType == BLIP_COORD &&
                traces[i].m_nRadarSprite == RADAR_SPRITE_WAYPOINT) {
                waypoint = &traces[i];
                break;
            }
        }
    }

    if (!waypoint)
        return "Place a waypoint on map first!";

    CVector dest = waypoint->m_vecPos;
    dest.z = 1000.0f;

    // Preload collision at destination
    CStreaming::LoadSceneCollision(dest);
    CStreaming::LoadScene(dest);

    bool found = false;
    CEntity *ground = nullptr;
    float z = CWorld::FindGroundZFor3DCoord(dest.x, dest.y, dest.z, &found, &ground);
    if (found && std::isfinite(z)) {
        dest.z = z + 1.5f;
    } else {
        dest.z = 20.0f;
    }

    CVehicle *veh = FindPlayerVehicle ? FindPlayerVehicle(-1, false) : nullptr;
    if (veh) {
        dest.z += 1.0f;
        veh->Teleport(dest, false);
    } else {
        player->Teleport(dest, false);
    }

    return "Teleported to waypoint!";
}

MusicPlayer &Music() { return g_Music; }

const char *PlayMusic(float volume, bool looping) {
    if (!g_Music.IsLoaded()) {
        const std::string path = FindAsset({"music.mp3", "music.wav"});
        if (path.empty())
            return "Put music.mp3 or music.wav next to button.png";
        if (!g_Music.Load(path.c_str()))
            return "Can't play the music file";
        g_Music.SetVolume(volume);
        g_Music.SetLooping(looping);
    }
    g_Music.Play();
    return nullptr;
}

void Shutdown() { g_Music.Unload(); }
} // namespace Logic
