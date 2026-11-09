#ifndef TOKO_REPORTER_H
#define TOKO_REPORTER_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <deque>
#include <mutex>
#include <string>

// Sends the robot's real state and conversations to the shop website, so the dashboard shows
// what the robot actually does. All network work runs in its own task; every other method only
// queues data and is safe to call from any task, including the audio and protocol ones.
class TokoReporter {
public:
    static TokoReporter& GetInstance();

    void Start();
    void SetState(const char* state);
    void ReportHeard(const std::string& text);
    void AddSpoken(const std::string& sentence);
    void FlushSpoken();

private:
    TokoReporter() = default;

    void Run();
    bool Send(const std::string& heard, const std::string& spoken);
    void Enqueue(std::string heard, std::string spoken);
    void Wake();

    struct Item {
        std::string heard;
        std::string spoken;
    };

    std::mutex mutex_;
    std::deque<Item> queue_;
    std::string state_ = "menyala";
    std::string spoken_;
    TaskHandle_t task_ = nullptr;
};

#endif  // TOKO_REPORTER_H
