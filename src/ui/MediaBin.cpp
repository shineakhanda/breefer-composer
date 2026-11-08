#include "ui/MediaBin.h"

#include "core/Document.h"
#include "media/MediaProbe.h"
#include "media/ThumbnailCache.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QInputDialog>
#include <QLabel>
#include <QSlider>
#include <QTimer>

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace bc {
namespace {

const QString kMediaMime = QStringLiteral("application/x-breefer-media");
const QString kSequencePrefix = QStringLiteral("seq:");
const QString kBinPrefix = QStringLiteral("bin:");

const QStringList kMediaSuffixes = {
    "mp4", "mov", "mkv", "avi", "m4v", "webm", "mxf", "mts", "m2ts", "wmv", "flv", "mpg", "mpeg", "3gp",
    "wav", "mp3", "m4a", "aac", "flac", "ogg", "opus", "aif", "aiff",
    "png", "jpg", "jpeg", "bmp", "tif", "tiff", "webp", "gif"};

// List that drags media ids to the timeline.
class MediaList : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    QStringList mimeTypes() const override { return {kMediaMime}; }

    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override
    {
        QStringList ids;
        for (auto* item : items) {
            const QString id = item->data(Qt::UserRole).toString();
            if (!id.startsWith(kSequencePrefix) && !id.startsWith(kBinPrefix)) ids << id;
        }
        auto* data = new QMimeData;
        data->setData(kMediaMime, ids.join(QLatin1Char('\n')).toUtf8());
        return data;
    }
};

QString durationText(Tick t)
{
    const double s = ticksToSec(t);
    const int m = static_cast<int>(s) / 60;
    return QStringLiteral("%1:%2").arg(m).arg(s - m * 60, 4, 'f', 1, QLatin1Char('0'));
}

} // namespace

MediaBin::MediaBin(Document* document, QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Top: search, like Premiere's Project panel.
    auto* top = new QWidget(this);
    auto* topRow = new QHBoxLayout(top);
    topRow->setContentsMargins(6, 4, 6, 4);
    topRow->setSpacing(4);
    m_search = new QLineEdit(top);
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(icons::icon(icons::Id::Search), QLineEdit::LeadingPosition);
    topRow->addWidget(m_search, 1);
    m_countLabel = new QLabel(top);
    m_countLabel->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
    topRow->addWidget(m_countLabel);
    root->addWidget(top);

    m_list = new MediaList(this);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    root->addWidget(m_list, 1);

    // Bottom bar: list/icon view, thumbnail size, new item, import, clear.
    auto* bottom = new QWidget(this);
    auto* row = new QHBoxLayout(bottom);
    row->setContentsMargins(4, 2, 4, 2);
    row->setSpacing(1);
    auto* listView = iconButton(icons::Id::ListView, tr("List View"), bottom, true, 15);
    auto* iconView = iconButton(icons::Id::IconView, tr("Icon View"), bottom, true, 15);
    iconView->setChecked(true);
    connect(listView, &QToolButton::clicked, this, [this, listView, iconView] {
        m_iconMode = false; listView->setChecked(true); iconView->setChecked(false); refresh();
    });
    connect(iconView, &QToolButton::clicked, this, [this, listView, iconView] {
        m_iconMode = true; listView->setChecked(false); iconView->setChecked(true); refresh();
    });
    row->addWidget(listView);
    row->addWidget(iconView);
    m_sizeSlider = new QSlider(Qt::Horizontal, bottom);
    m_sizeSlider->setRange(60, 200);
    m_sizeSlider->setValue(100);
    m_sizeSlider->setMaximumWidth(90);
    m_sizeSlider->setToolTip(tr("Adjust the size of icons and thumbnails"));
    connect(m_sizeSlider, &QSlider::valueChanged, this, [this] { refresh(); });
    row->addWidget(m_sizeSlider);
    row->addStretch(1);
    auto* importButton = iconButton(icons::Id::Import, tr("Import (Ctrl+I)"), bottom, false, 15);
    connect(importButton, &QToolButton::clicked, this, &MediaBin::importFiles);
    row->addWidget(importButton);
    auto* newItem = iconButton(icons::Id::NewItem, tr("New Item"), bottom, false, 15);
    newItem->setPopupMode(QToolButton::InstantPopup);
    auto* newMenu = new QMenu(newItem);
    newMenu->addAction(icons::icon(icons::Id::Sequence), tr("Sequence..."), this, &MediaBin::newSequenceRequested);
    newMenu->addAction(icons::icon(icons::Id::Title), tr("Title..."), this, &MediaBin::newTitleRequested);
    newMenu->addAction(icons::icon(icons::Id::Matte), tr("Color Matte..."), this, &MediaBin::newColorMatteRequested);
    newMenu->addAction(icons::icon(icons::Id::Nest), tr("Adjustment Layer"), this, &MediaBin::newAdjustmentRequested);
    newMenu->addAction(icons::icon(icons::Id::Still), tr("3D Model (OBJ)..."), this, &MediaBin::newModelRequested);
    newMenu->addAction(icons::icon(icons::Id::Film), tr("Remotion / React Composition..."), this, &MediaBin::newRemotionRequested);
    newItem->setMenu(newMenu);
    row->addWidget(newItem);
    auto* newBin = iconButton(icons::Id::NewBin, tr("New Bin"), bottom, false, 15);
    connect(newBin, &QToolButton::clicked, this, &MediaBin::newBin);
    row->addWidget(newBin);
    auto* trash = iconButton(icons::Id::Trash, tr("Clear (remove selected media)"), bottom, false, 15);
    connect(trash, &QToolButton::clicked, this, &MediaBin::removeSelected);
    row->addWidget(trash);
    root->addWidget(bottom);

    connect(&ThumbnailCache::instance(), &ThumbnailCache::ready, this, [this](const QString&) {
        if (!m_refreshQueued) {
            m_refreshQueued = true;
            QTimer::singleShot(150, this, [this] { m_refreshQueued = false; m_icons.clear(); refresh(); });
        }
    });
    connect(m_search, &QLineEdit::textChanged, this, &MediaBin::refresh);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (!item) return;
        const QString id = item->data(Qt::UserRole).toString();
        if (id.startsWith(kBinPrefix)) {
            m_currentBin = id.mid(kBinPrefix.size());
            refresh();
        } else if (id.startsWith(kSequencePrefix)) emit sequenceActivated(id.mid(kSequencePrefix.size()));
        else emit mediaActivated(id);
    });
    connect(m_list, &QListWidget::customContextMenuRequested, this, &MediaBin::showContextMenu);
    connect(m_document, &Document::mediaChanged, this, &MediaBin::refresh);
    connect(m_document, &Document::sequencesChanged, this, &MediaBin::refresh);
    connect(m_document, &Document::documentLoaded, this, [this] {
        m_icons.clear();
        refresh();
    });

    refresh();
}

