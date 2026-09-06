/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore/Audacity CLA applies
 *
 * Copyright (C) 2026 MuseScore/Audacity and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "tcptransport.h"

#include <QTcpServer>
#include <QTcpSocket>

#include "global/serialization/json.h"

#include "log.h"

static const int DEFAULT_PORT = 2212;

using namespace muse::rcontrol::mcp;

TcpConnection::TcpConnection(QTcpSocket* socket, const ITransport::RequestHandler& onRequest, QObject* parent)
    : QObject(parent), m_socket(socket), m_onRequest(onRequest)
{
    m_socket->setParent(this);
    connect(m_socket, &QTcpSocket::readyRead, this, &TcpConnection::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &QObject::deleteLater);
}

//! One request is not expected to approach this. The cap exists because everything
//! received before a newline is buffered, so without it a client that never sends
//! one can grow this without bound.
static constexpr int MAX_BUFFERED_BYTES = 8 * 1024 * 1024;

static bool looksLikeHttpRequest(const QByteArray& line)
{
    static const char* methods[] = { "GET ", "POST ", "PUT ", "HEAD ", "DELETE ", "OPTIONS ", "PATCH ", "TRACE ", "CONNECT " };
    for (const char* m : methods) {
        if (line.startsWith(m)) {
            return true;
        }
    }
    return false;
}

void TcpConnection::rejectAndClose(const char* reason)
{
    LOGW() << "rejecting connection: " << reason;
    m_buffer.clear();
    if (m_socket) {
        m_socket->close();
    }
}

void TcpConnection::onReadyRead()
{
    m_buffer += m_socket->readAll();

    if (m_buffer.size() > MAX_BUFFERED_BYTES) {
        rejectAndClose("message exceeds the maximum buffered size");
        return;
    }

    int idx = -1;
    while ((idx = m_buffer.indexOf('\n')) != -1) {
        QByteArray message = m_buffer.left(idx);
        m_buffer.remove(0, idx + 1);

        //! Checked on the first line only: a genuine client's first line is JSON,
        //! and an HTTP request's is its request line. Everything after the headers
        //! - including the body, which is what a web page would put a call in - is
        //! then never reached, because the connection is already gone.
        if (!m_firstLineChecked) {
            m_firstLineChecked = true;
            if (looksLikeHttpRequest(message.trimmed())) {
                rejectAndClose("looks like an HTTP request, not an MCP client");
                return;
            }
        }

        processMessage(message);
    }
}

//! The token authenticates every request, so it must never reach a log file -
//! these are exactly what users attach to bug reports. Only the value is masked;
//! the rest of the request stays readable for diagnostics.
static QByteArray redactToken(const QByteArray& in)
{
    const int key = in.indexOf("\"token\"");
    if (key < 0) {
        return in;
    }
    const int colon = in.indexOf(':', key);
    if (colon < 0) {
        return in;
    }
    const int open = in.indexOf('"', colon + 1);
    if (open < 0) {
        return in;
    }
    const int close = in.indexOf('"', open + 1);
    if (close < 0) {
        return in;
    }

    QByteArray out = in;
    out.replace(open + 1, close - open - 1, "<redacted>");
    return out;
}

void TcpConnection::processMessage(const QByteArray& request)
{
    LOGD() << "request: " << redactToken(request);

    if (m_onRequest) {
        //! Copied deliberately rather than wrapped with fromQByteArrayNoCopy(). The
        //! handler resolves through an async promise, so it can run after this
        //! function has returned - by which point the caller's buffer for this line
        //! is gone and a no-copy view into it dangles. Confirmed live: sending
        //! several messages in one packet produced the right response and then
        //! crashed with SIGSEGV.
        ByteArray req = ByteArray::fromQByteArray(request);
        m_onRequest(req, [this](const ByteArray& response) {
            QByteArray resp = response.toQByteArrayNoCopy();
            LOGD() << "response: " << resp;
            m_socket->write(resp);
            m_socket->write("\n");
            m_socket->flush();
        });
    } else {
        LOGE() << "No onResponse handler";
        m_socket->write("\n");
        m_socket->flush();
    }
}

TcpTransport::~TcpTransport()
{
    stop();
}

bool TcpTransport::start()
{
    if (!m_server) {
        m_server = new QTcpServer();
        QObject::connect(m_server, &QTcpServer::newConnection, [this]() {
            //! NOTE Always drain the pending connection - leaving it unconsumed in the
            //! server's backlog when a connection is already active would leak the socket.
            QTcpSocket* socket = m_server->nextPendingConnection();

            if (m_connection) {
                LOGW() << "New connection while one is already active - rejecting the new one";
                socket->disconnectFromHost();
                socket->deleteLater();
                return;
            }

            m_connection = new TcpConnection(socket, m_onRequest);
            //! NOTE m_connection self-deletes (via deleteLater) when its socket disconnects
            //! (see TcpConnection's constructor). Without this, m_connection would be left
            //! dangling after the first client disconnects, permanently blocking every
            //! connection after it for the rest of this process's lifetime.
            QObject::connect(m_connection, &QObject::destroyed, [this]() {
                m_connection = nullptr;
            });
        });
    }

    if (!m_server->listen(QHostAddress::LocalHost, DEFAULT_PORT)) {
        return false;
    }

    LOGI() << "TcpTransport started on port " << DEFAULT_PORT;

    return true;
}

void TcpTransport::stop()
{
    if (m_server) {
        delete m_connection;
        m_connection = nullptr;

        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
}

void TcpTransport::onRequest(const RequestHandler& onRequest)
{
    m_onRequest = onRequest;
}
