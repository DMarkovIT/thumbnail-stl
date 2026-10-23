#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Mesh {
    std::vector<float> vertices;
    uint32_t triangleCount = 0;

    float bounds[6]{};

    float radius = 1.0f;
};

bool LoadStl(const void* data, size_t size, uint32_t maxTriangles, Mesh& out, std::wstring& error);
