#define TINYGLTF_IMPLEMENTATION
#include "gltf/gltf.hpp"

namespace gltf {

    void sync(std::shared_ptr<Model> model) {
        model->_loaded.wait();
    }
}