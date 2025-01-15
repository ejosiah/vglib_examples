#include <glm/gtc/matrix_transform.hpp>
#include <fmt/format.h>
#include <glm/gtc/matrix_access.hpp>

#include <algorithm>
#include <vector>
#include <glm/glm.hpp>
#include "vol_loader.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <ranges>
#include <span>
#include <vector>
#include <algorithm>


#include "cbt/cbt.hpp"
//#define SPLIT
constexpr auto maxDepth = 20UL;
constexpr auto iniMaxDepth = maxDepth;

int main() {
    auto cbt = cbt::Tree(maxDepth, iniMaxDepth);

    fmt::print("leaf nodes:{}\n", cbt.nodeCount());
    // execute the update callback in parallel
    cbt.update([](cbt::Tree& cbt, const cbt::Node& node, const std::any* userData) {
        if ((node.id & 1) == 0) {
#ifdef SPLIT
            cbt.split(node);
#else
            cbt.merge(node);
#endif
        }
        }, nullptr);

//    cbt_Update(cbt, &UpdateCallback, NULL);
    auto heap = cbt.getHeap();
    fmt::print("leaf nodes:{}\n", cbt.nodeCount());

}
