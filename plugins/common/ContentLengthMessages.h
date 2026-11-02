// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>

// JSON messages after a "Content-Length" header, as the Language Server
// Protocol and the Debug Adapter Protocol frame them.
namespace eudaimonia::messages {

inline QByteArray frame(const QJsonObject& message) {
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    return "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

// Takes the whole messages out of what was read so far; the rest stays.
inline QList<QJsonObject> take(QByteArray& buffer) {
    QList<QJsonObject> messages;
    for (;;) {
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) break;
        qsizetype length = -1;
        for (const QByteArray& line : buffer.left(headerEnd).split('\n')) {
            const QByteArray header = line.trimmed();
            if (header.toLower().startsWith("content-length:")) length = header.mid(15).trimmed().toLongLong();
        }
        if (length < 0) {
            buffer.remove(0, headerEnd + 4);  // a header without a length: skipped
            continue;
        }
        if (buffer.size() < headerEnd + 4 + length) break;  // the body is not all here yet
        const QByteArray body = buffer.mid(headerEnd + 4, length);
        buffer.remove(0, headerEnd + 4 + length);
        const QJsonDocument document = QJsonDocument::fromJson(body);
        if (document.isObject()) messages << document.object();
    }
    return messages;
}

} // namespace eudaimonia::messages
