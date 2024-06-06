#pragma once

#include "mp4.hpp"
#include <iostream>

namespace mp4 {

    FileTypeBox parseFileType(BoxHeader header, std::istream& stream);
};
