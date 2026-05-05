# Whitecaps

Vulkan port of Jonathan Dupuy and Eric Bruneton's OpenGL whitecaps demo.

This example keeps the original whitecaps program structure: the Vulkan shaders
mirror the original `spectrum`, `init`, `fftx`, `ffty`, `variances`,
`whitecap_precompute`, `ocean`, `sky`, `clouds`, and `skymap` stages, with GLSL
updated for Vulkan layout qualifiers and explicit descriptor bindings.
