// Http (net.h) over libcurl, for Linux.
#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <mutex>

#include "net.h"

namespace ks {

namespace {

std::once_flag curlInit;

// Easy is a curl handle with the settings every request shares: as the
// Windows client's WinHTTP session, 10s to connect, TLS 1.2 or later,
// compressed responses accepted, redirects followed.
struct Easy {
    CURL* h = nullptr;
    curl_slist* headers = nullptr;
    char error[CURL_ERROR_SIZE] = {};

    Easy(const std::string& method, const std::string& url, const std::string& token, const std::string& body) {
        std::call_once(curlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
        h = curl_easy_init();
        if (!h) return;
        curl_easy_setopt(h, CURLOPT_URL, url.c_str());
        curl_easy_setopt(h, CURLOPT_USERAGENT, "kitchensink-linux/1.0");
        curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
        curl_easy_setopt(h, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
        curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(h, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(h, CURLOPT_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(h, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);  // safe from several threads
        curl_easy_setopt(h, CURLOPT_ERRORBUFFER, error);
        if (!token.empty()) headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
        if (!body.empty()) {
            headers = curl_slist_append(headers, "Content-Type: application/json");
            curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, (long)body.size());
            curl_easy_setopt(h, CURLOPT_COPYPOSTFIELDS, body.c_str());
        }
        if (method != "GET") curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, method.c_str());
        if (method == "POST" && body.empty()) {
            curl_easy_setopt(h, CURLOPT_POST, 1L);
            curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, 0L);
        }
        if (headers) curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);
    }
    ~Easy() {
        if (h) curl_easy_cleanup(h);
        if (headers) curl_slist_free_all(headers);
    }

    unsigned long status() const {
        long code = 0;
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
        return (unsigned long)code;
    }

    // describe is a failed transfer's error, plainly.
    std::string describe(CURLcode rc) const {
        if (rc == CURLE_OPERATION_TIMEDOUT) return "timed out";
        return error[0] ? std::string(error) : std::string(curl_easy_strerror(rc));
    }
};

struct Collect {
    std::string* body;
    size_t max;
    bool tooLarge = false;
};

size_t collect(char* data, size_t size, size_t n, void* user) {
    auto* c = static_cast<Collect*>(user);
    size_t len = size * n;
    if (c->body->size() + len > c->max) {
        c->tooLarge = true;
        return 0;  // aborts the transfer
    }
    c->body->append(data, len);
    return len;
}

struct StreamState {
    Easy* easy;
    const std::function<bool(std::string_view)>* onData;
    std::atomic<bool>* cancel;
    std::chrono::milliseconds idleLimit;
    std::chrono::steady_clock::time_point lastData = std::chrono::steady_clock::now();
    unsigned long status = 0;
    bool stopped = false;
    bool idle = false;
};

size_t streamWrite(char* data, size_t size, size_t n, void* user) {
    auto* s = static_cast<StreamState*>(user);
    s->lastData = std::chrono::steady_clock::now();
    if (s->status == 0) s->status = s->easy->status();
    if (s->status != 200) return 0;  // reported after the transfer
    if (!(*s->onData)(std::string_view(data, size * n))) {
        s->stopped = true;
        return 0;
    }
    return size * n;
}

// streamProgress runs about once a second even while nothing arrives, so a
// Cancel from another thread ends the stream promptly, as does a connection
// that has sent nothing (not even the server's heartbeat) for too long.
int streamProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* s = static_cast<StreamState*>(user);
    if (s->cancel->load()) return 1;
    if (std::chrono::steady_clock::now() - s->lastData > s->idleLimit) {
        s->idle = true;
        return 1;
    }
    return 0;
}

}  // namespace

Http::Http() = default;
Http::~Http() = default;

HttpResult Http::Request(const std::string& method, const std::string& url, const std::string& token,
                         size_t maxBytes, const std::string& body) {
    HttpResult r;
    Easy e(method, url, token, body);
    if (!e.h) {
        r.error = "couldn't start a request";
        return r;
    }
    // As WinHTTP's 15s send and 30s receive timeouts: give up on a server
    // that stops sending for 30s.
    curl_easy_setopt(e.h, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(e.h, CURLOPT_LOW_SPEED_TIME, 30L);
    Collect c{&r.body, maxBytes};
    curl_easy_setopt(e.h, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(e.h, CURLOPT_WRITEDATA, &c);
    CURLcode rc = curl_easy_perform(e.h);
    r.status = e.status();
    if (c.tooLarge) {
        r.error = "response too large";
    } else if (rc != CURLE_OK) {
        r.error = e.describe(rc);
        if (r.status == 0) r.body.clear();
    }
    if (r.error.empty() && !r.ok()) r.error = "server returned " + std::to_string(r.status);
    return r;
}

std::string Http::Stream(const std::string& url, const std::string& token, unsigned long receiveTimeoutMs,
                         const std::function<bool(std::string_view)>& onData, unsigned long* status) {
    *status = 0;
    cancel_ = false;
    Easy e("GET", url, token, "");
    if (!e.h) return "couldn't start a request";
    StreamState s{&e, &onData, &cancel_, std::chrono::milliseconds(receiveTimeoutMs)};
    curl_easy_setopt(e.h, CURLOPT_WRITEFUNCTION, streamWrite);
    curl_easy_setopt(e.h, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(e.h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(e.h, CURLOPT_XFERINFOFUNCTION, streamProgress);
    curl_easy_setopt(e.h, CURLOPT_XFERINFODATA, &s);
    CURLcode rc = curl_easy_perform(e.h);
    *status = s.status ? s.status : e.status();
    if (*status != 0 && *status != 200) {
        if (*status == 401) return "the server rejected the token";
        return "server returned " + std::to_string(*status);
    }
    if (s.idle) return "timed out";
    if (s.stopped) return "stopped";
    if (rc == CURLE_ABORTED_BY_CALLBACK) return "stopped";
    if (rc == CURLE_OK) return "the server closed the stream";
    return e.describe(rc);
}

void Http::Cancel() { cancel_ = true; }

}  // namespace ks
