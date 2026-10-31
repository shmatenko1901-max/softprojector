/***************************************************************************
//
//    softProjector - an open source media projection software
//    Copyright (C) 2017  Vladislav Kobzar
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation version 3 of the License.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
***************************************************************************/

#include "../headers/remoteserver.hpp"
#include <QTcpSocket>
#include <QNetworkInterface>
#include <QTimer>
#include <QUrl>

RemoteServer::RemoteServer(Handler h, QObject *parent)
    : QObject(parent), handler(h)
{
    connect(&server, &QTcpServer::newConnection, this, &RemoteServer::onNewConnection);
}

bool RemoteServer::start(quint16 firstPort)
{
    // Try a few ports in case the first one is busy
    for (quint16 p = firstPort; p < firstPort + 10; ++p)
    {
        if (server.listen(QHostAddress::AnyIPv4, p))
            return true;
    }
    return false;
}

QStringList RemoteServer::urls() const
{
    QStringList list;
    if (!server.isListening())
        return list;

    const QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces)
    {
        if (!(iface.flags() & QNetworkInterface::IsUp) || !(iface.flags() & QNetworkInterface::IsRunning))
            continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack)
            continue;
        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const QNetworkAddressEntry &e : entries)
        {
            QHostAddress a = e.ip();
            if (a.protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            if (a.isInSubnet(QHostAddress("169.254.0.0"), 16))
                continue; // no real network on this adapter
            QString u = QString("http://%1:%2").arg(a.toString()).arg(server.serverPort());
            // Prefer typical home / church Wi-Fi addresses first
            if (a.toString().startsWith("192.168."))
                list.prepend(u);
            else
                list.append(u);
        }
    }
    return list;
}

void RemoteServer::onNewConnection()
{
    while (QTcpSocket *sock = server.nextPendingConnection())
    {
        // Drop connections that never send a full request
        QTimer::singleShot(15000, sock, [sock]() { sock->abort(); });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);

        connect(sock, &QTcpSocket::readyRead, this, [this, sock]()
        {
            QByteArray buf = sock->property("buf").toByteArray() + sock->readAll();
            if (buf.size() > 32 * 1024)
            {
                sock->abort();
                return;
            }
            int end = buf.indexOf("\r\n\r\n");
            if (end < 0)
            {
                sock->setProperty("buf", buf);
                return;
            }
            sock->setProperty("buf", QByteArray());

            // Request line: "GET /path?query HTTP/1.1"
            QList<QByteArray> parts = buf.left(buf.indexOf("\r\n")).split(' ');
            RemoteReply reply;
            if (parts.size() < 2 || parts.at(0) != "GET")
            {
                reply.status = 405;
                reply.contentType = "text/plain";
                reply.body = "Method not allowed";
            }
            else
            {
                QUrl url(QString("http://local") + QString::fromUtf8(parts.at(1)));
                reply = handler(url.path(), QUrlQuery(url));
            }

            QByteArray statusText = "OK";
            if (reply.status == 404) statusText = "Not Found";
            else if (reply.status == 400) statusText = "Bad Request";
            else if (reply.status == 405) statusText = "Method Not Allowed";

            QByteArray out;
            out += "HTTP/1.1 " + QByteArray::number(reply.status) + " " + statusText + "\r\n";
            out += "Content-Type: " + reply.contentType + "\r\n";
            out += "Content-Length: " + QByteArray::number(reply.body.size()) + "\r\n";
            out += "Cache-Control: no-store\r\n";
            out += "Connection: close\r\n\r\n";
            out += reply.body;
            sock->write(out);
            sock->disconnectFromHost();
        });
    }
}
