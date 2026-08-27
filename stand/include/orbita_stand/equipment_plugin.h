#ifndef ORBITA_STAND_EQUIPMENT_PLUGIN_H
#define ORBITA_STAND_EQUIPMENT_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORBITA_EQUIPMENT_ABI_V1 0x00010000u

#if defined(_WIN32)
#  define ORBITA_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define ORBITA_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

typedef enum orbita_plugin_status_v1 {
    ORBITA_PLUGIN_OK = 0,
    ORBITA_PLUGIN_INVALID_ARGUMENT = 1,
    ORBITA_PLUGIN_NOT_SUPPORTED = 2,
    ORBITA_PLUGIN_NOT_READY = 3,
    ORBITA_PLUGIN_IO_ERROR = 4,
    ORBITA_PLUGIN_BUFFER_TOO_SMALL = 5,
    ORBITA_PLUGIN_CANCELLED = 6,
    ORBITA_PLUGIN_INTERNAL_ERROR = 7
} orbita_plugin_status_v1;

typedef struct orbita_plugin_buffer_v1 {
    char* data;
    size_t capacity;
    size_t size;
} orbita_plugin_buffer_v1;

typedef struct orbita_equipment_api_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    const char* plugin_id;
    const char* display_name_utf8;
    /* Список стабильных capability-id через точку с запятой. */
    const char* capabilities_utf8;

    orbita_plugin_status_v1 (*create)(
        const char* instance_id_utf8,
        const char* config_utf8,
        void** instance,
        orbita_plugin_buffer_v1* diagnostic);
    void (*destroy)(void* instance);
    orbita_plugin_status_v1 (*invoke)(
        void* instance,
        const char* capability_utf8,
        const char* operation_utf8,
        const char* request_utf8,
        orbita_plugin_buffer_v1* response);
    void (*cancel)(void* instance);
    void (*safe_stop)(void* instance);
} orbita_equipment_api_v1;

typedef const orbita_equipment_api_v1* (*orbita_plugin_get_api_v1_fn)(void);

ORBITA_PLUGIN_EXPORT const orbita_equipment_api_v1* orbita_plugin_get_api_v1(void);

#ifdef __cplusplus
}
#endif

#endif
