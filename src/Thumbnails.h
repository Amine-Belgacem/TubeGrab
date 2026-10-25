#pragma once
#include "Common.h"
#include <condition_variable>
#include <deque>
#include <set>
namespace cf {
struct Bitmap { HBITMAP handle = nullptr; ~Bitmap() { if (handle) DeleteObject(handle); } };
class Thumbnails {
public:
    using Sink = std::function<void(std::string, std::shared_ptr<Bitmap>)>;
    explicit Thumbnails(Sink sink);
    ~Thumbnails();
    void Request(const std::vector<std::string>& ids);
    void Stop();
private:
    void Work();
    Sink sink_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::string> pending_;
    std::set<std::string> requested_;
    std::string working_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};
}
