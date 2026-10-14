// SPDX-License-Identifier: GPL-2.0-or-later
#include "room_explorer.hpp"
#include "scimate/picture.hpp"
#include "scimate/view.hpp"
#include <QtWidgets>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <algorithm>
#include <cmath>
namespace {
QString text(const std::string& value) { return QString::fromUtf8(value.data(), static_cast<int>(value.size())); }
QImage pixels(const scimate::Bytes& data, int width, int height, int transparent = -1) {
    QImage image(width, height, QImage::Format_ARGB32);
    const auto palette = scimate::ega_picture_palette();
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto index = data.at(static_cast<std::size_t>(y * width + x));
        const auto& color = palette[index];
        image.setPixel(x, y, index == transparent ? qRgba(0, 0, 0, 0) : qRgb(color[0], color[1], color[2]));
    }
    return image;
}
QString label(const scimate::Room& room) {
    return QString("%1 · %2").arg(room.number ? QString::number(*room.number) : "?").arg(text(room.name));
}
const scimate::Resource* resource(const scimate::Project& project, scimate::ResourceType type, unsigned number) {
    if (number > 2047) return nullptr;
    for (const auto& value : project.game.resources) if (value.entry.id.type == type && value.entry.id.number == number) return &value;
    return nullptr;
}
}
RoomExplorerWidget::RoomExplorerWidget(std::shared_ptr<const scimate::Project> project,
    std::function<void(scimate::ResourceId)> openResource, QWidget* parent)
    : QWidget(parent), project_(std::move(project)), openResource_(std::move(openResource)) {
    setObjectName("roomExplorer");
    auto* layout = new QVBoxLayout(this);
    status_ = new QLabel("Analyzing room candidates…"); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); layout->addWidget(status_);
    auto* split = new QSplitter; layout->addWidget(split, 1);
    auto* browser = new QWidget; auto* left = new QVBoxLayout(browser);
    search_ = new QLineEdit; search_->setObjectName("roomSearch"); search_->setPlaceholderText("Search room, name or script…"); search_->setClearButtonEnabled(true); left->addWidget(search_);
    rooms_ = new QTreeWidget; rooms_->setObjectName("rooms"); rooms_->setHeaderLabels({"Room / name", "Script", "Picture"}); rooms_->setRootIsDecorated(false); left->addWidget(rooms_, 1); split->addWidget(browser);
    auto* right = new QWidget; auto* content = new QVBoxLayout(right);
    auto* controls = new QHBoxLayout;
    plane_ = new QComboBox; plane_->setObjectName("roomPlane"); plane_->addItems({"Visual", "Priority", "Control"}); controls->addWidget(plane_);
    sprites_ = new QCheckBox("Initial same-script sprites"); sprites_->setToolTip("Approximate placement; initialization and runtime drawing are not executed."); controls->addWidget(sprites_);
    auto* zoom = new QComboBox; zoom->addItems({"1×", "2×", "3×", "4×"}); zoom->setCurrentIndex(1); controls->addWidget(zoom);
    auto* script = new QPushButton("Open script"); controls->addWidget(script); content->addLayout(controls);
    auto* tabs = new QTabWidget;
    scene_ = new QGraphicsScene(this); canvas_ = new QGraphicsView(scene_); canvas_->setObjectName("roomCanvas"); canvas_->scale(2, 2); tabs->addTab(canvas_, "Room preview");
    mapScene_ = new QGraphicsScene(this); map_ = new QGraphicsView(mapScene_); map_->setObjectName("roomMap"); map_->setDragMode(QGraphicsView::ScrollHandDrag); tabs->addTab(map_, "Connections"); content->addWidget(tabs, 2);
    auto* info = new QTabWidget;
    links_ = new QTreeWidget; links_->setHeaderLabels({"Exit clue", "Target room", "Resolution"}); links_->setRootIsDecorated(false); info->addTab(links_, "Exits");
    objects_ = new QTreeWidget; objects_->setHeaderLabels({"Same-script object", "View", "Position / frame"}); objects_->setRootIsDecorated(false); info->addTab(objects_, "Objects");
    evidence_ = new QPlainTextEdit; evidence_->setReadOnly(true); info->addTab(evidence_, "Evidence / diagnostics"); content->addWidget(info, 1); split->addWidget(right); split->setSizes({300, 650});
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& filter) {
        for (int i = 0; i < rooms_->topLevelItemCount(); ++i) {
            auto* item = rooms_->topLevelItem(i);
            item->setHidden(!(item->text(0) + " " + item->text(1)).contains(filter, Qt::CaseInsensitive));
        }
    });
    connect(rooms_, &QTreeWidget::currentItemChanged, this, [this] { selectRoom(); });
    connect(plane_, &QComboBox::currentIndexChanged, this, [this] { drawRoom(); });
    connect(sprites_, &QCheckBox::toggled, this, [this] { drawRoom(); });
    connect(zoom, &QComboBox::currentIndexChanged, this, [this](int index) { canvas_->resetTransform(); canvas_->scale(index+1, index+1); });
    connect(script, &QPushButton::clicked, this, [this] {
        if (selected_) openResource_({scimate::ResourceType::script, analysis_.rooms[*selected_].script});
    });
    connect(links_, &QTreeWidget::itemDoubleClicked, this, [this] { followExit(); });
    connect(objects_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const auto view = item->data(0, Qt::UserRole).toUInt();
        if (view <= 2047) openResource_({scimate::ResourceType::view, static_cast<std::uint16_t>(view)});
    });
    connect(mapScene_, &QGraphicsScene::selectionChanged, this, [this] {
        const auto items = mapScene_->selectedItems();
        if (items.size() == 1 && items.front()->data(0).isValid()) {
            auto* item = rooms_->topLevelItem(items.front()->data(0).toInt());
            if (item) { item->setHidden(false); rooms_->setCurrentItem(item); rooms_->scrollToItem(item); }
        }
    });
    auto* watcher = new QFutureWatcher<scimate::RoomExplorer>(this);
    connect(watcher, &QFutureWatcher<scimate::RoomExplorer>::finished, this, [this, watcher] {
        watcher->deleteLater();
        try { install(watcher->result()); }
        catch (const std::exception& error) { status_->setText("Room analysis failed: " + text(error.what())); }
    });
    watcher->setFuture(QtConcurrent::run([snapshot = project_] { return scimate::explore_sci0_rooms(snapshot->game); }));
}
void RoomExplorerWidget::install(scimate::RoomExplorer analysis) {
    analysis_ = std::move(analysis);
    for (std::size_t i = 0; i < analysis_.rooms.size(); ++i) {
        const auto& room = analysis_.rooms[i];
        auto* item = new QTreeWidgetItem(rooms_, {label(room), QString::number(room.script), room.picture ? QString::number(*room.picture) : "?"});
        item->setData(0, Qt::UserRole, static_cast<int>(i)); item->setToolTip(0, text(room.id));
    }
    status_->setText(QString("%1 room candidates · %2 diagnostics · project snapshot. Static clues; runtime state is not executed.")
        .arg(analysis_.rooms.size()).arg(analysis_.diagnostics.size()));
    drawMap();
    if (rooms_->topLevelItemCount()) rooms_->setCurrentItem(rooms_->topLevelItem(0));
    else { evidence_->setPlainText(text(scimate::format_rooms_text(analysis_))); scene_->addText("No Rm/Room instances resolved. See diagnostics."); }
}
void RoomExplorerWidget::selectRoom() {
    auto* item = rooms_->currentItem(); if (!item) return;
    selected_ = static_cast<std::size_t>(item->data(0, Qt::UserRole).toInt());
    const auto& room = analysis_.rooms[*selected_]; links_->clear(); objects_->clear();
    QString evidence = text(room.id) + "\nScript " + QString::number(room.script) + " @ $" + QString::number(room.offset, 16) + "\n";
    for (const auto& exit : room.exits) {
        const auto count = std::count_if(analysis_.rooms.begin(), analysis_.rooms.end(), [&](const auto& target) { return target.number == exit.target; });
        auto* link = new QTreeWidgetItem(links_, {text(exit.kind), QString::number(exit.target), count == 1 ? "Double-click to follow" : count == 0 ? "Unresolved" : "Ambiguous"});
        link->setData(0, Qt::UserRole, exit.target); link->setToolTip(0, text(exit.evidence));
        evidence += text(exit.kind + ": " + exit.evidence) + "\n";
    }
    for (const auto& object : room.objects) {
        QString values;
        for (const auto* name : {"x", "y", "z", "loop", "cel", "priority"}) {
            auto found = object.properties.find(name);
            if (found != object.properties.end()) values += QString("%1=%2 ").arg(name).arg(found->second);
        }
        auto* entry = new QTreeWidgetItem(objects_, {text(object.name), QString::number(object.properties.at("view")), values});
        entry->setData(0, Qt::UserRole, object.properties.at("view")); entry->setToolTip(0, "Double-click to inspect view; object offset $" + QString::number(object.offset, 16));
    }
    for (const auto& note : room.notes) evidence += text(note) + "\n";
    for (const auto& diagnostic : analysis_.diagnostics) evidence += "Diagnostic: " + text(diagnostic) + "\n";
    evidence_->setPlainText(evidence); drawRoom();
}
void RoomExplorerWidget::drawRoom() {
    scene_->clear(); if (!selected_) return;
    const auto& room = analysis_.rooms[*selected_];
    scene_->setSceneRect(0, 0, 320, 190);
    try {
        const auto* picture = room.picture ? resource(*project_, scimate::ResourceType::pic, *room.picture) : nullptr;
        if (!picture) { scene_->addText("Initial picture unresolved or absent."); return; }
        const auto decoded = scimate::read_sci0_picture(picture->payload);
        scene_->addPixmap(QPixmap::fromImage(pixels(decoded.planes.at(plane_->currentIndex()), 320, 190)));
        if (sprites_->isChecked() && plane_->currentIndex() == 0) for (const auto& object : room.objects) {
            const auto& props = object.properties;
            if (!props.count("x") || !props.count("y") || !props.count("loop") || !props.count("cel")) continue;
            const auto* view = resource(*project_, scimate::ResourceType::view, props.at("view")); if (!view) continue;
            try {
                const auto decodedView = scimate::read_sci0_view(view->payload);
                const auto& loop = decodedView.loops.at(props.at("loop"));
                const auto source = loop.mirror_of >= 0 ? static_cast<unsigned>(loop.mirror_of) : props.at("loop");
                auto cel = decodedView.loops.at(source).cels.at(props.at("cel"));
                auto image = pixels(cel.pixels, cel.width, cel.height, cel.transparent);
                if (loop.mirror_of >= 0) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
                    image = image.flipped(Qt::Horizontal);
#else
                    image = image.mirrored(true, false);
#endif
                    cel.x = -cel.x;
                }
                auto* sprite = scene_->addPixmap(QPixmap::fromImage(image));
                const int x = static_cast<std::int16_t>(props.at("x")), y = static_cast<std::int16_t>(props.at("y"));
                const int z = props.count("z") ? static_cast<std::int16_t>(props.at("z")) : 0;
                sprite->setPos(x - int(cel.width/2) + cel.x, y - z - cel.height + 1 + cel.y);
                sprite->setZValue(1 + y); sprite->setToolTip(text(object.name) + " — approximate initial placement");
            } catch (const std::exception& error) { evidence_->appendPlainText("Object " + text(object.name) + ": " + text(error.what())); }
        }
    } catch (const std::exception& error) { scene_->clear(); scene_->addText("Preview unavailable: " + text(error.what())); }
}
void RoomExplorerWidget::drawMap() {
    mapScene_->clear();
    const auto position = [](std::size_t i) { return QPointF(static_cast<int>(i % 4) * 220, static_cast<int>(i / 4) * 100); };
    for (std::size_t i = 0; i < analysis_.rooms.size(); ++i) for (const auto& exit : analysis_.rooms[i].exits) {
        std::optional<std::size_t> target;
        bool ambiguous = false;
        for (std::size_t n = 0; n < analysis_.rooms.size(); ++n) if (analysis_.rooms[n].number == exit.target) {
            if (target) { ambiguous = true; break; } target = n;
        }
        if (!target || ambiguous) continue;
        const auto from = position(i) + QPointF(190, 30), to = position(*target) + QPointF(0, 30);
        auto* edge = mapScene_->addLine(QLineF(from, to), QPen(palette().color(QPalette::Mid), 1, Qt::DashLine));
        edge->setToolTip(text(exit.kind + ": " + exit.evidence)); edge->setZValue(-1);
        const auto delta = from - to;
        const auto length = std::hypot(delta.x(), delta.y());
        if (length > 0) {
            const auto unit = delta / length;
            const QPointF normal(-unit.y(), unit.x());
            auto* arrow = mapScene_->addPolygon(QPolygonF{to, to + unit * 10 + normal * 4, to + unit * 10 - normal * 4},
                QPen(palette().color(QPalette::Mid)), palette().brush(QPalette::Mid));
            arrow->setZValue(-1);
        }
    }
    for (std::size_t i = 0; i < analysis_.rooms.size(); ++i) {
        auto* node = mapScene_->addRect(0, 0, 190, 60, QPen(palette().color(QPalette::Mid)), palette().brush(QPalette::Base));
        node->setPos(position(i)); node->setFlag(QGraphicsItem::ItemIsSelectable); node->setData(0, static_cast<int>(i));
        const auto& room = analysis_.rooms[i]; node->setToolTip(text(room.id));
        auto* caption = new QGraphicsTextItem(label(room) + "\nScript " + QString::number(room.script), node);
        caption->setDefaultTextColor(palette().color(QPalette::Text)); caption->setTextWidth(185); caption->setAcceptedMouseButtons(Qt::NoButton);
    }
    mapScene_->setSceneRect(mapScene_->itemsBoundingRect().adjusted(-20, -20, 20, 20));
}
void RoomExplorerWidget::followExit() {
    auto* link = links_->currentItem(); if (!link) return;
    const auto number = link->data(0, Qt::UserRole).toUInt();
    std::optional<std::size_t> target;
    for (std::size_t i = 0; i < analysis_.rooms.size(); ++i) if (analysis_.rooms[i].number == number) {
        if (target) return; target = i;
    }
    if (target) { auto* item = rooms_->topLevelItem(static_cast<int>(*target)); item->setHidden(false); rooms_->setCurrentItem(item); rooms_->scrollToItem(item); }
}
