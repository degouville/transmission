// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

/* thanks amc1! */

#include <algorithm>
#include <array>
#include <cctype> /* isprint() */
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "transmission.h"
#include "clients.h"
#include "utils.h"

using namespace std::literals; // "foo"sv

namespace
{

constexpr std::pair<char*, size_t> buf_append(char* buf, size_t buflen, char ch)
{
    if (buflen >= 2)
    {
        *buf++ = ch;
    }
    *buf = '\0';
    return { buf, buflen - 1 };
}

constexpr std::pair<char*, size_t> buf_append(char* buf, size_t buflen, std::string_view name)
{
    auto const len = std::min(buflen - 1, std::size(name));
    for (size_t i = 0; i < len; ++i)
    {
        *buf++ = name[i];
    }
    *buf = '\0';
    return { buf, buflen - len };
}

constexpr std::pair<char*, size_t> buf_append(char* buf, size_t buflen, int n)
{
    auto mybuf = std::array<char, 32>{};
    auto const end = std::data(mybuf) + std::size(mybuf);
    auto constexpr base = 10;
    auto* ptr = end;

    while ((n / base) > 0)
    {
        *--ptr = char('0' + (n % base));
        n /= base;
    }
    *--ptr = char('0' + (n % base));

    return buf_append(buf, buflen, std::string_view(ptr, end - ptr));
}

template<typename T, typename... ArgTypes>
constexpr std::pair<char*, size_t> buf_append(char* buf, size_t buflen, T t, ArgTypes... args)
{
    std::tie(buf, buflen) = buf_append(buf, buflen, t);
    return buf_append(buf, buflen, args...);
}

constexpr std::string_view charint(char ch)
{
    switch (ch)
    {
    case '0':
        return "0"sv;
    case '1':
        return "1"sv;
    case '2':
        return "2"sv;
    case '3':
        return "3"sv;
    case '4':
        return "4"sv;
    case '5':
        return "5"sv;
    case '6':
        return "6"sv;
    case '7':
        return "7"sv;
    case '8':
        return "8"sv;
    case '9':
        return "9"sv;
    case 'a':
    case 'A':
        return "10"sv;
    case 'b':
    case 'B':
        return "11"sv;
    case 'c':
    case 'C':
        return "12"sv;
    case 'd':
    case 'D':
        return "13"sv;
    case 'e':
    case 'E':
        return "14"sv;
    case 'f':
    case 'F':
        return "15"sv;
    case 'g':
    case 'G':
        return "16"sv;
    case 'h':
    case 'H':
        return "17"sv;
    case 'i':
    case 'I':
        return "18"sv;
    case 'j':
    case 'J':
        return "19"sv;
    case 'k':
    case 'K':
        return "20"sv;
    case 'l':
    case 'L':
        return "21"sv;
    case 'm':
    case 'M':
        return "22"sv;
    case 'n':
    case 'N':
        return "23"sv;
    case 'o':
    case 'O':
        return "24"sv;
    case 'p':
    case 'P':
        return "25"sv;
    case 'q':
    case 'Q':
        return "26"sv;
    case 'r':
    case 'R':
        return "27"sv;
    case 's':
    case 'S':
        return "28"sv;
    case 't':
    case 'T':
        return "29"sv;
    case 'u':
    case 'U':
        return "30"sv;
    case 'v':
    case 'V':
        return "31"sv;
    case 'w':
    case 'W':
        return "32"sv;
    case 'x':
    case 'X':
        return "33"sv;
    case 'y':
    case 'Y':
        return "34"sv;
    case 'z':
    case 'Z':
        return "35"sv;
    default:
        return "x"sv;
    }
}

int strint(char const* pch, int span, int base = 10)
{
    auto sv = std::string_view{ pch, static_cast<size_t>(span) };
    return tr_parseNum<int>(sv, nullptr, base).value_or(0);
}

constexpr std::string_view getMnemonicEnd(uint8_t ch)
{
    switch (ch)
    {
    case 'b':
    case 'B':
        return " (Beta)"sv;

    case 'd':
        return " (Debug)"sv;

    case 'x':
    case 'X':
    case 'Z':
        return " (Dev)"sv;

    default:
        return ""sv;
    }
}

void two_major_two_minor_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    std::tie(buf, buflen) = buf_append(buf, buflen, name, ' ', strint(&id[3], 2), '.');
    *fmt::format_to_n(buf, buflen - 1, FMT_STRING("{:02d}"), strint(&id[5], 2)).out = '\0';
}

