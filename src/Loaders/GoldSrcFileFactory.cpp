#include <fstream>

#include "Loaders/GoldSrcFileFactory.hpp"
#include "Loaders/GoldSrcFileStream.hpp"
#include "FileSystem.h"

namespace MetaAudio
{
  alure::UniquePtr<std::istream> GoldSrcFileFactory::openFile(const alure::String& name) noexcept
  {
    alure::String namebuffer = "sound";

    if (name[0] != '/')
    {
      namebuffer.append("/");
    }

    namebuffer.append(name);

    int fileSize = 0;
    auto fileExists = g_pNightfireFileSystem->COM_FileExists(namebuffer.c_str(), &fileSize);//g_pFileSystem->FileExists(namebuffer.c_str());
    if (!fileExists)
    {
      namebuffer.clear();
      if (name[0] != '/')
      {
        namebuffer.append("/");
      }
      namebuffer.append(name);

      fileExists = g_pNightfireFileSystem->COM_FileExists(namebuffer.c_str(), &fileSize);
    }

    alure::UniquePtr<std::istream> file;
    if (fileExists)
    {
      char final_file_path[260]; // MAX_PATH
      //DYLAN FIXME
      if (!g_pNightfireFileSystem->COM_ExpandFilename(final_file_path, sizeof(final_file_path)))
          memcpy(final_file_path, namebuffer.c_str(), namebuffer.length() + 1);

        file = alure::MakeUnique<std::ifstream>(final_file_path, std::ios::binary);
        if (file->fail())
        {
            file = alure::MakeUnique<GoldSrcFileStream>(namebuffer.c_str());
            if (file->fail())
            {
                file = nullptr;
            }
            else
            {
                *file >> std::noskipws;
            }
        }
    }

    return std::move(file);
  }
}