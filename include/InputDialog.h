#pragma once
#include <QDialog>
#include <QString>

class QLineEdit;

class InputDialog : public QDialog {
    Q_OBJECT
public:
    InputDialog(const QString& title, const QString& label,
                const QString& defaultText = {}, QWidget* parent = nullptr);

    QString result() const;

private:
    QLineEdit* m_edit = nullptr;
    QString    m_result;
};
