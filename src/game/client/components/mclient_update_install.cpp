#include "mclient_update_install.h"

#include <base/fs.h>
#include <base/io.h>
#include <base/log.h>
#include <base/mem.h>
#include <base/str.h>

#include <zlib.h>

#include <cstdlib>
#include <vector>

#if !defined(CONF_FAMILY_WINDOWS)
#include <sys/stat.h>
#endif

namespace
{

	constexpr uint32_t ZIP_LOCAL_HEADER_SIG = 0x04034b50;
	constexpr uint32_t ZIP_CENTRAL_HEADER_SIG = 0x02014b50;
	constexpr uint32_t ZIP_END_OF_CENTRAL_DIR_SIG = 0x06054b50;
	constexpr int ZIP_LOCAL_HEADER_SIZE = 30;
	constexpr int ZIP_CENTRAL_HEADER_SIZE = 46;
	constexpr int ZIP_END_OF_CENTRAL_DIR_SIZE = 22;
	constexpr int ZIP_HOST_UNIX = 3;
	constexpr uint16_t ZIP_METHOD_STORED = 0;
	constexpr uint16_t ZIP_METHOD_DEFLATED = 8;

	struct SZipEntry
	{
		std::string m_Path; // relative to the install directory
		uint16_t m_Method;
		uint32_t m_Crc;
		uint32_t m_CompressedSize;
		uint32_t m_Size;
		uint32_t m_LocalHeaderOffset;
		bool m_Executable;
	};

	struct SStagedFile
	{
		std::string m_Tmp;
		std::string m_Dest;
		bool m_Executable;
	};

	uint16_t ReadU16(const unsigned char *p)
	{
		return (uint16_t)(p[0] | (p[1] << 8));
	}

	uint32_t ReadU32(const unsigned char *p)
	{
		return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
	}

	bool IsSafeRelativePath(const std::string &Path)
	{
		if(Path.empty() || Path[0] == '/' || Path.find('\\') != std::string::npos || Path.find(':') != std::string::npos)
			return false;
		size_t Start = 0;
		while(Start <= Path.size())
		{
			size_t End = Path.find('/', Start);
			if(End == std::string::npos)
				End = Path.size();
			const std::string Part = Path.substr(Start, End - Start);
			if(Part.empty() || Part == "." || Part == "..")
				return false;
			Start = End + 1;
		}
		return true;
	}

	bool EndsWithNoCase(const std::string &Str, const char *pSuffix)
	{
		return str_endswith_nocase(Str.c_str(), pSuffix) != nullptr;
	}

	// files that can be locked while the client runs on Windows and have to be moved away before replacing them
	bool NeedsMoveAside(const std::string &Path)
	{
		return EndsWithNoCase(Path, ".exe") || EndsWithNoCase(Path, ".dll");
	}

	bool LocalFileMatches(const char *pPath, const SZipEntry &Entry)
	{
		IOHANDLE File = io_open(pPath, IOFLAG_READ);
		if(!File)
			return false;
		const int64_t Length = io_length(File);
		if(Length != (int64_t)Entry.m_Size)
		{
			io_close(File);
			return false;
		}
		void *pData = nullptr;
		unsigned DataLength = 0;
		const bool Read = io_read_all(File, &pData, &DataLength);
		io_close(File);
		if(!Read)
			return false;
		const uint32_t Crc = (uint32_t)crc32(0L, (const Bytef *)pData, DataLength);
		free(pData);
		return DataLength == Entry.m_Size && Crc == Entry.m_Crc;
	}

	bool Inflate(const unsigned char *pIn, uint32_t InSize, unsigned char *pOut, uint32_t OutSize)
	{
		z_stream Stream = {};
		if(inflateInit2(&Stream, -MAX_WBITS) != Z_OK)
			return false;
		Stream.next_in = (Bytef *)pIn;
		Stream.avail_in = InSize;
		Stream.next_out = pOut;
		Stream.avail_out = OutSize;
		const int Result = inflate(&Stream, Z_FINISH);
		const bool Success = Result == Z_STREAM_END && Stream.total_out == OutSize;
		inflateEnd(&Stream);
		return Success;
	}

#if !defined(CONF_FAMILY_WINDOWS)
	bool SetExecutable(const char *pPath)
	{
		struct stat FileStats;
		if(stat(pPath, &FileStats) != 0)
			return false;
		return chmod(pPath, FileStats.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) == 0;
	}
#endif

