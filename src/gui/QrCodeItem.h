#pragma once

#include <QQuickPaintedItem>
#include <QString>

/**
 * @brief Paints a QR code encoding whatever text currently holds (Fase 16
 *        Part 2), using the vendored Nayuki qrcodegen library
 *        (src/vendor/qrcodegen) - so RemoteControlPopup.qml can show a
 *        scannable pairing URL without depending on any external
 *        QR-rendering service or network call.
 *
 * Repaints automatically on both triggers the task requires: setText()
 * calls update() directly, and a size change is already handled by
 * QQuickPaintedItem itself (its geometryChange() override calls update()),
 * so paint() re-renders at the new module size without any extra wiring
 * here.
 */
class QrCodeItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)

public:
    explicit QrCodeItem(QQuickItem *parent = nullptr);

    QString text() const;
    void setText(const QString &text);

    void paint(QPainter *painter) override;

signals:
    void textChanged();

private:
    QString m_text;
};
