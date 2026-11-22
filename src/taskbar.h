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
    // Hide also takes away its shape (an empty window region), so it can't
    // be seen when Explorer shows it for a moment.
    // Restore shows it again, auto-hiding only if it did before; if it
    // wasn't hidden by this run, it mends one a crash left hidden.
    void Restore(bool autoHide);
    // Check hides it again if Explorer has shown it (or restarted). Cheap
    // enough to call every few seconds.
    void Check();

private:
    void watch();
    void unwatch();
    bool hidden_ = false;
    void* hook_ = nullptr;    // the WinEvent hook on Explorer's windows being shown
    unsigned long hookPid_ = 0;  // Explorer's process, which changes when it restarts
};

}  // namespace ks
