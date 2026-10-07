#version 450
layout(local_size_x = 1024, local_size_y = 1, local_size_z = 1) in;
layout(push_constant) uniform PushConstants { uint width, height; };
layout(set = 0, binding = 0) readonly buffer Source { uint src[]; };
layout(set = 0, binding = 2) writeonly buffer Destination { uint dst[]; };
uint guest_index(uint x, uint y) {
    uint offset = 0u;
    uint mask = 1u;
    for (uint bit = 1u; bit < width || bit < height; bit <<= 1u) {
        if (bit < width) {
            if ((x & bit) != 0u) offset |= mask;
            mask <<= 1u;
        }
        if (bit < height) {
            if ((y & bit) != 0u) offset |= mask;
            mask <<= 1u;
        }
    }
    return offset;
}
void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= width * height) return;
    dst[index] = src[guest_index(index % width, index / width)];
}
