#include "QrCodeItem.h"

#include <QDebug>
#include <QPainter>

#include "qrcodegen.hpp"

QrCodeItem::QrCodeItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    // Crisp module edges - anti-aliasing just blurs a QR code's hard
    // black/white boundaries, which hurts scanability rather than helping it.
    setAntialiasing(false);
}

QString QrCodeItem::text() const
{
    return m_text;
}

void QrCodeItem::setText(const QString &text)
{
    if (m_text == text) {
        return;
    }
    m_text = text;
    emit textChanged();
    update();
}

void QrCodeItem::paint(QPainter *painter)
{
    painter->fillRect(boundingRect(), Qt::white);

    if (m_text.isEmpty()) {
        return;
    }

    try {
        const qrcodegen::QrCode qr =
            qrcodegen::QrCode::encodeText(m_text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int size = qr.getSize();
        const qreal moduleSize = qMin(width(), height()) / size;

        painter->setPen(Qt::NoPen);
        painter->setBrush(Qt::black);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                if (qr.getModule(x, y)) {
                    painter->drawRect(QRectF(x * moduleSize, y * moduleSize, moduleSize, moduleSize));
                }
            }
        }
    } catch (const std::exception &e) {
        qWarning() << "QrCodeItem: failed to encode text -" << e.what();
    }
}
