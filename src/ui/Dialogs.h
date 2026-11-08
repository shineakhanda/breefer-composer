#pragma once

#include "core/Project.h"
#include "media/Exporter.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace bc {

// Edits a TitleSpec with a live preview.
class TitleDialog : public QDialog {
    Q_OBJECT

public:
    TitleDialog(const TitleSpec& spec, int seqWidth, int seqHeight, QWidget* parent = nullptr);
    TitleSpec spec() const;

private:
    void updatePreview();
    QPlainTextEdit* m_text = nullptr;
    QFontComboBox* m_font = nullptr;
    QDoubleSpinBox* m_size = nullptr;
    QCheckBox* m_bold = nullptr;
    QCheckBox* m_italic = nullptr;
    QComboBox* m_align = nullptr;
    QDoubleSpinBox* m_x = nullptr;
    QDoubleSpinBox* m_y = nullptr;
    QCheckBox* m_background = nullptr;
    QCheckBox* m_shadow = nullptr;
    QDoubleSpinBox* m_outline = nullptr;
    QPushButton* m_color = nullptr;
    QPushButton* m_bgColor = nullptr;
    QLabel* m_preview = nullptr;
    QColor m_textColor;
    QColor m_backgroundColor;
    int m_seqWidth;
    int m_seqHeight;
};

// New sequence / sequence settings with presets (incl. vertical and square).
class SequenceDialog : public QDialog {
    Q_OBJECT

public:
    SequenceDialog(const QString& name, int width, int height, FrameRate rate, bool isNew, QWidget* parent = nullptr);
    QString name() const;
    int sequenceWidth() const;
    int sequenceHeight() const;
    FrameRate rate() const;

private:
    QLineEdit* m_name = nullptr;
    QComboBox* m_preset = nullptr;
    QSpinBox* m_width = nullptr;
    QSpinBox* m_height = nullptr;
    QComboBox* m_rate = nullptr;
};

// Export Settings: presets, format, codec, size, bitrate, range, destination.
class ExportDialog : public QDialog {
    Q_OBJECT

public:
    ExportDialog(const Sequence& seq, const QString& baseName, const QString& lastFolder, QWidget* parent = nullptr);
    Exporter::Settings settings() const;
    bool queueOnly() const { return m_queueOnly; }

private:
    void applyPreset(int index);
    void updateSummary();
    void updatePathSuffix();

    const Sequence& m_seq;
    QVector<Exporter::Preset> m_presets;
    QComboBox* m_preset = nullptr;
    QComboBox* m_container = nullptr;
    QComboBox* m_codec = nullptr;
    QComboBox* m_audioCodec = nullptr;
    QSpinBox* m_width = nullptr;
    QSpinBox* m_height = nullptr;
    QSpinBox* m_bitrate = nullptr;
    QSpinBox* m_audioBitrate = nullptr;
    QCheckBox* m_audio = nullptr;
    QCheckBox* m_hardware = nullptr;
    QComboBox* m_range = nullptr;
    QLineEdit* m_path = nullptr;
    QLabel* m_summary = nullptr;
    bool m_queueOnly = false;
};

// Remotion / React composition picker (project folder, bundle or React plugin).
class RemotionDialog : public QDialog {
    Q_OBJECT

public:
    explicit RemotionDialog(QWidget* parent = nullptr);
    QString project() const;
    QString entry() const;
    QString composition() const;
    QString props() const;
    bool studioRequested() const { return m_studio; }

private:
    void detect();
    void refreshCompositions();
    QLineEdit* m_project = nullptr;
    QLineEdit* m_entry = nullptr;
    QComboBox* m_composition = nullptr;
    QPlainTextEdit* m_props = nullptr;
    QLabel* m_status = nullptr;
    bool m_studio = false;
};

class ShortcutsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ShortcutsDialog(QWidget* parent = nullptr);
};

} // namespace bc
