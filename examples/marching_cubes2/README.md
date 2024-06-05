# Marching Cubes

## Description
Marching Cubes is a method for creating a polygonal surface representation of an isosurface of a 3D scalar field, it combines simplicity with high speed since it works almost entirely on lookup tables

## Implementation Details
### volume construction / loading
Level set volume generated using openvdb [vdb_tool](https://github.com/AcademySoftwareFoundation/openvdb/tree/master/openvdb_cmd/vdb_tool) mesh2ls command, the volume is then loaded using [opvdb's api](https://github.com/AcademySoftwareFoundation/openvdb)
and then copied into a Vulkan VK_IMAGE_TYPE_3D image

### Marching cube
The marching cube algorithm is implemented in a compute shader, 2 lookup tables are used, an edgeTable which maps vertices under the isosurface to intersection edges and a TriangleTable which is used to lookup isosurface triangulation configurations.
These lookup tables are bound to the shaders as uniforms. The volume containing the scalar field is bound as a 3d texture to the shader along with worldToVoxel / voxelToWorld transformations

Each compute shader invocation is centered at a marching cube instance with a cube size which is a factor of the voxel size (smaller means more details), the cube is then offset from the lower corner of world space bounds of the volume, the rest of the compute
shader implementation follows the description detailed here [Polygonising a scalar field](https://paulbourke.net/geometry/polygonise/)

### Vertex merging and Index generation (in Progress)
The marching cube algorithm has a tendency to generate a lot of duplicate vertices as marching cubes instances are independent of each other. To remove duplicate vertices and generate indexes, I'll detail 2 approaches, one based based on distance and the order based on area

#### Distance based vertex merging
A minimum distance which I call the threshold is set, the generated vertices are sorted based on their distance from the lower corner of the world bounds (AABB) of the volume, I then scan the sorted vertices compare the distance between adjacent vertices with the threshold distance
and any pair with distances less than the threshold value are grouped together.

### Area based vertex merging
This method uses spacial hashing with the gridSize being the threshold value, I generate a hash for each vertex, vertices clustered around threshold * threshold area will generate the same hash and will be put in the same hash bucket. depending on the hashing strategy vertices not in the same area
may generate the same hash (this is known as hash collision), this will be resolved during index generation.

### Index generation
The initial index generation will be based on the order of the generated vertices as the vertices are generated as a triangle triplet so I will initially have as many indexes are there are vertices.
Before I merge vertices, each vertex is tagged with its old index and after merging I scan the index list and replace duplicate vertex indexes with the vertex index of the vertex at the head of the duplicate list.
For the Area based vertex merging method, I use a distance check to make sure vertices with colliding hashes don't map to the same index

# Demo
<a href="http://www.youtube.com/watch?feature=player_embedded&v=49RZ0w_6QTk
" target="_blank"><img src="http://img.youtube.com/vi/49RZ0w_6QTk/0.jpg"
alt="IMAGE ALT TEXT HERE" width="240" height="180" border="10" /></a>