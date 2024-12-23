#include "mp4.hpp"
#include "mp4_parser.hpp"

#include <stdexcept>
#include <fstream>
#include <cstdio>
#include <fmt/format.h>

std::vector<mp4::BoxHeader> mp4::readHeaders(const std::filesystem::path &path) {

    std::ifstream fin{ path.string().data(), std::ios::binary | std::ios::ate };
    if(!fin.good()) throw std::runtime_error{"unable to open path"};

    auto size = fin.tellg();
    fin.seekg(0);

    std::vector<BoxHeader> headers;
    BoxHeader header{};
    char sizeBuf[4];
    char nameBuf[5]{};
    char skip[1024];
    uint seekHead = 0;
    do {
        fin.read(sizeBuf, SizeBytes);
        header.size = toInt(sizeBuf);
        if(header.size == 0) break;
        fin.read(nameBuf, NameBytes);
        header.type = strToBoxName(nameBuf);
        headers.push_back(header);
        seekHead += HeaderSize + header.size;
        fin.seekg(seekHead); // skip body
    } while (fin);

//    fmt::print("file size: {}, seek head: {}\n", size, seekHead);

    return headers;
}

int mp4::toInt(const char *bytes) {
    // byte order is Big Endian
    return ((bytes[0] & 0xFF) << 24) | ((bytes[1] & 0xFF) << 16) | ((bytes[2] & 0xFF) << 8) | (bytes[3] & 0xFF);
}

mp4::Box mp4::parse(const mp4::BoxHeader &header, std::istream& stream) {
    switch (header.type) {
        case BoxType::ftyp: return parseFileType(header, stream);
        default: throw std::runtime_error{ fmt::format("{} not yet implemented!", toString(header.type))};
    }
}

mp4::BoxType mp4::strToBoxName(const std::string& str) {
    if(str == "ftyp"){
        return BoxType::ftyp;
    }
    else if(str == "mvhd") {
        return BoxType::mvhd;
    }
    else if(str == "mdat") {
        return BoxType::mdat;
    }
    else if(str == "tkhd") {
        return BoxType::tkhd;
    }
    else if(str == "elst") {
        return BoxType::elst;
    }
    else if(str == "mdhd") {
        return BoxType::mdhd;
    }

    throw std::runtime_error{ fmt::format("unknown box name {}", str) };
}

std::string mp4::toString(mp4::BoxType type) {
    switch(type) {
        case BoxType::ftyp: return "ftyp";
        case BoxType::mvhd : return "mvhd";
        case BoxType::mdat : return "mdat";
        case BoxType::tkhd : return "tkhd";
        case BoxType::elst : return "elst";
        case BoxType::mdhd : return "mdhd";
        default:
            throw std::runtime_error{ "Not yet implemented! "};
    }
}