	bool ReadCentralDirectory(const unsigned char *pData, size_t Size, std::vector<SZipEntry> &vEntries)
	{
		if(Size < (size_t)ZIP_END_OF_CENTRAL_DIR_SIZE)
			return false;

		// the end of central directory record sits at the very end, followed by an optional comment of up to 64 KiB
		const size_t SearchStop = Size > ZIP_END_OF_CENTRAL_DIR_SIZE + 0xFFFF ? Size - ZIP_END_OF_CENTRAL_DIR_SIZE - 0xFFFF : 0;
		size_t EndOffset = (size_t)-1;
		for(size_t i = Size - ZIP_END_OF_CENTRAL_DIR_SIZE + 1; i-- > SearchStop;)
		{
			if(ReadU32(pData + i) == ZIP_END_OF_CENTRAL_DIR_SIG)
			{
				EndOffset = i;
				break;
			}
		}
		if(EndOffset == (size_t)-1)
			return false;

		const unsigned char *pEnd = pData + EndOffset;
		const uint16_t NumEntries = ReadU16(pEnd + 10);
		const uint32_t DirSize = ReadU32(pEnd + 12);
		const uint32_t DirOffset = ReadU32(pEnd + 16);
		if(NumEntries == 0xFFFF || DirOffset == 0xFFFFFFFF || (size_t)DirOffset + DirSize > EndOffset)
			return false; // zip64 is not needed for our packages

		std::vector<SZipEntry> vRaw;
		size_t Offset = DirOffset;
		for(int i = 0; i < NumEntries; i++)
		{
			if(Offset + ZIP_CENTRAL_HEADER_SIZE > EndOffset)
				return false;
			const unsigned char *p = pData + Offset;
			if(ReadU32(p) != ZIP_CENTRAL_HEADER_SIG)
				return false;
			const uint8_t Host = p[5];
			const uint16_t Flags = ReadU16(p + 8);
			const uint16_t NameLength = ReadU16(p + 28);
			const uint16_t ExtraLength = ReadU16(p + 30);
			const uint16_t CommentLength = ReadU16(p + 32);
			if(Offset + ZIP_CENTRAL_HEADER_SIZE + NameLength > EndOffset)
				return false;
			if(Flags & 1)
				return false; // encrypted

			SZipEntry Entry;
			Entry.m_Method = ReadU16(p + 10);
			Entry.m_Crc = ReadU32(p + 16);
			Entry.m_CompressedSize = ReadU32(p + 20);
			Entry.m_Size = ReadU32(p + 24);
			Entry.m_LocalHeaderOffset = ReadU32(p + 42);
			const uint32_t ExternalAttributes = ReadU32(p + 38);
			Entry.m_Executable = Host == ZIP_HOST_UNIX && ((ExternalAttributes >> 16) & 0111) != 0;
			Entry.m_Path.assign((const char *)p + ZIP_CENTRAL_HEADER_SIZE, NameLength);
			if(Entry.m_CompressedSize == 0xFFFFFFFF || Entry.m_Size == 0xFFFFFFFF || Entry.m_LocalHeaderOffset == 0xFFFFFFFF)
				return false;
			vRaw.push_back(std::move(Entry));
			Offset += ZIP_CENTRAL_HEADER_SIZE + NameLength + ExtraLength + CommentLength;
		}

		// release packages wrap everything in a single "M-Client-<version>-<platform>/" folder, strip it
		std::string Root;
		if(!vRaw.empty())
		{
			const size_t Slash = vRaw[0].m_Path.find('/');
			if(Slash != std::string::npos)
				Root = vRaw[0].m_Path.substr(0, Slash + 1);
			for(const auto &Entry : vRaw)
			{
				if(Root.empty() || Entry.m_Path.compare(0, Root.size(), Root) != 0)
				{
					Root.clear();
					break;
				}
			}
		}

		for(auto &Entry : vRaw)
		{
			Entry.m_Path.erase(0, Root.size());
			if(Entry.m_Path.empty() || Entry.m_Path.back() == '/')
				continue; // directory
			if(!IsSafeRelativePath(Entry.m_Path))
			{
				log_error("mclient", "Update package contains an invalid path '%s'", Entry.m_Path.c_str());
				return false;
			}
			if(Entry.m_Method != ZIP_METHOD_STORED && Entry.m_Method != ZIP_METHOD_DEFLATED)
			{
				log_error("mclient", "Update package uses an unsupported compression method for '%s'", Entry.m_Path.c_str());
				return false;
			}
			vEntries.push_back(std::move(Entry));
		}
		return true;
	}

} // namespace

