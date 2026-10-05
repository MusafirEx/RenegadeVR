#pragma once
namespace RenegadeVR {
class VRCore {
public:
    bool Initialize();
    void Shutdown();
    void Update();
    bool IsInitialized() const { return initialized_; }
private:
    bool initialized_ = false;
};
}
