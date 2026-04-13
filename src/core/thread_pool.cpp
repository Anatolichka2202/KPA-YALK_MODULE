#include "thread_pool.h"
#include "logger.h"
#include <cstdlib>
struct orbita_capture_thread {
    void* orbita_ctx;
    int running;
    // TODO: добавить HANDLE thread (Windows) или pthread_t
};

orbita_capture_thread_t* orbita_capture_thread_create(void* orbita_ctx) {
    orbita_capture_thread_t* th = (orbita_capture_thread_t*)calloc(1, sizeof(orbita_capture_thread_t));
    if (!th) return nullptr;
    th->orbita_ctx = orbita_ctx;
    LOG_INFO("Capture thread created");
    return th;
}

void orbita_capture_thread_destroy(orbita_capture_thread_t* th) {
    if (!th) return;
    orbita_capture_thread_stop(th);
    free(th);
    LOG_INFO("Capture thread destroyed");
}

int orbita_capture_thread_start(orbita_capture_thread_t* th) {
    if (!th) return -1;
    if (th->running) return -2;
    // TODO: запустить поток
    th->running = 1;
    LOG_INFO("Capture thread started");
    return 0;
}

int orbita_capture_thread_stop(orbita_capture_thread_t* th) {
    if (!th) return -1;
    if (!th->running) return 0;
    // TODO: остановить поток
    th->running = 0;
    LOG_INFO("Capture thread stopped");
    return 0;
}

int orbita_capture_thread_is_running(orbita_capture_thread_t* th) {
    return th ? th->running : 0;
}
