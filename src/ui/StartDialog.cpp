#include "ui/StartDialog.h"

#include "ui/Theme.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace bc {

StartDialog::StartDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Breefer Composer"));
    setMinimumSize(760, 460);
    buildUi();
    refreshRecents();
}

void StartDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 20, 22, 16);
    root->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("Breefer Composer"), this);
    title->setStyleSheet(QStringLiteral("font-size: 20pt; font-weight: 700; color: #ffffff;"));
    root->addWidget(title);

    auto* subtitle = new QLabel(tr("Free, open-source non-linear editor"), this);
    subtitle->setStyleSheet(QStringLiteral("color: #8a8a92; font-size: 10pt;"));
    root->addWidget(subtitle);

    auto* columns = new QHBoxLayout;
    columns->setSpacing(18);

    // ---- recent projects
    auto* recentBox = new QGroupBox(tr("Recent Projects"), this);
    auto* recentLayout = new QVBoxLayout(recentBox);

    m_recents = new QListWidget(recentBox);
    m_recents->setAlternatingRowColors(false);
    recentLayout->addWidget(m_recents, 1);

    auto* openButton = new QPushButton(tr("Open Project..."), recentBox);
    recentLayout->addWidget(openButton);

    columns->addWidget(recentBox, 3);

    // ---- new project
    auto* newBox = new QGroupBox(tr("New Project"), this);
    auto* newLayout = new QVBoxLayout(newBox);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_nameEdit = new QLineEdit(tr("Untitled Project"), newBox);
    form->addRow(tr("Name"), m_nameEdit);

    auto* folderRow = new QWidget(newBox);
    auto* folderLayout = new QHBoxLayout(folderRow);
    folderLayout->setContentsMargins(0, 0, 0, 0);
    m_folderEdit = new QLineEdit(
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), folderRow);
    folderLayout->addWidget(m_folderEdit, 1);
    auto* browse = new QPushButton(QStringLiteral("..."), folderRow);
    browse->setFixedWidth(32);
    folderLayout->addWidget(browse);
    form->addRow(tr("Location"), folderRow);

    newLayout->addLayout(form);
    newLayout->addStretch(1);

    auto* createButton = new QPushButton(tr("Create Project"), newBox);
    createButton->setDefault(true);
    createButton->setMinimumHeight(30);
    newLayout->addWidget(createButton);

    columns->addWidget(newBox, 2);
    root->addLayout(columns, 1);

    // ---- bottom row
    auto* bottom = new QHBoxLayout;
    auto* settingsButton = new QPushButton(tr("Settings"), this);
    auto* quitButton = new QPushButton(tr("Exit"), this);
    bottom->addWidget(settingsButton);
    bottom->addStretch(1);
    bottom->addWidget(quitButton);
    root->addLayout(bottom);

    connect(createButton, &QPushButton::clicked, this, &StartDialog::createProject);
    connect(openButton, &QPushButton::clicked, this, &StartDialog::openProjectDialog);
    connect(settingsButton, &QPushButton::clicked, this, &StartDialog::showSettings);
    connect(quitButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_recents, &QListWidget::itemDoubleClicked, this, &StartDialog::openRecent);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Project Location"),
                                                              m_folderEdit->text());
        if (!dir.isEmpty()) m_folderEdit->setText(dir);
    });
}

void StartDialog::refreshRecents()
{
    QSettings settings(QStringLiteral("Breefer"), QStringLiteral("Composer"));
    const QStringList recents = settings.value(QStringLiteral("recentProjects")).toStringList();

    m_recents->clear();
    for (const QString& path : recents) {
        auto* item = new QListWidgetItem(QFileInfo(path).fileName(), m_recents);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
    }

    if (recents.isEmpty()) {
        auto* empty = new QListWidgetItem(tr("No recent projects yet."), m_recents);
        empty->setFlags(Qt::NoItemFlags);
        empty->setForeground(theme::textDim());
    }
}

void StartDialog::remember(const QString& path)
{
    QSettings settings(QStringLiteral("Breefer"), QStringLiteral("Composer"));
    QStringList recents = settings.value(QStringLiteral("recentProjects")).toStringList();
    recents.removeAll(path);
    recents.prepend(path);
    while (recents.size() > 10) recents.removeLast();
    settings.setValue(QStringLiteral("recentProjects"), recents);
}

void StartDialog::createProject()
{
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) return;

    QDir folder(m_folderEdit->text().trimmed());
    if (!folder.exists() && !QDir().mkpath(folder.absolutePath())) return;

    m_newName = name;
    m_newFolder = folder.absolutePath();
    m_projectToOpen.clear();
    remember(folder.absoluteFilePath(name + QStringLiteral(".bcproj")));
    accept();
}

void StartDialog::openProjectDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Project"), QString(),
        tr("Breefer Project (*.bcproj);;All Files (*)"));
    if (path.isEmpty()) return;

    m_projectToOpen = path;
    m_newName.clear();
    remember(path);
    accept();
}

void StartDialog::openRecent(QListWidgetItem* item)
{
    if (!item) return;
    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;

    m_projectToOpen = path;
    m_newName.clear();
    remember(path);
    accept();
}

void StartDialog::showSettings()
{
    QSettings settings(QStringLiteral("Breefer"), QStringLiteral("Composer"));

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Settings"));
    dialog.setMinimumWidth(420);

    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;

    auto* cache = new QLineEdit(settings.value(QStringLiteral("cacheDir")).toString(), &dialog);
    cache->setPlaceholderText(tr("Default system cache"));
    form->addRow(tr("Media cache folder"), cache);

    auto* autosave = new QSpinBox(&dialog);
    autosave->setRange(0, 120);
    autosave->setValue(settings.value(QStringLiteral("autosaveMinutes"), 10).toInt());
    autosave->setSuffix(tr(" min"));
    autosave->setSpecialValueText(tr("Off"));
    form->addRow(tr("Auto-save every"), autosave);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() == QDialog::Accepted) {
        settings.setValue(QStringLiteral("cacheDir"), cache->text());
        settings.setValue(QStringLiteral("autosaveMinutes"), autosave->value());
    }
}

} // namespace bc
