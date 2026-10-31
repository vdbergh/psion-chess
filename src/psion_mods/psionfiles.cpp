#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

#include "psionfiles.h"

namespace psion {

    const char *PSION_ZIP_URL =
        "https://www.classicdosgames.com/files/games/psion/psion213.zip";
    const char *PSION_ZIP_SHA256 =
        "f2c50e8d46f09c7434e8a26579483586ef92998a4b9ad632b05e367d3741b2d1";

    const FileHash PSION_FILES[] = {
        {"Chess.exe",
         "df1691734fe9f1082bca3f5513380807104aa4cc9b68ae97ce2bbff74221660c"},
        {"Chess1.ovl",
         "238a2f23bb9ad36c9b3bc620b91b127899fd7b8a5ef299db1786e482cac67e6a"},
        {"Chess2.ovl",
         "37b735722a51b54ac2201a4e2d7f18addc1ca7591399fe82d7298b60e5e27950"},
        {"Chess3.ovl",
         "63bf94f4fc0a5152551e491d6bb9ce6b67e12f5baebca23979a3bbee7dfc7d69"},
        {"Chess4.ovl",
         "d8d79199492150c2b4564dc3289b970da168b8f647c7bc4d516bbde522834714"},
        {"Chess5.ovl",
         "6cc735059dba6b3e1617cf9dd1f7f3b2a8e13001fe16e886e7d81a6dd6254ebd"},
        {"Chess6.ovl",
         "8bf8c86dd0f93390dad3db42d7a809b1911181ff24421ec4558393df0c113857"},
        {"Chess7.ovl",
         "a1f1a0c506ef0df74cf3bbcd68b93b32aa3631d1d9a8712ce6b170545e00991d"},
        {"Chess.hlp",
         "ede583b2768c8b9d1548c107421d9a469df7f86dce57e254b1e8e90cc7464353"},
        {"Psion.grt",
         "f48970a275d44dce41fe39a779d1e71e66880ec36f1ddcddccf1997dfe6e5b1c"},
        {"Verm2.13",
         "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    };
    const size_t PSION_FILE_COUNT = sizeof(PSION_FILES) / sizeof(PSION_FILES[0]);

    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

    static uint32_t rotr(uint32_t x, int n) {
        return (x >> n) | (x << (32 - n));
    }

    Sha256::Sha256() : buf_len(0), total(0) {
        static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                         0xa54ff53a, 0x510e527f, 0x9b05688c,
                                         0x1f83d9ab, 0x5be0cd19};
        memcpy(h, init, sizeof(h));
    }

    void Sha256::Block(const unsigned char *p) {
        uint32_t w[64], a, b, c, d, e, f, g, hh, t1, t2;
        int i;
        for (i = 0; i < 16; i++) {
            w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 |
                   (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
        }
        for (i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^
                          (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^
                          (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        a = h[0], b = h[1], c = h[2], d = h[3];
        e = h[4], f = h[5], g = h[6], hh = h[7];
        for (i = 0; i < 64; i++) {
            t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) +
                 ((e & f) ^ (~e & g)) + K[i] + w[i];
            t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) +
                 ((a & b) ^ (a & c) ^ (b & c));
            hh = g, g = f, f = e, e = d + t1;
            d = c, c = b, b = a, a = t1 + t2;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d;
        h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
    }

    void Sha256::Update(const unsigned char *data, size_t len) {
        total += len;
        while (len > 0) {
            size_t n = 64 - buf_len;
            if (n > len) {
                n = len;
            }
            memcpy(buf + buf_len, data, n);
            buf_len += n;
            data += n;
            len -= n;
            if (buf_len == 64) {
                Block(buf);
                buf_len = 0;
            }
        }
    }

    std::string Sha256::HexDigest() {
        uint64_t bits = total * 8;
        unsigned char pad[72] = {0x80};
        unsigned char length[8];
        char hex[65];
        int i;
        // Pad to 56 bytes mod 64, then the length in bits, big endian.
        Update(pad, (buf_len < 56 ? 56 : 120) - buf_len);
        for (i = 0; i < 8; i++) {
            length[i] = (unsigned char)(bits >> (56 - 8 * i));
        }
        Update(length, 8);
        for (i = 0; i < 8; i++) {
            snprintf(hex + 8 * i, 9, "%08x", (unsigned)h[i]);
        }
        return std::string(hex, 64);
    }

    std::string Sha256Hex(const std::vector<unsigned char> &data) {
        Sha256 sha;
        if (!data.empty()) {
            sha.Update(&data[0], data.size());
        }
        return sha.HexDigest();
    }

    std::string PsionDataDir() {
        const char *env = getenv("PSION_DIR");
        char exe[4096];
        if (env) {
            return env;
        }
#ifdef _WIN32
        DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
        if (n == 0 || n >= sizeof(exe)) {
            return "data";
        }
        std::string path(exe, n);
        return path.substr(0, path.find_last_of("\\/")) + "\\data";
#else
        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n <= 0) {
            return "data";
        }
        exe[n] = 0;
        std::string path(exe);
        return path.substr(0, path.rfind('/')) + "/data";
#endif
    }

    static bool same_name(const char *a, const char *b) {
        for (; *a && *b; a++, b++) {
            if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
                return false;
            }
        }
        return *a == *b;
    }

    // The SHA-256 of a file; false if it cannot be read.
    static bool hash_file(const std::string &path, std::string &digest) {
        unsigned char data[65536];
        size_t n;
        Sha256 sha;
        FILE *f = fopen(path.c_str(), "rb");
        if (!f) {
            return false;
        }
        while ((n = fread(data, 1, sizeof(data), f)) > 0) {
            sha.Update(data, n);
        }
        bool ok = !ferror(f);
        fclose(f);
        digest = sha.HexDigest();
        return ok;
    }

    bool CheckFiles(const std::string &dir, const FileHash *files, size_t n,
                    std::string &error) {
        std::vector<std::string> found(n);
        struct dirent *entry;
        size_t i;
        error.clear();
        DIR *d = opendir(dir.c_str());
        if (!d) {
            error = "cannot read the directory " + dir + "\n";
            return false;
        }
        while ((entry = readdir(d)) != NULL) {
            for (i = 0; i < n; i++) {
                if (!same_name(entry->d_name, files[i].name)) {
                    continue;
                }
                if (!found[i].empty()) {
                    error += "both " + found[i] + " and " + entry->d_name +
                             " exist\n";
                }
                found[i] = entry->d_name;
            }
        }
        closedir(d);
        for (i = 0; i < n; i++) {
            std::string digest;
            if (found[i].empty()) {
                error += std::string(files[i].name) + " is missing\n";
            } else if (!hash_file(dir + "/" + found[i], digest)) {
                error += "cannot read " + found[i] + "\n";
            } else if (digest != files[i].sha256) {
                error += found[i] + " has SHA-256 " + digest +
                         ", expected " + files[i].sha256 + "\n";
            }
        }
        return error.empty();
    }

    std::vector<std::string> FindNoCase(const std::string &dir,
                                        const char *name) {
        std::vector<std::string> names;
        struct dirent *entry;
        DIR *d = opendir(dir.c_str());
        if (!d) {
            return names;
        }
        while ((entry = readdir(d)) != NULL) {
            if (same_name(entry->d_name, name)) {
                names.push_back(entry->d_name);
            }
        }
        closedir(d);
        return names;
    }

    // Inflate after Mark Adler's puff.c: canonical Huffman codes, decoded
    // a bit at a time.
    namespace {

        struct Huffman {
            short count[16];  // number of codes of each length
            short symbol[288]; // symbols ordered by code
        };

        class Inflater {
          public:
            Inflater(const unsigned char *in, size_t len,
                     std::vector<unsigned char> &out)
                : in(in), len(len), pos(0), bitbuf(0), bitcnt(0), out(out),
                  failed(false) {}
            bool Run();

          private:
            int bits(int need);
            bool stored();
            int decode(const Huffman &h);
            bool codes(const Huffman &lencode, const Huffman &distcode);
            bool fixed();
            bool dynamic();
            const unsigned char *in;
            size_t len, pos;
            uint32_t bitbuf;
            int bitcnt;
            std::vector<unsigned char> &out;
            bool failed; // out of input
        };

        // Fills h from the code lengths of n symbols. Returns 0 for a
        // complete code, more for an incomplete one, less than 0 for an
        // over-subscribed one.
        int construct(Huffman &h, const short *length, int n) {
            short offs[16];
            int sym, l, left;
            for (l = 0; l < 16; l++) {
                h.count[l] = 0;
            }
            for (sym = 0; sym < n; sym++) {
                h.count[length[sym]]++;
            }
            if (h.count[0] == n) {
                return 0;
            }
            left = 1;
            for (l = 1; l < 16; l++) {
                left <<= 1;
                left -= h.count[l];
                if (left < 0) {
                    return left;
                }
            }
            offs[1] = 0;
            for (l = 1; l < 15; l++) {
                offs[l + 1] = offs[l] + h.count[l];
            }
            for (sym = 0; sym < n; sym++) {
                if (length[sym] != 0) {
                    h.symbol[offs[length[sym]]++] = (short)sym;
                }
            }
            return left;
        }

        int Inflater::bits(int need) {
            uint32_t val = bitbuf;
            while (bitcnt < need) {
                if (pos == len) {
                    failed = true;
                    return 0;
                }
                val |= (uint32_t)in[pos++] << bitcnt;
                bitcnt += 8;
            }
            bitbuf = val >> need;
            bitcnt -= need;
            return (int)(val & ((1u << need) - 1));
        }

        bool Inflater::stored() {
            size_t n;
            bitbuf = 0;
            bitcnt = 0;
            if (len - pos < 4) {
                return false;
            }
            n = in[pos] | in[pos + 1] << 8;
            if ((in[pos + 2] | in[pos + 3] << 8) != (~n & 0xffff)) {
                return false;
            }
            pos += 4;
            if (len - pos < n) {
                return false;
            }
            out.insert(out.end(), in + pos, in + pos + n);
            pos += n;
            return true;
        }

        int Inflater::decode(const Huffman &h) {
            int code = 0, first = 0, index = 0, l, count;
            for (l = 1; l < 16; l++) {
                code |= bits(1);
                if (failed) {
                    return -1;
                }
                count = h.count[l];
                if (code - count < first) {
                    return h.symbol[index + (code - first)];
                }
                index += count;
                first += count;
                first <<= 1;
                code <<= 1;
            }
            return -1;
        }

        bool Inflater::codes(const Huffman &lencode, const Huffman &distcode) {
            static const short lbase[29] = {
                3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
            static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
                                           1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                                           4, 4, 4, 4, 5, 5, 5, 5, 0};
            static const short dbase[30] = {
                1,    2,    3,    4,    5,    7,     9,     13,    17,  25,
                33,   49,   65,   97,   129,  193,   257,   385,   513, 769,
                1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
            static const short dext[30] = {0, 0, 0, 0, 1, 1, 2,  2,  3,  3,
                                           4, 4, 5, 5, 6, 6, 7,  7,  8,  8,
                                           9, 9, 10, 10, 11, 11, 12, 12, 13,
                                           13};
            for (;;) {
                int sym = decode(lencode);
                if (sym < 0) {
                    return false;
                }
                if (sym < 256) {
                    out.push_back((unsigned char)sym);
                } else if (sym == 256) {
                    return true;
                } else {
                    size_t n, dist;
                    sym -= 257;
                    if (sym >= 29) {
                        return false;
                    }
                    n = lbase[sym] + bits(lext[sym]);
                    sym = decode(distcode);
                    if (sym < 0 || sym >= 30) {
                        return false;
                    }
                    dist = dbase[sym] + bits(dext[sym]);
                    if (failed || dist > out.size()) {
                        return false;
                    }
                    while (n--) {
                        out.push_back(out[out.size() - dist]);
                    }
                }
            }
        }

        bool Inflater::fixed() {
            static Huffman lencode, distcode;
            static bool built = false;
            if (!built) {
                short lengths[288];
                int sym;
                for (sym = 0; sym < 288; sym++) {
                    lengths[sym] = sym < 144 ? 8 : sym < 256 ? 9
                                   : sym < 280 ? 7 : 8;
                }
                construct(lencode, lengths, 288);
                for (sym = 0; sym < 30; sym++) {
                    lengths[sym] = 5;
                }
                construct(distcode, lengths, 30);
                built = true;
            }
            return codes(lencode, distcode);
        }

        bool Inflater::dynamic() {
            static const short order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                            11, 4,  12, 3, 13, 2, 14, 1, 15};
            short lengths[320];
            Huffman lencode, distcode;
            int nlen, ndist, ncode, index, err;
            nlen = bits(5) + 257;
            ndist = bits(5) + 1;
            ncode = bits(4) + 4;
            if (failed || nlen > 286 || ndist > 30) {
                return false;
            }
            for (index = 0; index < 19; index++) {
                lengths[order[index]] = index < ncode ? (short)bits(3) : 0;
            }
            if (failed || construct(lencode, lengths, 19) != 0) {
                return false;
            }
            index = 0;
            while (index < nlen + ndist) {
                int sym = decode(lencode), n;
                short l = 0;
                if (sym < 0) {
                    return false;
                }
                if (sym < 16) {
                    lengths[index++] = (short)sym;
                    continue;
                }
                if (sym == 16) {
                    if (index == 0) {
                        return false;
                    }
                    l = lengths[index - 1];
                    n = 3 + bits(2);
                } else if (sym == 17) {
                    n = 3 + bits(3);
                } else {
                    n = 11 + bits(7);
                }
                if (failed || index + n > nlen + ndist) {
                    return false;
                }
                while (n--) {
                    lengths[index++] = l;
                }
            }
            if (lengths[256] == 0) {
                return false;
            }
            // Incomplete codes are only allowed for a single code.
            err = construct(lencode, lengths, nlen);
            if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) {
                return false;
            }
            err = construct(distcode, lengths + nlen, ndist);
            if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) {
                return false;
            }
            return codes(lencode, distcode);
        }

        bool Inflater::Run() {
            int last;
            do {
                bool ok;
                last = bits(1);
                switch (bits(2)) {
                case 0:
                    ok = stored();
                    break;
                case 1:
                    ok = fixed();
                    break;
                case 2:
                    ok = dynamic();
                    break;
                default:
                    ok = false;
                }
                if (!ok || failed) {
                    return false;
                }
            } while (!last);
            return true;
        }

        uint32_t get16(const std::vector<unsigned char> &d, size_t i) {
            return d[i] | d[i + 1] << 8;
        }

        uint32_t get32(const std::vector<unsigned char> &d, size_t i) {
            return get16(d, i) | get16(d, i + 2) << 16;
        }

        uint32_t crc32(const std::vector<unsigned char> &data) {
            uint32_t crc = 0xffffffff;
            size_t i;
            int k;
            for (i = 0; i < data.size(); i++) {
                crc ^= data[i];
                for (k = 0; k < 8; k++) {
                    crc = crc >> 1 ^ (0xedb88320 & (0 - (crc & 1)));
                }
            }
            return ~crc;
        }

    } // namespace

