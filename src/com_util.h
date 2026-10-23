#pragma once

#include <windows.h>
#include <d3d11.h>
#include <objidl.h>

#include <cstdint>
#include <string>
#include <vector>

bool ReadStreamAll(IStream* stream, std::vector<uint8_t>& out, std::wstring& error);
bool ReadFileAll(const wchar_t* path, std::vector<uint8_t>& out, std::wstring& error);

bool CreateD3D11Device(ID3D11Device** device, ID3D11DeviceContext** context);

std::wstring GuidToString(REFGUID guid);

HBITMAP CreateHBitmapFromBgra(const std::vector<uint8_t>& bgra, UINT width, UINT height);

void DownsampleBgraHalf(const std::vector<uint8_t>& source, UINT sourceWidth, UINT sourceHeight,
                        std::vector<uint8_t>& destination, UINT& destinationWidth, UINT& destinationHeight);

void DebugLog(const wchar_t* message);

COLORREF SystemThemeBackgroundColor();

