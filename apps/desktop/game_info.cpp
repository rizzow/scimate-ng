// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_info.hpp"
#include "scimate/project.hpp"
#include <QtWidgets>
#include <map>
#include <set>

GameInfoDialog::GameInfoDialog(const scimate::Project& project, QWidget* parent) : QDialog(parent) {
    setWindowTitle("Game Info");
    resize(720, 640);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    auto* fields = new QFormLayout(content);
    fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    fields->setRowWrapPolicy(QFormLayout::WrapLongRows);
    fields->setVerticalSpacing(12);
    auto field = [&](const QString& name, const QString& value, const char* id = "") {
        auto* label = new QLabel(value);
        label->setObjectName(id); label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true); label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        fields->addRow(name, label);
    };
    const QLocale locale;
    auto number = [&](std::size_t value) { return locale.toString(static_cast<qulonglong>(value)); };
    field("Project name", QString::fromStdString(project.name), "gameInfoProjectName");
    field("Project directory", QString::fromStdString(project.directory.u8string()));
    field("Resource format", "SCI0 — validated by the SCI0 project loader", "gameInfoFormat");
    field("Interpreter version", "Unknown — resource format does not identify an exact interpreter release.");
    field("Game identity", "Not identified. The project name is user-defined, not a detected game title.");
    field("Original game directory", "Not retained. This project contains independent resource snapshots.");
    struct Totals { std::size_t count = 0; std::size_t bytes = 0; };
    std::map<scimate::ResourceType, Totals> types;
    std::set<std::pair<scimate::ResourceType, std::uint16_t>> identities;
    std::size_t payloadBytes = 0;
    for (const auto& resource : project.game.resources) {
        auto& total = types[resource.entry.id.type];
        ++total.count; total.bytes += resource.payload.size();
        payloadBytes += resource.payload.size();
        identities.emplace(resource.entry.id.type, resource.entry.id.number);
    }
    field("Resource records", number(project.game.resources.size()), "gameInfoResourceCount");
    field("Unique type/number pairs", number(identities.size()));
    field("Decoded payload bytes", number(payloadBytes), "gameInfoPayloadBytes");
    field("Original map bytes", number(project.game.original.map.size()));
    QStringList volumes;
    for (const auto& volume : project.game.original.volumes)
        volumes << QString("resource.%1 — %2 bytes").arg(volume.first, 3, 10, QChar('0')).arg(number(volume.second.size()));
    field("Referenced source volumes", volumes.isEmpty() ? "None" : volumes.join('\n'));
    auto* table = new QTableWidget(static_cast<int>(types.size()), 3);
    table->setObjectName("gameInfoResourceTypes");
    table->setHorizontalHeaderLabels({"Resource type", "Records", "Decoded bytes"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setMinimumHeight(180);
    int row = 0;
    for (const auto& type : types) {
        table->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(std::string(scimate::resource_type_name(type.first)))));
        table->setItem(row, 1, new QTableWidgetItem(number(type.second.count)));
        table->setItem(row, 2, new QTableWidgetItem(number(type.second.bytes)));
        ++row;
    }
    fields->addRow(table);
    field("Editing support", "SCI0 EGA projects: editable views, backgrounds/masks and text; script disassembly, but no integrated high-level decompilation or compilation.");
    field("Statistics", "Counts include duplicate map records. Payload sizes reflect the current loaded edits; map and volume sizes describe original snapshots. Refresh from Disk to include external edits.");
    scroll->setWidget(content); layout->addWidget(scroll);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
