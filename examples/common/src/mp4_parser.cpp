#include "mp4_parser.hpp"

mp4::FileTypeBox mp4::parseFileType(mp4::BoxHeader header, std::istream& stream) {
    FileTypeBox fileType{};
    char buf[4];

    stream.read(buf, 4);
    fileType.major_brand = toInt(buf);

    stream.read(buf, 4);
    fileType.minor_version = toInt(buf);

    auto cbSize = (header.size - 8)/sizeof(int);
    fileType.compatible_brands.resize(cbSize);
    for(int i = 0; i < cbSize; ++i) {
        stream.read(buf, 4);
        fileType.compatible_brands[i] = toInt(buf);
    }

    return fileType;
}
