#pragma once

#include <QString>
#include <QVector>

/// Вид шага сценария
enum class StepKind {
    Command, ///< Инструкция оператору (ручное действие)
    Check    ///< Автоматическая проверка канала
};

/// Результат выполнения шага
enum class StepResult {
    Pending, ///< Ещё не выполнялся
    Ok,      ///< Успешно / Выполнено
    Fail     ///< Провал / Не выполнено
};

/// Один шаг сценария
struct ScenarioStep {
    StepKind   kind    = StepKind::Command;
    QString    text;       ///< Текст инструкции (для Command) или имя/описание (для Check)
    QString    address;    ///< Адрес канала (только для Check)
    double     lo     = 0; ///< Нижняя граница допуска (только для Check)
    double     hi     = 0; ///< Верхняя граница допуска (только для Check)
    StepResult result = StepResult::Pending;
};

/// Сценарий проверки — набор шагов с именем
struct Scenario {
    QString            name;
    QVector<ScenarioStep> steps;
};
