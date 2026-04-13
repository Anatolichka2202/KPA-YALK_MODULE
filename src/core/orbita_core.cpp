#include "orbita_core.h"
#include "data_pool.h"
#include "thread_pool.h"
#include "logger.h"

#include <cstdlib>
#include <cstring>

struct orbita_context {
    // Устройства
    int device_type; // 0=none, 1=e2010, 2=limesdr
    orbita_e2010_config_t e2010_cfg;
    orbita_limesdr_config_t limesdr_cfg;
    orbita_visa_config_t visa_cfg;
    int invert_signal;

    // Адреса и декодер
    void* address_manager;   // orbita_address_manager_t*
    void* decoder;           // orbita_decoder_t*
    int group_size;
    int cycle_size;

    // Данные
    orbita_data_pool_t* data_pool;

    // TLM
    void* tlm_writer;        // orbita_tlm_writer_t*

    // Потоки
    void* capture_thread;    // внутренний поток захвата
    int is_running;

    // Статистика
    int phrase_errors;
    int group_errors;
    int total_phrases;
    int total_groups;

    // Скрипты
    void* script_engine;     // orbita_script_engine_t*
};

// --------------------------------------------------------------
// Реализация функций (пока заглушки)
// --------------------------------------------------------------

orbita_context_t* orbita_context_create(void) {
    orbita_context_t* ctx = (orbita_context_t*)calloc(1, sizeof(orbita_context_t));
    if (!ctx) return nullptr;
    ctx->data_pool = orbita_data_pool_create();
    if (!ctx->data_pool) {
        free(ctx);
        return nullptr;
    }
    LOG_INFO("Context created");
    return ctx;
}

void orbita_context_destroy(orbita_context_t* ctx) {
    if (!ctx) return;
    orbita_context_stop(ctx);
    orbita_data_pool_destroy(ctx->data_pool);
    // TODO: освободить остальные ресурсы
    free(ctx);
    LOG_INFO("Context destroyed");
}

int orbita_context_set_device_e2010(orbita_context_t* ctx, int channel, double sample_rate_khz) {
    if (!ctx) return -1;
    ctx->device_type = 1;
    ctx->e2010_cfg.channel = channel;
    ctx->e2010_cfg.sample_rate_khz = sample_rate_khz;
    ctx->e2010_cfg.input_range_mv = 3000;
    LOG_INFO("E20-10 device set: channel=%d, rate=%.1f kHz", channel, sample_rate_khz);
    return 0;
}

int orbita_context_set_device_limesdr(orbita_context_t* ctx, const char* params) {
    if (!ctx || !params) return -1;
    ctx->device_type = 2;
    // TODO: разобрать params
    LOG_INFO("LimeSDR device set with params: %s", params);
    return 0;
}

int orbita_context_set_voltmeter_visa(orbita_context_t* ctx, const char* resource) {
    if (!ctx || !resource) return -1;
    strncpy(ctx->visa_cfg.resource, resource, sizeof(ctx->visa_cfg.resource)-1);
    ctx->visa_cfg.timeout_ms = 1000;
    LOG_INFO("VISA voltmeter set: %s", resource);
    return 0;
}

int orbita_context_set_channels(orbita_context_t* ctx, const orbita_channel_desc_t* channels, int count) {
    if (!ctx || !channels || count <= 0) return -1;
    // TODO: передать в address_manager
    LOG_INFO("Set %d channels", count);
    return 0;
}

int orbita_context_set_channels_from_file(orbita_context_t* ctx, const char* filename) {
    if (!ctx || !filename) return -1;
    // TODO: загрузить через address_manager
    LOG_INFO("Load channels from file: %s", filename);
    return 0;
}

int orbita_context_start(orbita_context_t* ctx) {
    if (!ctx) return -1;
    if (ctx->is_running) return -2;
    // TODO: запустить поток захвата
    ctx->is_running = 1;
    LOG_INFO("Capture started");
    return 0;
}

int orbita_context_stop(orbita_context_t* ctx) {
    if (!ctx) return -1;
    if (!ctx->is_running) return 0;
    // TODO: остановить поток
    ctx->is_running = 0;
    LOG_INFO("Capture stopped");
    return 0;
}

int orbita_context_set_invert_signal(orbita_context_t* ctx, int invert) {
    if (!ctx) return -1;
    ctx->invert_signal = invert ? 1 : 0;
    LOG_INFO("Invert signal set to %d", ctx->invert_signal);
    return 0;
}

int orbita_context_start_recording(orbita_context_t* ctx, const char* filename) {
    if (!ctx || !filename) return -1;
    // TODO: создать TLM writer
    LOG_INFO("Recording started to %s", filename);
    return 0;
}

int orbita_context_stop_recording(orbita_context_t* ctx) {
    if (!ctx) return -1;
    // TODO: закрыть TLM writer
    LOG_INFO("Recording stopped");
    return 0;
}

int orbita_context_wait_for_data(orbita_context_t* ctx, int timeout_ms) {
    if (!ctx) return -1;
    // TODO: ожидание новых данных от декодера
    // Пока заглушка – имитируем успех
    return 0;
}

double orbita_context_get_analog(orbita_context_t* ctx, int idx) {
    if (!ctx || !ctx->data_pool) return 0.0;
    return orbita_data_pool_get_analog(ctx->data_pool, idx);
}

int orbita_context_get_contact(orbita_context_t* ctx, int idx) {
    if (!ctx || !ctx->data_pool) return 0;
    return orbita_data_pool_get_contact(ctx->data_pool, idx);
}

int orbita_context_get_fast(orbita_context_t* ctx, int idx) {
    if (!ctx || !ctx->data_pool) return 0;
    return orbita_data_pool_get_fast(ctx->data_pool, idx);
}

int orbita_context_get_temperature(orbita_context_t* ctx, int idx) {
    if (!ctx || !ctx->data_pool) return 0;
    return orbita_data_pool_get_temperature(ctx->data_pool, idx);
}

int orbita_context_get_bus(orbita_context_t* ctx, int idx) {
    if (!ctx || !ctx->data_pool) return 0;
    return orbita_data_pool_get_bus(ctx->data_pool, idx);
}

int orbita_context_get_phrase_error_percent(orbita_context_t* ctx) {
    if (!ctx || ctx->total_phrases == 0) return 0;
    return (ctx->phrase_errors * 100) / ctx->total_phrases;
}

int orbita_context_get_group_error_percent(orbita_context_t* ctx) {
    if (!ctx || ctx->total_groups == 0) return 0;
    return (ctx->group_errors * 100) / ctx->total_groups;
}

int orbita_context_load_script(orbita_context_t* ctx, const char* script_path) {
    if (!ctx || !script_path) return -1;
    // TODO: создать script_engine и загрузить
    LOG_INFO("Script loaded: %s", script_path);
    return 0;
}

int orbita_context_run_script_function(orbita_context_t* ctx, const char* func_name, const char* args_json, char** result_json) {
    if (!ctx || !func_name) return -1;
    // TODO: выполнить функцию
    if (result_json) *result_json = strdup("{}");
    return 0;
}
