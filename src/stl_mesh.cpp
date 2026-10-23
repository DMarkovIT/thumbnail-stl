#include "stl_mesh.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <string_view>

namespace {
constexpr size_t kBinaryHeaderSize = 84;
constexpr size_t kBinaryTriangleSize = 50;

bool LooksLikeAscii(const uint8_t* data, size_t size) {
    const size_t probeSize = std::min<size_t>(size, 4096);
    if (std::memchr(data, 0, probeSize) != nullptr) return false;
    const std::string_view head(reinterpret_cast<const char*>(data), probeSize);
    return head.find("solid") != std::string_view::npos;
}

bool ParseBinary(const uint8_t* data, size_t size, std::vector<float>& out) {
    if (size < kBinaryHeaderSize) return false;

    uint32_t count = 0;
    std::memcpy(&count, data + 80, sizeof(count));
    const size_t available = (size - kBinaryHeaderSize) / kBinaryTriangleSize;
    if (count == 0 || available == 0) return false;
    count = static_cast<uint32_t>(std::min<size_t>(count, available));

    out.resize(static_cast<size_t>(count) * 9);
    const uint8_t* p = data + kBinaryHeaderSize;
    for (uint32_t i = 0; i < count; ++i, p += kBinaryTriangleSize) {
        std::memcpy(&out[static_cast<size_t>(i) * 9], p + 12, 36);
    }
    return true;
}

float ReadNumber(const char*& p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',')) ++p;
    if (p < end && *p == '+') ++p;

    float value = 0.0f;
    const auto result = std::from_chars(p, end, value);
    if (result.ec == std::errc()) {
        p = result.ptr;
        return value;
    }

    while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') ++p;
    return 0.0f;
}

bool ParseAscii(const uint8_t* data, size_t size, std::vector<float>& out) {
    const char* begin = reinterpret_cast<const char*>(data);
    const char* end = begin + size;
    const char* keyword = "vertex";
    const char* keywordEnd = keyword + 6;

    std::vector<float> positions;
    const char* p = begin;
    while (p < end) {
        const char* found = std::search(p, end, keyword, keywordEnd);
        if (found == end) break;
        p = found + 6;
        for (int i = 0; i < 3; ++i) positions.push_back(ReadNumber(p, end));
    }

    const size_t triangleCount = positions.size() / 9;
    if (triangleCount == 0) return false;
    out.assign(positions.begin(), positions.begin() + static_cast<ptrdiff_t>(triangleCount * 9));
    return true;
}
}

bool LoadStl(const void* data, size_t size, uint32_t maxTriangles, Mesh& out, std::wstring& error) {
    out = Mesh{};
    error.clear();

    if (!data || size < 15) {
        error = L"Файл пуст или слишком мал";
        return false;
    }

    const auto* bytes = static_cast<const uint8_t*>(data);
    const bool ascii = LooksLikeAscii(bytes, size);

    std::vector<float> raw;
    bool parsed = ascii ? ParseAscii(bytes, size, raw) : ParseBinary(bytes, size, raw);
    if (!parsed) parsed = ascii ? ParseBinary(bytes, size, raw) : ParseAscii(bytes, size, raw);
    if (!parsed || raw.empty()) {
        error = L"Не удалось разобрать STL: нет треугольников";
        return false;
    }

    const size_t total = raw.size() / 9;

    double minValue[3] = {1e300, 1e300, 1e300};
    double maxValue[3] = {-1e300, -1e300, -1e300};
    for (size_t t = 0; t < total; ++t) {
        const float* tri = &raw[t * 9];
        for (int v = 0; v < 3; ++v) {
            for (int k = 0; k < 3; ++k) {
                const double c = tri[v * 3 + k];
                minValue[k] = std::min(minValue[k], c);
                maxValue[k] = std::max(maxValue[k], c);
            }
        }
    }

    double center[3];
    double halfExtent[3];
    for (int k = 0; k < 3; ++k) {
        center[k] = (minValue[k] + maxValue[k]) * 0.5;
        halfExtent[k] = (maxValue[k] - minValue[k]) * 0.5;
    }

    size_t step = 1;
    if (maxTriangles > 0 && total > maxTriangles) {
        step = (total + maxTriangles - 1) / maxTriangles;
    }

    out.vertices.reserve((total / step + 1) * 18);
    for (size_t t = 0; t < total; t += step) {
        const float* tri = &raw[t * 9];
        const double ax = tri[0] - center[0];
        const double ay = tri[1] - center[1];
        const double az = tri[2] - center[2];
        const double bx = tri[3] - center[0];
        const double by = tri[4] - center[1];
        const double bz = tri[5] - center[2];
        const double cx = tri[6] - center[0];
        const double cy = tri[7] - center[1];
        const double cz = tri[8] - center[2];

        const double ux = bx - ax, uy = by - ay, uz = bz - az;
        const double vx = cx - ax, vy = cy - ay, vz = cz - az;

        double nx = uy * vz - uz * vy;
        double ny = uz * vx - ux * vz;
        double nz = ux * vy - uy * vx;
        const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (length < 1e-12) continue;
        nx /= length;
        ny /= length;
        nz /= length;

        const float n[3] = {static_cast<float>(nx), static_cast<float>(ny), static_cast<float>(nz)};
        const double positions[9] = {ax, ay, az, bx, by, bz, cx, cy, cz};
        for (int v = 0; v < 3; ++v) {
            out.vertices.push_back(static_cast<float>(positions[v * 3 + 0]));
            out.vertices.push_back(static_cast<float>(positions[v * 3 + 1]));
            out.vertices.push_back(static_cast<float>(positions[v * 3 + 2]));
            out.vertices.push_back(n[0]);
            out.vertices.push_back(n[1]);
            out.vertices.push_back(n[2]);
        }
    }

    out.triangleCount = static_cast<uint32_t>(out.vertices.size() / 18);
    if (out.triangleCount == 0) {
        error = L"В файле нет пригодных треугольников";
        return false;
    }

    for (int k = 0; k < 3; ++k) {
        out.bounds[k] = static_cast<float>(-halfExtent[k]);
        out.bounds[k + 3] = static_cast<float>(halfExtent[k]);
    }
    out.radius = static_cast<float>(
        std::sqrt(halfExtent[0] * halfExtent[0] + halfExtent[1] * halfExtent[1] +
                  halfExtent[2] * halfExtent[2]));
    if (out.radius < 1e-6f) out.radius = 1.0f;

    return true;
}
