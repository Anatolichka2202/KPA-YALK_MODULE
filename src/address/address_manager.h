/**
 * @file address_manager.h
 * @brief Управление списком каналов (адресов) для сбора телеметрии.
 *
 * Загружает адреса из файла или массива строк, выполняет постобработку:
 * - определение размеров группы/цикла по информативности
 * - заполнение массивов групп и циклов для каналов с большим шагом
 * - коррекция медленных и контактных адресов (опционально)
 */

#ifndef ORBITA_ADDRESS_MANAGER_H
#define ORBITA_ADDRESS_MANAGER_H

#include "orbita_address.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct orbita_address_manager orbita_address_manager_t;

// Создание менеджера с заданной информативностью (1,2,4,8,16)
orbita_address_manager_t* orbita_address_manager_create(int informativnost);
void orbita_address_manager_destroy(orbita_address_manager_t* mgr);

// Добавление канала (дескриптор копируется)
int orbita_address_manager_add_channel(orbita_address_manager_t* mgr, const orbita_channel_desc_t* desc);

// Загрузка из файла (использует парсер)
int orbita_address_manager_load_file(orbita_address_manager_t* mgr, const char* filename);

// Загрузка из массива строк
int orbita_address_manager_load_lines(orbita_address_manager_t* mgr, const char** lines, int count);

// Получить массив каналов (для внешнего использования)
const orbita_channel_desc_t* orbita_address_manager_get_channels(const orbita_address_manager_t* mgr, int* out_count);

// Выполнить постобработку (заполнить arrNumGroup, arrNumCikl)
int orbita_address_manager_finalize(orbita_address_manager_t* mgr);

// Получить размер группы (количество слов в группе)
int orbita_address_manager_get_group_size(const orbita_address_manager_t* mgr);

// Получить размер цикла (количество слов в цикле)
int orbita_address_manager_get_cycle_size(const orbita_address_manager_t* mgr);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_ADDRESS_MANAGER_H
