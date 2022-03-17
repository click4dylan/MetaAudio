#include "Loaders/GoldSrcFileBuf.hpp"
#include "FileSystem.h"

namespace MetaAudio
{
  GoldSrcFileBuf::int_type GoldSrcFileBuf::underflow()
  {
    if (mFile.handle_index != -1 && gptr() == egptr())
    {
      auto got = g_pNightfireFileSystem->COM_ReadFile(mFile, mBuffer.data(), mBuffer.size());
      if (got)
      {
        setg(mBuffer.data(), mBuffer.data(), mBuffer.data() + got);
      }
    }

    if (gptr() == egptr())
    {
      return traits_type::eof();
    }
    return traits_type::to_int_type(*gptr());
  }

  GoldSrcFileBuf::pos_type GoldSrcFileBuf::seekoff(off_type offset, std::ios_base::seekdir whence, std::ios_base::openmode mode)
  {
    if (mFile.handle_index == -1 || (mode & std::ios_base::out) || !(mode & std::ios_base::in))
    {
      return traits_type::eof();
    }

    if (offset < 0)
    {
      return traits_type::eof();
    }

    auto seekType = FILESYSTEM_SEEK_HEAD;
    switch (whence)
    {
    case std::ios_base::beg:
      break;

    case std::ios_base::cur:
      seekType = FILESYSTEM_SEEK_CURRENT;
      if ((offset >= 0 && offset < off_type(egptr() - gptr())) ||
        (offset < 0 && -offset <= off_type(gptr() - eback())))
      {
        auto initialPos = g_pNightfireFileSystem->COM_FileTell(mFile);
        g_pNightfireFileSystem->COM_FileSeek(mFile, static_cast<int>(offset), (GbxFileSeekBehavior)seekType);
        auto newPos = g_pNightfireFileSystem->COM_FileTell(mFile);
        if (newPos - initialPos != offset)
        {
          return traits_type::eof();
        }
        setg(eback(), gptr() + offset, egptr());
        return newPos - off_type(egptr() - gptr());
      }
      offset -= off_type(egptr() - gptr());
      break;

    case std::ios_base::end:
      offset += mFileLength;
      break;

    default:
      return traits_type::eof();
    }

    g_pNightfireFileSystem->COM_FileSeek(mFile, static_cast<int>(offset), (GbxFileSeekBehavior)seekType);

    auto curPosition = g_pNightfireFileSystem->COM_FileTell(mFile);

    setg(nullptr, nullptr, nullptr);
    return curPosition;
  }

  GoldSrcFileBuf::pos_type GoldSrcFileBuf::seekpos(pos_type pos, std::ios_base::openmode mode)
  {
    if (mFile.handle_index == -1 || (mode & std::ios_base::out) || !(mode & std::ios_base::in))
    {
      return traits_type::eof();
    }

    g_pNightfireFileSystem->COM_FileSeek(mFile, static_cast<int>(pos), GbxFileSeekBehavior::SEEK_FROM_START);

    if (g_pNightfireFileSystem->COM_EndOfFile(mFile))
    {
      return traits_type::eof();
    }

    auto curPosition = g_pNightfireFileSystem->COM_FileTell(mFile);

    setg(nullptr, nullptr, nullptr);
    return curPosition;
  }

  bool GoldSrcFileBuf::open(const char* filename) noexcept
  {
    mFileLength = g_pNightfireFileSystem->COM_OpenFile(filename, &mFile);
    return mFile.handle_index != -1;
  }

  GoldSrcFileBuf::~GoldSrcFileBuf()
  {
    g_pNightfireFileSystem->COM_CloseFile(mFile);
    mFile = HCOMFILE();
  }
}