// Shad0w with his experimental BitTorrent implementation and BitTornado
// introduced peer ids that begin with a character which is``T`` in the
// case of BitTornado followed by up to five ascii characters for version
// number, padded with dashes if less than 5, followed by ---. The ascii
// characters denoting version are limited to the following characters:
// 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.-
// For example: 'S58B-----'... for Shadow's 5.8.11
std::optional<size_t> get_shad0w_int(char ch)
{
    auto constexpr Str = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.-"sv;

    if (auto const pos = Str.find(ch); pos != std::string_view::npos)
    {
        return pos;
    }

    return {};
}

bool decodeShad0wClient(char* buf, size_t buflen, std::string_view in)
{
    auto const* const buf_in = buf;

    auto peer_id = std::string_view{ std::data(in), 9 };

    if (std::size(peer_id) != 9 || peer_id[6] != '-' || peer_id[7] != '-' || peer_id[8] != '-')
    {
        return false;
    }
    while (!std::empty(peer_id) && peer_id.back() == '-')
    {
        peer_id.remove_suffix(1);
    }
    auto vals = std::vector<int>{};
    while (std::size(peer_id) > 1)
    {
        auto const num = get_shad0w_int(peer_id.back());
        if (!num)
        {
            return false;
        }
        vals.push_back(*num);
        peer_id.remove_suffix(1);
    }

    auto name = std::string_view{};
    switch (peer_id.front())
    {
    case 'A':
        name = "ABC"sv;
        break;
    case 'O':
        name = "Osprey";
        break;
    case 'Q':
        name = "BTQueue";
        break;
    case 'R':
        name = "Tribler";
        break;
    case 'S':
        name = "Shad0w";
        break;
    case 'T':
        name = "BitTornado";
        break;
    case 'U':
        name = "UPnP NAT Bit Torrent";
        break;
    default:
        return false;
    }

    std::tie(buf, buflen) = buf_append(buf, buflen, name, ' ');
    std::for_each(
        std::rbegin(vals),
        std::rend(vals),
        [&buf, &buflen](int num) { std::tie(buf, buflen) = buf_append(buf, buflen, num, '.'); });
    if (buf > buf_in)
    {
        buf[-1] = '\0'; // remove trailing '.'
    }
    return true;
}

bool decodeBitCometClient(char* buf, size_t buflen, std::string_view peer_id)
{
    // BitComet produces peer ids that consists of four ASCII characters exbc,
    // followed by two bytes x and y, followed by random characters. The version
    // number is x in decimal before the decimal point and y as two decimal
    // digits after the decimal point. BitLord uses the same scheme, but adds
    // LORD after the version bytes. An unofficial patch for BitComet once
    // replaced exbc with FUTB. The encoding for BitComet Peer IDs changed
    // to Azureus-style as of BitComet version 0.59.
    auto mod = std::string_view{};
    auto const lead = std::string_view{ std::data(peer_id), std::min(std::size(peer_id), size_t{ 4 }) };
    if (lead == "exbc")
    {
        mod = "";
    }
    else if (lead == "FUTB")
    {
        mod = "(Solidox Mod) ";
    }
    else if (lead == "xUTB"sv)
    {
        mod = "(Mod 2) ";
    }
    else
    {
        return false;
    }

    bool const is_bitlord = std::string_view(std::data(peer_id) + 6, 4) == "LORD"sv;
    auto const name = is_bitlord ? "BitLord"sv : "BitComet"sv;
    int const major = uint8_t(peer_id[4]);
    int const minor = uint8_t(peer_id[5]);

    std::tie(buf, buflen) = buf_append(buf, buflen, name, ' ', mod, major, '.');
    *fmt::format_to_n(buf, buflen - 1, FMT_STRING("{:02d}"), minor).out = '\0';
    return true;
}

using format_func = void (*)(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id);

constexpr void three_digit_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', charint(id[3]), '.', charint(id[4]), '.', charint(id[5]));
}

constexpr void four_digit_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', charint(id[3]), '.', charint(id[4]), '.', charint(id[5]), '.', charint(id[6]));
}

void no_version_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t /*id*/)
{
    buf_append(buf, buflen, name);
}

