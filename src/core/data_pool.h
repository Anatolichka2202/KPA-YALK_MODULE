/**
 * @file data_pool.h
 * @brief Хранилище текущих значений каналов.
 *
 * Содержит массивы для аналоговых, контактных, быстрых, температурных, БУС значений.
 * Потокобезопасен (мьютексы).
 */

#ifndef ORBITA_DATA_POOL_H
#define ORBITA_DATA_POOL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct orbita_data_pool orbita_data_pool_t;

orbita_data_pool_t* orbita_data_pool_create(void);
void orbita_data_pool_destroy(orbita_data_pool_t* pool);

// Установка количества каналов каждого типа (вызывается после загрузки адресов)
int orbita_data_pool_resize(orbita_data_pool_t* pool, int analog_count, int contact_count, int fast_count, int temp_count, int bus_count);

// Обновление значений (декодер вызывает при поступлении новой группы)
int orbita_data_pool_set_analog(orbita_data_pool_t* pool, int index, double value);
int orbita_data_pool_set_contact(orbita_data_pool_t* pool, int index, int value);
int orbita_data_pool_set_fast(orbita_data_pool_t* pool, int index, int value);
int orbita_data_pool_set_temperature(orbita_data_pool_t* pool, int index, int value);
int orbita_data_pool_set_bus(orbita_data_pool_t* pool, int index, int value);

// Чтение значений
double orbita_data_pool_get_analog(orbita_data_pool_t* pool, int index);
int    orbita_data_pool_get_contact(orbita_data_pool_t* pool, int index);
int    orbita_data_pool_get_fast(orbita_data_pool_t* pool, int index);
int    orbita_data_pool_get_temperature(orbita_data_pool_t* pool, int index);
int    orbita_data_pool_get_bus(orbita_data_pool_t* pool, int index);

// Флаг обновления (для ожидания)
int orbita_data_pool_has_new_data(orbita_data_pool_t* pool);
void orbita_data_pool_clear_new_flag(orbita_data_pool_t* pool);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_DATA_POOL_H
