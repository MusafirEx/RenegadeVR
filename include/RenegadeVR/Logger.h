#pragma once
#include <string>
namespace RenegadeVR {
void InitializeLogger();
void ShutdownLogger();
void Log(const std::string& message);
}
