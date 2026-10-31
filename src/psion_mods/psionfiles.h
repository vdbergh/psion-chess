#ifndef PSION_PSIONFILES_H
#define PSION_PSIONFILES_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

// Verification and installation of Psion's files. Like chess.cpp it does
// not depend on the emulator (see tests/).

namespace psion {

    class Sha256 {
      public:
        Sha256();
        void Update(const unsigned char *data, size_t len);
        // The digest in lower case hex. Call once, after the last Update.
        std::string HexDigest();

      private:
        void Block(const unsigned char *p);
        uint32_t h[8];
        unsigned char buf[64];
        size_t buf_len;
        uint64_t total;
    };

    // The directory with Psion's files: $PSION_DIR, or "data" next to
    // the executable.
    std::string PsionDataDir();

    // Where psion -d downloads Psion's files from, and the SHA-256 of that
    // zip file.
    extern const char *PSION_ZIP_URL;
    extern const char *PSION_ZIP_SHA256;

    std::string Sha256Hex(const std::vector<unsigned char> &data);

    struct FileHash {
        const char *name;
        const char *sha256;
    };

    // Psion's files from psion213.zip.
    extern const FileHash PSION_FILES[];
    extern const size_t PSION_FILE_COUNT;

    // Whether dir contains each of the n files with the given SHA-256.
    // Names are matched regardless of case, as DOSBox does; two names
    // that differ only in case are an error, since it is then unclear
    // which one DOSBox would use. If not, error lists the problems, one
    // per line.
    bool CheckFiles(const std::string &dir, const FileHash *files, size_t n,
                    std::string &error);

    // Decompresses raw deflate data (RFC 1951) and appends it to out.
    bool Inflate(const unsigned char *in, size_t len,
                 std::vector<unsigned char> &out);

    // Extracts the file name (exact case) from a zip file in memory. Only
    // the methods "stored" and "deflated" are supported; the size and
    // CRC-32 are checked.
    bool ZipExtract(const std::vector<unsigned char> &zip, const char *name,
                    std::vector<unsigned char> &data, std::string &error);

    // The names in dir that equal name regardless of case.
    std::vector<std::string> FindNoCase(const std::string &dir,
                                        const char *name);

    // psion -d (in download.cpp): shows the copyright status, asks for
    // confirmation, then downloads psion213.zip with curl and installs
    // Psion's files in PsionDataDir(). Returns the exit code.
    int DownloadPsionFiles();

} // namespace psion

#endif
