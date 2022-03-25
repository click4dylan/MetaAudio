#include "AudioEngine.hpp"

#include "Utilities/VectorUtils.hpp"

#include "Loaders/GoldSrcFileFactory.hpp"

#include "Effects/GoldSrcOcclusionCalculator.hpp"
#include "Effects/SteamAudioOcclusionCalculator.hpp"

#include "SoundSources/SoundSourceFactory.hpp"

IConsoleVariable doppler(CVAR_FLOAT, "al_doppler", "", "0.3", FCVAR_EXTDLL);
IConsoleVariable xfi_workaround(CVAR_FLOAT, "al_xfi_workaround", "", "0.0", FCVAR_EXTDLL);
IConsoleVariable sndshow(CVAR_BOOL, "snd_show", "", "0", FCVAR_EXTDLL);
IConsoleVariable occluder(CVAR_BOOL, "al_occluder", "", "1", FCVAR_EXTDLL);
IConsoleVariable roomtype(CVAR_INT, "room_type", "", "0", FCVAR_EXTDLL);
IConsoleVariable roomoff(CVAR_BOOL, "room_off", "", "0", FCVAR_EXTDLL);
IConsoleVariable waterroomtype(CVAR_INT, "waterroom_type", "", "14", FCVAR_EXTDLL);
extern bool g_bMetaAudioPaused;

namespace MetaAudio
{
  AudioEngine::AudioEngine(std::shared_ptr<AudioCache> cache, std::shared_ptr<SoundLoader> loader) : m_cache(cache), m_loader(loader)
  {
  }

  void AudioEngine::S_FreeCache(sfx_t* sfx)
  {
    aud_sfxcache_t* sc = static_cast<aud_sfxcache_t*>(sfx->cache.data);
    if (!sc)
      return;

    //2015-12-12 fixed a bug that a buffer in use is freed
    if (channel_manager->IsPlaying(sfx))
    {
      return;
    }

    if (sc->buffer)
    {
      al_context->removeBuffer(sc->buffer);
    }

    sfx->cache.data = nullptr;

    m_cache->Cache_Free(sfx->name);
  }

  void AudioEngine::S_FlushCaches(void)
  {
    for (auto& sfx : known_sfx)
    {
      S_FreeCache(&(sfx.second));
    }
    known_sfx.clear();
  }

  sfx_t* AudioEngine::S_FindName(char* name, int* pfInCache)
  {
    try
    {
      sfx_t* sfx = nullptr;

      if (!name)
        Sys_ErrorEx("S_FindName: NULL\n");

      if (strlen(name) >= MAX_QPATH)
        Sys_ErrorEx("Sound name too long: %s", name);

      auto sfx_iterator = known_sfx.find(name);
      if (sfx_iterator != known_sfx.end())
      {
        if (pfInCache)
        {
          *pfInCache = sfx_iterator->second.cache.data != nullptr ? 1 : 0;
        }

        if (sfx_iterator->second.servercount > 0)
          sfx_iterator->second.servercount = *gAudEngine.cl_servercount;

        return &(sfx_iterator->second);
      }
      else
      {
        for (auto& sfxElement : known_sfx)
        {
          if (sfxElement.second.servercount > 0 && sfxElement.second.servercount != *gAudEngine.cl_servercount)
          {
            S_FreeCache(&(sfxElement.second));
            known_sfx.erase(sfxElement.first);
            break;
          }
        }
      }

      if (!sfx)
      {
          if (known_sfx.size() >= 768)
          {
              //TODO: fix hardcoded audio table in CL_LookupSound, etc
              g_pEngfuncs->Con_Printf("Exceeded our max number of sound effects!! (768)\n");
              return nullptr;
          }

        auto& result = known_sfx.emplace(name, sfx_t());
        sfx = &(result.first->second);
      }
      else
      {
        //free OpenAL buffer and cache
        S_FreeCache(sfx);
      }

      sfx->is_vox_file = strstr(name, ".lwv"); //nightfire uses this
      sfx->gbx_sample_data = nullptr;
      sfx->gbx_sound_handle = -1;

      strncpy_s(sfx->name, name, sizeof(sfx->name) - 1);
      sfx->name[sizeof(sfx->name) - 1] = 0;

      if (pfInCache)
        *pfInCache = 0;

      sfx->servercount = *gAudEngine.cl_servercount;
      return sfx;
    }
    catch (const std::exception& e)
    {
      MessageBox(NULL, e.what(), "Error on S_FindName", MB_ICONERROR);
      exit(0);
    }
  }