QString MediaBin::currentMediaId() const
{
    if (auto* item = m_list->currentItem()) {
        const QString id = item->data(Qt::UserRole).toString();
        if (!id.startsWith(kSequencePrefix)) return id;
    }
    return {};
}

QStringList MediaBin::selectedMediaIds() const
{
    QStringList ids;
    for (auto* item : m_list->selectedItems()) {
        const QString id = item->data(Qt::UserRole).toString();
        if (!id.startsWith(kSequencePrefix) && !id.startsWith(kBinPrefix)) ids << id;
    }
    return ids;
}

QIcon MediaBin::iconFor(const QString& mediaId, const QString& path, bool hasVideo, bool offline)
{
    auto it = m_icons.find(mediaId);
    if (it != m_icons.end()) return it.value();

    const QSize size(148, 83);
    QPixmap px(size);
    px.fill(offline ? QColor(0x4a, 0x1c, 0x1c) : QColor(0x12, 0x12, 0x12));
    QPainter p(&px);
    bool final = true;
    if (hasVideo && !offline) {
        const QImage thumb = ThumbnailCache::instance().get(path, size.width(), size.height());
        if (thumb.width() > 1) {
            const QImage scaled = thumb.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawImage((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2, scaled);
        } else {
            final = thumb.isNull() ? false : true;
            icons::paint(p, icons::Id::Film, QRectF(size.width() / 2 - 14, size.height() / 2 - 14, 28, 28), theme::textFaint());
        }
    } else {
        icons::paint(p, offline ? icons::Id::Close : icons::Id::Audio,
                     QRectF(size.width() / 2 - 14, size.height() / 2 - 14, 28, 28),
                     offline ? QColor(0xff, 0x80, 0x80) : QColor(0x4c, 0xae, 0x55));
    }
    p.end();
    QIcon icon(px);
    if (final) m_icons.insert(mediaId, icon);
    return icon;
}

void MediaBin::refresh()
{
    const QString keep = m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString();
    m_list->clear();

    const double scale = m_sizeSlider ? m_sizeSlider->value() / 100.0 : 1.0;
    if (m_iconMode) {
        m_list->setViewMode(QListView::IconMode);
        m_list->setIconSize(QSize(static_cast<int>(124 * scale), static_cast<int>(70 * scale)));
        m_list->setGridSize(QSize(static_cast<int>(132 * scale) + 4, static_cast<int>(70 * scale) + 34));
        m_list->setResizeMode(QListView::Adjust);
        m_list->setMovement(QListView::Static);
        m_list->setWordWrap(true);
        m_list->setUniformItemSizes(true);
        m_list->setSpacing(4);
    } else {
        m_list->setViewMode(QListView::ListMode);
        m_list->setIconSize(QSize(static_cast<int>(36 * scale), static_cast<int>(20 * scale)));
        m_list->setGridSize(QSize());
        m_list->setWordWrap(false);
        m_list->setSpacing(1);
    }
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);

    const QString filter = m_search->text().trimmed();
    const bool searching = !filter.isEmpty();
    if (!m_currentBin.isEmpty() && !childBins(QString()).isEmpty() && !m_document->project().bins.contains(m_currentBin)) {
        bool used = false;
        for (const auto& m : m_document->project().media) used = used || m.bin.startsWith(m_currentBin);
        if (!used) m_currentBin.clear();
    }
    m_countLabel->setText(m_currentBin.isEmpty() ? QString() : m_currentBin);

    auto folderIcon = [] {
        QPixmap px(148, 83);
        px.fill(QColor(0x12, 0x12, 0x12));
        QPainter p(&px);
        icons::paint(p, icons::Id::Folder, QRectF(52, 14, 44, 44), QColor(0xc8, 0xa9, 0x5a));
        p.end();
        return QIcon(px);
    };
    if (!searching && !m_currentBin.isEmpty()) {
        auto* up = new QListWidgetItem(folderIcon(), QStringLiteral(".."), m_list);
        up->setData(Qt::UserRole, kBinPrefix + m_currentBin.section(QLatin1Char('/'), 0, -2));
        up->setToolTip(tr("Up one level"));
    }
    if (!searching) {
        for (const QString& bin : childBins(m_currentBin)) {
            auto* item = new QListWidgetItem(folderIcon(), bin.section(QLatin1Char('/'), -1), m_list);
            item->setData(Qt::UserRole, kBinPrefix + bin);
            item->setToolTip(tr("Bin - double-click to open"));
        }
    }

    // Sequences first (root only), as in Premiere's bin.
    for (const auto& seq : m_document->project().sequences) {
        if (!searching && !m_currentBin.isEmpty()) break;
        if (!filter.isEmpty() && !seq.name.contains(filter, Qt::CaseInsensitive)) continue;
        auto* item = new QListWidgetItem(m_list);
        const bool active = m_document->sequence() && m_document->sequence()->id == seq.id;
        item->setText(m_iconMode ? seq.name : QStringLiteral("%1   %2x%3 %4fps").arg(seq.name).arg(seq.width).arg(seq.height).arg(seq.rate.label()));
        item->setData(Qt::UserRole, kSequencePrefix + seq.id);
        QPixmap px(148, 83);
        px.fill(QColor(0x12, 0x12, 0x12));
        QPainter p(&px);
        icons::paint(p, icons::Id::Sequence, QRectF(56, 18, 36, 36), active ? theme::accent() : QColor(0x9a, 0x9a, 0x9a));
        p.setPen(QColor(0x9a, 0x9a, 0x9a));
        p.setFont(theme::uiFont(7));
        p.drawText(px.rect().adjusted(6, 0, -6, -4), Qt::AlignHCenter | Qt::AlignBottom,
                   QStringLiteral("%1x%2  %3 fps").arg(seq.width).arg(seq.height).arg(seq.rate.label()));
        p.end();
        item->setIcon(QIcon(px));
        item->setToolTip(tr("Double-click to open in the Timeline"));
        if (kSequencePrefix + seq.id == keep) m_list->setCurrentItem(item);
    }

    for (const auto& media : m_document->project().media) {
        if (!filter.isEmpty() && !media.name.contains(filter, Qt::CaseInsensitive)) continue;
        if (!searching && media.bin != m_currentBin) continue;
        auto* item = new QListWidgetItem(m_list);
        QString text = media.name;
        if (!m_iconMode) {
            text = QStringLiteral("%1   %2").arg(media.name, media.isStillImage() ? tr("still") : durationText(media.duration));
            if (media.hasVideo) text += QStringLiteral("   %1x%2").arg(media.width).arg(media.height);
        }
        if (media.offline) text = tr("[offline] ") + text;
        item->setText(text);
        item->setData(Qt::UserRole, media.id);
        item->setIcon(iconFor(media.id, media.proxyPath.isEmpty() ? media.path : media.proxyPath, media.hasVideo, media.offline));
        item->setTextAlignment(m_iconMode ? (Qt::AlignHCenter | Qt::AlignTop) : (Qt::AlignLeft | Qt::AlignVCenter));
        item->setToolTip(QStringLiteral("%1\n%2x%3 - %4 fps - %5 s%6%7")
                             .arg(media.path)
                             .arg(media.width)
                             .arg(media.height)
                             .arg(media.rate.value(), 0, 'f', 2)
                             .arg(ticksToSec(media.duration), 0, 'f', 2)
                             .arg(media.proxyPath.isEmpty() ? QString() : tr("\nProxy attached"))
                             .arg(media.offline ? tr("\nMEDIA OFFLINE - right-click to relink") : QString()));
        if (media.id == keep) m_list->setCurrentItem(item);
    }
}