// specific clients

constexpr void amazon_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', id[3], '.', id[5], '.', id[7]);
}

constexpr void aria2_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    if (id[4] == '-' && id[6] == '-' && id[8] == '-')
    {
        buf_append(buf, buflen, name, ' ', id[3], '.', id[5], '.', id[7]);
    }
    else if (id[4] == '-' && id[7] == '-' && id[9] == '-')
    {
        buf_append(buf, buflen, name, ' ', id[3], '.', id[5], id[6], '.', id[8]);
    }
    else
    {
        buf_append(buf, buflen, name);
    }
}

constexpr void bitbuddy_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', id[3], '.', id[4], id[5], id[6]);
}

constexpr void bitlord_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', id[3], '.', id[4], '.', id[5], '-', std::string_view(&id[6], 3));
}

constexpr void bitrocket_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', id[3], '.', id[4], ' ', '(', id[5], id[6], ')');
}

void bittorrent_dna_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    std::tie(buf, buflen) = buf_append(buf, buflen, name, ' ');
    *fmt::format_to_n(buf, buflen - 1, FMT_STRING("{:d}.{:d}.{:d}"), strint(&id[3], 2), strint(&id[5], 2), strint(&id[7], 2))
         .out = '\0';
}

void bits_on_wheels_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    // Bits on Wheels uses the pattern -BOWxxx-yyyyyyyyyyyy, where y is random
    // (uppercase letters) and x depends on the version.
    // Version 1.0.6 has xxx = A0C.

    if (strncmp(&id[4], "A0B", 3) == 0)
    {
        buf_append(buf, buflen, name, " 1.0.5"sv);
    }
    else if (strncmp(&id[4], "A0C", 3) == 0)
    {
        buf_append(buf, buflen, name, " 1.0.6"sv);
    }
    else
    {
        buf_append(buf, buflen, name, ' ', id[4], '.', id[5], '.', id[6]);
    }
}

constexpr void blizzard_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', int(id[3] + 1), int(id[4]));
}

constexpr void btpd_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', std::string_view(&id[5], 3));
}

constexpr void burst_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', id[5], '.', id[7], '.', id[9]);
}

constexpr void ctorrent_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', charint(id[3]), '.', charint(id[4]), '.', id[5], id[6]);
}

constexpr void folx_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', charint(id[3]), '.', 'x');
}

constexpr void ktorrent_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    if (id[5] == 'D')
    {
        buf_append(buf, buflen, name, ' ', charint(id[3]), '.', charint(id[4]), " Dev "sv, charint(id[6]));
    }
    else if (id[5] == 'R')
    {
        buf_append(buf, buflen, name, ' ', charint(id[3]), '.', charint(id[4]), " RC "sv, charint(id[6]));
    }
    else
    {
        three_digit_formatter(buf, buflen, name, id);
    }
}

constexpr void mainline_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    // Queen Bee uses Bram`s new style:
    // Q1-0-0-- or Q1-10-0- followed by random bytes.

    if (id[4] == '-' && id[6] == '-') // Mx-y-z--
    {
        buf_append(buf, buflen, name, ' ', id[1], '.', id[3], '.', id[5]);
    }
    else if (id[5] == '-') // Mx-yy-z-
    {
        buf_append(buf, buflen, name, ' ', id[1], '.', id[3], id[4], '.', id[6]);
    }
    else
    {
        buf_append(buf, buflen, name);
    }
}

constexpr void mediaget_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', charint(id[3]), '.', charint(id[4]));
}

constexpr void mldonkey_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    // MLdonkey use the following peer_id scheme: the first characters are
    // -ML followed by a dotted version then a - followed by randomness.
    // e.g. -ML2.7.2-kgjjfkd
    buf_append(buf, buflen, name, ' ', std::string_view(&id[3], 5));
}

constexpr void opera_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    // Opera 8 previews and Opera 9.x releases use the following peer_id
    // scheme: The first two characters are OP and the next four digits equal
    // the build number. All following characters are random lowercase
    // hexadecimal digits.
    buf_append(buf, buflen, name, ' ', std::string_view(&id[2], 4));
}

constexpr void picotorrent_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', charint(id[3]), '.', id[4], id[5], '.', charint(id[6]));
}

constexpr void plus_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', id[4], '.', id[5], id[6]);
}