  void AudioEngine::S_CheckWavEnd(aud_channel_t* ch, aud_sfxcache_t* sc)
  {
    if (ch->voicecache)
    {
      return;
    }

    if (!sc)
      return;

    qboolean fWaveEnd = false;

    if (!ch->sound_source->IsPlaying())
    {
      fWaveEnd = true;
    }
    else if (ch->entchannel != CHAN_STREAM)
    {
      uint64_t iSamplesPlayed = ch->sound_source->GetSampleOffset();

      if (!sc->looping && iSamplesPlayed >= ch->end)
      {
        fWaveEnd = true;
        ch->sound_source->Stop();
      }
    }

    if (!fWaveEnd)
      return;

    if (ch->words.size() > 0)
    {
      sfx_t* sfx = nullptr;

      auto& currentWord = ch->words.front();
      if (currentWord.sfx && !currentWord.fKeepCached)
        S_FreeCache(currentWord.sfx);

      ch->words.pop();

      if (ch->words.size() > 0)
      {
        ch->sfx = sfx = ch->words.front().sfx;

        if (sfx)
        {
          sc = m_loader->S_LoadSound(sfx, ch);

          if (sc)
          {
            ch->start = 0;
            ch->end = sc->length;

            vox->TrimStartEndTimes(ch, sc);
            if (ch->entchannel == CHAN_STREAM)
            {
              ch->sound_source = SoundSourceFactory::GetStreamingSource(sc->decoder, al_context->createSource(), 16348, 4);
            }
            else
            {
              ch->sound_source = SoundSourceFactory::GetStaticSource(sc->buffer, al_context->createSource());
            }

            ConfigureSource(ch, sc);
            ch->sound_source->Play();

            return;
          }
        }
      }
    }
  }

  void AudioEngine::SND_Spatialize(aud_channel_t* ch, qboolean init)
  {
    ch->firstpass = init;
    if (!ch->sfx)
      return;

    // invalid source
    if (!ch->sound_source)
      return;

    //apply effect
    qboolean underwater = (*gAudEngine.cl_waterlevel > 2) ? true : false;
    al_efx->ApplyEffect(ch, underwater);

    //for later usage
    aud_sfxcache_t* sc = static_cast<aud_sfxcache_t*>(ch->sfx->cache.data);

    //move mouth
    if (ch->entnum > 0 && (ch->entchannel == CHAN_VOICE || ch->entchannel == CHAN_STREAM))
    {
      if (sc && sc->channels == alure::ChannelConfig::Mono && !sc->data.empty())
      {
        vox->MoveMouth(ch, sc);
      }
    }

    //update position
    alure::Vector3 alure_position(0, 0, 0);
    if (ch->entnum != *gAudEngine.cl_viewentity)
    {
      ch->sound_source->SetRelative(false);
      if (ch->entnum > 0 && ch->entnum < *gAudEngine.cl_num_entities)
      {
        auto sent = g_pEngfuncs->GetEntityByIndex(ch->entnum);

        if (sent && sent->model && sent->curstate.messagenum == *gAudEngine.cl_parsecount)
        {
          VectorCopy(sent->origin, ch->origin);

          if (sent->model->type == mod_brush)
          {
            // Mobile brushes (such as trains and platforms) have the correct origin set,
            // but most other bushes do not. How to correctly detect them?
            if (sent->baseline.origin[0] == 0.0f || sent->baseline.origin[1] == 0.0f || sent->baseline.origin[2] == 0.0f)
            {
              ch->origin[0] = (sent->curstate.mins[0] + sent->curstate.maxs[0]) * 0.5f + sent->curstate.origin[0];
              ch->origin[1] = (sent->curstate.mins[1] + sent->curstate.maxs[1]) * 0.5f + sent->curstate.origin[1];
              ch->origin[2] = (sent->curstate.mins[2] + sent->curstate.maxs[2]) * 0.5f + sent->curstate.origin[2];
            }
          }

          float ratio = 0;
          if ((*gAudEngine.cl_time) != (*gAudEngine.cl_oldtime))
          {
            ratio = static_cast<float>(1 / ((*gAudEngine.cl_time) - (*gAudEngine.cl_oldtime)));
          }

          vec3_t sent_velocity = { (sent->curstate.origin[0] - sent->prevstate.origin[0]) * ratio,
            (sent->curstate.origin[1] - sent->prevstate.origin[1]) * ratio,
            (sent->curstate.origin[2] - sent->prevstate.origin[2]) * ratio };

          ch->sound_source->SetVelocity(AL_UnpackVector(sent_velocity));
          ch->sound_source->SetRadius(sent->model->radius * AL_UnitToMeters);
        }
      }
      else
      {
        // It seems that not only sounds from the view entity can be source relative...
        if (ch->origin[0] == 0.0f && ch->origin[1] == 0.0f && ch->origin[2] == 0.0f)
        {
          ch->sound_source->SetRelative(true);
        }
      }
      alure_position = AL_UnpackVector(ch->origin);
    }
    else
    {
      ch->sound_source->SetRelative(true);
    }
    ch->sound_source->SetPosition(alure_position);

    float fvol = 1.0f;
    float fpitch = 1.0f;

    vox->SetChanVolPitch(ch, &fvol, &fpitch);

    if (sc && sc->length != 0x40000000)
    {
        fvol /= 1.0f; //DYLAN FIXME: WHATS THIS //(*gAudEngine.g_SND_VoiceOverdrive);
    }

    ch->sound_source->SetGain(ch->volume * fvol);
    ch->sound_source->SetPitch(ch->pitch * fpitch);

    if (!init)
    {
      S_CheckWavEnd(ch, sc);
    }
  }

