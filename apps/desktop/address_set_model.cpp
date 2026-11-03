#include "address_set_model.h"

#include <set>
#include <stdexcept>
#include <utility>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

AddressSetModel::AddressSetModel()
{
    // Первый набор создаётся сразу, чтобы добавление параметров из каталога
    // всегда имело однозначную цель. Кнопка «Добавить набор» создаёт второй и
    // последующие независимые наборы без остановки мониторинга.
    addSet(QStringLiteral("Набор 1"));
}

int AddressSetModel::count() const noexcept
{
    return static_cast<int>(sets_.size());
}

int AddressSetModel::activeIndex() const noexcept
{
    return activeIndex_;
}

const AddressSetDefinition& AddressSetModel::at(int index) const
{
    if (index < 0 || index >= count())
        throw std::out_of_range("Индекс набора адресов вне диапазона");
    return sets_[static_cast<std::size_t>(index)];
}

void AddressSetModel::setActiveIndex(int index)
{
    if (index < 0 || index >= count())
        throw std::out_of_range("Индекс набора адресов вне диапазона");
    activeIndex_ = index;
}

int AddressSetModel::addSet(const QString& title)
{
    const QString actualTitle = title.trimmed().isEmpty()
        ? QStringLiteral("Набор %1").arg(nextId_)
        : title.trimmed();
    const QString id = QStringLiteral("address-set-%1").arg(nextId_++);
    sets_.push_back({id, actualTitle, {}});
    activeIndex_ = count() - 1;
    return activeIndex_;
}

bool AddressSetModel::removeSet(int index)
{
    // Нельзя оставить свободный контур без активного набора: тогда действия из
    // библиотеки параметров не имели бы определённого получателя.
    if (count() <= 1 || index < 0 || index >= count())
        return false;

    sets_.erase(sets_.begin() + index);
    if (activeIndex_ >= count())
        activeIndex_ = count() - 1;
    else if (index < activeIndex_)
        --activeIndex_;
    return true;
}

void AddressSetModel::renameSet(int index, const QString& title)
{
    const QString actualTitle = title.trimmed();
    if (actualTitle.isEmpty())
        return;
    at(index);
    sets_[static_cast<std::size_t>(index)].title = actualTitle;
}

void AddressSetModel::replaceActive(const std::vector<orbita::ChannelSpec>& specs)
{
    at(activeIndex_);
    sets_[static_cast<std::size_t>(activeIndex_)].specs = specs;
}

bool AddressSetModel::containsInActive(const std::string& address) const
{
    const auto& active = at(activeIndex_).specs;
    for (const auto& spec : active) {
        if (spec.address == address)
            return true;
    }
    return false;
}

void AddressSetModel::addToActive(const std::vector<orbita::ChannelSpec>& specs)
{
    auto& active = sets_[static_cast<std::size_t>(activeIndex_)].specs;
    for (const auto& spec : specs) {
        if (spec.address.empty() || containsInActive(spec.address))
            continue;
        active.push_back(spec);
    }
}

std::vector<orbita::ChannelSpec> AddressSetModel::combinedSpecs() const
{
    std::vector<orbita::ChannelSpec> combined;
    std::set<std::string> seenAddresses;
    for (const auto& set : sets_) {
        for (const auto& spec : set.specs) {
            if (spec.address.empty() || !seenAddresses.insert(spec.address).second)
                continue;
            combined.push_back(spec);
        }
    }
    return combined;
}

QByteArray AddressSetModel::serialize() const
{
    QJsonArray sets;
    for (const auto& set : sets_) {
        QJsonArray channels;
        for (const auto& spec : set.specs) {
            channels.append(QJsonObject{{QStringLiteral("address"), QString::fromStdString(spec.address)},
                                       {QStringLiteral("name"), QString::fromStdString(spec.name)},
                                       {QStringLiteral("category"), QString::fromStdString(spec.category)}});
        }
        sets.append(QJsonObject{{QStringLiteral("title"), set.title},
                                {QStringLiteral("channels"), channels}});
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("schema"), 1},
                                     {QStringLiteral("active"), activeIndex_},
                                     {QStringLiteral("sets"), sets}}).toJson(QJsonDocument::Indented);
}

bool AddressSetModel::restore(const QByteArray& data, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(QStringLiteral("Файл не является корректным JSON-объектом"));
    const auto root = document.object();
    if (root.value(QStringLiteral("schema")) != QJsonValue(1)
        || !root.value(QStringLiteral("sets")).isArray())
        return fail(QStringLiteral("Неизвестная версия или отсутствуют наборы параметров"));
    const auto sets = root.value(QStringLiteral("sets")).toArray();
    const auto active = root.value(QStringLiteral("active"));
    if (sets.isEmpty() || !active.isDouble() || active.toDouble() != active.toInt(-1)
        || active.toInt(-1) < 0 || active.toInt() >= sets.size())
        return fail(QStringLiteral("Нет наборов или неверно указан активный набор"));

    // Сначала строим отдельную модель. Ошибочный импорт не меняет ни экран,
    // ни подписку на параметры действующего потока.
    AddressSetModel candidate;
    for (int i = 0; i < sets.size(); ++i) {
        if (!sets[i].isObject()) return fail(QStringLiteral("Набор должен быть объектом"));
        const auto item = sets[i].toObject();
        if (!item.value(QStringLiteral("title")).isString()
            || item.value(QStringLiteral("title")).toString().trimmed().isEmpty()
            || !item.value(QStringLiteral("channels")).isArray())
            return fail(QStringLiteral("У набора должно быть имя и список параметров"));
        if (i == 0) candidate.renameSet(0, item.value(QStringLiteral("title")).toString());
        else candidate.addSet(item.value(QStringLiteral("title")).toString());
        std::vector<orbita::ChannelSpec> channels;
        std::set<std::string> seen;
        for (const auto& value : item.value(QStringLiteral("channels")).toArray()) {
            if (!value.isObject()) return fail(QStringLiteral("Параметр должен быть объектом"));
            const auto channel = value.toObject();
            const auto address = channel.value(QStringLiteral("address"));
            const auto name = channel.value(QStringLiteral("name"));
            const auto category = channel.value(QStringLiteral("category"));
            if (!address.isString() || address.toString().trimmed().isEmpty()
                || !name.isString() || !category.isString())
                return fail(QStringLiteral("У параметра должны быть адрес, имя и категория"));
            const auto key = address.toString().toStdString();
            if (!orbita::isValidChannelAddress(key))
                return fail(QStringLiteral("Некорректный адрес параметра: %1").arg(address.toString()));
            if (!seen.insert(key).second) return fail(QStringLiteral("Повтор адреса внутри набора"));
            channels.push_back({key, name.toString().toStdString(), category.toString().toStdString()});
        }
        candidate.replaceActive(channels);
    }
    candidate.setActiveIndex(active.toInt());
    *this = std::move(candidate);
    if (error) error->clear();
    return true;
}
