#pragma once
#include <unordered_map>
#include <memory>

#include "snd_local.h"
#include "alure2.h"
#include "Effects/EnvEffects.hpp"
#include "Vox/VoxManager.hpp"
#include "Utilities/AudioCache.hpp"
#include "Loaders/SoundLoader.hpp"
#include "Utilities/ChannelManager.hpp"

namespace MetaAudio
{
  class AudioEngine final
  {
  private:
    std::unordered_map<alure::String, sfx_t> known_sfx;
    std::shared_ptr<SteamAudio> m_steamaudio = nullptr;

    //active control
    bool openal_started = false;
    bool openal_mute = false;

    alure::DeviceManager al_dev_manager;
    alure::AutoObj<alure::Device> al_device;
    alure::AutoObj<alure::Context> al_context;
    alure::UniquePtr<EnvEffects> al_efx;
    alure::UniquePtr<VoxManager> vox;
    alure::UniquePtr<ChannelManager> channel_pool;

    char al_device_name[1024] = "";
    int al_device_majorversion = 0;
    int al_device_minorversion = 0;

    std::shared_ptr<AudioCache> m_cache;
    std::shared_ptr<SoundLoader> m_loader;

    //Print buffer
    std::string dprint_buffer;

    void S_FreeCache(sfx_t* sfx);
    void S_FlushCaches(void);
    void S_CheckWavEnd(aud_channel_t* ch, aud_sfxcache_t* sc);
    void SND_Spatialize(aud_channel_t* ch, qboolean init);
    void S_StartSound(int entnum, int entchannel, sfx_t* sfx, float* origin, float fvol, float attenuation, int flags, int pitch, bool is_static);
    void ConfigureSource(aud_channel_t* channel, aud_sfxcache_t* data);

    bool OpenAL_Init();

    // SteamAudio
    std::shared_ptr<IPLhandle> sa_context = nullptr;
    IPLSimulationSettings sa_simulationSettings{};

    aud_channel_t sa_wetaudio;

    void SteamAudio_Init();
    std::shared_ptr<IOcclusionCalculator> GetOccluder();

    std::shared_ptr<SteamAudioMapMeshLoader> sa_meshloader = nullptr;

  public:
    AudioEngine(std::shared_ptr<AudioCache> cache, std::shared_ptr<SoundLoader> loader);
    ~AudioEngine();

    void AL_Version();
    void AL_ResetEFX();
    void AL_Devices(bool basic);

    void S_Startup();
    void S_Init();
    void S_Shutdown();

    void S_StartDynamicSound(int entnum, int entchannel, sfx_t* sfx, float* origin, float fvol, float attenuation, int flags, int pitch);
    void S_StartStaticSound(int entnum, int entchannel, sfx_t* sfx, float* origin, float fvol, float attenuation, int flags, int pitch);
    void S_StopSound(int entnum, int entchannel);
    void S_StopAllSounds(qboolean clear);
    void S_Update(float* origin, float* forward, float* right, float* up);

    sfx_t* S_FindName(char* name, int* pfInCache);
  };
}