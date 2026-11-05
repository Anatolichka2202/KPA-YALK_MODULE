#include "miltech/orbita_sample_simulator.h"
#include "orbita_stand/sample_source.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace miltech::integration {
namespace {

std::vector<int16_t> makeSyntheticSamples()
{
    // Формируем синтетические слова с маркерами фразы, цикла и кадра.
    // Проверка интеграции пропускает эти отсчёты через штатный декодер M16.
    constexpr int firstMarker[8] = {0, 1, 1, 1, 1, 0, 0, 0};
    constexpr int secondMarker[8] = {0, 1, 0, 0, 1, 1, 0, 1};
    std::vector<uint8_t> bits;
    bits.reserve(128 * 12 * 16 * 12);
    for (int phrase = 0; phrase < 128 * 12; ++phrase) {
        const int pair = (phrase % 128) / 2;
        int cycleBit = pair >= 56 ? (141 >> (63 - pair)) & 1 : 0;
        if (pair == 7 && phrase < 256) cycleBit = 1;
        for (int word = 0; word < 16; ++word) {
            int marker = 0;
            if (word % 2 == 0) {
                marker = phrase % 2 == 0 ? firstMarker[word / 2] : secondMarker[word / 2];
                if (phrase % 2 && word == 0) marker = cycleBit;
            }
            const int syntheticWord = (marker << 11) | 123;
            for (int bit = 11; bit >= 0; --bit)
                bits.push_back(static_cast<uint8_t>((syntheticWord >> bit) & 1));
        }
    }

    std::vector<int16_t> samples;
    samples.reserve(bits.size() * 4);
    for (std::size_t begin = 0; begin < bits.size();) {
        std::size_t end = begin + 1;
        while (end < bits.size() && bits[end] == bits[begin]) ++end;
        const auto count = static_cast<std::size_t>(
            std::llround(static_cast<double>(end - begin) * (10.0 / 3.145728)));
        samples.insert(samples.end(), count, bits[begin] ? 1000 : -1000);
        begin = end;
    }
    return samples;
}

class OrbitaSampleSimulator final : public orbita::stand::ISampleSource {
public:
    ~OrbitaSampleSimulator() override { close(); }
    bool open() override { open_ = true; return true; }
    void close() noexcept override { stop(); open_ = false; }
    bool isOpen() const noexcept override { return open_.load(); }

    bool start() override
    {
        if (!open_) return false;
        if (running_.exchange(true)) return true;
        if (worker_.joinable()) worker_.join();
        try {
            worker_ = std::thread([this] { produce(); });
        } catch (...) {
            running_ = false;
            throw;
        }
        return true;
    }

    void stop() noexcept override
    {
        running_ = false;
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    bool isRunning() const noexcept override { return running_.load(); }
    void setSamplesCallback(SamplesCallback callback) override
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        samplesCallback_ = std::move(callback);
    }
    void setErrorCallback(ErrorCallback callback) override
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        errorCallback_ = std::move(callback);
    }

private:
    void produce() noexcept
    {
        try {
            const auto samples = makeSyntheticSamples();
            constexpr std::size_t chunkSize = 120000;
            std::size_t offset = 0;
            while (running_) {
                const auto end = std::min(samples.size(), offset + chunkSize);
                SamplesCallback callback;
                {
                    std::lock_guard<std::mutex> lock(callbackMutex_);
                    callback = samplesCallback_;
                }
                if (callback) callback({samples.begin() + offset, samples.begin() + end});
                offset = end == samples.size() ? 0 : end;
                std::unique_lock<std::mutex> lock(waitMutex_);
                wake_.wait_for(lock, std::chrono::milliseconds(25), [this] { return !running_; });
            }
        } catch (const std::exception& error) {
            ErrorCallback callback;
            {
                std::lock_guard<std::mutex> lock(callbackMutex_);
                callback = errorCallback_;
            }
            running_ = false;
            if (callback) { try { callback(error.what()); } catch (...) {} }
        } catch (...) {
            running_ = false;
        }
    }

    std::atomic_bool open_{false};
    std::atomic_bool running_{false};
    std::mutex callbackMutex_;
    SamplesCallback samplesCallback_;
    ErrorCallback errorCallback_;
    std::mutex waitMutex_;
    std::condition_variable wake_;
    std::thread worker_;
};

} // namespace

std::unique_ptr<orbita::stand::ISampleSource> createOrbitaSampleSimulator()
{
    return std::make_unique<OrbitaSampleSimulator>();
}

} // namespace miltech::integration
