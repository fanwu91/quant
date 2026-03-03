#ifndef __ASYNC_LOGGER_H__
#define __ASYNC_LOGGER_H__

#include "Constants.h"

#include <algorithm>
#include <fstream>
#include <thread>


class AsyncLogger {
public:
    // Public members and methods for AsyncLogger
    // binary mode & append mode
    explicit AsyncLogger(std::string file_path)
        : buffer_(new char[LOG_BUFFER_SIZE]), file_(file_path, std::ios::binary | std::ios::app) {
        if (!file_.is_open()) {
            throw std::runtime_error("Failed to open log file");
        }
        running_.store(true, std::memory_order_release);
        flush_thread_ = std::thread(&AsyncLogger::flush, this);
    }

    ~AsyncLogger() noexcept {
        running_.store(false, std::memory_order_release);
        if (flush_thread_.joinable()) {
            flush_thread_.join();
        }
        flush_remaining_logs();
        delete[] buffer_;
    }

    void log(const char* msg, size_t len) {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count() / 1000.0;

        char header[32];
        size_t header_len = std::snprintf(header, sizeof(header), "%.3f", ms);
        size_t total_len = header_len + len + 1; // 1 for std::endl;

        size_t current_write = write_index_.load(std::memory_order_relaxed);
        size_t current_read = read_index_.load(std::memory_order_acquire);

        if (current_write + total_len >= LOG_BUFFER_SIZE) {
            if (total_len > current_read) {
                return; // discard log
            }
            buffer_[current_write] = '\0';
            write_index_.store(0, std::memory_order_release); // start from index zero
            current_write = 0;
        }

        memcpy(buffer_ + current_write, header, header_len);
        memcpy(buffer_ + current_write + header_len, msg, len);
        buffer_[current_write + header_len + len] = '\n';

        write_index_.store(current_write + total_len, std::memory_order_release);
    }

    void log(const std::string& msg) {
        log(msg.c_str(), msg.size());
    }

private:
    // Private members and methods for AsyncLogger
    char* buffer_;
    std::atomic<bool> running_ { false };
    std::thread flush_thread_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> read_index_ {0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> write_index_ {0};
    std::ofstream file_;

    void flush() {
        while (running_.load(std::memory_order_acquire)) {
            size_t current_read = read_index_.load(std::memory_order_relaxed);
            size_t current_write = write_index_.load(std::memory_order_relaxed);

            if (current_write > current_read) {
                file_.write(buffer_ + current_read, current_write - current_read);
                file_.flush();
                read_index_.store(current_write, std::memory_order_release);
            } else if (current_write < current_read) {
                file_.write(buffer_ + current_read, LOG_BUFFER_SIZE - current_read);
                file_.flush();
                read_index_.store(0, std::memory_order_release);
            } else {
                std::this_thread::yield();
            }
        }
    }

    void flush_remaining_logs() {
        size_t current_read = read_index_.load(std::memory_order_relaxed);
        size_t current_write = write_index_.load(std::memory_order_relaxed);

        if (current_read < current_write) {
            file_.write(buffer_ + current_read, current_write - current_read);
        } else if (current_read > current_write) {
            file_.write(buffer_ + current_read, LOG_BUFFER_SIZE - current_read);
            file_.write(buffer_, current_write);
        }

        file_.flush();
    }
};

#endif
