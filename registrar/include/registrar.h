#ifndef REGISTRAR_H
#define REGISTRAR_H

#include "model.h"

#include <QSqlDatabase>
#include <QString>

#include <string>
#include <vector>

namespace ktma::registrar {

class Registrar
{
public:
    explicit Registrar(const std::string& databasePath);
    ~Registrar();

    Registrar(const Registrar&) = delete;
    Registrar& operator=(const Registrar&) = delete;
    Registrar(Registrar&&) = delete;
    Registrar& operator=(Registrar&&) = delete;

    std::string createProduct(
        const std::string& productType,
        const std::string& serialNumber);

    std::string createComponent(
        const std::string& componentType,
        const std::string& serialNumber);

    void installComponent(
        const std::string& productId,
        const std::string& componentId);

    void removeComponent(
        const std::string& productId,
        const std::string& componentId,
        const std::string& reason);

    std::string beginStage(
        const std::string& productId,
        Stage stage);

    // Основной API жизненного цикла: этап всегда привязан к конкретной
    // установленной ячейке. beginStage оставлен для агрегатных/совместимых
    // сценариев, где componentId отсутствует.
    std::string beginComponentStage(
        const std::string& productId,
        const std::string& componentId,
        Stage stage);

    void attachRun(
        const std::string& stageAttemptId,
        const std::string& runId);

    void finishStage(
        const std::string& stageAttemptId,
        Verdict verdict);

    std::vector<Product> listProducts() const;
    std::vector<Component> listComponents() const;
    std::vector<ComponentBinding> listInstalledComponents(
        const std::string& productId) const;
    std::vector<StageAttempt> listStageAttempts(
        const std::string& productId) const;

    // Итог вычисляется из текущего состава изделия и четырёх обязательных
    // этапов каждой активной ячейки. История замен не удаляется.
    Verdict productVerdict(const std::string& productId) const;
    ProductReport productReport(const std::string& productId) const;

private:
    void initializeSchema();
    void ensureColumn(const char* table, const char* column, const char* definition);
    void ensureProduct(const std::string& productId) const;
    void ensureComponent(const std::string& componentId) const;
    void ensureActiveBinding(
        const std::string& productId,
        const std::string& componentId) const;
    std::string beginStageInternal(
        const std::string& productId,
        const std::string& componentId,
        Stage stage);

    QSqlDatabase database_;
    QString connectionName_;
};

} // namespace ktma::registrar

#endif // REGISTRAR_H
