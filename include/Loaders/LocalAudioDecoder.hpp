#pragma once

#include "snd_local.h"

#ifdef WINXP
#include <unordered_map>
#endif

namespace MetaAudio
{
  class LocalAudioDecoder final : public alure::MessageHandler
  {
  public:
    static const alure::Array<alure::String, 5> SupportedExtensions;
    bool GetWavinfo(wavinfo_t& info, alure::String full_path, alure::Vector<ALubyte>& data_output);
    void bufferLoading(alure::StringView name, alure::ChannelConfig channels, alure::SampleType type, ALuint samplerate, alure::ArrayView<ALbyte> data) noexcept override;

  private:
    struct Audio
    {
      wavinfo_t info;
      alure::Vector<ALubyte> data;
    };

    std::unordered_map<alure::String, Audio> m_data;
  };
}