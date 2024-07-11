#include "gltf/gltf2.hpp"

namespace gltf2 {

    void sync(std::shared_ptr<Model> model) {
        model->_loaded.wait();
    }
}