QStringList MediaBin::importPaths(const QStringList& paths, QStringList* failures)
{
    QStringList ids;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const QString& file : paths) {
        // Skip duplicates (same path already in the project).
        bool exists = false;
        for (const auto& m : m_document->project().media) {
            if (QFileInfo(m.path) == QFileInfo(file)) {
                ids << m.id;
                exists = true;
                break;
            }
        }
        if (exists) continue;
        ProbeResult probe = probeMedia(file);
        if (!probe.ok) {
            if (failures) *failures << QStringLiteral("%1: %2").arg(QFileInfo(file).fileName(), probe.error);
            continue;
        }
        probe.ref.bin = m_currentBin;
        m_document->addMedia(probe.ref);
        ids << probe.ref.id;
    }
    QApplication::restoreOverrideCursor();
    refresh();
    return ids;
}

void MediaBin::importFiles()
{
    QStringList patterns;
    for (const QString& s : kMediaSuffixes) patterns << QStringLiteral("*.") + s;
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Import Media"), QString(),
        tr("Media Files (%1);;All Files (*)").arg(patterns.join(QLatin1Char(' '))));
    if (files.isEmpty()) return;

    QStringList failures;
    importPaths(files, &failures);
    if (!failures.isEmpty())
        QMessageBox::warning(this, tr("Some files could not be imported"), failures.join(QLatin1Char('\n')));
}

