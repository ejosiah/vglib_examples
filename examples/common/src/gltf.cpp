#define TINYGLTF_IMPLEMENTATION
#include "gltf/gltf.hpp"

namespace gltf {

    void sync(std::shared_ptr<Model> model) {
        model->_loaded.wait();
    }

    Model::~Model() {
        if(_sourceDescriptorPool) {
            std::vector<VkDescriptorSet> sets{
                    meshDescriptorSet.u16.handle,
                    meshDescriptorSet.u32.handle,
                    materialDescriptorSet
            };
            _sourceDescriptorPool->free(sets);
        }
    }
}