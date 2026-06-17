#include "channel_picker_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFrame>
#include <QDialogButtonBox>
#include <QTreeWidgetItem>
#include <QMap>
#include <QList>
#include <algorithm>
#include <numeric>

ChannelPickerDialog::ChannelPickerDialog(const std::vector<orbita::ChannelSpec>& specs,
                                          MetadataService* db,
                                          QWidget* parent)
    : QDialog(parent)
    , specs_(specs)
    , db_(db)
{
    setWindowTitle("Выбор канала");
    resize(720, 480);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    search_ = new QLineEdit;
    search_->setPlaceholderText("Поиск по имени или адресу…");
    search_->setClearButtonEnabled(true);
    root->addWidget(search_);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);

    // ── Left: channel tree ─────────────────────────────────────────────────
    tree_ = new QTreeWidget;
    tree_->setHeaderHidden(true);
    tree_->setIndentation(14);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setUniformRowHeights(true);
    splitter->addWidget(tree_);

    // ── Right: detail panel ───────────────────────────────────────────────
    auto* detail = new QFrame;
    detail->setFrameShape(QFrame::StyledPanel);
    detail->setMinimumWidth(180);
    detail->setMaximumWidth(260);
    auto* dl = new QVBoxLayout(detail);
    dl->setContentsMargins(10, 10, 10, 10);
    dl->setSpacing(6);

    auto makeLabel = [](const QString& placeholder) {
        auto* l = new QLabel(placeholder);
        l->setWordWrap(true);
        l->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        return l;
    };

    dl->addWidget(new QLabel("<b>Канал</b>"));
    detailName_ = makeLabel("—");
    detailAddr_ = makeLabel("—");
    detailCat_  = makeLabel("—");
    dl->addWidget(detailName_);
    dl->addWidget(detailAddr_);
    dl->addWidget(detailCat_);
    dl->addStretch();
    splitter->addWidget(detail);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    root->addWidget(buttons);

    buildTree();

    connect(search_,  &QLineEdit::textChanged,        this, &ChannelPickerDialog::onSearchChanged);
    connect(tree_,    &QTreeWidget::itemClicked,       this, &ChannelPickerDialog::onItemClicked);
    connect(tree_,    &QTreeWidget::itemDoubleClicked, this, &ChannelPickerDialog::onItemDoubleClicked);
    connect(buttons,  &QDialogButtonBox::accepted,     this, &QDialog::accept);
    connect(buttons,  &QDialogButtonBox::rejected,     this, &QDialog::reject);
}

QString ChannelPickerDialog::selectedAddress() const { return selectedAddress_; }
QString ChannelPickerDialog::selectedName()    const { return selectedName_; }

// ---------------------------------------------------------------------------

void ChannelPickerDialog::buildTree()
{
    tree_->clear();

    if (db_) {
        // Grouped by category
        QMap<QString, QList<int>> byCategory;
        for (int i = 0; i < (int)specs_.size(); ++i) {
            QString cat = db_->getCategory(QString::fromStdString(specs_[i].address));
            if (cat.isEmpty()) cat = "Прочие";
            byCategory[cat].append(i);
        }

        for (auto it = byCategory.begin(); it != byCategory.end(); ++it) {
            auto* catItem = new QTreeWidgetItem(tree_);
            catItem->setText(0, it.key());
            catItem->setData(0, RoleLeaf, false);
            QFont f = catItem->font(0);
            f.setBold(true);
            catItem->setFont(0, f);
            catItem->setFlags(catItem->flags() & ~Qt::ItemIsSelectable);

            for (int idx : it.value()) {
                const auto& spec = specs_[idx];
                QString addr = QString::fromStdString(spec.address);
                QString name = QString::fromStdString(spec.name);
                if (name.isEmpty()) name = db_->getName(addr);

                auto* leaf = new QTreeWidgetItem(catItem);
                leaf->setText(0, name.isEmpty() ? addr : QString("%1  (%2)").arg(name, addr));
                leaf->setData(0, RoleAddress, addr);
                leaf->setData(0, RoleName,    name);
                leaf->setData(0, RoleLeaf,    true);
            }
            catItem->setExpanded(true);
        }
    } else {
        // Flat list sorted by address
        std::vector<int> indices(specs_.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::sort(indices.begin(), indices.end(), [&](int a, int b) {
            return specs_[a].address < specs_[b].address;
        });

        for (int i : indices) {
            const auto& spec = specs_[i];
            QString addr = QString::fromStdString(spec.address);
            QString name = QString::fromStdString(spec.name);

            auto* leaf = new QTreeWidgetItem(tree_);
            leaf->setText(0, name.isEmpty() ? addr : QString("%1  (%2)").arg(name, addr));
            leaf->setData(0, RoleAddress, addr);
            leaf->setData(0, RoleName,    name);
            leaf->setData(0, RoleLeaf,    true);
        }
    }
}

void ChannelPickerDialog::onSearchChanged(const QString& text)
{
    const QString lower = text.toLower();

    std::function<bool(QTreeWidgetItem*)> applyFilter = [&](QTreeWidgetItem* item) -> bool {
        bool anyChildVisible = false;
        for (int i = 0; i < item->childCount(); ++i)
            anyChildVisible |= applyFilter(item->child(i));

        bool isLeaf = item->data(0, RoleLeaf).toBool();
        bool visible = false;
        if (isLeaf) {
            visible = lower.isEmpty()
                   || item->text(0).toLower().contains(lower)
                   || item->data(0, RoleAddress).toString().toLower().contains(lower);
        } else {
            visible = anyChildVisible || lower.isEmpty();
        }
        item->setHidden(!visible);
        if (!isLeaf && !lower.isEmpty())
            item->setExpanded(anyChildVisible);
        return visible;
    };

    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        applyFilter(tree_->topLevelItem(i));
}

void ChannelPickerDialog::onItemClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item || !item->data(0, RoleLeaf).toBool()) return;
    selectedAddress_ = item->data(0, RoleAddress).toString();
    selectedName_    = item->data(0, RoleName).toString();
    updateDetail(item);
}

void ChannelPickerDialog::onItemDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item || !item->data(0, RoleLeaf).toBool()) return;
    selectedAddress_ = item->data(0, RoleAddress).toString();
    selectedName_    = item->data(0, RoleName).toString();
    accept();
}

void ChannelPickerDialog::updateDetail(QTreeWidgetItem* item)
{
    QString addr = item->data(0, RoleAddress).toString();
    QString name = item->data(0, RoleName).toString();
    QString cat;
    if (db_) cat = db_->getCategory(addr);

    detailName_->setText(name.isEmpty() ? "<i>нет имени</i>" : name);
    detailAddr_->setText("<tt>" + addr + "</tt>");
    detailCat_->setText(cat.isEmpty() ? "—" : cat);
}
