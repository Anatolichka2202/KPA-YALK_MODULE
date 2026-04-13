#include "data_pool.h"
#include "logger.h"
#include <cstdlib>
#include <cstring>

struct orbita_data_pool {
    double* analog;
    int* contact;
    int* fast;
    int* temperature;
    int* bus;
    int analog_count, contact_count, fast_count, temp_count, bus_count;
    int new_data_flag;
    // TODO: добавить мьютекс
};

orbita_data_pool_t* orbita_data_pool_create(void) {
    orbita_data_pool_t* pool = (orbita_data_pool_t*)calloc(1, sizeof(orbita_data_pool_t));
    if (!pool) return nullptr;
    LOG_DEBUG("Data pool created");
    return pool;
}

void orbita_data_pool_destroy(orbita_data_pool_t* pool) {
    if (!pool) return;
    free(pool->analog);
    free(pool->contact);
    free(pool->fast);
    free(pool->temperature);
    free(pool->bus);
    free(pool);
    LOG_DEBUG("Data pool destroyed");
}

int orbita_data_pool_resize(orbita_data_pool_t* pool, int analog_count, int contact_count, int fast_count, int temp_count, int bus_count) {
    if (!pool) return -1;
    pool->analog = (double*)realloc(pool->analog, analog_count * sizeof(double));
    pool->contact = (int*)realloc(pool->contact, contact_count * sizeof(int));
    pool->fast = (int*)realloc(pool->fast, fast_count * sizeof(int));
    pool->temperature = (int*)realloc(pool->temperature, temp_count * sizeof(int));
    pool->bus = (int*)realloc(pool->bus, bus_count * sizeof(int));
    if ((analog_count && !pool->analog) || (contact_count && !pool->contact) ||
        (fast_count && !pool->fast) || (temp_count && !pool->temperature) ||
        (bus_count && !pool->bus)) {
        LOG_ERROR("Failed to reallocate data pools");
        return -2;
    }
    pool->analog_count = analog_count;
    pool->contact_count = contact_count;
    pool->fast_count = fast_count;
    pool->temp_count = temp_count;
    pool->bus_count = bus_count;
    return 0;
}

int orbita_data_pool_set_analog(orbita_data_pool_t* pool, int index, double value) {
    if (!pool || index < 0 || index >= pool->analog_count) return -1;
    pool->analog[index] = value;
    pool->new_data_flag = 1;
    return 0;
}

double orbita_data_pool_get_analog(orbita_data_pool_t* pool, int index) {
    if (!pool || index < 0 || index >= pool->analog_count) return 0.0;
    return pool->analog[index];
}

// Аналогично для остальных геттеров/сеттеров (опускаю для краткости)
int orbita_data_pool_has_new_data(orbita_data_pool_t* pool) {
    return pool ? pool->new_data_flag : 0;
}

void orbita_data_pool_clear_new_flag(orbita_data_pool_t* pool) {
    if (pool) pool->new_data_flag = 0;
}