constexpr void qvod_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', charint(id[4]), '.', charint(id[5]), '.', charint(id[6]), '.', charint(id[7]));
}

void transmission_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    std::tie(buf, buflen) = buf_append(buf, buflen, name, ' ');

    if (strncmp(&id[3], "000", 3) == 0) // very old client style: -TR0006- is 0.6
    {
        *fmt::format_to_n(buf, buflen - 1, FMT_STRING("0.{:c}"), id[6]).out = '\0';
    }
    else if (strncmp(&id[3], "00", 2) == 0) // previous client style: -TR0072- is 0.72
    {
        *fmt::format_to_n(buf, buflen - 1, FMT_STRING("0.{:02d}"), strint(&id[5], 2)).out = '\0';
    }
    else // current client style: -TR111Z- is 1.11+ */
    {
        *fmt::format_to_n(
             buf,
             buflen - 1,
             FMT_STRING("{:d}.{:02d}{:s}"),
             strint(&id[3], 1),
             strint(&id[4], 2),
             (id[6] == 'Z' || id[6] == 'X') ? "+" : "")
             .out = '\0';
    }
}

void utorrent_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    if (id[7] == '-')
    {
        buf_append(
            buf,
            buflen,
            name,
            ' ',
            strint(&id[3], 1, 16),
            '.',
            strint(&id[4], 1, 16),
            '.',
            strint(&id[5], 1, 16),
            getMnemonicEnd(id[6]));
    }
    else // uTorrent replaces the trailing dash with an extra digit for longer version numbers
    {
        buf_append(
            buf,
            buflen,
            name,
            ' ',
            strint(&id[3], 1, 16),
            '.',
            strint(&id[4], 1, 16),
            '.',
            strint(&id[5], 2, 10),
            getMnemonicEnd(id[7]));
    }
}

constexpr void xbt_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', id[3], '.', id[4], '.', id[5], getMnemonicEnd(id[6]));
}

constexpr void xfplay_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    if (id[6] == '0')
    {
        three_digit_formatter(buf, buflen, name, id);
    }
    else
    {
        buf_append(buf, buflen, name, ' ', id[3], '.', id[4], '.', id[5], id[6]);
    }
}

void xtorrent_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    std::tie(buf, buflen) = buf_append(buf, buflen, name, ' ', charint(id[3]), '.', charint(id[4]), " ("sv);
    *fmt::format_to_n(buf, buflen - 1, FMT_STRING("{:d}"), strint(&id[5], 2)).out = '\0';
}

struct Client
{
    std::string_view begins_with;
    std::string_view name;
    format_func formatter;
};

