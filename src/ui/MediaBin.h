#pragma once

#include <QHash>
#include <QIcon>
#include <QWidget>

class QLineEdit;
class QLabel;
class QListWidget;
class QSlider;
class QListWidgetItem;
class QToolButton;

namespace bc {

class Document;

class MediaBin : public QWidget {
    Q_OBJECT

public:
    explicit MediaBin(Document* document, QWidget* parent = nullptr);

    QString currentMediaId() const;
    QStringList selectedMediaIds() const;

    // Probes and adds files; returns the new media ids in order.
    QStringList importPaths(const QStringList& paths, QStringList* failures = nullptr);

signals:
    void mediaActivated(const QString& mediaId);
    void insertRequested(const QString& mediaId);
    void overwriteRequested(const QString& mediaId);
    void newSequenceRequested();
    void newTitleRequested();
    void newColorMatteRequested();
    void newAdjustmentRequested();
    void newModelRequested();
    void newRemotionRequested();
    void rerenderRequested(const QString& mediaId);
    void sequenceActivated(const QString& sequenceId);
    void statusMessage(const QString& message);

public slots:
    void importFiles();
    void importFolder();
    void removeSelected();
    void refresh();

private:
    void showContextMenu(const QPoint& pos);
    QIcon iconFor(const QString& mediaId, const QString& path, bool hasVideo, bool offline);
    void relink(const QString& mediaId);

    Document* m_document;
    QListWidget* m_list = nullptr;
    QLineEdit* m_search = nullptr;
    QSlider* m_sizeSlider = nullptr;
    QLabel* m_countLabel = nullptr;
    bool m_refreshQueued = false;
    QHash<QString, QIcon> m_icons;
    bool m_iconMode = true;
    QString m_currentBin;            // folder shown ("" = project root)
    QStringList childBins(const QString& parent) const;
    void newBin();
};

} // namespace bc