CMClientUpdateInstallJob::CMClientUpdateInstallJob(const char *pArchivePath, const char *pInstallDir, const char *pClientExec) :
	m_ArchivePath(pArchivePath), m_InstallDir(pInstallDir), m_ClientExec(pClientExec)
{
}

void CMClientUpdateInstallJob::Run()
{
	m_Success = Install();
	m_Progress = 100;
	if(fs_remove(m_ArchivePath.c_str()) != 0)
		log_error("mclient", "Could not remove the update package '%s'", m_ArchivePath.c_str());
}

bool CMClientUpdateInstallJob::Install()
{
	IOHANDLE Archive = io_open(m_ArchivePath.c_str(), IOFLAG_READ);
	if(!Archive)
	{
		log_error("mclient", "Could not open the update package '%s'", m_ArchivePath.c_str());
		return false;
	}
	void *pRawData = nullptr;
	unsigned Size = 0;
	const bool Read = io_read_all(Archive, &pRawData, &Size);
	io_close(Archive);
	if(!Read)
	{
		log_error("mclient", "Could not read the update package '%s'", m_ArchivePath.c_str());
		return false;
	}
	const unsigned char *pData = (const unsigned char *)pRawData;

	std::vector<SZipEntry> vEntries;
	if(!ReadCentralDirectory(pData, Size, vEntries))
	{
		log_error("mclient", "The update package is not a valid release zip");
		free(pRawData);
		return false;
	}

	bool HasClient = false;
	uint64_t TotalBytes = 0;
	for(const auto &Entry : vEntries)
	{
		HasClient |= Entry.m_Path == m_ClientExec;
		TotalBytes += Entry.m_Size;
	}
	if(!HasClient)
	{
		log_error("mclient", "The update package does not contain '%s'", m_ClientExec.c_str());
		free(pRawData);
		return false;
	}

	// stage: extract every changed file next to the install, nothing is touched yet
	std::vector<SStagedFile> vStaged;
	const auto RemoveStaged = [&]() {
		for(const auto &Staged : vStaged)
			(void)fs_remove(Staged.m_Tmp.c_str());
	};

	bool Success = true;
	uint64_t DoneBytes = 0;
	for(const auto &Entry : vEntries)
	{
		const std::string Dest = m_InstallDir + "/" + Entry.m_Path;
		DoneBytes += Entry.m_Size;
		m_Progress = TotalBytes > 0 ? (int)(DoneBytes * 90 / TotalBytes) : 0;

		// keep a storage.cfg the user may have edited
		if(Entry.m_Path == "storage.cfg" && fs_is_file(Dest.c_str()))
			continue;
		if(LocalFileMatches(Dest.c_str(), Entry))
			continue;

		if((size_t)Entry.m_LocalHeaderOffset + ZIP_LOCAL_HEADER_SIZE > Size || ReadU32(pData + Entry.m_LocalHeaderOffset) != ZIP_LOCAL_HEADER_SIG)
		{
			log_error("mclient", "The update package is corrupt at '%s'", Entry.m_Path.c_str());
			Success = false;
			break;
		}
		const unsigned char *pLocal = pData + Entry.m_LocalHeaderOffset;
		const size_t DataOffset = (size_t)Entry.m_LocalHeaderOffset + ZIP_LOCAL_HEADER_SIZE + ReadU16(pLocal + 26) + ReadU16(pLocal + 28);
		if(DataOffset + Entry.m_CompressedSize > Size)
		{
			log_error("mclient", "The update package is truncated at '%s'", Entry.m_Path.c_str());
			Success = false;
			break;
		}

		std::vector<unsigned char> vFile(Entry.m_Size);
		bool Extracted;
		if(Entry.m_Method == ZIP_METHOD_STORED)
		{
			Extracted = Entry.m_CompressedSize == Entry.m_Size;
			if(Extracted && Entry.m_Size > 0)
				mem_copy(vFile.data(), pData + DataOffset, Entry.m_Size);
		}
		else
		{
			Extracted = Inflate(pData + DataOffset, Entry.m_CompressedSize, vFile.data(), Entry.m_Size);
		}
		if(!Extracted || (uint32_t)crc32(0L, vFile.data(), Entry.m_Size) != Entry.m_Crc)
		{
			log_error("mclient", "Could not extract '%s' from the update package", Entry.m_Path.c_str());
			Success = false;
			break;
		}

		char aTmp[IO_MAX_PATH_LENGTH];
		str_format(aTmp, sizeof(aTmp), "%s/update/mclient-staged-%d.tmp", m_InstallDir.c_str(), (int)vStaged.size());
		IOHANDLE Out = io_open(aTmp, IOFLAG_WRITE);
		if(!Out)
		{
			log_error("mclient", "Could not write '%s', the client directory may not be writable", aTmp);
			Success = false;
			break;
		}
		vStaged.push_back({aTmp, Dest, Entry.m_Executable || Entry.m_Path == m_ClientExec});
		const bool Written = Entry.m_Size == 0 || io_write(Out, vFile.data(), Entry.m_Size) == Entry.m_Size;
		const bool Closed = io_close(Out) == 0;
		if(!Written || !Closed)
		{
			log_error("mclient", "Could not write '%s', the disk may be full", aTmp);
			Success = false;
			break;
		}
	}
	free(pRawData);

	if(!Success)
	{
		RemoveStaged();
		return false;
	}

	// commit: move the staged files into place
	log_info("mclient", "Installing %d changed files", (int)vStaged.size());
	for(size_t i = 0; i < vStaged.size(); i++)
	{
		const SStagedFile &Staged = vStaged[i];
		m_Progress = 90 + (int)(i * 10 / vStaged.size());
		const char *pDest = Staged.m_Dest.c_str();
		if(fs_makedir_rec_for(pDest) != 0)
		{
			log_error("mclient", "Could not create the folder for '%s'", pDest);
			Success = false;
			continue;
		}

#if !defined(CONF_FAMILY_WINDOWS)
		if(Staged.m_Executable && !SetExecutable(Staged.m_Tmp.c_str()))
			log_error("mclient", "Could not make '%s' executable", pDest);
#endif

		const std::string Old = Staged.m_Dest + ".old";
		const bool MoveAside = NeedsMoveAside(Staged.m_Dest) && fs_is_file(pDest);
		if(MoveAside)
		{
			// a running executable or a loaded library can be renamed, but not overwritten
			(void)fs_remove(Old.c_str());
			if(fs_rename(pDest, Old.c_str()) != 0)
			{
				log_error("mclient", "Could not move '%s' out of the way", pDest);
				Success = false;
				continue;
			}
		}
		if(fs_rename(Staged.m_Tmp.c_str(), pDest) != 0)
		{
			log_error("mclient", "Could not install '%s'", pDest);
			if(MoveAside)
				(void)fs_rename(Old.c_str(), pDest);
			Success = false;
		}
	}
	RemoveStaged(); // only leftovers of failed renames are still there
	return Success;
}
