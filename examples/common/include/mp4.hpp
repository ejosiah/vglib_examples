#pragma once

#include <vector>
#include <filesystem>
#include <cinttypes>
#include <variant>
#include <fmt/format.h>
#include <istream>

namespace mp4 {

    enum class BoxType { ftyp, mvhd, mdat, tkhd, elst, mdhd };


    using uint8 = uint8_t;
    using uint = uint32_t;
    using uint64 = uint64_t;

    struct FullBox {
        uint8 version{};
        uint flag{};
    };

    struct GeneralBox {
        uint major_brand;
        uint minor_version;
        std::vector<uint> compatible_brands;
    };


    struct FileTypeBox : public GeneralBox {};



    using Box = std::variant<FileTypeBox>;

    static constexpr int NameBytes = 4;
    static constexpr int SizeBytes = 4;
    static constexpr int HeaderSize = 8;

    struct BoxHeader {
        uint size{};
        BoxType type{};
    };

    std::vector<BoxHeader> readHeaders(const std::filesystem::path& path);

    int toInt(const char* bytes);

    Box parse(const BoxHeader& header, std::istream& stream);

    BoxType strToBoxName(const std::string& str);

    std::string toString(BoxType type);
};