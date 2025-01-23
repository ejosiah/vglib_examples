# GLTF Viewer

## GLTF

[glTF™ (GL Transmission Format)](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_transmission/README.md) is a royalty-free specification for the efficient transmission and loading of 3D scenes and models by applications. glTF minimizes both the size of 3D assets, and the runtime processing needed to unpack and use those assets. glTF defines an extensible, common publishing format for 3D content tools and services that streamlines authoring workflows and enables interoperable use of content across the industry.


## Supported Extensions

- KHR_lights_punctual
- KHR_materials_dispersion
- KHR_materials_emissive_strength
- KHR_materials_ior
- KHR_materials_transmission
- KHR_materials_volume
- KHR_texture_transform
- KHR_materials_unlit
- KHR_materials_sheen
- KHR_materials_anisotropy
- KHR_materials_specular
- KHR_materials_iridescence

## TODO
- use GL_EXT_scalar_block_layout extension to avoid padding structs for alignment
- add animation support
- implement more robust Tone mapping
- implement path traced renderer
- Allow users to use their own environment maps
- add support for other glTF extensions
- remove file dialog when path selected and display loading screen
- add support for other drawing modes (currently on default drawing mode (Triangles) supported)
- add support for multi uvs
- add support for multi scenes
-  use a different concurrent data structure for task queues
- make loading of meshes preserve their ordering
  - gltf meshes may be ordered, with transparent meshes last for easy blending
  - order is preserved with one worker thread
  - ond the order hand multiple worker threads do not preserve this order
  - pass in meshId when processing, use to determine position in GPU mesh buffer 
### Fixes
- loading of interleaved buffer
- fork tinygltf loader and remove upfront image loads as this a serialization point
- descriptorPool memory leak (imageDescriptorSet not freed after use)
- free up texture binding points used by a model when its disposed
- app fails on exit during async loads
- KHR_materials_anisotropy tangent rotation
