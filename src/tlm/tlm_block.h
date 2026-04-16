/**
 * @file tlm_block.h
 * @brief Определения структур TLM-файла (заголовок, префикс блока).
 */

#ifndef ORBITA_TLM_BLOCK_H
#define ORBITA_TLM_BLOCK_H

#define TLM_HEADER_SIZE 256
#define TLM_BLOCK_PREFIX_SIZE 32  // исправлено на 32
typedef struct {
    char data[TLM_HEADER_SIZE];
} tlm_header_t;

#endif
