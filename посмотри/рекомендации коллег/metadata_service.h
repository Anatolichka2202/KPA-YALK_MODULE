/**
 * @file metadata_service.h
 * @brief Доступ к базе данных параметров (SQLite).
 *
 * Выделен из MainWindow в отдельный класс чтобы:
 *   • MainWindow не знал деталей схемы БД
 *   • Логику нормализации адресов держать в одном месте
 *   • Упростить тестирование (можно мокнуть)
 *
 * Используется ТОЛЬКО в UI-слое (Qt). Ядро о БД не знает.
 */

#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QHash>
#include <QString>
#include <QStringList>
#include <optional>

struct ParamInfo {
    QString name;
    QString category;
    // Пороговые значения (NaN если не заданы в БД)
    double  minNominal = std::numeric_limits<double>::quiet_NaN();
    double  maxNominal = std::numeric_limits<double>::quiet_NaN();
    QString units;
};

class MetadataService : public QObject {
    Q_OBJECT
public:
    explicit MetadataService(QObject* parent = nullptr);
    ~MetadataService() override = default;

    // ─── Инициализация ────────────────────────────────────────────────────
    /**
     * @brief Открывает parameters.db рядом с .exe.
     *        Путь: QCoreApplication::applicationDirPath() + "/address/parameters.db"
     * @return true если БД открыта и таблица существует
     */
    bool open();

    /**
     * @brief Явный путь к файлу БД (для тестов).
     */
    bool openPath(const QString& dbPath);

    bool isOpen() const;

    // ─── Запросы ──────────────────────────────────────────────────────────
    /**
     * @brief Поиск по нормализованному адресу.
     *        Нормализация: кирилл. П→P, М→M, пробелы убраны, верхний регистр.
     */
    std::optional<ParamInfo> lookup(const QString& rawAddress) const;

    /** @brief Список всех категорий (для фильтрации в UI). */
    QStringList categories() const;

    /**
     * @brief Все параметры в категории.
     * @return список пар {нормализованный адрес, имя параметра}
     */
    QList<QPair<QString,QString>> paramsByCategory(const QString& category) const;

    /**
     * @brief Все параметры с непустым адресом.
     * @return список пар {нормализованный адрес, имя параметра}
     */
    QList<QPair<QString,QString>> allParams() const;

    /**
     * @brief Хэш адрес→имя для быстрого поиска (используется в ConfigWidget).
     */
    QHash<QString,QString> addressToName()     const;
    QHash<QString,QString> addressToCategory() const;

    // ─── Утилиты (статические, доступны снаружи) ─────────────────────────
    /**
     * @brief Нормализует адрес для сравнения с данными из БД.
     *
     * Правила:
     *   • Убираем пробелы (simplified)
     *   • Кирилл. П (U+041F) → P (U+0050)
     *   • Кирилл. М (U+041C) → M (U+004D)
     *   • Приводим к верхнему регистру
     */
    static QString normalize(const QString& raw);

signals:
    void loadError(const QString& message);

private:
    QSqlDatabase db_;
    QString      connectionName_;

    bool ensureSchema() const;
    QList<QPair<QString,QString>> queryPairs(const QString& sql,
                                              const QVariantList& binds = {}) const;
};
