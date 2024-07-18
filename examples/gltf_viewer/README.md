# GLTF Viewer

## GLTF

[glTF™ (GL Transmission Format)](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_transmission/README.md) is a royalty-free specification for the efficient transmission and loading of 3D scenes and models by applications. glTF minimizes both the size of 3D assets, and the runtime processing needed to unpack and use those assets. glTF defines an extensible, common publishing format for 3D content tools and services that streamlines authoring workflows and enables interoperable use of content across the industry.


## Supported Extensions

- KHR_lights_punctual
- KHR_materials_dispersion
- KHR_materials_ior
- KHR_materials_transmission
- KHR_materials_volume

## TODO
- fix - free descriptorSets when model is disposed of
- use unique resource each unique swapchain image
- split HDR from Tone mapping
- implement more robust Tone mapping
- implement bloom
- Allow users to use their own environment maps
- add support for other glTF extensions
- remove file dialog when path selected and display loading screen
- add support for other drawing modes (currently on default drawing mode (Triangles) supported)
- add support for multi uvs
- add support for multi scenes
- fix - app fails on exit during async loads
- make loading of meshes preserve their ordering
  - gltf meshes may be ordered, with transparent meshes last for easy blending
  - order is preserved with one worker thread
  - ond the order hand multiple worker threads do not preserve this order
  - pass in meshId when processing, use to determine position in GPU mesh buffer 
