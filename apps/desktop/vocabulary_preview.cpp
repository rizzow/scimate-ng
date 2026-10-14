// SPDX-License-Identifier: GPL-2.0-or-later
#include "vocabulary_preview.hpp"
#include "resource_preview.hpp"
#include "scimate/assets.hpp"
#include "scimate/structured_resource.hpp"
#include <QtWidgets>
#include <memory>

namespace {
void appendJson(QStandardItem* parent, const QString& name, const QJsonValue& value) {
    auto* key = new QStandardItem(name);
    QString display;
    if (value.isArray()) {
        display = QString("%1 entries").arg(value.toArray().size());
        int index = 0;
        for (const auto& child : value.toArray()) appendJson(key, QString::number(index++), child);
    } else if (value.isObject()) {
        const auto object = value.toObject();
        for (auto entry = object.begin(); entry != object.end(); ++entry) appendJson(key, entry.key(), entry.value());
    } else if (value.isNull()) display = "(absent)";
    else display = value.toVariant().toString();
    parent->appendRow({key, new QStandardItem(display)});
}
}

QString showVocabularyPreview(QStackedWidget* stack, scimate::ResourceId id, const scimate::Bytes& payload) {
    auto page = std::make_unique<QWidget>();
    auto* layout = new QVBoxLayout(page.get());
    auto* search = new QLineEdit;
    search->setObjectName("vocabularySearch"); search->setPlaceholderText("Filter words, groups, IDs or fields…");
    search->setClearButtonEnabled(true); layout->addWidget(search);
    auto* model = new QStandardItemModel(page.get());
    QString summary;
    if (id.number == 0) {
        const auto words = scimate::read_sci0_vocabulary(payload);
        model->setHorizontalHeaderLabels({"Word", "Class bits", "Group"});
        for (const auto& word : words) {
            auto* group = new QStandardItem;
            group->setData(word.group, Qt::DisplayRole);
            model->appendRow({new QStandardItem(QString::fromLatin1(word.text.data(), int(word.text.size()))),
                new QStandardItem(QString("0x%1").arg(word.word_class, 3, 16, QChar('0'))), group});
        }
        summary = QString("\n%1 parser words. Words sharing a group are synonyms; class values are bit masks.").arg(words.size());
    } else {
        const auto bundle = scimate::export_resource_json_bundle(id, payload);
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(bundle.json), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            throw std::runtime_error("Could not read the structured vocabulary representation.");
        model->setHorizontalHeaderLabels({"Field / index", "Value"});
        const auto object = document.object();
        for (auto entry = object.begin(); entry != object.end(); ++entry)
            appendJson(model->invisibleRootItem(), entry.key(), entry.value());
        summary = bundle.semantic
            ? "\nStructured vocabulary fields. Expand records to inspect selector names, class locations, grammar or other supported tables."
            : "\nUnrecognized vocabulary layout: showing explicitly opaque byte records, without inferred meanings.";
    }
    auto* filter = new QSortFilterProxyModel(page.get());
    filter->setSourceModel(model); filter->setFilterKeyColumn(-1);
    filter->setFilterCaseSensitivity(Qt::CaseInsensitive); filter->setRecursiveFilteringEnabled(true);
    auto* tree = new QTreeView;
    tree->setObjectName("vocabularyEntries"); tree->setModel(filter);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setAlternatingRowColors(true); tree->setUniformRowHeights(true);
    tree->setSortingEnabled(true); tree->sortByColumn(0, Qt::AscendingOrder);
    tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tree->header()->setStretchLastSection(true); tree->expandToDepth(id.number == 0 ? 0 : 1);
    layout->addWidget(tree, 1);
    QObject::connect(search, &QLineEdit::textChanged, filter, &QSortFilterProxyModel::setFilterFixedString);
    QObject::connect(search, &QLineEdit::textChanged, tree, [tree](const QString& value) {
        if (!value.isEmpty()) tree->expandAll(); else tree->expandToDepth(1);
    });
    installResourcePreview(stack, page.release());
    return summary;
}