QStringList MediaBin::childBins(const QString& parent) const
{
    QStringList all = m_document->project().bins;
    for (const auto& m : m_document->project().media)
        if (!m.bin.isEmpty()) all << m.bin;
    QStringList out;
    const QString prefix = parent.isEmpty() ? QString() : parent + QLatin1Char('/');
    for (const QString& b : all) {
        if (!b.startsWith(prefix) || b == parent) continue;
        const QString rest = b.mid(prefix.size());
        const QString child = prefix + rest.section(QLatin1Char('/'), 0, 0);
        if (!rest.isEmpty() && !out.contains(child)) out << child;
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

void MediaBin::newBin()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Bin"), tr("Bin name:"), QLineEdit::Normal, tr("Bin"), &ok)
                             .trimmed().replace(QLatin1Char('/'), QLatin1Char('-'));
    if (!ok || name.isEmpty()) return;
    const QString path = m_currentBin.isEmpty() ? name : m_currentBin + QLatin1Char('/') + name;
    if (!m_document->project().bins.contains(path)) m_document->project().bins << path;
    m_document->markMetaDirty();
    refresh();
}

void MediaBin::removeSelected()
{
    const QStringList ids = selectedMediaIds();
    if (ids.isEmpty()) return;
    if (QMessageBox::question(this, tr("Clear"), tr("Remove %n item(s) and every clip that uses them?", nullptr, ids.size()))
        != QMessageBox::Yes) return;
    for (const QString& id : ids) m_document->removeMedia(id);
}

void MediaBin::importFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Import Folder"));
    if (dir.isEmpty()) return;
    QStringList files;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        if (kMediaSuffixes.contains(QFileInfo(f).suffix().toLower())) files << f;
    }
    QStringList failures;
    const QStringList ids = importPaths(files, &failures);
    emit statusMessage(tr("Imported %1 file(s) from %2").arg(ids.size()).arg(QDir(dir).dirName()));
}

