#include "media.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include "local.h"
#include "net.h"

namespace ks {

namespace {

using namespace winrt::Windows::Media::Control;
using winrt::Windows::Storage::Streams::Buffer;
using winrt::Windows::Storage::Streams::InputStreamOptions;

double seconds(winrt::Windows::Foundation::TimeSpan t) { return std::chrono::duration<double>(t).count(); }

double unixSeconds(winrt::clock::time_point t) {
    return std::chrono::duration<double>(winrt::clock::to_sys(t).time_since_epoch()).count();
}

// readCover reads a session's cover art into bytes for the image cache.
std::string readCover(const GlobalSystemMediaTransportControlsSessionMediaProperties& props) {
    auto ref = props.Thumbnail();
    if (!ref) return {};
    // Null checks as in run(): a closing session can hand back nulls.
    auto stream = ref.OpenReadAsync().get();
    if (!stream) return {};
    uint32_t size = uint32_t(std::min<uint64_t>(stream.Size(), 8 << 20));
    Buffer buf(size);
    auto read = stream.ReadAsync(buf, size, InputStreamOptions::None).get();
    if (!read) return {};
    return std::string(reinterpret_cast<const char*>(read.data()), read.Length());
}

}  // namespace

MediaWatcher::MediaWatcher(std::function<void()> wake, ImageCache* images) : wake_(std::move(wake)), images_(images) {
    thread_ = std::thread([this] { run(); });
}

MediaWatcher::~MediaWatcher() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
}

void MediaWatcher::Want() {
    unsigned long long before = wanted_.exchange(GetTickCount64());
    if (GetTickCount64() - before > 3000) {
        { std::lock_guard<std::mutex> lock(mu_); }
        cv_.notify_all();
    }
}

bool MediaWatcher::Take(Snapshot& out) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!fresh_) return false;
    out = latest_;
    fresh_ = false;
    return true;
}

void MediaWatcher::Command(MediaCommand c, const std::string& id) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        command_ = c;
        commandId_ = id;
    }
    cv_.notify_all();
}

void MediaWatcher::run() {
    // WinRT calls block with .get(), which needs a multithreaded apartment.
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    // Covers by track (title, artist and album), so each is read once.
    std::map<std::string, std::string> covers;
    int coverCount = 0;

    std::unique_lock<std::mutex> lock(mu_);
    while (!stop_) {
        if (GetTickCount64() - wanted_.load() >= 3000 && command_ == MediaCommand::None) {
            cv_.wait(lock, [&] {
                return stop_ || command_ != MediaCommand::None || GetTickCount64() - wanted_.load() < 3000;
            });
            continue;
        }
        MediaCommand command = command_;
        std::string commandId = commandId_;
        command_ = MediaCommand::None;
        lock.unlock();

        // Every session worth showing, with the session itself for commands.
        struct Shown {
            NowPlaying np;
            GlobalSystemMediaTransportControlsSession session{nullptr};
            std::string appID;
            bool current = false;
        };
        std::vector<Shown> shown;
        std::map<std::string, std::string> seenCovers;
        try {
            if (!manager) manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            auto current = manager.GetCurrentSession();
            for (const auto& session : manager.GetSessions()) {
                // Any of these reads can come back null rather than
                // throwing when an app closes its session mid-read, so
                // each is checked; a session that fails is left out.
                try {
                    if (!session) continue;
                    auto props = session.TryGetMediaPropertiesAsync().get();
                    auto playback = session.GetPlaybackInfo();
                    if (!props || !playback) continue;
                    auto controls = playback.Controls();
                    auto timeline = session.GetTimelineProperties();
                    if (!controls || !timeline) continue;
                    Shown s;
                    s.session = session;
                    s.appID = Narrow(session.SourceAppUserModelId());
                    s.current = current && current == session;
                    NowPlaying& np = s.np;
                    np.title = Narrow(props.Title());
                    np.artist = Narrow(props.Artist());
                    if (np.artist.empty()) np.artist = Narrow(props.AlbumArtist());
                    np.album = Narrow(props.AlbumTitle());
                    np.app = AppName(s.appID);
                    np.media.playing =
                        playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                    np.media.canPrevious = controls.IsPreviousEnabled();
                    np.media.canNext = controls.IsNextEnabled();
                    np.media.canToggle = controls.IsPlayPauseToggleEnabled();
                    double start = seconds(timeline.StartTime());
                    np.media.duration = std::max(0.0, seconds(timeline.EndTime()) - start);
                    np.media.position = seconds(timeline.Position()) - start;
                    np.media.asOf = unixSeconds(timeline.LastUpdatedTime());
                    std::string track = np.title + "\n" + np.artist + "\n" + np.album;
                    np.media.id = s.appID + "\n" + track;
                    // Every session Windows lists shows, paused or not,
                    // like its own media pop-up.
                    np.active = true;

                    // The cover only changes with the track. Each gets a
                    // new key, since the cache keeps images by key.
                    if (auto c = seenCovers.find(track); c != seenCovers.end()) {
                        np.image = c->second;
                    } else if (auto c = covers.find(track); c != covers.end()) {
                        np.image = seenCovers[track] = c->second;
                    } else {
                        std::string bytes = readCover(props);
                        if (!bytes.empty()) {
                            np.image = "media:" + std::to_string(++coverCount);
                            images_->Provide(np.image, std::move(bytes));
                        }
                        seenCovers[track] = np.image;
                    }
                    shown.push_back(std::move(s));
                } catch (const winrt::hresult_error&) {
                    // That app closing mid-read; the next reading will be fine.
                }
            }
        } catch (const winrt::hresult_error&) {
            manager = nullptr;
        }
        covers = std::move(seenCovers);
        // Playing ones first, Windows' current one first among equals.
        std::stable_sort(shown.begin(), shown.end(), [](const Shown& a, const Shown& b) {
            if (a.np.media.playing != b.np.media.playing) return a.np.media.playing;
            return a.current && !b.current;
        });

        // The command goes to the session it was for: the same track, or
        // failing that (it's moved on) the same app, or the first shown.
        if (command != MediaCommand::None && !shown.empty()) {
            const Shown* target = &shown[0];
            std::string app = commandId.substr(0, commandId.find('\n'));
            for (const auto& s : shown)
                if (s.np.media.id == commandId) {
                    target = &s;
                    break;
                }
            if (target->np.media.id != commandId)
                for (const auto& s : shown)
                    if (s.appID == app) {
                        target = &s;
                        break;
                    }
            try {
                switch (command) {
                case MediaCommand::Previous: target->session.TrySkipPreviousAsync().get(); break;
                case MediaCommand::TogglePlay: target->session.TryTogglePlayPauseAsync().get(); break;
                case MediaCommand::Next: target->session.TrySkipNextAsync().get(); break;
                case MediaCommand::None: break;
                }
            } catch (const winrt::hresult_error&) {
            }
        }

        std::vector<NowPlaying> list;
        for (auto& s : shown) list.push_back(std::move(s.np));
        Snapshot snap = MediaSnapshot(list, int64_t(std::time(nullptr)));

        lock.lock();
        latest_ = std::move(snap);
        fresh_ = true;
        lock.unlock();
        wake_();
        lock.lock();
        // Read again in a second, or at once for a command; and shortly
        // after a command, since apps take a moment to update.
        cv_.wait_for(lock, std::chrono::milliseconds(command != MediaCommand::None ? 250 : 1000),
                     [&] { return stop_ || command_ != MediaCommand::None; });
    }
    lock.unlock();
    manager = nullptr;
    winrt::uninit_apartment();
}

}  // namespace ks