  void AudioEngine::S_Update(float* origin, float* forward, float* right, float* up)
  {
    try
    {
      // call nightfire's original S_Update...
      // required for music to work. music still uses gearbox software's audio engine
      gAudEngine.S_Update(origin, forward, right, up);

      vec_t orientation[6];

      // Update Alure's OpenAL context at the start of processing.
      al_context->update();
      if (sa_meshloader)
      {
        sa_meshloader->update();
      }
      channel_manager->ClearFinished();
      channel_manager->ClearLoopingRemovedEntities();

      // Print buffer and clear it.
      if (dprint_buffer.length())
      {
        g_pEngfuncs->Con_DPrintf(const_cast<char*>((dprint_buffer.c_str())));
        dprint_buffer.clear();
      }

      AL_CopyVector(forward, orientation);
      AL_CopyVector(up, orientation + 3);

      alure::Listener al_listener = al_context->getListener();
      if (openal_mute || g_bMetaAudioPaused)
      {
        al_listener.setGain(0.0f);
      }
      else
      {
        if (volume)
          al_listener.setGain(std::clamp(volume->getValueFloat(), 0.0f, 1.0f));
        else
          al_listener.setGain(1.0f);
      }

      float test = al_doppler->getValueFloat();

      al_context->setDopplerFactor(std::clamp(al_doppler->getValueFloat(), 0.0f, 10.0f));

      std::pair<alure::Vector3, alure::Vector3> alure_orientation(
        alure::Vector3(orientation[0], orientation[1], orientation[2]),
        alure::Vector3(orientation[3], orientation[4], orientation[5])
        );

      // Force unit vector if all zeros (Rapture3D workaround).
      if (orientation[0] == 0.0f && orientation[1] == 0.0f && orientation[2] == 0.0f)
      {
        alure_orientation.first[0] = 1;
      }
      if (orientation[3] == 0.0f && orientation[4] == 0.0f && orientation[5] == 0.0f)
      {
        alure_orientation.second[0] = 1;
      }

      al_efx->SetListenerOrientation(alure_orientation);

      cl_entity_t* pent = g_pEngfuncs->GetEntityByIndex(*gAudEngine.cl_viewentity);
      if (pent != nullptr)
      {
        float ratio = 0;
        if ((*gAudEngine.cl_time) != (*gAudEngine.cl_oldtime))
        {
          ratio = static_cast<float>(1 / ((*gAudEngine.cl_time) - (*gAudEngine.cl_oldtime)));
        }

        vec3_t view_velocity = { (pent->curstate.origin[0] - pent->prevstate.origin[0]) * ratio,
          (pent->curstate.origin[1] - pent->prevstate.origin[1]) * ratio,
          (pent->curstate.origin[2] - pent->prevstate.origin[2]) * ratio };

        al_listener.setVelocity(AL_UnpackVector(view_velocity));
      }
      al_listener.setPosition(AL_UnpackVector(origin));
      al_listener.setOrientation(alure_orientation);

      int roomtype = 0;
      bool underwater = (*gAudEngine.cl_waterlevel > 2) ? true : false;
      if (sxroomwater_type && sxroom_type)
      {
        roomtype = underwater ? (int)sxroomwater_type->getValue() : (int)sxroom_type->getValue();
      }
      al_efx->InterplEffect(roomtype);

      channel_manager->ForEachChannel([&](aud_channel_t& channel) { SND_Spatialize(&channel, false); });

      if (snd_show && snd_show->getValue())
      {
        std::string output;
        size_t total = 0;
        channel_manager->ForEachChannel([&](aud_channel_t& channel)
          {
            if (channel.sfx && channel.volume > 0)
            {
              output.append(std::to_string(static_cast<int>(channel.volume * 255.0f)) + " " + channel.sfx->name + "\n");
              ++total;
            }
          });

        if (!output.empty())
        {
          output.append("----(" + std::to_string(total) + ")----\n");
          g_pEngfuncs->Con_Printf(const_cast<char*>(output.c_str()));
        }
      }
    }
    catch (const std::exception& e)
    {
      MessageBox(NULL, e.what(), "Error on S_Update", MB_ICONERROR);
      exit(0);
    }
  }

