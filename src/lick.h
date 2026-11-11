// Separate apps that live as hidden parts of the dashboard, out of the
// taskbar and hidden or shown by a title-bar button: Lick of the Day
// (dailylick, started with the dashboard) and Mango (started on first
// click). Each is found by its window class, the same as its name, and
// installed as %LOCALAPPDATA%\Programs\<name>\<name>.exe.
#pragma once

namespace ks {

class HiddenApp {
public:
    explicit HiddenApp(const wchar_t* name) : name_(name) {}

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
    const wchar_t* name_;
    double checked_ = -1e9;
    bool installed_ = false;
};

}  // namespace ks
