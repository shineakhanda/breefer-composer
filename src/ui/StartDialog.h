#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace bc {

// Premiere-style Home screen: recent projects, new project, settings.
class StartDialog : public QDialog {
    Q_OBJECT

public:
    explicit StartDialog(QWidget* parent = nullptr);

    QString projectToOpen() const { return m_projectToOpen; }
    QString newProjectName() const { return m_newName; }
    QString newProjectFolder() const { return m_newFolder; }

private:
    void buildUi();
    void refreshRecents();
    void createProject();
    void openProjectDialog();
    void openRecent(QListWidgetItem* item);
    void showSettings();
    void remember(const QString& path);

    QString m_projectToOpen;
    QString m_newName;
    QString m_newFolder;

    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_folderEdit = nullptr;
    QListWidget* m_recents = nullptr;
};

} // namespace bc