  void AudioEngine::ConfigureSource(aud_channel_t* channel, aud_sfxcache_t* audioData)
  {
    channel->sound_source->SetOffset(channel->start);
    channel->sound_source->SetPitch(channel->pitch);
    channel->sound_source->SetRolloffFactors(channel->attenuation, channel->attenuation);
    channel->sound_source->SetDistanceRange(0.0f, 1000.0f * AL_UnitToMeters);
    channel->sound_source->SetAirAbsorptionFactor(1.0f);

    // Should also set source priority
    if (audioData)
    {
      channel->sound_source->SetLooping(audioData->looping);
    }

    SND_Spatialize(channel, true);
  }

  float get_fixed_pitch_for_sound(sfx_t* sfx, int pitch)
  {
      // temporary hack to fix broken footstep sounds
      if (strstr(sfx->name, "player/pl_"))
      {
          const char* file = sfx->name + 10;
          if (strstr(file, "step")
              || strstr(file, "dirt")
              || strstr(file, "metal")
              || strstr(file, "carpet")
              || strstr(file, "snow")
              || strstr(file, "tile")
              || strstr(file, "grate")
              || strstr(file, "sand")
              || strstr(file, "duct")
              || strstr(file, "ladder")
              || strstr(file, "wade")
              || strstr(file, "slosh")
              || strstr(file, "girttyconcrete"))
          {
              return g_pEngfuncs->RandomLong(90, 110);
          }              
      }
      else if (strstr(sfx->name, "weapons/ric"))
      {
          return g_pEngfuncs->RandomLong(90, 110);
      }

      return pitch;
  }

