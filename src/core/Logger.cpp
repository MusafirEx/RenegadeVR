#include "RenegadeVR/Logger.h"
#include <windows.h>
#include <fstream>
#include <mutex>
namespace { std::ofstream g_log; std::mutex g_logMutex; }
namespace RenegadeVR {
void InitializeLogger() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_log.is_open()) g_log.open("RenegadeVR.log", std::ios::out | std::ios::app);
}
void ShutdownLogger() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_log.is_open()) { g_log.flush(); g_log.close(); }
}
void Log(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_log.is_open()) { g_log << message << std::endl; g_log.flush(); }
    OutputDebugStringA(("[RenegadeVR] " + message + "\n").c_str());
}
}
