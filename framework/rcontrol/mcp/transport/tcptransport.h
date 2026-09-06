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

#pragma once

#include <QObject>
#include <QByteArray>

#include "../itransport.h"

class QTcpServer;
class QTcpSocket;

namespace muse::rcontrol::mcp {
class TcpConnection : public QObject
{
    Q_OBJECT
public:
    explicit TcpConnection(QTcpSocket* socket, const ITransport::RequestHandler& onRequest, QObject* parent = nullptr);

private slots:
    void onReadyRead();

private:
    void processMessage(const QByteArray& message);
    void rejectAndClose(const char* reason);

    QTcpSocket* m_socket = nullptr;
    QByteArray m_buffer;
    ITransport::RequestHandler m_onRequest = nullptr;

    //! A browser cannot speak this protocol, but it can POST to the port, and a
    //! request body is just another line once the headers have been skipped - so a
    //! web page could drive the application (confirmed live before this check was
    //! added). Connections that begin with an HTTP request line are dropped.
    bool m_firstLineChecked = false;
};

class TcpTransport : public ITransport
{
public:

    ~TcpTransport();

    bool start() override;
    void stop() override;

    void onRequest(const RequestHandler& onRequest) override;

private:
    QTcpServer* m_server = nullptr;
    TcpConnection* m_connection = nullptr;
    RequestHandler m_onRequest = nullptr;
};
}
