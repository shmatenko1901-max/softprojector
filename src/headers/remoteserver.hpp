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

#ifndef REMOTESERVER_HPP
#define REMOTESERVER_HPP

// Small built-in web server: lets a tablet or phone on the same Wi-Fi
// control SoftProjector from a browser (http://<computer-ip>:8080).

#include <QObject>
#include <QTcpServer>
#include <QUrlQuery>
#include <functional>

struct RemoteReply
{
    int status = 200;
    QByteArray contentType = "application/json; charset=utf-8";
    QByteArray body;
};

class RemoteServer : public QObject
{
    Q_OBJECT

public:
    typedef std::function<RemoteReply(const QString &path, const QUrlQuery &query)> Handler;

    RemoteServer(Handler handler, QObject *parent = nullptr);
    bool start(quint16 firstPort = 8080);
    quint16 port() const { return server.serverPort(); }
    bool isRunning() const { return server.isListening(); }
    QStringList urls() const;

private slots:
    void onNewConnection();

private:
    QTcpServer server;
    Handler handler;
};

#endif // REMOTESERVER_HPP