    bool Inflate(const unsigned char *in, size_t len,
                 std::vector<unsigned char> &out) {
        return Inflater(in, len, out).Run();
    }

    bool ZipExtract(const std::vector<unsigned char> &zip, const char *name,
                    std::vector<unsigned char> &data, std::string &error) {
        size_t eocd, cd, i, entries, size = zip.size();
        data.clear();
        // The end of central directory record: 22 bytes and a comment.
        for (eocd = size >= 22 ? size - 22 : 0;; eocd--) {
            if (size >= 22 && get32(zip, eocd) == 0x06054b50) {
                break;
            }
            if (eocd == 0 || size - eocd > 22 + 65535) {
                error = "not a zip file";
                return false;
            }
        }
        entries = get16(zip, eocd + 10);
        cd = get32(zip, eocd + 16);
        for (i = 0; i < entries; i++) {
            size_t name_len, local, start, csize, usize, method;
            uint32_t crc;
            if (cd + 46 > eocd || get32(zip, cd) != 0x02014b50) {
                break;
            }
            method = get16(zip, cd + 10);
            crc = get32(zip, cd + 16);
            csize = get32(zip, cd + 20);
            usize = get32(zip, cd + 24);
            name_len = get16(zip, cd + 28);
            local = get32(zip, cd + 42);
            if (cd + 46 + name_len > eocd) {
                break;
            }
            if (std::string((const char *)&zip[cd + 46], name_len) != name) {
                cd += 46 + name_len + get16(zip, cd + 30) + get16(zip, cd + 32);
                continue;
            }
            if (local + 30 > size || get32(zip, local) != 0x04034b50) {
                break;
            }
            start = local + 30 + get16(zip, local + 26) +
                    get16(zip, local + 28);
            if (start > size || size - start < csize) {
                break;
            }
            if (method == 0) {
                data.assign(zip.begin() + start, zip.begin() + start + csize);
            } else if (method != 8) {
                error = std::string(name) + ": unsupported compression";
                return false;
            } else if (!Inflate(csize ? &zip[start] : NULL, csize, data)) {
                error = std::string(name) + ": invalid compressed data";
                return false;
            }
            if (data.size() != usize || crc32(data) != crc) {
                error = std::string(name) + ": wrong size or CRC";
                return false;
            }
            return true;
        }
        error = i < entries ? "invalid zip file"
                            : std::string(name) + " is not in the zip file";
        return false;
    }

} // namespace psion
