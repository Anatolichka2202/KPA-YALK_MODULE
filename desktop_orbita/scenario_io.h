#pragma once

#include "scenario_model.h"
#include <QString>
#include <optional>

/// Формат файла .scn (UTF-8, текстовый):
///   # строки-комментарии и пустые строки игнорируются
///   SCENARIO <название сценария>
///   CMD <текст команды>
///   CHECK <адрес> <lo> <hi>
///
/// Пример:
///   # Сценарий автопрогона питания
///   SCENARIO Проверка питания 27В
///   CMD Подать питание 27 В
///   CHECK M16P1A70B12C10D10T01 100 900
///   CMD Снять питание

/// Загрузить сценарий из файла. Возвращает nullopt при ошибке открытия.
/// Строки CHECK с неверным числом полей/неверными числами — пропускаются (пишут в qWarning).
std::optional<Scenario> loadScenario(const QString& path);

/// Сохранить сценарий в файл. Возвращает false при ошибке записи.
bool saveScenario(const Scenario& scenario, const QString& path);