auto constexpr Clients = std::array<Client, 129>{ {
    { "-AD", "Advanced Download Manager", three_digit_formatter },
    { "-AG", "Ares", four_digit_formatter },
    { "-AR", "Arctic", four_digit_formatter },
    { "-AT", "Artemis", four_digit_formatter },
    { "-AV", "Avicora", four_digit_formatter },
    { "-AX", "BitPump", two_major_two_minor_formatter },
    { "-AZ", "Azureus / Vuze", four_digit_formatter },
    { "-A~", "Ares", three_digit_formatter },
    { "-BB", "BitBuddy", bitbuddy_formatter },
    { "-BC", "BitComet", two_major_two_minor_formatter },
    { "-BE", "BitTorrent SDK", four_digit_formatter },
    { "-BF", "BitFlu", no_version_formatter },
    { "-BG", "BTGetit", four_digit_formatter },
    { "-BH", "BitZilla", four_digit_formatter },
    { "-BI", "BiglyBT", four_digit_formatter },
    { "-BL", "BitLord", bitlord_formatter },
    { "-BM", "BitMagnet", four_digit_formatter },
    { "-BN", "Baidu Netdisk", no_version_formatter },
    { "-BOW", "Bits on Wheels", bits_on_wheels_formatter },
    { "-BP", "BitTorrent Pro (Azureus + Spyware)", four_digit_formatter },
    { "-BR", "BitRocket", bitrocket_formatter },
    { "-BS", "BTSlave", four_digit_formatter },
    { "-BT", "BitTorrent", utorrent_formatter },
    { "-BW", "BitWombat", four_digit_formatter },
    { "-BX", "BittorrentX", four_digit_formatter },
    { "-CD", "Enhanced CTorrent", two_major_two_minor_formatter },
    { "-CT", "CTorrent", ctorrent_formatter },
    { "-DE", "Deluge", four_digit_formatter },
    { "-DP", "Propagate Data Client", four_digit_formatter },
    { "-EB", "EBit", four_digit_formatter },
    { "-ES", "Electric Sheep", three_digit_formatter },
    { "-FC", "FileCroc", four_digit_formatter },
    { "-FD", "Free Download Manager", three_digit_formatter },
    { "-FG", "FlashGet", two_major_two_minor_formatter },
    { "-FL", "Folx", folx_formatter },
    { "-FT", "FoxTorrent/RedSwoosh", four_digit_formatter },
    { "-FW", "FrostWire", three_digit_formatter },
    { "-FX", "Freebox", four_digit_formatter },
    { "-G3", "G3 Torrent", no_version_formatter },
    { "-GR", "GetRight", four_digit_formatter },
    { "-GS", "GSTorrent", four_digit_formatter },
    { "-HK", "Hekate", four_digit_formatter },
    { "-HL", "Halite", three_digit_formatter },
    { "-HN", "Hydranode", four_digit_formatter },
    { "-KG", "KGet", four_digit_formatter },
    { "-KT", "KTorrent", ktorrent_formatter },
    { "-LC", "LeechCraft", four_digit_formatter },
    { "-LH", "LH-ABC", four_digit_formatter },
    { "-LP", "Lphant", two_major_two_minor_formatter },
    { "-LT", "libtorrent (Rasterbar)", three_digit_formatter },
    { "-LW", "LimeWire", no_version_formatter },
    { "-Lr", "LibreTorrent", three_digit_formatter },
    { "-MG", "MediaGet", mediaget_formatter },
    { "-MK", "Meerkat", four_digit_formatter },
    { "-ML", "MLDonkey", mldonkey_formatter },
    { "-MO", "MonoTorrent", four_digit_formatter },
    { "-MP", "MooPolice", three_digit_formatter },
    { "-MR", "Miro", four_digit_formatter },
    { "-MT", "Moonlight", four_digit_formatter },
    { "-NE", "BT Next Evolution", four_digit_formatter },
    { "-NX", "Net Transport", four_digit_formatter },
    { "-OS", "OneSwarm", four_digit_formatter },
    { "-OT", "OmegaTorrent", four_digit_formatter },
    { "-PD", "Pando", four_digit_formatter },
    { "-PI", "PicoTorrent", picotorrent_formatter },
    { "-QD", "QQDownload", four_digit_formatter },
    { "-QT", "QT 4 Torrent example", four_digit_formatter },
    { "-RS", "Rufus", four_digit_formatter },
    { "-RT", "Retriever", four_digit_formatter },
    { "-RZ", "RezTorrent", four_digit_formatter },
    { "-SB", "~Swiftbit", four_digit_formatter },
    { "-SD", "Thunder", four_digit_formatter },
    { "-SM", "SoMud", four_digit_formatter },
    { "-SP", "BitSpirit", three_digit_formatter },
    { "-SS", "SwarmScope", four_digit_formatter },
    { "-ST", "SymTorrent", four_digit_formatter },
    { "-SZ", "Shareaza", four_digit_formatter },
    { "-S~", "Shareaza", four_digit_formatter },
    { "-TN", "Torrent .NET", four_digit_formatter },
    { "-TR", "Transmission", transmission_formatter },
    { "-TS", "Torrentstorm", four_digit_formatter },
    { "-TT", "TuoTu", four_digit_formatter },
    { "-UE", "\xc2\xb5Torrent Embedded", utorrent_formatter },
    { "-UL", "uLeecher!", four_digit_formatter },
    { "-UM", "\xc2\xb5Torrent Mac", utorrent_formatter },
    { "-UT", "\xc2\xb5Torrent", utorrent_formatter },
    { "-UW", "\xc2\xb5Torrent Web", utorrent_formatter },
    { "-VG", "Vagaa", four_digit_formatter },
    { "-WS", "HTTP Seed", no_version_formatter },
    { "-WT", "BitLet", four_digit_formatter },
    { "-WT-", "BitLet", no_version_formatter },
    { "-WW", "WebTorrent", four_digit_formatter },
    { "-WY", "FireTorrent", four_digit_formatter },
    { "-XC", "Xtorrent", xtorrent_formatter },
    { "-XF", "Xfplay", xfplay_formatter },
    { "-XL", "Xunlei", four_digit_formatter },
    { "-XS", "XSwifter", four_digit_formatter },
    { "-XT", "XanTorrent", four_digit_formatter },
    { "-XX", "Xtorrent", xtorrent_formatter },
    { "-ZO", "Zona", four_digit_formatter },
    { "-ZT", "Zip Torrent", four_digit_formatter },
    { "-bk", "BitKitten (libtorrent)", four_digit_formatter },
    { "-lt", "libTorrent (Rakshasa)", three_digit_formatter },
    { "-pb", "pbTorrent", three_digit_formatter },
    { "-qB", "qBittorrent", three_digit_formatter },
    { "-st", "SharkTorrent", four_digit_formatter },
    { "10-------", "JVtorrent", no_version_formatter },
    { "346-", "TorrentTopia", no_version_formatter },
    { "A2", "aria2", aria2_formatter },
    { "AZ2500BT", "BitTyrant (Azureus Mod)", no_version_formatter },
    { "BLZ", "Blizzard Downloader", blizzard_formatter },
    { "DNA", "BitTorrent DNA", bittorrent_dna_formatter },
    { "LIME", "Limewire", no_version_formatter },
    { "M", "BitTorrent", mainline_formatter },
    { "Mbrst", "burst!", burst_formatter },
    { "OP", "Opera", opera_formatter },
    { "Pando", "Pando", no_version_formatter },
    { "Plus", "Plus!", plus_formatter },
    { "Q", "Queen Bee", mainline_formatter },
    { "QVOD", "QVOD", qvod_formatter },
    { "S3", "Amazon S3", amazon_formatter },
    { "TIX", "Tixati", two_major_two_minor_formatter },
    { "XBT", "XBT Client", xbt_formatter },
    { "a00---0", "Swarmy", no_version_formatter },
    { "a02---0", "Swarmy", no_version_formatter },
    { "aria2-", "aria2", no_version_formatter },
    { "btpd", "BT Protocol Daemon", btpd_formatter },
    { "eX", "eXeem", no_version_formatter },
    { "martini", "Martini Man", no_version_formatter },
} };

} // namespace

