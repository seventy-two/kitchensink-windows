// Lick of the Day (a separate app, dailylick) as a hidden part of the
// dashboard: started with it, out of the taskbar, and hidden or shown by
// the button in the corner.
#pragma once

namespace ks {

class LickOfTheDay {
public:
    // Installed reports whether dailylick.exe is in its usual place
    // (looked for again every few seconds).
    bool Installed();
    // Running and Shown: whether it's started, and its window showing.
    bool Running() const;
    bool Shown() const;
    // Start starts it if it isn't running, staying hidden if it was hidden.
    void Start();
    // Toggle hides it if it's showing, else shows it (starting it first if
    // need be).
    void Toggle();

private:
    double checked_ = -1e9;
    bool installed_ = false;
};

}  // namespace ks
