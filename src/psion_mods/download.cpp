#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "psionfiles.h"

namespace psion {

    static const char *NOTICE =
        "Psion Chess (1985) is not included with this adapter, since it is\n"
        "still under copyright. Richard Lang wrote its chess engine and kept\n"
        "its copyright; in October 2026 he gave permission for his part to\n"
        "be freely distributed. The graphics and input/output were written\n"
        "by Psion, and nobody has given permission for that part: Zebra\n"
        "Technologies, Psion's successor, could not confirm in October 2026\n"
        "whether it still holds these rights. Classic DOS Games offers\n"
        "Psion Chess as a free download.\n";

    // The zip file is about 100 KB; more than this is not it.
    static const size_t MAX_ZIP_SIZE = 10000000;

    static bool confirmed() {
        char line[64];
        if (!fgets(line, sizeof(line), stdin)) {
            return false;
        }
        std::string answer;
        for (char *p = line; *p && !isspace((unsigned char)*p); p++) {
            answer += (char)tolower((unsigned char)*p);
        }
        return answer == "y" || answer == "yes";
    }

    // Runs curl and returns what it wrote to stdout.
    static bool download(const std::string &url,
                         std::vector<unsigned char> &zip) {
        std::string cmd = "curl -fsSL --max-filesize 10000000 \"" + url + "\"";
        unsigned char buf[65536];
        size_t n;
        zip.clear();
#ifdef _WIN32
        FILE *p = _popen(cmd.c_str(), "rb");
#else
        FILE *p = popen(cmd.c_str(), "r");
#endif
        if (!p) {
            return false;
        }
        while ((n = fread(buf, 1, sizeof(buf), p)) > 0) {
            if (zip.size() + n > MAX_ZIP_SIZE) {
                break;
            }
            zip.insert(zip.end(), buf, buf + n);
        }
#ifdef _WIN32
        int status = _pclose(p);
#else
        int status = pclose(p);
#endif
        return status == 0 && n == 0 && !zip.empty();
    }

    static bool write_file(const std::string &path,
                           const std::vector<unsigned char> &data) {
        FILE *f = fopen(path.c_str(), "wb");
        if (!f) {
            return false;
        }
        bool ok = data.empty() ||
                  fwrite(&data[0], 1, data.size(), f) == data.size();
        return fclose(f) == 0 && ok;
    }

    int DownloadPsionFiles() {
        std::string dir = PsionDataDir(), error;
        std::vector<std::string> existing;
        std::vector<unsigned char> zip;
        std::vector<std::vector<unsigned char> > contents(PSION_FILE_COUNT);
        size_t i, j;
        // For tests, e.g. a file:// URL. Anything else than psion213.zip is
        // rejected below anyway.
        const char *env = getenv("PSION_ZIP_URL");
        std::string url = env ? env : PSION_ZIP_URL;
        if (url.empty() || url[0] == '-' ||
            url.find_first_of("\"\\$`%!\r\n") != std::string::npos) {
            fprintf(stderr, "psion: invalid PSION_ZIP_URL\n");
            return 2;
        }
        if (CheckFiles(dir, PSION_FILES, PSION_FILE_COUNT, error)) {
            printf("Psion Chess is already installed in %s.\n", dir.c_str());
            return 0;
        }
        for (i = 0; i < PSION_FILE_COUNT; i++) {
            std::vector<std::string> names =
                FindNoCase(dir, PSION_FILES[i].name);
            existing.insert(existing.end(), names.begin(), names.end());
        }
        printf("%s\n", NOTICE);
        printf("This downloads\n\n    %s\n\nwith curl and installs Psion "
               "Chess in\n\n    %s\n\n",
               url.c_str(), dir.c_str());
        if (!existing.empty()) {
            printf("These files there will be replaced:");
            for (i = 0; i < existing.size(); i++) {
                printf(" %s", existing[i].c_str());
            }
            printf("\n\n");
        }
        printf("Download and install Psion Chess? [y/N] ");
        fflush(stdout);
        if (!confirmed()) {
            printf("Nothing downloaded.\n");
            return 1;
        }
        printf("Downloading...\n");
        fflush(stdout);
        if (!download(url, zip)) {
            fprintf(stderr, "psion: the download failed. Is curl installed? "
                            "See data/README.md to install Psion Chess by "
                            "hand.\n");
            return 1;
        }
        // Nothing of the download is used unless it is the known file.
        std::string digest = Sha256Hex(zip);
        if (digest != PSION_ZIP_SHA256) {
            fprintf(stderr, "psion: the download is not the known "
                            "psion213.zip:\nit has SHA-256 %s, expected %s.\n"
                            "Nothing installed.\n",
                    digest.c_str(), PSION_ZIP_SHA256);
            return 1;
        }
        for (i = 0; i < PSION_FILE_COUNT; i++) {
            if (!ZipExtract(zip, PSION_FILES[i].name, contents[i], error) ||
                Sha256Hex(contents[i]) != PSION_FILES[i].sha256) {
                fprintf(stderr, "psion: cannot extract %s from the "
                                "download. Nothing installed.\n",
                        PSION_FILES[i].name);
                return 1;
            }
        }
#ifdef _WIN32
        int made = _mkdir(dir.c_str());
#else
        int made = mkdir(dir.c_str(), 0777);
#endif
        if (made != 0 && errno != EEXIST) {
            fprintf(stderr, "psion: cannot create %s: %s\n", dir.c_str(),
                    strerror(errno));
            return 1;
        }
        for (i = 0; i < PSION_FILE_COUNT; i++) {
            std::vector<std::string> names =
                FindNoCase(dir, PSION_FILES[i].name);
            for (j = 0; j < names.size(); j++) {
                remove((dir + "/" + names[j]).c_str());
            }
            std::string path = dir + "/" + PSION_FILES[i].name;
            if (!write_file(path, contents[i])) {
                fprintf(stderr, "psion: cannot write %s: %s\n", path.c_str(),
                        strerror(errno));
                return 1;
            }
        }
        if (!CheckFiles(dir, PSION_FILES, PSION_FILE_COUNT, error)) {
            fprintf(stderr, "psion: the files in %s are still wrong:\n%s",
                    dir.c_str(), error.c_str());
            return 1;
        }
        printf("Installed Psion Chess in %s.\n", dir.c_str());
        return 0;
    }

} // namespace psion
