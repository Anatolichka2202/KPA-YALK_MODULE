/**
 * @file orbita_decoder.h
 * @brief Декодирование телеметрического кадра "Орбита-IV".
 *
 * Преобразует поток битов (0/1) в массив 12‑битных слов (masGroupAll).
 *
 * Вход: биты, полученные после пороговой обработки АЦП.
 * Выход: массив uint16_t (12 бит) для всей группы.
 *
 * Декодеры:
 * - M16 (2048 слов в группе)
 * - M08 (1024 слова)
 * - M04 (512)
 * - M02 (256)
 * - M01 (128)
 *
 * Функции:
 * - orbita_decoder_create – создание декодера под конкретную информативность
 * - orbita_decoder_feed_bit – подача одного бита (0/1)
 * - orbita_decoder_get_group – получить массив группы (если накоплена)
 * - orbita_decoder_get_stat – статистика ошибок маркеров
 */

#ifndef ORBITA_DECODER_H
#define ORBITA_DECODER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* orbita_decoder_t;

// Создание декодера для заданной информативности (1,2,4,8,16)
// Возвращает NULL при ошибке.
orbita_decoder_t orbita_decoder_create(int informativnost);

// Уничтожение декодера
void orbita_decoder_destroy(orbita_decoder_t dec);

// Подача одного бита (0 или 1)
void orbita_decoder_feed_bit(orbita_decoder_t dec, int bit);

// Проверка, накоплена ли новая группа
int orbita_decoder_has_group(orbita_decoder_t dec);

// Получить массив 12‑битных слов группы (размер = masGroupSize)
// Возвращает указатель на внутренний буфер, который будет перезаписан при следующей группе.
const uint16_t* orbita_decoder_get_group(orbita_decoder_t dec, size_t* out_size);

// Получить статистику ошибок синхронизации (проценты)
void orbita_decoder_get_errors(orbita_decoder_t dec, int* phrase_error_percent, int* group_error_percent);

// Сброс состояния (например, при потере сигнала)
void orbita_decoder_reset(orbita_decoder_t dec);

#ifdef __cplusplus
}
#endif

#endif // ORBITA_DECODER_H