  void AudioEngine::S_StartSound(int entnum, int entchannel, sfx_t* sfx, float* origin, float fvol, float attenuation, int flags, int pitch, bool is_static)
  {
    std::string _function_name;
    if (is_static)
    {
      _function_name = "S_StartStaticSound";
    }
    else
    {
      _function_name = "S_StartDynamicSound";
    }

    aud_channel_t* ch{ nullptr };
    aud_sfxcache_t* sc{ nullptr };
    float fpitch;

    if (!sfx)
    {
      return;
    }

    if (nosound && nosound->getValue())
    {
      return;
    }

    if (sfx->name[0] == '*')
      entchannel = CHAN_STREAM;

    if (entchannel == CHAN_STREAM && pitch != 100)
    {
      g_pEngfuncs->Con_DPrintf("Warning: pitch shift ignored on stream sound %s\n", sfx->name);
      pitch = 100;
    }

    if (pitch != PITCH_NORM)
    {
      g_pEngfuncs->Con_Printf("%s has pitch %i\n", sfx->name, pitch);
    }

    if (fvol > 1.0f)
    {
      g_pEngfuncs->Con_DPrintf("%s: %s fvolume > 1.0", _function_name, sfx->name);
    }

    fpitch = get_fixed_pitch_for_sound(sfx, pitch) / 100.0f;

    if (flags & (SND_STOP | SND_CHANGE_VOL | SND_CHANGE_PITCH))
    {
        //gbx does this for some reason?
        //attenuation /= 1000.0f;

      if (channel_manager->S_AlterChannel(entnum, entchannel, sfx, fvol, fpitch, flags))
        return;

      if (flags & SND_STOP)
        return;
    }

    if (pitch == 0)
    {
      g_pEngfuncs->Con_DPrintf("Warning: %s Ignored, called with pitch 0", _function_name);
      return;
    }

    if (is_static)
    {
      ch = channel_manager->SND_PickStaticChannel(entnum, entchannel, sfx);
    }
    else
    {
      ch = channel_manager->SND_PickDynamicChannel(entnum, entchannel, sfx);
    }

    if (!ch)
    {
      return;
    }

    VectorCopy(origin, ch->origin);
    ch->attenuation = attenuation;
    ch->volume = fvol;
    ch->entnum = entnum;
    ch->entchannel = entchannel;
    ch->pitch = fpitch;



    if (sfx->name[0] == '!' || sfx->name[0] == '#')
    {
      sc = vox->LoadSound(ch, sfx->name + 1);
    }
    else
    {
      sc = m_loader->S_LoadSound(sfx, ch);
      ch->sfx = sfx;
    }

    if (!sc)
    {
      channel_manager->FreeChannel(ch);
      return;
    }

    ch->start = 0;
    ch->end = sc->length;

    vox->TrimStartEndTimes(ch, sc);

    if (!is_static)
    {
      vox->InitMouth(entnum, entchannel);
    }

    if (ch->entchannel == CHAN_STREAM || (ch->entchannel >= CHAN_NETWORKVOICE_BASE && ch->entchannel <= CHAN_NETWORKVOICE_END))
    {
      if (ch->entchannel >= CHAN_NETWORKVOICE_BASE && ch->entchannel <= CHAN_NETWORKVOICE_END)
      {
        ch->sound_source = SoundSourceFactory::GetStreamingSource(sc->decoder, al_context->createSource(), 1024, 2);
        delete sc; // must be deleted here as voice data does not go to the cache to be deleted later
        sc = nullptr;
      }
      else
      {
        ch->sound_source = SoundSourceFactory::GetStreamingSource(sc->decoder, al_context->createSource(), 4096, 4);
      }
    }
    else
    {
      if (al_xfi_workaround->getValueFloat() == 2.0f || sc->force_streaming)
      {
        ch->sound_source = SoundSourceFactory::GetStreamingSource(al_context->createDecoder(sc->buffer.getName()), al_context->createSource(), 16384, 4);
      }
      else
      {
        ch->sound_source = SoundSourceFactory::GetStaticSource(sc->buffer, al_context->createSource());
      }
    }

    try
    {
      ConfigureSource(ch, sc);

      ch->sound_source->Play();
    }
    catch (const std::runtime_error& error)
    {
      dprint_buffer.append(_function_name).append(": ").append(error.what()).append("\n");
    }
  }

  void AudioEngine::S_StartDynamicSound(int entnum, int entchannel, sfx_t* sfx, float* origin, float fvol, float attenuation, int flags, int pitch)
  {
    try
    {
      S_StartSound(entnum, entchannel, sfx, origin, fvol, attenuation, flags, pitch, false);
    }
    catch (const std::exception& e)
    {
      MessageBox(NULL, e.what(), "Error on S_StartDynamicSound", MB_ICONERROR);
      exit(0);
    }
  }

  void AudioEngine::S_StartStaticSound(int entnum, int entchannel, sfx_t* sfx, float* origin, float fvol, float attenuation, int flags, int pitch)
  {
    try
    {
      S_StartSound(entnum, entchannel, sfx, origin, fvol, attenuation, flags, pitch, true);
    }
    catch (const std::exception& e)
    {
      MessageBox(NULL, e.what(), "Error on S_StartStaticSound", MB_ICONERROR);
      exit(0);
    }
  }

