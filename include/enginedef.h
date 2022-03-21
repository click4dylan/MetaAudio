#pragma once
#include "alure2.h"

class CGBWav
{
public:
	int m_samplerate;
	int m_channels;
	int m_channels2; //same as m_channels
	int m_unknown; //-1
	int m_datasize;
	int m_headersize;

	
	static unsigned char* m_pData;
	static unsigned char* m_iffEnd;
	static unsigned char* m_lastChunk;
	static unsigned char m_iffData;
	static int m_iffChunkLen;
	static void findNextChunk(const char* tag)
	{
		for (;;)
		{
			m_pData = m_lastChunk;
			if (m_lastChunk >= m_iffEnd)
			{
				m_pData = nullptr;
				return;
			}
			m_pData = m_lastChunk + 4;
			DWORD data1 = m_lastChunk[5];
			DWORD data2 = m_lastChunk[7];
			DWORD data3 = m_lastChunk[4] + (data1 << 8);
			DWORD data4 = m_lastChunk[6] << 16;
			m_pData = m_lastChunk + 8;
			int chunklen = data3 + data4 + (data2 << 24);
			m_iffChunkLen = chunklen;
			if (chunklen < 0)
				break;
			m_pData = m_lastChunk;
			m_lastChunk += ((chunklen + 1) & 0xFFFFFFFE) + 8;
			int result = !strncmp((const char*)m_pData, tag, 4);
			if (!result)
				return;
		}
	}
	void getWavInfo(const char* name, unsigned char* heapfile, long size);
};

#pragma pack(push, 4)
struct sfx_t
{
  bool is_vox_file; //file_extension == .lmv
  char name[MAX_QPATH];
  cache_user_t cache;
  int servercount;
  void* gbx_sample_data;
  int gbx_sound_handle;
};
//size: 0x54
#pragma pack(pop)

struct sfxcache_t
{
  int length;
  int loopstart;
  int samplerate;
  int width;
  int stereo;
  byte data[1];  // variable sized
};

struct channel_t
{
  sfx_t *sfx;       // sfx number
  int leftvol;      // 0-255 volume
  int rightvol;     // 0-255 volume
  int end;          // end time in global paintsamples
  int pos;          // sample position in sfx
  int looping;      // where to loop, -1 = no looping
  int entnum;       // to allow overriding a specific sound
  int entchannel;   //
  vec3_t origin;    // origin of sound effect
  vec_t dist_mult;  // distance multiplier (attenuation/clipK)
  int master_vol;   // 0-255 master volume
  int isentence;
  int iword;
  int pitch;        // real-time pitch after any modulation or shift by dynamic data
};

struct voxword_t
{
  int volume;             // increase percent, ie: 125 = 125% increase
  int pitch;              // pitch shift up percent
  int start;              // offset start of wave percent
  int end;                // offset end of wave percent
  uint64_t cbtrim;        // end of wave after being trimmed to 'end'
  int fKeepCached;        // 1 if this word was already in cache before sentence referenced it
  uint64_t samplefrac;    // if pitch shifting, this is position into wav * 256
  int timecompress;       // % of wave to skip during playback (causes no pitch shift)
  sfx_t *sfx;             // name and cache pointer
};

struct wavinfo_t
{
  uint64_t samples;
  uint64_t loopstart;
  uint64_t loopend;
  ALuint samplerate;
  alure::SampleType stype;
  alure::ChannelConfig channels;
  bool looping;
};

// a sound with no channel is a local only sound
constexpr auto SND_VOLUME = (1 << 0); //0x1       // a byte
constexpr auto SND_ATTENUATION = (1<<1); //0x2    // a byte
constexpr auto SND_LARGE_INDEX = (1<<2); //0x4   // a long
constexpr auto SND_PITCH = (1<<3); //0x8
constexpr auto SND_SENTENCE = (1<<4); //0x10
constexpr auto SND_STOP = (1<<5); //0x20
constexpr auto SND_CHANGE_VOL = (1<<6); //0x40
constexpr auto SND_CHANGE_PITCH = (1<<7); //0x80
constexpr auto SND_SPAWNING = (1<<8); //0x100

constexpr auto CVOXWORDMAX = 32;
constexpr auto CVOXZEROSCANMAX = 255;
constexpr auto CVOXFILESENTENCEMAX = 1536;
constexpr auto CAVGSAMPLES = 10;
constexpr auto CSXROOM = 29;