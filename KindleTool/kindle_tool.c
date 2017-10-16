//
//  kindle_tool.c
//  KindleTool
//
//  Copyright (C) 2011-2012  Yifan Lu
//  Copyright (C) 2012-2016  NiLuJe
//  Concept based on an original Python implementation by Igor Skochinsky & Jean-Yves Avenard,
//    cf., http://www.mobileread.com/forums/showthread.php?t=63225
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "kindle_tool.h"
#include "kindle_main.h"
#include "kindle_table.h"

#if defined(_WIN32) && !defined(__CYGWIN__)
// NOTE: Handle the rest of the Win32 tempfiles mess in a quick'n dirty way...
// Namely: - We can't use MinGW's mkstemp until 5.0 comes out (the implementation in 4.0.1 unlinks on close, which is unexpected)
//         - MSVCRT's tmpfile() creates files in the root drive, which, as we've already mentioned, is a recipe for disaster...
// Whip crude hacks around both of these issues without having to resort to GetTempPathW() and deal with wchar_t...
// Inspired from fontconfig's compatibility helpers (http://cgit.freedesktop.org/fontconfig/tree/src/fccompat.c)
int kt_win_mkstemp(char *template)
{
    if(_mktemp(template) == NULL)
    {
        fprintf(stderr, "Couldn't create temporary file template: %s.\n", strerror(errno));
        return -1;
    }
    // NOTE: Don't use _O_TEMPORARY, we expect to handle the unlink ourselves!
    // NOTE: And while we probably could use _O_NOINHERIT, we do not, for a question of feature parity:
    //       We don't use O_CLOEXEC on Linux because it depends on Glibc 2.7 & Linux 2.6.23, and we routinely run on stuff much older than that...
    return _open(template, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
}

// Inspired from gnulib's tmpfile implementation (http://git.savannah.gnu.org/gitweb/?p=gnulib.git;a=blob;f=lib/tmpfile.c)
FILE *kt_win_tmpfile(void)
{
    char template[] = KT_TMPDIR "/kindletool_tmpfile_XXXXXX";
    int fd = -1;
    if(_mktemp(template) == NULL)
    {
        fprintf(stderr, "Couldn't create temporary file template: %s.\n", strerror(errno));
        return NULL;
    }
    fd = _open(template, _O_CREAT | _O_EXCL | _O_TEMPORARY | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
    if(fd == -1)
    {
        fprintf(stderr, "Couldn't open temporary file: %s.\n", strerror(errno));
        return NULL;
    }
    FILE *fp = _fdopen(fd, "w+b");
    if(fp != NULL)
        return fp;
    else
    {
        // We need to close the fd ourselves in case of error, since our own code expects a FP, not an fd... Which means we have to fudge errno to keep the one from fdopen...
        int saved_errno = errno;
        _close(fd);
        errno = saved_errno;
    }
    return NULL;
}
#endif

void md(unsigned char *bytes, size_t length)
{
    unsigned int i;
    for(i = 0; i < length; i++)
    {
        bytes[i] = (unsigned char)ptog[bytes[i]];
    }
}

void dm(unsigned char *bytes, size_t length)
{
    unsigned int i;
    for(i = 0; i < length; i++)
    {
        bytes[i] = (unsigned char)gtop[bytes[i]];
    }
}

int munger(FILE *input, FILE *output, size_t length, const bool fake_sign)
{
    unsigned char bytes[BUFFER_SIZE];
    size_t bytes_read;
    size_t bytes_written;

    while((bytes_read = fread(bytes, sizeof(unsigned char), (length < BUFFER_SIZE && length > 0 ? length : BUFFER_SIZE), input)) > 0)
    {
        // Don't munge if we asked for a fake package
        if(!fake_sign)
            md(bytes, bytes_read);
        bytes_written = fwrite(bytes, sizeof(unsigned char), bytes_read, output);
        if(ferror(output) != 0)
        {
            fprintf(stderr, "Error munging, cannot write to output: %s.\n", strerror(errno));
            return -1;
        }
        else if(bytes_written < bytes_read)
        {
            fprintf(stderr, "Error munging, read %zu bytes but only wrote %zu bytes.\n", bytes_read, bytes_written);
            return -1;
        }
        length -= bytes_read;
    }
    if(ferror(input) != 0)
    {
        fprintf(stderr, "Error munging, cannot read input: %s.\n", strerror(errno));
        return -1;
    }

    return 0;
}

int demunger(FILE *input, FILE *output, size_t length, const bool fake_sign)
{
    unsigned char bytes[BUFFER_SIZE];
    size_t bytes_read;
    size_t bytes_written;

    while((bytes_read = fread(bytes, sizeof(unsigned char), (length < BUFFER_SIZE && length > 0 ? length : BUFFER_SIZE), input)) > 0)
    {
        // Don't demunge if we supplied a fake package
        if(!fake_sign)
            dm(bytes, bytes_read);
        bytes_written = fwrite(bytes, sizeof(unsigned char), bytes_read, output);
        if(ferror(output) != 0)
        {
            fprintf(stderr, "Error demunging, cannot write to output: %s.\n", strerror(errno));
            return -1;
        }
        else if(bytes_written < bytes_read)
        {
            fprintf(stderr, "Error demunging, read %zu bytes but only wrote %zu bytes.\n", bytes_read, bytes_written);
            return -1;
        }
        length -= bytes_read;
    }
    if(ferror(input) != 0)
    {
        fprintf(stderr, "Error demunging, cannot read input: %s.\n", strerror(errno));
        return -1;
    }

    return 0;
}

const char *convert_device_id(Device dev)
{
    switch(dev)
    {
        case Kindle1:
            return "Kindle 1";
        case Kindle2US:
            return "Kindle 2 US";
        case Kindle2International:
            return "Kindle 2 International";
        case KindleDXUS:
            return "Kindle DX US";
        case KindleDXInternational:
            return "Kindle DX International";
        case KindleDXGraphite:
            return "Kindle DX Graphite";
        case Kindle3WiFi:
            return "Kindle 3 WiFi";
        case Kindle3WiFi3G:
            return "Kindle 3 WiFi+3G";
        case Kindle3WiFi3GEurope:
            return "Kindle 3 WiFi+3G Europe";
        case Kindle4NonTouch:
            return "Silver Kindle 4 Non-Touch (2011)";
        case Kindle5TouchWiFi:
            return "Kindle 5 Touch WiFi";
        case Kindle5TouchWiFi3G:
            return "Kindle 5 Touch WiFi+3G";
        case Kindle5TouchWiFi3GEurope:
            return "Kindle 5 Touch WiFi+3G Europe";
        case Kindle5TouchUnknown:
            return "Kindle 5 Touch (Unknown Variant)";
        case Kindle4NonTouchBlack:
            return "Black Kindle 4 Non-Touch (2012)";
        case KindlePaperWhiteWiFi:
            return "Kindle PaperWhite WiFi";
        case KindlePaperWhiteWiFi3G:
            return "Kindle PaperWhite WiFi+3G";
        case KindlePaperWhiteWiFi3GCanada:
            return "Kindle PaperWhite WiFi+3G Canada";
        case KindlePaperWhiteWiFi3GEurope:
            return "Kindle PaperWhite WiFi+3G Europe";
        case KindlePaperWhiteWiFi3GJapan:
            return "Kindle PaperWhite WiFi+3G Japan";
        case KindlePaperWhiteWiFi3GBrazil:
            return "Kindle PaperWhite WiFi+3G Brazil";
        case KindlePaperWhite2WiFi:
            return "Kindle PaperWhite 2 (2013) WiFi";
        case KindlePaperWhite2WiFiJapan:
            return "Kindle PaperWhite 2 (2013) WiFi Japan";
        case KindlePaperWhite2WiFi3G:
            return "Kindle PaperWhite 2 (2013) WiFi+3G";
        case KindlePaperWhite2WiFi3GCanada:
            return "Kindle PaperWhite 2 (2013) WiFi+3G Canada";
        case KindlePaperWhite2WiFi3GEurope:
            return "Kindle PaperWhite 2 (2013) WiFi+3G Europe";
        case KindlePaperWhite2WiFi3GRussia:
            return "Kindle PaperWhite 2 (2013) WiFi+3G Russia";
        case KindlePaperWhite2WiFi3GJapan:
            return "Kindle PaperWhite 2 (2013) WiFi+3G Japan";
        case KindlePaperWhite2WiFi4GBInternational:
            return "Kindle PaperWhite 2 (2013) WiFi (4GB) International";
        case KindlePaperWhite2WiFi3G4GBEurope:
            return "Kindle PaperWhite 2 (2013) WiFi+3G (4GB) Europe";
        case KindlePaperWhite2Unknown_0xF4:
            return "Kindle PaperWhite 2 (2013) (Unknown Variant 0xF4)";
        case KindlePaperWhite2Unknown_0xF9:
            return "Kindle PaperWhite 2 (2013) (Unknown Variant 0xF9)";
        case KindlePaperWhite2WiFi3G4GB:
            return "Kindle PaperWhite 2 (2013) WiFi+3G (4GB)";
        case KindlePaperWhite2WiFi3G4GBBrazil:
            return "Kindle PaperWhite 2 (2013) WiFi+3G (4GB) Brazil";
        case KindlePaperWhite2WiFi3G4GBCanada:
            return "Kindle PaperWhite 2 (2013) WiFi+3G (4GB) Canada";
        case KindleBasic:
            return "Kindle Basic (2014)";
        case KindleVoyageWiFi:
            return "Kindle Voyage WiFi";
        case ValidKindleUnknown_0x16:
            return "Unknown Kindle (0x16)";
        case ValidKindleUnknown_0x21:
            return "Unknown Kindle (0x21)";
        case KindleVoyageWiFi3G:
            return "Kindle Voyage WiFi+3G";
        case KindleVoyageWiFi3GJapan:
            return "Kindle Voyage WiFi+3G Japan";
        case KindleVoyageUnknown_0x4F:
            return "Kindle Voyage (Unknown Variant 0x4F)";
        case KindleVoyageWiFi3GMexico:
            return "Kindle Voyage WiFi+3G Mexico";
        case KindleVoyageWiFi3GEurope:
            return "Kindle Voyage WiFi+3G Europe";
        case ValidKindleUnknown_0x07:
            return "Unknown Kindle (0x07)";
        case ValidKindleUnknown_0x0B:
            return "Unknown Kindle (0x0B)";
        case ValidKindleUnknown_0x0C:
            return "Unknown Kindle (0x0C)";
        case ValidKindleUnknown_0x0D:
            return "Unknown Kindle (0x0D)";
        case ValidKindleUnknown_0x99:
            return "Unknown Kindle (0x99)";
        case KindleBasicKiwi:
            return "Kindle Basic (2014) Australia";
        case KindlePaperWhite3WiFi:
            return "Kindle PaperWhite 3 (2015) WiFi";
        case KindlePaperWhite3WiFi3G:
            return "Kindle PaperWhite 3 (2015) WiFi+3G";
        case KindlePaperWhite3WiFi3GMexico:
            return "Kindle PaperWhite 3 (2015) WiFi+3G Mexico";
        case KindlePaperWhite3WiFi3GEurope:
            return "Kindle PaperWhite 3 (2015) WiFi+3G Europe";
        case KindlePaperWhite3WiFi3GCanada:
            return "Kindle PaperWhite 3 (2015) WiFi+3G Canada";
        case KindlePaperWhite3WiFi3GJapan:
            return "Kindle PaperWhite 3 (2015) WiFi+3G Japan";
        case KindlePaperWhite3WhiteWiFi:
            return "White Kindle PaperWhite 3 (2016) WiFi";
        case KindlePaperWhite3WhiteWiFi3GJapan:
            return "White Kindle PaperWhite 3 (2016) WiFi+3G Japan";
        case KindlePW3WhiteUnknown_0KD:
            return "White Kindle PaperWhite 3 (Unknown Variant 0KD)";
        case KindlePW3WhiteUnknown_0KE:
            return "White Kindle PaperWhite 3 (Unknown Variant 0KE)";
        case KindlePW3WhiteUnknown_0KF:
            return "White Kindle PaperWhite 3 (Unknown Variant 0KF)";
        case KindlePW3WhiteUnknown_0KG:
            return "White Kindle PaperWhite 3 (Unknown Variant 0KG)";
        case KindleOasisWiFi:
            return "Kindle Oasis WiFi";
        case KindlePW3_32GB_Color1:
            return "Kindle PW3 Manga 1";
        case KindlePW3_32GB_Color2:
            return "Kindle PW3 Manga 2";
        case KindleOasisWiFi3G:
            return "Kindle Oasis WiFi+3G";
        case KindleOasisUnknown_0GR:
            return "Kindle Oasis (Unknown Variant 0GR)";
        case KindleOasisUnknown_0GS:
            return "Kindle Oasis (Unknown Variant 0GS)";
        case KindleOasisUnknown_0GT:
            return "Kindle Oasis (Unknown Variant 0GT)";
        case KindleOasisWiFi3GEurope:
            return "Kindle Oasis WiFi+3G Europe";
        case KindleBasic2White:
            return "White Kindle Basic 2 (2016)";
        case KindleBasic2:
            return "Kindle Basic 2 (2016)";
        case KindleBasic2Unknown_0DU:
            return "Kindle Basic 2 (2016) (Unknown Variant 0DU)";
        case KindleUnknown:
        default:
            return "Unknown";
    }
}

const char *convert_platform_id(Platform plat)
{
    switch(plat)
    {
        case Plat_Unspecified:
            return "Unspecified";
        case MarioDeprecated:
            return "Mario (Deprecated)";
        case Luigi:
            return "Luigi";
        case Banjo:
            return "Banjo";
        case Yoshi:
            return "Yoshi";
        case YoshimeProto:
            return "Yoshime (Prototype)";
        case Yoshime:
            return "Yoshime (Yoshime3)";
        case Wario:
            return "Wario";
        case Duet:
            return "Duet";
        case Heisenberg:
            return "Heisenberg";
        default:
            return "Unknown";
    }
}

const char *convert_board_id(Board board)
{
    switch(board)
    {
        case Board_Unspecified:
            return "Unspecified";
        case Tequila:
            return "Tequila";
        case Whitney:
            return "Whitney";
        default:
            return "Unknown";
    }
}

BundleVersion get_bundle_version(char magic_number[MAGIC_NUMBER_LENGTH])
{
    if(!strncmp(magic_number, "FB02", MAGIC_NUMBER_LENGTH) || !strncmp(magic_number, "FB01", MAGIC_NUMBER_LENGTH))
        return RecoveryUpdate;
    else if(!strncmp(magic_number, "FB03", MAGIC_NUMBER_LENGTH))
        return RecoveryUpdateV2;
    else if(!strncmp(magic_number, "FC02", MAGIC_NUMBER_LENGTH) || !strncmp(magic_number, "FD03", MAGIC_NUMBER_LENGTH))
        return OTAUpdate;
    else if(!strncmp(magic_number, "FC04", MAGIC_NUMBER_LENGTH) || !strncmp(magic_number, "FD04", MAGIC_NUMBER_LENGTH) || !strncmp(magic_number, "FL01", MAGIC_NUMBER_LENGTH))
        return OTAUpdateV2;
    else if(!strncmp(magic_number, "SP01", MAGIC_NUMBER_LENGTH))
        return UpdateSignature;
    else if(!memcmp(magic_number, "\x1F\x8B\x08\x00", MAGIC_NUMBER_LENGTH))       // GZIP magic number
        return UserDataPackage;
    else
        return UnknownUpdate;
}

int md5_sum(FILE *input, char output_string[BASE16_ENCODE_LENGTH(MD5_DIGEST_SIZE)])
{
    unsigned char bytes[BUFFER_SIZE];
    size_t bytes_read;
    struct md5_ctx md5;
    uint8_t digest[MD5_DIGEST_SIZE];

    md5_init(&md5);
    while((bytes_read = fread(bytes, sizeof(unsigned char), BUFFER_SIZE, input)) > 0)
    {
        md5_update(&md5, bytes_read, bytes);
    }
    if(ferror(input) != 0)
    {
        fprintf(stderr, "Error reading input file: %s.\n", strerror(errno));
        return -1;
    }
    md5_digest(&md5, MD5_DIGEST_SIZE, digest);
    // And build the hex checksum the nettle way ;)
    base16_encode_update((uint8_t *)output_string, MD5_DIGEST_SIZE, digest);

    return 0;
}

static int kindle_print_help(const char *prog_name)
{
    printf(
        "usage:\n"
        "  %s md [ <input> ] [ <output> ]\n"
        "    Obfuscates data using Amazon's update algorithm.\n"
        "    If no input is provided, input from stdin\n"
        "    If no output is provided, output to stdout\n"
        "    \n"
        "  %s dm [ <input> ] [ <output> ]\n"
        "    Deobfuscates data using Amazon's update algorithm.\n"
        "    If no input is provided, input from stdin\n"
        "    If no output is provided, output to stdout\n"
        "    \n"
        "  %s convert [options] <input>...\n"
        "    Converts a Kindle update package to a gzipped tar archive file, and delete input.\n"
        "    \n"
        "    Options:\n"
        "      -c, --stdout                Write to standard output, keeping original files unchanged.\n"
        "      -i, --info                  Just print the package information, no conversion done.\n"
        "      -s, --sig                   OTA V2 updates only. Extract the payload signature.\n"
        "      -k, --keep                  Don't delete the input package.\n"
        "      -u, --unsigned              Assume input is an unsigned & mangled userdata package.\n"
        "      -w, --unwrap                Just unwrap the package, if it's wrapped in an UpdateSignature header (especially useful for userdata packages).\n"
        "      \n"
        "  %s extract [options] <input> <output>\n"
        "    Extracts a Kindle update package to a directory.\n"
        "    \n"
        "    Options:\n"
        "      -u, --unsigned              Assume input is an unsigned & mangled userdata package.\n"
        "      \n"
        "  %s create <type> <devices> [options] <dir|file>... [ <output> ]\n"
        "    Creates a Kindle update package.\n"
        "    You should be able to throw a mix of files & directories as input without trouble.\n"
        "    Just keep in mind that by default, if you feed it absolute paths, it will archive absolute paths, which usually isn't what you want!\n"
        "    If input is a single gzipped tarball (\".tgz\" or \".tar.gz\") file, we assume it is properly packaged (bundlefile & sigfile), and will only convert it to an update.\n"
        "    Output should be a file with the extension \".bin\", if it is not provided, or if it's a single dash, outputs to standard output.\n"
        "    In case of OTA updates, all files with the extension \".ffs\" or \".sh\" will be treated as update scripts.\n"
        "    \n"
        "    Type:\n"
        "      ota                         OTA V1 update package. Works on Kindle 3 and older.\n"
        "      ota2                        OTA V2 signed update package. Works on Kindle 4 and newer.\n"
        "      recovery                    Recovery package for restoring partitions.\n"
        "      recovery2                   Recovery V2 package for restoring partitions. Works on Kindle 5 (PaperWhite) and newer.\n"
        "      sig                         Signature envelope. Use this to build a signed userdata package with the -U switch (FW >= 5.1 only, but device agnostic).\n"
        "    \n"
        "    Devices:\n"
        "      OTA V1 & Recovery packages only support one device. OTA V2 & Recovery V2 packages can support multiple devices.\n"
        "      \n"
        "      -d, --device k1             Kindle 1\n"
        "      -d, --device k2             Kindle 2 US\n"
        "      -d, --device k2i            Kindle 2 International\n"
        "      -d, --device dx             Kindle DX US\n"
        "      -d, --device dxi            Kindle DX International\n"
        "      -d, --device dxg            Kindle DX Graphite\n"
        "      -d, --device k3w            Kindle 3 WiFi\n"
        "      -d, --device k3g            Kindle 3 WiFi+3G\n"
        "      -d, --device k3gb           Kindle 3 WiFi+3G Europe\n"
        "      -d, --device k4             Silver Kindle 4 (No Touch) (2011)\n"
        "      -d, --device k4b            Black Kindle 4 (No Touch) (2012)\n"
        "      -d, --device k5w            Kindle 5 (Kindle Touch) WiFi\n"
        "      -d, --device k5g            Kindle 5 (Kindle Touch) WiFi+3G\n"
        "      -d, --device k5gb           Kindle 5 (Kindle Touch) WiFi+3G Europe\n"
        "      -d, --device k5u            Kindle 5 (Kindle Touch) Unknown Variant (4th device code found in Touch official updates).\n"
        "      -d, --device pw             Kindle PaperWhite WiFi\n"
        "      -d, --device pwg            Kindle PaperWhite WiFi+3G\n"
        "      -d, --device pwgc           Kindle PaperWhite WiFi+3G Canada\n"
        "      -d, --device pwgb           Kindle PaperWhite WiFi+3G Europe\n"
        "      -d, --device pwgj           Kindle PaperWhite WiFi+3G Japan\n"
        "      -d, --device pwgbr          Kindle PaperWhite WiFi+3G Brazil\n"
        "      -d, --device pw2            Kindle PaperWhite 2 (2013) WiFi\n"
        "      -d, --device pw2j           Kindle PaperWhite 2 (2013) WiFi Japan\n"
        "      -d, --device pw2g           Kindle PaperWhite 2 (2013) WiFi+3G\n"
        "      -d, --device pw2gc          Kindle PaperWhite 2 (2013) WiFi+3G Canada\n"
        "      -d, --device pw2gb          Kindle PaperWhite 2 (2013) WiFi+3G Europe\n"
        "      -d, --device pw2gr          Kindle PaperWhite 2 (2013) WiFi+3G Russia\n"
        "      -d, --device pw2gj          Kindle PaperWhite 2 (2013) WiFi+3G Japan\n"
        "      -d, --device pw2il          Kindle PaperWhite 2 (2013) WiFi (4GB) International\n"
        "      -d, --device pw2gbl         Kindle PaperWhite 2 (2013) WiFi+3G (4GB) Europe\n"
        "      -d, --device pw2gl          Kindle PaperWhite 2 (2013) WiFi+3G (4GB)\n"
        "      -d, --device pw2gcl         Kindle PaperWhite 2 (2013) WiFi+3G (4GB) Canada\n"
        "      -d, --device pw2gbrl        Kindle PaperWhite 2 (2013) WiFi+3G (4GB) Brazil\n"
        "      -d, --device kt2            Kindle Basic (2014)\n"
        "      -d, --device kt2a           Kindle Basic (2014) Australia\n"
        "      -d, --device kv             Kindle Voyage WiFi\n"
        "      -d, --device kvg            Kindle Voyage WiFi+3G\n"
        "      -d, --device kvgb           Kindle Voyage WiFi+3G Europe\n"
        "      -d, --device kvgj           Kindle Voyage WiFi+3G Japan\n"
        "      -d, --device kvgm           Kindle Voyage WiFi+3G Mexico\n"
        "      -d, --device pw3            Kindle PaperWhite 3 (2015) WiFi\n"
        "      -d, --device pw3g           Kindle PaperWhite 3 (2015) WiFi+3G\n"
        "      -d, --device pw3gj          Kindle PaperWhite 3 (2015) WiFi+3G Japan\n"
        "      -d, --device pw3gc          Kindle PaperWhite 3 (2015) WiFi+3G Canada\n"
        "      -d, --device pw3gb          Kindle PaperWhite 3 (2015) WiFi+3G Europe\n"
        "      -d, --device pw3gm          Kindle PaperWhite 3 (2015) WiFi+3G Mexico\n"
        "      -d, --device pw3w           White Kindle PaperWhite 3 (2016) WiFi\n"
        "      -d, --device pw3wgj         White Kindle PaperWhite 3 (2016) WiFi+3G Japan\n"
        "      -d, --device koa            Kindle Oasis WiFi\n"
        "      -d, --device koag           Kindle Oasis WiFi+3G\n"
        "      -d, --device koagb          Kindle Oasis WiFi+3G Europe\n"
        "      -d, --device kt3            Kindle Basic 2 (2016)\n"
        "      -d, --device kt3w           White Kindle Basic 2 (2016)\n"
        "      -d, --device kindle2        Alias for k2 & k2i\n"
        "      -d, --device kindledx       Alias for dx, dxi & dxg\n"
        "      -d, --device kindle3        Alias for k3w, k3g & k3gb\n"
        "      -d, --device legacy         Alias for kindle2, kindledx & kindle3\n"
        "      -d, --device kindle4        Alias for k4 & k4b\n"
        "      -d, --device touch          Alias for k5w, k5g & k5gb\n"
        "      -d, --device paperwhite     Alias for pw, pwg, pwgc, pwgb, pwgj & pwgbr\n"
        "      -d, --device paperwhite2    Alias for pw2, pw2j, pw2g, pw2gc, pw2gb, pw2gr, pw2gj, pw2il, pw2gbl, pw2gl, pw2gcl & pw2gbrl\n"
        "      -d, --device basic          Alias for kt2 & kt2a\n"
        "      -d, --device voyage         Alias for kv, kvg, kvgb, kvgj & kvgm\n"
        "      -d, --device paperwhite3    Alias for pw3, pw3g, pw3gj, pw3gc, pw3gb, pw3gm, pw3w & pw3wgj\n"
        "      -d, --device oasis          Alias for koa, koag & koagb\n"
        "      -d, --device basic2         Alias for kt3 & kt3w\n"
        "      -d, --device kindle5        Alias for touch, paperwhite, paperwhite2, basic, voyage, paperwhite3, oasis & basic2\n"
        "      -d, --device none           No specific device (Recovery V2 & Recovery FB02 with header rev 2 only, default).\n"
        "      -d, --device auto           The current device (Obviously, has to be run from a Kindle).\n"
        "      \n"
        "    Platforms:\n"
        "      Recovery V2 & recovery FB02 with header rev 2 updates only. Use a single platform per package.\n"
        "      \n"
        "      -p, --platform unspecified  Don't target a specific platform.\n"
        "      -p, --platform mario        Mario (mostly devices shipped on FW 1.x?) [Deprecated].\n"
        "      -p, --platform luigi        Luigi (mostly devices shipped on FW 2.x?).\n"
        "      -p, --platform banjo        Banjo (devices shipped on FW 3.x?).\n"
        "      -p, --platform yoshi        Yoshi (mostly devices shipped on FW <= 5.1).\n"
        "      -p, --platform yoshime-p    Yoshime (Prototype).\n"
        "      -p, --platform yoshime      Yoshime (Also known as Yoshime3, mostly devices shipped on FW >= 5.2).\n"
        "      -p, --platform wario        Wario (mostly devices shipped on FW >= 5.4).\n"
        "      -p, --platform duet         Duet (mostly devices shipped on FW >= 5.7).\n"
        "      -p, --platform heisenberg   Heisenberg (mostly devices shipped on FW >= 5.8).\n"
        "      \n"
        "    Boards:\n"
        "      Recovery V2 & Recovery FB02 with header rev 2 updates only. Use a single board per package.\n"
        "      \n"
        "      -B, --board unspecified     Don't target a specific board, skip the device check.\n"
        "      -B, --board tequila         Tequila (Kindle 4)\n"
        "      -B, --board whitney         Whitney (Kindle Touch)\n"
        "      \n"
        "    Options:\n"
        "      All the following options are optional and advanced.\n"
        "      -k, --key <file>            PEM file containing RSA private key to sign update. Default is popular jailbreak key.\n"
        "      -b, --bundle <type>         Manually specify package magic number. May override the value dictated by \"type\", if it makes sense. Valid bundle versions:\n"
        "                                    FB01, FB02 = recovery; FB03 = recovery2; FC02, FD03 = ota; FC04, FD04, FL01 = ota2; SP01 = sig\n"
        "      -s, --srcrev <ulong|uint>   OTA updates only. Source revision. OTA V1 uses uint, OTA V2 uses ulong.\n"
        "                                    Lowest version of device that package supports. Default is 0.\n"
        "      -t, --tgtrev <ulong|uint>   OTA & Recovery V2 updates only. Target revision. OTA V1 uses uint, OTA V2 & Recovery V2 uses ulong.\n"
        "                                    Highest version of device that package supports. Default is ulong/uint max value.\n"
        "      -h, --hdrrev <uint>         Recovery V2 & Recovery FB02 updates only. Header Revision. Default is 0.\n"
        "      -1, --magic1 <uint>         Recovery updates only. Magic number 1. Default is 0.\n"
        "      -2, --magic2 <uint>         Recovery updates only. Magic number 2. Default is 0.\n"
        "      -m, --minor <uint>          Recovery updates only. Minor number. Default is 0.\n"
        "      -c, --cert <ushort>         OTA V2 updates only. The number of the certificate to use (found in /etc/uks on device). Default is 0.\n"
        "                                    0 = pubdevkey01.pem, 1 = pubprodkey01.pem, 2 = pubprodkey02.pem\n"
        "      -o, --opt <uchar>           OTA V1 updates only. One byte optional data expressed as a number. Default is 0.\n"
        "      -r, --crit <uchar>          OTA V2 updates only. One byte optional data expressed as a number. Default is 0.\n"
        "      -x, --meta <str>            OTA V2 updates only. An optional string to add. Multiple \"--meta\" options supported.\n"
        "                                    Format of metastring must be: key=value\n"
        "      -a, --archive               Keep the intermediate archive.\n"
        "      -u, --unsigned              Build an unsigned & mangled userdata package.\n"
        "      -U, --userdata              Build an userdata package (can only be used with the sig update type).\n"
        "      -O, --ota                   Build a versioned OTA bundle (can only be used with the ota2 update type).\n"
        "      -C, --legacy                Emulate the behaviour of yifanlu's KindleTool regarding directories. By default, we behave like tar:\n"
        "                                    every path passed on the commandline is stored as-is in the archive. This switch changes that, and store paths\n"
        "                                    relative to the path passed on the commandline, like if we had chdir'ed into it.\n"
        "      \n"
        "  %s info <serialno>\n"
        "    Get the default root password.\n"
        "    Unless you changed your password manually, the first password shown will be the right one.\n"
        "    (The Kindle defaults to DES hashed passwords, which are truncated to 8 characters).\n"
        "    If you're looking for the recovery MMC export password, that's the second one.\n"
        "    \n"
        "  %s version\n"
        "    Show some info about this KindleTool build.\n"
        "    \n"
        "  %s help\n"
        "    Show this help screen.\n"
        "    \n"
        "notices:\n"
        "  1)  If the variable KT_WITH_UNKNOWN_DEVCODES is set in your environment (no matter the value), some device checks will be relaxed with the create command.\n"
        "  \n"
        "  2)  Kindle 4.0+ has a known bug that prevents some updates with meta-strings to run.\n"
        "  3)  Currently, even though OTA V2 supports updates that run on multiple devices, it is not possible to create an update package that will run on both the Kindle 4 (No Touch) and Kindle 5 (Touch/PW).\n"
        , prog_name, prog_name, prog_name, prog_name, prog_name, prog_name, prog_name, prog_name);
    return 0;
}

static int kindle_print_version(const char *prog_name)
{
    printf("%s (KindleTool) %s built by %s with ", prog_name, KT_VERSION, KT_USERATHOST);
#ifdef __clang__
    printf("Clang %s ", __clang_version__);
#else
    printf("GCC %s ", __VERSION__);
#endif
    printf("on %s @ %s against %s ", __DATE__, __TIME__, ARCHIVE_VERSION_STRING);
    printf("& nettle %s\n", NETTLE_VERSION);            // NOTE: This is completely custom, I couldn't find a way to get this info at buildtime in a saner way...
    return 0;
}

static int kindle_obfuscate_main(int argc, char *argv[])
{
    FILE *input;
    FILE *output;
    input = stdin;
    output = stdout;

    // Skip command
    argv++;
    argc--;
    if(argc > 1)
    {
        if((output = fopen(argv[1], "wb")) == NULL)
        {
            fprintf(stderr, "Cannot open output for writing: %s.\n", strerror(errno));
            return -1;
        }
    }
    if(argc > 0)
    {
        if((input = fopen(argv[0], "rb")) == NULL)
        {
            fprintf(stderr, "Cannot open input for reading: %s.\n", strerror(errno));
            fclose(output);
            return -1;
        }
    }
    if(munger(input, output, 0, false) < 0)
    {
        fprintf(stderr, "Cannot obfuscate.\n");
        fclose(input);
        fclose(output);
        return -1;
    }
    fclose(input);
    fclose(output);
    return 0;
}

static int kindle_deobfuscate_main(int argc, char *argv[])
{
    FILE *input;
    FILE *output;
    input = stdin;
    output = stdout;

    // Skip command
    argv++;
    argc--;
    if(argc > 1)
    {
        if((output = fopen(argv[1], "wb")) == NULL)
        {
            fprintf(stderr, "Cannot open output for writing: %s.\n", strerror(errno));
            return -1;
        }
    }
    if(argc > 0)
    {
        if((input = fopen(argv[0], "rb")) == NULL)
        {
            fprintf(stderr, "Cannot open input for reading: %s.\n", strerror(errno));
            fclose(output);
            return -1;
        }
    }
    if(demunger(input, output, 0, false) < 0)
    {
        fprintf(stderr, "Cannot deobfuscate.\n");
        fclose(input);
        fclose(output);
        return -1;
    }
    fclose(input);
    fclose(output);
    return 0;
}

static int kindle_info_main(int argc, char *argv[])
{
    char serial_no[SERIAL_NO_LENGTH+1] = {'\0'};   // Leave an extra space for the LF...
    struct md5_ctx md5;
    uint8_t digest[MD5_DIGEST_SIZE];
    char hash[BASE16_ENCODE_LENGTH(MD5_DIGEST_SIZE)];
    char device_code[4] = {'\0'};
    Device device;
    unsigned int i;

    // Skip command
    argv++;
    argc--;
    if(argc < 1)
    {
        fprintf(stderr, "Missing argument. You must pass a serial number.\n");
        return -1;
    }
    // Don't manipulate argv directly, make a copy of it first...
    strncpy(serial_no, argv[0], SERIAL_NO_LENGTH);
    if(strlen(serial_no) != SERIAL_NO_LENGTH)
    {
        fprintf(stderr, "Serial number must be composed of 16 characters (without spaces). For example: %s\n", "B0NNXXXXXXXXXXXX");
        return -1;
    }
    // Make it fully uppercase
    for(i = 0; i < SERIAL_NO_LENGTH; i++)
    {
        if(islower((int)serial_no[i]))
        {
            serial_no[i] = (char)toupper((int)serial_no[i]);
        }
    }
    // We need to terminate the string with a LF, no matter the system (probably to match the procfs usid format)...
    serial_no[SERIAL_NO_LENGTH] = '\xA';
    // The root password is based on the MD5 hash of the S/N, so, hash it first.
    md5_init(&md5);
    md5_update(&md5, SERIAL_NO_LENGTH+1, (uint8_t *)serial_no);
    md5_digest(&md5, MD5_DIGEST_SIZE, digest);
    base16_encode_update((uint8_t *)hash, MD5_DIGEST_SIZE, digest);

    // And finally, do the device dance...
    snprintf(device_code, 3, "%.*s", 2, &serial_no[2]);
    device = (Device)strtoul(device_code, NULL, 16);
    // Handle the new device ID position, since the PW3
    if(strcmp(convert_device_id(device), "Unknown") == 0)
    {
        snprintf(device_code, 4, "%.*s", 3, &serial_no[3]);
        device = (Device)strtoul(device_code, NULL, 32);
        if(strcmp(convert_device_id(device), "Unknown") == 0)
        {
            fprintf(stderr, "Unknown device!\n");
            return -1;
        }
        else
        {
            fprintf(stderr, "Device uses the new device ID scheme\n");
        }
    }
    // Handle the Wario (>= PW2) passwords while we're at it... Thanks to npoland for this one ;).
    // NOTE: Remember to check if this is still sane w/ kindle_model_sort.py when new stuff comes out!
    if(device == KindleVoyageWiFi || device == KindlePaperWhite2WiFi4GBInternational || device >= KindleVoyageWiFi3GJapan)
    {
        fprintf(stderr, "Platform is Wario or newer\n");
        fprintf(stderr, "Root PW            %s%.*s\nRecovery PW        %s%.*s\n", "fiona", 3, &hash[13], "fiona", 4, &hash[13]);
    }
    else
    {
        fprintf(stderr, "Platform is pre Wario\n");
        fprintf(stderr, "Root PW            %s%.*s\nRecovery PW        %s%.*s\n", "fiona", 3, &hash[7], "fiona", 4, &hash[7]);
    }
    // Default root passwords are DES hashed, so we only care about the first 8 chars. On the other hand,
    // the recovery MMC export option expects a 9 chars password, so, provide both...
    return 0;
}

int main(int argc, char *argv[])
{
    const char *prog_name;
    const char *cmd;

    // Do we want to use unknown devcodes? Very lame test, only check if the var actually exists, we don't check the value...
    if(getenv("KT_WITH_UNKNOWN_DEVCODES") == NULL)
        kt_with_unknown_devcodes = 0;
    else
        kt_with_unknown_devcodes = 1;

    prog_name = argv[0];
    // Discard program name for easier parsing
    argv++;
    argc--;

    if(argc > 0)
    {
        if(strncmp(argv[0], "--", 2) == 0)
        {
            // Allow our commands to be passed in longform
            argv[0] += 2;
        }
    }
    else
    {
        // No command was given, print help and die
        fprintf(stderr, "No command was specified!\n\n");
        kindle_print_help(prog_name);
        exit(1);
    }
    cmd = argv[0];

#if defined(_WIN32) && !defined(__CYGWIN__)
    // Set binary mode properly on MingW, MSVCRT craps out when freopen'ing NULL ;)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#else
    if(freopen(NULL, "rb", stdin) == NULL)
    {
        fprintf(stderr, "Cannot set stdin to binary mode: %s.\n", strerror(errno));
        return -1;
    }
    if(freopen(NULL, "wb", stdout) == NULL)
    {
        fprintf(stderr, "Cannot set stdout to binary mode: %s.\n", strerror(errno));
        return -1;
    }
#endif

    if(strncmp(cmd, "md", 2) == 0)
        return kindle_obfuscate_main(argc, argv);
    else if(strncmp(cmd, "dm", 2) == 0)
        return kindle_deobfuscate_main(argc, argv);
    else if(strncmp(cmd, "convert", 7) == 0)
        return kindle_convert_main(argc, argv);
    else if(strncmp(cmd, "extract", 7) == 0)
        return kindle_extract_main(argc, argv);
    else if(strncmp(cmd, "create", 6) == 0)
        return kindle_create_main(argc, argv);
    else if(strncmp(cmd, "info", 4) == 0)
        return kindle_info_main(argc, argv);
    else if(strncmp(cmd, "version", 7) == 0)
        return kindle_print_version(prog_name);
    else if(strncmp(cmd, "help", 4) == 0 || strncmp(cmd, "-help", 5) == 0 || strncmp(cmd, "-h", 2) == 0 || strncmp(cmd, "-?", 2) == 0 || strncmp(cmd, "/?", 2) == 0 || strncmp(cmd, "/h", 2) == 0 || strncmp(cmd, "/help", 2) == 0)
        return kindle_print_help(prog_name);
    else
    {
        fprintf(stderr, "Unknown command '%s'!\n\n", cmd);
        kindle_print_help(prog_name);
        exit(1);
    }

    return 1;
}

// kate: indent-mode cstyle; indent-width 4; replace-tabs on;
