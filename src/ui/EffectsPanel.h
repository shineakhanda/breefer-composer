#pragma once

#include <QWidget>

class QLineEdit;
class QListWidget;

namespace bc {

class Document;

// Searchable effect browser. Double-click applies the effect to the selected clip.
class EffectsPanel : public QWidget {
    Q_OBJECT

public:
    explicit EffectsPanel(Document* document, QWidget* parent = nullptr);

private:
    void applyEffect(const QString& effectType);
    void populate(const QString& filter);

    Document* m_document;
    QLineEdit* m_search = nullptr;
    QListWidget* m_list = nullptr;
};

} // namespace bc
