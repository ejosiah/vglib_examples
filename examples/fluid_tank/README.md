# Fluid Tank

A 3D liquid demo inspired by GPU Gems 3, Chapter 30.

## What It Does

- simulates a 3D velocity and density field on the GPU
- uses advection, force application, divergence, Jacobi pressure solve, and projection
- advects density with a MacCormack-style correction step
- drops a moving spherical obstacle into a partially filled water tank
- raymarches the resulting volume in a fullscreen render pass

## Notes

This is intentionally a compact, real-time interpretation of the chapter rather than a literal port of the original Direct3D 10 sample code.
