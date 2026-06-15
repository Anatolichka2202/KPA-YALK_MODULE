#pragma once
#include <string>
#include <vector>

namespace encoding {

// Читает файл в UTF‑8, автоматически определяя кодировку (UTF‑8 / Windows‑1251).
std::string readFileToUtf8(const std::string& path);

// Каноническая нормализация адреса (единственное место в проекте):
//   • кириллические двойники латиницы → латиница (П→P, М→M, А→A, ...)
//   • Latin-1 артефакты битого декода (Ï→P, Ì→M, ...) → латиница
//   • убираются пробелы/табуляции, верхний регистр
// Вход и выход – UTF‑8. Применять ПОСЛЕ readFileToUtf8.
std::string normalizeAddress(const std::string& utf8_address);

} // namespace encoding