char* tr_clientForId(char* buf, size_t buflen, tr_peer_id_t peer_id)
{
    *buf = '\0';

    auto const key = std::string_view{ std::data(peer_id), std::size(peer_id) };

    if (decodeShad0wClient(buf, buflen, key) || decodeBitCometClient(buf, buflen, key))
    {
        return buf;
    }

    if (peer_id[0] == '\0' && peer_id[2] == 'B' && peer_id[3] == 'S')
    {
        *fmt::format_to_n(buf, buflen - 1, FMT_STRING("BitSpirit {:d}"), peer_id[1] == '\0' ? 1 : int(peer_id[1])).out = '\0';
        return buf;
    }

    struct Compare
    {
        bool operator()(std::string_view const& key, Client const& client) const
        {
            auto const key_lhs = std::string_view{ std::data(key), std::min(std::size(key), std::size(client.begins_with)) };
            return key_lhs < client.begins_with;
        }
        bool operator()(Client const& client, std::string_view const& key) const
        {
            auto const key_lhs = std::string_view{ std::data(key), std::min(std::size(key), std::size(client.begins_with)) };
            return client.begins_with < key_lhs;
        }
    };

    auto eq = std::equal_range(std::begin(Clients), std::end(Clients), key, Compare{});
    if (eq.first != std::end(Clients) && eq.first != eq.second)
    {
        eq.first->formatter(buf, buflen, eq.first->name, peer_id);
        return buf;
    }

    // no match
    if (tr_str_is_empty(buf))
    {
        auto out = std::array<char, 32>{};
        char* walk = std::data(out);
        char const* const begin = walk;
        char const* const end = begin + std::size(out);

        for (size_t i = 0; i < 8; ++i)
        {
            char const c = peer_id[i];

            if (isprint((unsigned char)c) != 0)
            {
                *walk++ = c;
            }
            else
            {
                walk = fmt::format_to_n(walk, end - walk - 1, FMT_STRING("%{:02X}"), static_cast<unsigned char>(c)).out;
            }
        }

        buf_append(buf, buflen, std::string_view(begin, walk - begin));
    }

    return buf;
}
