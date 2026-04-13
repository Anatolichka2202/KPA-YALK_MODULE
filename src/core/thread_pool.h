/**
 * @file thread_pool.h
 * @brief Управление потоками для захвата данных.
 *
 * Создаёт отдельный поток, который читает данные с устройства и подаёт в декодер.
 * Поддерживает остановку и перезапуск.
 */

#ifndef ORBITA_THREAD_POOL_H
#define ORBITA_THREAD_POOL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct orbita_capture_thread orbita_capture_thread_t;

// Создание потока захвата (передаётся указатель на контекст)
orbita_capture_thread_t* orbita_capture_thread_create(void* orbita_ctx);
void orbita_capture_thread_destroy(orbita_capture_thread_t* th);

// Запуск/остановка потока
int orbita_capture_thread_start(orbita_capture_thread_t* th);
int orbita_capture_thread_stop(orbita_capture_thread_t* th);
int orbita_capture_thread_is_running(orbita_capture_thread_t* th);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_THREAD_POOL_H
