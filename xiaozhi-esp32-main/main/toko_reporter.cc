#include "toko_reporter.h"

#include <board.h>
#include <cJSON.h>
#include <esp_log.h>

#include <memory>
#include <utility>

#if __has_include("toko_lapor_config.h")
#include "toko_lapor_config.h"
#else
#define TOKO_LAPOR_URL ""
#define TOKO_LAPOR_TOKEN ""
#endif

#define TAG "TokoReporter"

namespace {
constexpr TickType_t kHeartbeatTicks = pdMS_TO_TICKS(20000);
constexpr int kHttpTimeoutMs = 4000;
constexpr size_t kMaxQueued = 8;
constexpr size_t kMaxSpokenBytes = 700;
}  // namespace

TokoReporter& TokoReporter::GetInstance() {
    static TokoReporter instance;
    return instance;
}

void TokoReporter::Start() {
    if (task_ != nullptr || TOKO_LAPOR_URL[0] == '\0') {
        return;
    }
    xTaskCreate(
        [](void* arg) {
            static_cast<TokoReporter*>(arg)->Run();
            vTaskDelete(nullptr);
        },
        "toko_lapor", 6144, this, 2, &task_);
}

void TokoReporter::Wake() {
    if (task_ != nullptr) {
        xTaskNotifyGive(task_);
    }
}

void TokoReporter::Enqueue(std::string heard, std::string spoken) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= kMaxQueued) {
        queue_.pop_front();
    }
    queue_.push_back({std::move(heard), std::move(spoken)});
}

void TokoReporter::SetState(const char* state) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == state) {
            return;
        }
        state_ = state;
    }
    Wake();
}

void TokoReporter::ReportHeard(const std::string& text) {
    FlushSpoken();  // an interrupted answer never gets its "tts stop"
    if (text.empty()) {
        return;
    }
    Enqueue(text, "");
    Wake();
}

void TokoReporter::AddSpoken(const std::string& sentence) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (spoken_.size() + sentence.size() < kMaxSpokenBytes) {
        spoken_ += sentence;
    }
}

void TokoReporter::FlushSpoken() {
    std::string spoken;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        spoken.swap(spoken_);
    }
    if (!spoken.empty()) {
        Enqueue("", std::move(spoken));
        Wake();
    }
}

void TokoReporter::Run() {
    while (true) {
        ulTaskNotifyTake(pdTRUE, kHeartbeatTicks);
        bool sent = false;
        while (true) {
            Item item;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (queue_.empty()) {
                    break;
                }
                item = std::move(queue_.front());
                queue_.pop_front();
            }
            sent = Send(item.heard, item.spoken) || sent;
        }
        if (!sent) {
            Send("", "");  // heartbeat or state change
        }
    }
}

bool TokoReporter::Send(const std::string& heard, const std::string& spoken) {
    std::string state;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state = state_;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "token", TOKO_LAPOR_TOKEN);
    cJSON_AddStringToObject(root, "status", state.c_str());
    if (!heard.empty()) {
        cJSON_AddStringToObject(root, "dengar", heard.c_str());
    }
    if (!spoken.empty()) {
        cJSON_AddStringToObject(root, "jawab", spoken.c_str());
    }
    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == nullptr) {
        return false;
    }
    std::string body(json);
    cJSON_free(json);

    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        return false;
    }
    auto http = network->CreateHttp(0);
    http->SetTimeout(kHttpTimeoutMs);
    http->SetHeader("Content-Type", "application/json");
    http->SetContent(std::move(body));
    bool ok = false;
    if (http->Open("POST", TOKO_LAPOR_URL)) {
        auto status = http->GetStatusCode();
        ok = status && *status == 200;
        if (!ok) {
            ESP_LOGW(TAG, "Web menolak laporan: %d", status ? *status : -1);
        }
    } else {
        ESP_LOGW(TAG, "Web tidak terjangkau");
    }
    http->Close();
    return ok;
}
