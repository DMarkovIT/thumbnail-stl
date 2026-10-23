#pragma once

#include <string>

bool RegisterShellExtension(const std::wstring& dllPath, std::wstring& log);
bool UnregisterShellExtension(std::wstring& log);
bool IsShellExtensionRegistered(std::wstring& log);