void MediaBin::relink(const QString& mediaId)
{
    const MediaRef* ref = m_document->project().mediaById(mediaId);
    if (!ref) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Locate %1").arg(ref->name),
                                                      QFileInfo(ref->path).absolutePath());
    if (path.isEmpty()) return;
    ProbeResult probe = probeMedia(path);
    if (!probe.ok) {
        QMessageBox::warning(this, tr("Relink failed"), probe.error);
        return;
    }
    m_icons.remove(mediaId);
    m_document->replaceMediaPath(mediaId, probe.ref);
    emit statusMessage(tr("Relinked %1").arg(QFileInfo(path).fileName()));
}

void MediaBin::showContextMenu(const QPoint& pos)
{
    QListWidgetItem* item = m_list->itemAt(pos);
    QMenu menu(this);
    if (!item) {
        menu.addAction(tr("Import..."), this, &MediaBin::importFiles);
        menu.addAction(tr("Import Folder..."), this, &MediaBin::importFolder);
        menu.addSeparator();
        menu.addAction(tr("New Sequence..."), this, &MediaBin::newSequenceRequested);
        menu.addAction(tr("New Title"), this, &MediaBin::newTitleRequested);
        menu.addAction(tr("New Color Matte"), this, &MediaBin::newColorMatteRequested);
        menu.exec(m_list->mapToGlobal(pos));
        return;
    }

    const QString id = item->data(Qt::UserRole).toString();
    if (id.startsWith(kSequencePrefix)) {
        const QString seqId = id.mid(kSequencePrefix.size());
        menu.addAction(tr("Open in Timeline"), this, [this, seqId] { emit sequenceActivated(seqId); });
        menu.exec(m_list->mapToGlobal(pos));
        return;
    }

    const MediaRef* ref = m_document->project().mediaById(id);
    if (!ref) return;
    const QString mediaPath = ref->path;
    menu.addAction(tr("Open in Source Monitor"), this, [this, id] { emit mediaActivated(id); });
    menu.addAction(tr("Insert at Playhead (,)"), this, [this, id] { emit insertRequested(id); });
    menu.addAction(tr("Overwrite at Playhead (.)"), this, [this, id] { emit overwriteRequested(id); });
    if (!ref->remotionEntry.isEmpty())
        menu.addAction(tr("Re-render Remotion Composition"), this, [this, id] { emit rerenderRequested(id); });
    QMenu* move = menu.addMenu(tr("Move to Bin"));
    QStringList allBins = m_document->project().bins;
    for (const auto& m : m_document->project().media)
        if (!m.bin.isEmpty() && !allBins.contains(m.bin)) allBins << m.bin;
    allBins.sort();
    allBins.prepend(QString());
    const QStringList selection = selectedMediaIds().isEmpty() ? QStringList{id} : selectedMediaIds();
    for (const QString& bin : allBins) {
        move->addAction(bin.isEmpty() ? tr("(Project root)") : bin, this, [this, selection, bin] {
            for (const QString& mid : selection)
                if (MediaRef* m = m_document->project().mediaById(mid)) m->bin = bin;
            m_document->markMetaDirty();
            refresh();
        });
    }
    move->addSeparator();
    move->addAction(tr("New Bin..."), this, &MediaBin::newBin);
    menu.addSeparator();
    QAction* relinkAction = menu.addAction(tr("Link Media..."), this, [this, id] { relink(id); });
    relinkAction->setEnabled(true);
    menu.addAction(tr("Reveal in Explorer"), this, [mediaPath] {
#ifdef Q_OS_WIN
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,"), QDir::toNativeSeparators(mediaPath)});
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(mediaPath).absolutePath()));
#endif
    });
    menu.addSeparator();
    menu.addAction(tr("Remove from Project"), this, [this, id, name = ref->name] {
        if (QMessageBox::question(this, tr("Remove media"),
                                  tr("Remove \"%1\" and every clip that uses it?\nThis clears the undo history.").arg(name))
            == QMessageBox::Yes) {
            m_icons.remove(id);
            m_document->removeMedia(id);
        }
    });
    menu.exec(m_list->mapToGlobal(pos));
}

} // namespace bc