  void AudioEngine::S_StopSound(int entnum, int entchannel)
  {
    try
    {
      channel_manager->ClearEntityChannels(entnum, entchannel);
    }
    catch (const std::exception& e)
    {
      MessageBox(NULL, e.what(), "Error on S_StopSound", MB_ICONERROR);
      exit(0);
    }
  }

  void AudioEngine::S_StopAllSounds(qboolean clear)
  {
    try
    {
      if (channel_manager != nullptr)
      {
        channel_manager->ClearAllChannels();
      }
    }
    catch (const std::exception& e)
    {
      MessageBox(NULL, e.what(), "Error on S_StopAllSounds", MB_ICONERROR);
      exit(0);
    }
  }

  bool AudioEngine::OpenAL_Init()
  {
    try
    {
      alure::FileIOFactory::set(alure::MakeUnique<GoldSrcFileFactory>());

      al_dev_manager = alure::DeviceManager::getInstance();

      char* _al_set_device;
      int device_set = g_pEngfuncs->CheckParm("-al_device", &_al_set_device);

      if (_al_set_device != nullptr)
        al_device = alure::MakeAuto(al_dev_manager.openPlayback(_al_set_device, std::nothrow));

      if (!al_device)
      {
        auto default_device = al_dev_manager.defaultDeviceName(alure::DefaultDeviceType::Full);
        al_device = alure::MakeAuto(al_dev_manager.openPlayback(default_device));
      }

      strncpy_s(al_device_name, al_device->getName().c_str(), sizeof(al_device_name));

      al_context = alure::MakeAuto(al_device->createContext());

      alure::Version ver = al_device->getALCVersion();
      al_device_majorversion = ver.getMajor();
      al_device_minorversion = ver.getMinor();

      alure::Context::MakeCurrent(al_context->getHandle());
      al_context->setDistanceModel(alure::DistanceModel::Linear);
      return true;
    }
    catch (const std::exception& e)
    {
      const size_t size = 4096;
      char ar[size] = "Unable to load. Reason:\n";
      int zero_index = 0;
      for (int i = 0; i < size; ++i)
      {
        if (ar[i] == 0)
        {
          zero_index = i;
          break;
        }
      }

      for (int i = 0; i < size - zero_index; ++i)
      {
        if (e.what()[i] == 0 || i == size - zero_index - 1)
        {
          ar[i + zero_index] = '\0';
          break;
        }

        ar[i + zero_index] = e.what()[i];
      }
      MessageBox(NULL, ar, "OpenAL plugin error", MB_ICONERROR);
      return false;
    }
  }

  void AudioEngine::S_Startup()
  {
    //gAudEngine.S_Startup();

    //stop mute me first
    openal_mute = false;

    if (!openal_started)
    {
      if (OpenAL_Init())
      {
        openal_started = true;
      }
    }
  }

  void AudioEngine::AL_Version()
  {
    if (openal_started)
      g_pEngfuncs->Con_Printf("%s\n OpenAL Device: %s\n OpenAL Version: %d.%d\n", META_AUDIO_VERSION, al_device_name, al_device_majorversion, al_device_minorversion);
    else
      g_pEngfuncs->Con_Printf("%s\n Failed to initalize OpenAL device.\n", META_AUDIO_VERSION, al_device_name, al_device_majorversion, al_device_minorversion);
  }

  void AudioEngine::AL_ResetEFX()
  {
    al_efx.reset();
    al_efx = alure::MakeUnique<EnvEffects>(*al_context, al_device->getMaxAuxiliarySends(), GetOccluder());
  }

  void AudioEngine::AL_Devices(bool basic)
  {
    alure::Vector<alure::String> devices;
    if (basic)
    {
      devices = al_dev_manager.enumerate(alure::DeviceEnumeration::Basic);
    }
    else
    {
      devices = al_dev_manager.enumerate(alure::DeviceEnumeration::Full);
    }
    g_pEngfuncs->Con_Printf("Available OpenAL devices:\n");
    for (const alure::String& device : devices)
    {
      g_pEngfuncs->Con_Printf("  %s\n", device.c_str());
    }
  }

