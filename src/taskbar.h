// Hides the Windows taskbar while the dashboard stands in for it, and puts
// it back afterwards. Windows only; does nothing elsewhere.
#pragma once

namespace ks {

class Taskbar {
public:
    // Hide sets the taskbar to auto-hide (so windows get the whole screen)
    // and hides it on every monitor, so it doesn't slide up at the edge.
    // wasAutoHide is set to whether it auto-hid before, the first time.
    void Hide(bool* wasAutoHide);
    // Restore shows it again, auto-hiding only if it did before.
    void Restore(bool autoHide);
    // Check hides it again if Explorer has shown it (or restarted). Cheap
    // enough to call every few seconds.
    void Check();
    bool Hidden() const { return hidden_; }

private:
    bool hidden_ = false;
};

}  // namespace ks
