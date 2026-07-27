/* Copyright 2013-2021 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ModListView.h"
#include "ui/pages/instance/ModFolderProxyModel.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDrag>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QHeaderView>
#include <QIcon>
#include <QLineF>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QScrollBar>
#include <QWheelEvent>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
constexpr auto RightDragStartProperty = "_prismModFoldersRightDragStart";
constexpr auto RightDragCandidateProperty = "_prismModFoldersRightDragCandidate";
constexpr auto RightDragRequestedProperty = "_prismModFoldersRightDragRequested";
constexpr auto SuppressContextMenuProperty = "_prismModFoldersSuppressContextMenu";

#ifdef Q_OS_WIN
class ScopedDragWheelHandler final {
   public:
    explicit ScopedDragWheelHandler(ModListView* view) : m_view(view)
    {
        s_activeHandler = this;
        m_hook = SetWindowsHookExW(WH_GETMESSAGE, hookProcedure, nullptr, GetCurrentThreadId());
    }

    ~ScopedDragWheelHandler()
    {
        if (m_hook) {
            UnhookWindowsHookEx(m_hook);
        }
        if (s_activeHandler == this) {
            s_activeHandler = nullptr;
        }
    }

   private:
    static LRESULT CALLBACK hookProcedure(int code, WPARAM removeMode, LPARAM messagePointer)
    {
        auto* handler = s_activeHandler;
        auto* message = reinterpret_cast<MSG*>(messagePointer);
        if (code >= 0 && handler && removeMode == PM_REMOVE && message && message->message == WM_MOUSEWHEEL) {
            const int wheelDelta = static_cast<short>(HIWORD(message->wParam));
            if (handler->scroll(wheelDelta, QPoint(message->pt.x, message->pt.y))) {
                message->message = WM_NULL;
                message->wParam = 0;
                message->lParam = 0;
            }
        }

        return CallNextHookEx(handler ? handler->m_hook : nullptr, code, removeMode, messagePointer);
    }

    bool scroll(int wheelDelta, const QPoint& globalPosition)
    {
        if (!m_view || wheelDelta == 0) {
            return false;
        }

        auto* viewport = m_view->viewport();
        if (!viewport || !viewport->rect().contains(viewport->mapFromGlobal(globalPosition))) {
            return false;
        }

        auto* scrollBar = m_view->verticalScrollBar();
        if (!scrollBar) {
            return false;
        }

        const int step = qMax(48, m_view->fontMetrics().height() * 3);
        int distance = qRound(static_cast<double>(wheelDelta) * step / WHEEL_DELTA);
        if (distance == 0) {
            distance = wheelDelta > 0 ? 1 : -1;
        }

        const int oldValue = scrollBar->value();
        scrollBar->setValue(scrollBar->value() - distance);
        return scrollBar->value() != oldValue;
    }

   private:
    static thread_local ScopedDragWheelHandler* s_activeHandler;
    QPointer<ModListView> m_view;
    HHOOK m_hook = nullptr;
};

thread_local ScopedDragWheelHandler* ScopedDragWheelHandler::s_activeHandler = nullptr;
#else
class ScopedDragWheelHandler final : public QObject {
   public:
    explicit ScopedDragWheelHandler(ModListView* view) : m_view(view)
    {
        qApp->installEventFilter(this);
    }

    ~ScopedDragWheelHandler() override
    {
        qApp->removeEventFilter(this);
    }

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        Q_UNUSED(watched)

        if (!m_view || event->type() != QEvent::Wheel) {
            return false;
        }

        auto* viewport = m_view->viewport();
        if (!viewport || !viewport->rect().contains(viewport->mapFromGlobal(QCursor::pos()))) {
            return false;
        }

        auto* wheelEvent = static_cast<QWheelEvent*>(event);
        auto* scrollBar = m_view->verticalScrollBar();
        if (!scrollBar) {
            return false;
        }

        int distance = wheelEvent->pixelDelta().y();
        if (distance == 0) {
            const int angle = wheelEvent->angleDelta().y();
            if (angle == 0) {
                return false;
            }

            const int step = qMax(48, m_view->fontMetrics().height() * 3);
            distance = qRound(static_cast<double>(angle) * step / 120.0);
            if (distance == 0) {
                distance = angle > 0 ? 1 : -1;
            }
        }

        const int oldValue = scrollBar->value();
        scrollBar->setValue(oldValue - distance);
        if (scrollBar->value() == oldValue) {
            return false;
        }

        wheelEvent->accept();
        return true;
    }

   private:
    QPointer<ModListView> m_view;
};
#endif
}  // namespace

ModListView::ModListView(QWidget* parent) : QTreeView(parent)
{
    setAllColumnsShowFocus(true);
    setExpandsOnDoubleClick(false);
    setRootIsDecorated(false);
    setSortingEnabled(true);
    setAlternatingRowColors(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setHeaderHidden(false);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setDropIndicatorShown(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    viewport()->setAcceptDrops(true);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
}

void ModListView::setModel(QAbstractItemModel* model)
{
    QTreeView::setModel(model);
    auto head = header();
    head->setStretchLastSection(false);
    // HACK: this is true for the checkbox column of mod lists
    auto string = model->headerData(0, head->orientation()).toString();
    if (head->count() < 1) {
        return;
    }
    if (!string.size()) {
        head->setSectionResizeMode(0, QHeaderView::Interactive);
        head->setSectionResizeMode(1, QHeaderView::Stretch);
        for (int i = 2; i < head->count(); i++)
            head->setSectionResizeMode(i, QHeaderView::Interactive);
    } else {
        head->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int i = 1; i < head->count(); i++)
            head->setSectionResizeMode(i, QHeaderView::Interactive);
    }
}

void ModListView::setResizeModes(const QList<QHeaderView::ResizeMode>& modes)
{
    auto head = header();
    for (int i = 0; i < modes.count(); i++) {
        head->setSectionResizeMode(i, modes[i]);
    }
}

void ModListView::drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QTreeView::drawRow(painter, option, index);
    if (!index.data(ModFolderProxyModel::SeparatorRole).toBool()) {
        return;
    }

    const QRect separatorRect(0, option.rect.top(), viewport()->width(), option.rect.height());
    painter->save();
    painter->fillRect(separatorRect, option.palette.base());

    QPen pen;
    pen.setWidth(2);
    pen.setCapStyle(Qt::RoundCap);
    auto penColor = option.palette.text().color();
    penColor.setAlphaF(0.12f);
    pen.setColor(penColor);
    painter->setPen(pen);
    painter->setRenderHint(QPainter::Antialiasing, true);

    constexpr int horizontalPadding = 8;
    const qreal separatorY = separatorRect.top() + separatorRect.height() / 2.0;
    painter->drawLine(QLineF(separatorRect.left() + horizontalPadding, separatorY,
                             separatorRect.right() - horizontalPadding, separatorY));
    painter->restore();
}

void ModListView::startDrag(Qt::DropActions supportedActions)
{
    if (!property(RightDragRequestedProperty).toBool()) {
        return;
    }

    if (!model() || !selectionModel()) {
        return;
    }

    const auto indexes = selectedIndexes();
    if (indexes.isEmpty()) {
        return;
    }

    auto rowIndex = currentIndex();
    if (!rowIndex.isValid() || rowIndex.data(ModFolderProxyModel::FolderRole).toBool()) {
        return;
    }

    QString label;
    QPixmap icon;
    for (int column = 0; column < model()->columnCount(rowIndex.parent()); ++column) {
        const auto index = rowIndex.siblingAtColumn(column);
        if (label.isEmpty()) {
            const auto candidate = index.data(Qt::DisplayRole).toString();
            if (!candidate.isEmpty()) {
                label = candidate;
            }
        }
        if (icon.isNull()) {
            icon = qvariant_cast<QPixmap>(index.data(Qt::DecorationRole));
        }
    }

    if (label.isEmpty()) {
        QTreeView::startDrag(supportedActions);
        return;
    }

    auto* drag = new QDrag(this);
    drag->setMimeData(model()->mimeData(indexes));

    constexpr int iconSize = 32;
    constexpr int outerPadding = 6;
    constexpr int textGap = 7;
    const QFontMetrics metrics(font());
    const auto elidedLabel = metrics.elidedText(label, Qt::ElideRight, 240);
    const int height = qMax(iconSize, metrics.height()) + outerPadding * 2;
    const int width = outerPadding + (icon.isNull() ? 0 : iconSize + textGap) + metrics.horizontalAdvance(elidedLabel) + outerPadding;

    QPixmap preview(width, height);
    preview.fill(Qt::transparent);

    QPainter painter(&preview);
    painter.setRenderHint(QPainter::Antialiasing);
    auto background = palette().color(QPalette::Highlight);
    background.setAlpha(220);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(preview.rect().adjusted(0, 0, -1, -1), 4, 4);

    int textLeft = outerPadding;
    if (!icon.isNull()) {
        painter.drawPixmap(QRect(outerPadding, outerPadding, iconSize, iconSize),
                           icon.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        textLeft += iconSize + textGap;
    }
    painter.setPen(palette().color(QPalette::HighlightedText));
    painter.drawText(QRect(textLeft, 0, width - textLeft - outerPadding, height), Qt::AlignVCenter | Qt::AlignLeft, elidedLabel);
    painter.end();

    drag->setPixmap(preview);
    drag->setHotSpot(QPoint(qMin(width / 2, 24), height / 2));

    ScopedDragWheelHandler wheelHandler(this);
    drag->exec(supportedActions, Qt::MoveAction);
}

void ModListView::mousePressEvent(QMouseEvent* event)
{
    setProperty(SuppressContextMenuProperty, false);
    QTreeView::mousePressEvent(event);

    if (event->button() != Qt::RightButton || !model()) {
        setProperty(RightDragCandidateProperty, false);
        return;
    }

    const auto index = indexAt(event->position().toPoint());
    const bool isDraggable = index.isValid() && !index.data(ModFolderProxyModel::FolderRole).toBool() &&
                             !index.data(ModFolderProxyModel::SeparatorRole).toBool();
    setProperty(RightDragCandidateProperty, isDraggable);
    setProperty(RightDragStartProperty, event->position().toPoint());
}

void ModListView::mouseMoveEvent(QMouseEvent* event)
{
    if (property(RightDragCandidateProperty).toBool() && (event->buttons() & Qt::RightButton)) {
        const auto startPosition = property(RightDragStartProperty).toPoint();
        const int distance = (event->position().toPoint() - startPosition).manhattanLength();
        if (distance >= QApplication::startDragDistance()) {
            setProperty(RightDragCandidateProperty, false);
            setProperty(SuppressContextMenuProperty, true);
            setProperty(RightDragRequestedProperty, true);
            startDrag(model()->supportedDragActions());
            setProperty(RightDragRequestedProperty, false);
            event->accept();
            return;
        }
    }

    if (event->buttons() & Qt::RightButton) {
        event->accept();
        return;
    }

    QTreeView::mouseMoveEvent(event);
}

void ModListView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        setProperty(RightDragCandidateProperty, false);
    }
    QTreeView::mouseReleaseEvent(event);
}

void ModListView::contextMenuEvent(QContextMenuEvent* event)
{
    if (property(SuppressContextMenuProperty).toBool()) {
        setProperty(SuppressContextMenuProperty, false);
        event->accept();
        return;
    }
    QTreeView::contextMenuEvent(event);
}

void ModListView::dragMoveEvent(QDragMoveEvent* event)
{
    QTreeView::dragMoveEvent(event);
    QModelIndex targetFolder;
    if (event->mimeData()->hasFormat(ModFolderProxyModel::MimeType) &&
        modFolderDropTargetAt(event->position().toPoint(), &targetFolder)) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }
}

void ModListView::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasFormat(ModFolderProxyModel::MimeType)) {
        QModelIndex targetFolder;
        if (modFolderDropTargetAt(event->position().toPoint(), &targetFolder) &&
            model()->dropMimeData(event->mimeData(), Qt::MoveAction, -1, -1, targetFolder)) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
            return;
        }
    }
    QTreeView::dropEvent(event);
}

bool ModListView::modFolderDropTargetAt(const QPoint& position, QModelIndex* targetFolder) const
{
    if (!targetFolder || !viewport()->rect().contains(position)) {
        return false;
    }

    *targetFolder = {};
    const auto hovered = indexAt(position);
    if (!hovered.isValid()) {
        // Empty viewport space represents the ungrouped root list.
        return true;
    }
    if (hovered.data(ModFolderProxyModel::FolderRole).toBool()) {
        *targetFolder = hovered.siblingAtColumn(0);
        return true;
    }

    const auto parent = hovered.parent();
    if (parent.data(ModFolderProxyModel::FolderRole).toBool()) {
        *targetFolder = parent.siblingAtColumn(0);
        return true;
    }

    // A root-level mod or separator represents the ungrouped list. Passing an
    // invalid parent to ModFolderProxyModel::dropMimeData unassigns the mod.
    return !parent.isValid();
}