  static IPLvoid SteamAudioLog(char* message)
  {
    g_pEngfuncs->Con_Printf(const_cast<char *>(("SteamAudio: " + std::string(message)).c_str()));
  }

  void AudioEngine::SteamAudio_Init()
  {
    if (gSteamAudio.iplCleanup != nullptr)
    {
      if (sa_context == nullptr)
      {
        auto error = gSteamAudio.iplCreateContext(SteamAudioLog, nullptr, nullptr, &sa_context);
        if (error)
        {
          throw std::exception("Error creating SA context: " + error);
        }
      }
      sa_simulationSettings.ambisonicsOrder = 1;
      sa_simulationSettings.bakingBatchSize = 1;
      sa_simulationSettings.irDuration = 1;
      sa_simulationSettings.irradianceMinDistance = 0.1f;
      sa_simulationSettings.maxConvolutionSources = 1;
      sa_simulationSettings.numBounces = 2;
      sa_simulationSettings.numDiffuseSamples = 1024;
      sa_simulationSettings.maxNumOcclusionSamples = 512;
      sa_simulationSettings.numRays = 4096;
      sa_simulationSettings.numThreads = std::thread::hardware_concurrency();
      sa_simulationSettings.sceneType = IPLSceneType::IPL_SCENETYPE_PHONON;
      sa_meshloader = std::make_shared<SteamAudioMapMeshLoader>(sa_context, sa_simulationSettings);
    }
  }

  void AudioEngine::SteamAudio_Shutdown()
  {
    if (gSteamAudio.iplCleanup != nullptr)
    {
      sa_meshloader.reset();
      if (sa_context)
      {
        gSteamAudio.iplDestroyContext(&sa_context);
        sa_context = nullptr;
      }
      gSteamAudio.iplCleanup();
      gSteamAudio = SteamAudio();
    }
  }

  extern std::shared_ptr<SoundLoader> m_loader;

  void AudioEngine::S_Init()
  {
    S_Startup();

    g_pEngfuncs->RegisterClient(&fAL_Version);
    g_pEngfuncs->RegisterClient(&fAL_ResetEFX);
    g_pEngfuncs->RegisterClient(&fAL_BasicDevices);
    g_pEngfuncs->RegisterClient(&fAL_FullDevices);
    room_type = g_pEngfuncs->CreateConsoleVariableClient(roomtype);
    room_off = g_pEngfuncs->CreateConsoleVariableClient(roomoff);
    waterroom_type = g_pEngfuncs->CreateConsoleVariableClient(waterroomtype);
    al_doppler = g_pEngfuncs->CreateConsoleVariableClient(doppler);
    al_xfi_workaround = g_pEngfuncs->CreateConsoleVariableClient(xfi_workaround);
    
    if (gSteamAudio.iplCleanup != nullptr)
    {
      al_occluder = g_pEngfuncs->CreateConsoleVariableClient(occluder);
    }

    gAudEngine.S_Init();

    if (!g_pEngfuncs->CheckParm("-nosound", NULL))
    {
      nosound = g_pEngfuncs->GetConsoleVariableClient("nosound");
      volume = g_pEngfuncs->GetConsoleVariableClient("snd_mastervolume");
      sxroomwater_type = g_pEngfuncs->GetConsoleVariableClient("waterroom_type");
      sxroom_type = g_pEngfuncs->GetConsoleVariableClient("room_type");
      snd_show = g_pEngfuncs->CreateConsoleVariableClient(sndshow);

      S_StopAllSounds(true);
    }

    SteamAudio_Init();
    AL_ResetEFX();

    channel_manager = alure::MakeUnique<ChannelManager>();
    vox = alure::MakeUnique<VoxManager>(this, m_loader);

#if 0
    int len;
    byte* file = g_pEngfuncs->COM_LoadHeapFile("sound/weapons/taser_fire.wav", &len);
    CGBWav gbwav;
    gbwav.getWavInfo("sound/weapons/taser_fire.wav", file, len);
    g_pEngfuncs->COM_FreeFile(file);

    file = g_pEngfuncs->COM_LoadHeapFile("sound/ambience/bg_crickets.wav", &len);
    CGBWav gbwav2;
    gbwav2.getWavInfo("sound/ambience/bg_crickets.wav", file, len);
    g_pEngfuncs->COM_FreeFile(file);

    file = g_pEngfuncs->COM_LoadHeapFile("sound/bond/Bond_ Escape14.wav", &len);
    CGBWav gbwav3;
    gbwav3.getWavInfo("sound/bond/Bond_ Escape14.wav", file, len);
    g_pEngfuncs->COM_FreeFile(file);

    file = g_pEngfuncs->COM_LoadHeapFile("sound/castle_guard/CG_alerted2.lwv", &len);
    CGBWav gbwav4;
    gbwav4.getWavInfo("sound/castle_guard/CG_alerted2.lwv", file, len);
    g_pEngfuncs->COM_FreeFile(file);

    file = g_pEngfuncs->COM_LoadHeapFile("sound/weapons/test_stereo.wav", &len);
    CGBWav gbwav5;
    gbwav5.getWavInfo("sound/weapons/test_stereo.wav", file, len);
    g_pEngfuncs->COM_FreeFile(file);

    auto test = S_FindName("sound/ambience/bg_crickets.wav", 0);
    m_loader->S_LoadSound(test, 0);
#endif

    auto test = S_FindName("sound/gadgets/taser_shocking.wav", 0);
    m_loader->S_LoadSound(test, 0);
  }

