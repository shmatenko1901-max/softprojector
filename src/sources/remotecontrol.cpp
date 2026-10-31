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

// Remote control: a tablet or phone on the same Wi-Fi opens
// http://<computer-ip>:8080 in a browser and controls SoftProjector.

#include "../headers/softprojector.hpp"
#include "ui_softprojector.h"
#include <QLabel>
#include <QStatusBar>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>

void SoftProjector::startRemoteControl()
{
    remoteServer = new RemoteServer([this](const QString &path, const QUrlQuery &query)
    {
        return remoteHandle(path, query);
    }, this);

    remoteLabel = new QLabel(this);
    remoteLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addPermanentWidget(remoteLabel);

    if(remoteServer->start(8080))
    {
        QStringList urls = remoteServer->urls();
        if(urls.isEmpty())
            urls << QString("http://<IP>:%1").arg(remoteServer->port());
        remoteLabel->setText(QStringLiteral("\U0001F4F1 %1").arg(urls.join("  |  ")));
        remoteLabel->setToolTip(tr("Open this address in a browser on a tablet or phone "
                                   "connected to the same Wi-Fi network."));
    }
    else
    {
        remoteLabel->setText(QStringLiteral("\U0001F4F1 \u2715"));
        remoteLabel->setToolTip(tr("Tablet remote could not start (ports 8080-8089 are busy)."));
    }
}

QByteArray SoftProjector::remoteState()
{
    QJsonObject o;
    o["showing"] = showing;
    o["title"] = ui->labelShow->text();
    o["current"] = ui->listShow->currentRow();

    QJsonArray slides;
    for(int i = 0; i < ui->listShow->count(); ++i)
        slides.append(ui->listShow->item(i)->text());
    o["slides"] = slides;

    QJsonArray sched;
    for(int i = 0; i < ui->listWidgetSchedule->count(); ++i)
        sched.append(ui->listWidgetSchedule->item(i)->text());
    o["schedule"] = sched;
    o["scheduleCurrent"] = ui->listWidgetSchedule->currentRow();

    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

RemoteReply SoftProjector::remoteHandle(const QString &path, const QUrlQuery &query)
{
    RemoteReply r;

    if(path == "/" || path == "/index.html")
    {
        QFile f(":/remote/remote/remote.html");
        f.open(QIODevice::ReadOnly);
        r.contentType = "text/html; charset=utf-8";
        r.body = f.readAll();
        return r;
    }

    if(path == "/api/state")
    {
        r.body = remoteState();
        return r;
    }

    if(path == "/api/cmd")
    {
        QString c = query.queryItemValue("c");
        int i = query.queryItemValue("i").toInt();

        if(c == "next")
            nextSlide();
        else if(c == "prev")
            prevSlide();
        else if(c == "show")
            on_actionShow_triggered();
        else if(c == "hide")
            on_actionHide_triggered();
        else if(c == "clear")
            on_actionClear_triggered();
        else if(c == "slide")
        {
            if(i >= 0 && i < ui->listShow->count())
            {
                if(ui->rbMultiVerse->isChecked())
                    ui->rbMultiVerse->setChecked(false);
                ui->listShow->setCurrentRow(i);
            }
        }
        else if(c == "schedule")
        {
            if(i >= 0 && i < ui->listWidgetSchedule->count() && i < schedule.count())
            {
                ui->listWidgetSchedule->setCurrentRow(i);
                on_listWidgetSchedule_doubleClicked(ui->listWidgetSchedule->model()->index(i, 0));
            }
        }
        else
        {
            r.status = 400;
        }
        r.body = remoteState();
        return r;
    }

    if(path == "/api/songs")
    {
        // Search songs by number or by part of the title
        QString q = query.queryItemValue("q", QUrl::FullyDecoded).trimmed();
        QJsonArray list;
        if(!q.isEmpty())
        {
            bool isNum;
            int num = q.toInt(&isNum);

            QSqlQuery sq;
            sq.exec("SELECT Songs.id, Songs.number, Songs.title, Songbooks.name FROM Songs "
                    "LEFT JOIN Songbooks ON Songs.songbook_id = Songbooks.id");
            while(sq.next() && list.count() < 60)
            {
                int number = sq.value(1).toInt();
                QString title = sq.value(2).toString();
                if((isNum && number == num) || (!isNum && title.contains(q, Qt::CaseInsensitive)))
                {
                    QJsonObject s;
                    s["id"] = sq.value(0).toInt();
                    s["number"] = number;
                    s["title"] = title;
                    s["songbook"] = sq.value(3).toString();
                    list.append(s);
                }
            }
        }
        r.body = QJsonDocument(list).toJson(QJsonDocument::Compact);
        return r;
    }

    if(path == "/api/song")
    {
        int id = query.queryItemValue("id").toInt();
        QSqlQuery sq;
        sq.exec("SELECT Songbooks.name FROM Songs LEFT JOIN Songbooks ON Songs.songbook_id = Songbooks.id "
                "WHERE Songs.id = " + QString::number(id));
        if(id <= 0 || !sq.first())
        {
            r.status = 404;
            r.body = remoteState();
            return r;
        }
        QString sbName = sq.value(0).toString();
        sq.clear();

        Song s(id);
        s.readData();
        s.songbook_name = sbName;

        ui->projectTab->setCurrentIndex(1);
        songWidget->sendToPreviewFromSchedule(s);
        setSongList(s, 0);
        songWidget->counter.addSongCount(s);

        r.body = remoteState();
        return r;
    }

    if(path == "/api/bible")
    {
        QString ref = query.queryItemValue("ref", QUrl::FullyDecoded);
        ui->projectTab->setCurrentIndex(0);
        bool ok = bibleWidget->remoteShow(ref);
        if(!ok)
            r.status = 404;
        r.body = remoteState();
        return r;
    }

    r.status = 404;
    r.contentType = "text/plain";
    r.body = "Not found";
    return r;
}