  std::shared_ptr<IOcclusionCalculator> AudioEngine::GetOccluder()
  {
    if (!al_occluder || al_occluder->getValueBool() == false)
    {
      return std::make_shared<GoldSrcOcclusionCalculator>(*g_pEngfuncs->pEventAPI);
    }
    else
    {
      return std::make_shared<SteamAudioOcclusionCalculator>(sa_meshloader, *g_pEngfuncs->pEventAPI);
    }
  }

  AudioEngine::~AudioEngine()
  {
    S_StopAllSounds(true);
    S_FlushCaches();
    al_efx.reset();
    channel_manager.reset();
    vox.reset();

    alure::FileIOFactory::set(nullptr);
    alure::Context::MakeCurrent(nullptr);

    openal_started = false;

    SteamAudio_Shutdown();
  }

  void AudioEngine::S_Shutdown()
  {
    //shall we mute OpenAL sound when S_Shutdown?
    openal_mute = true;

    gAudEngine.S_Shutdown();

    // unregister cvars
    // this crashes the game..
    // engine seems to already be unloaded before we call this
#if 0
    if (room_type)
        g_pEngfuncs->DestroyConsoleVariableClient(&roomtype);
    if (room_off)
        g_pEngfuncs->DestroyConsoleVariableClient(&roomoff);
    if (waterroom_type)
        g_pEngfuncs->DestroyConsoleVariableClient(&waterroomtype);
    if (al_doppler)
        g_pEngfuncs->DestroyConsoleVariableClient(&doppler);
    if (al_xfi_workaround)
        g_pEngfuncs->DestroyConsoleVariableClient(&xfi_workaround);
    if (al_occluder)
        g_pEngfuncs->DestroyConsoleVariableClient(&occluder);
    if (al_occlusion)
        g_pEngfuncs->DestroyConsoleVariableClient(&occlusion);
    if (al_occlusion_fade)
        g_pEngfuncs->DestroyConsoleVariableClient(&occlusion_fade);
    if (snd_show)
        g_pEngfuncs->DestroyConsoleVariableClient(&sndshow);

    // unregister console funcs
    g_pEngfuncs->UnregisterClient(&fAL_Version);
    g_pEngfuncs->UnregisterClient(&fAL_ResetEFX);
    g_pEngfuncs->UnregisterClient(&fAL_BasicDevices);
    g_pEngfuncs->UnregisterClient(&fAL_FullDevices);
#endif
  }

  // Nightfire
  sfx_t* AudioEngine::CL_LookupSound(const char* name)
  {
      sfx_t* og = gAudEngine.CL_LookupSound(name);
      if (og)
          return og;

      auto sfx_iterator = known_sfx.find(name);
      if (sfx_iterator != known_sfx.end())
      {
          return &(sfx_iterator->second);
      }
      return nullptr;
  }

  // Nightfire
  int AudioEngine::CL_Parse_RoomType()
  {
      // restores room_type console variable that gearbox removed
      int type = gAudEngine.CL_Parse_RoomType();
      if (room_type)
          room_type->setValueInt(type);
      return type;
  }
}