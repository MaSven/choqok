/*
    This file is part of Choqok, the KDE micro-blogging client

    Copyright (C) 2010 Mehrdad Momeny <mehrdad.momeny@gmail.com>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of
    the License or (at your option) version 3 or any later version
    accepted by the membership of KDE e.V. (or its successor approved
    by the membership of KDE e.V.), which shall act as a proxy
    defined in Section 14 of version 3 of the license.


    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, see http://www.gnu.org/licenses/

*/

#include "twitpic.h"
#include <KAction>
#include <KActionCollection>
#include <KAboutData>
#include <KGenericFactory>
// #include <QDBusInterface>
// #include <QDBusReply>
// #include <choqokuiglobal.h>
// #include <quickpost.h>
#include "twitpicsettings.h"
// #include "twitpicuploadimage.h"
#include <KIO/Job>
#include <kio/netaccess.h>
// #include <KMessageBox>
// #include <kmimetype.h>
#include <passwordmanager.h>
// #include <notifymanager.h>
#include <QDomDocument>
#include <mediamanager.h>

K_PLUGIN_FACTORY( MyPluginFactory, registerPlugin < Twitpic > (); )
K_EXPORT_PLUGIN( MyPluginFactory( "choqok_twitpic" ) )

Twitpic::Twitpic(QObject* parent, const QList<QVariant>& )
    :Choqok::Uploader(MyPluginFactory::componentData(), parent)
{
    
}

Twitpic::~Twitpic()
{

}

void Twitpic::upload(const KUrl& localUrl, const QByteArray& medium, const QByteArray& mediumType)
{
    QString tmp;
    ///Documentation: http://twitpic.com/api.do
    KUrl url( "http://twitpic.com/api/upload" );

    QMap<QString, QByteArray> formdata;
    formdata["username"] = TwitpicSettings::username().toLatin1();
    formdata["password"] = Choqok::PasswordManager::self()->readPassword( QString("twitpic_%1").arg(TwitpicSettings::username()) ).toUtf8();

    QMap<QString, QByteArray> mediafile;
    mediafile["name"] = "media";
    mediafile["filename"] = localUrl.fileName().toUtf8();
    mediafile["mediumType"] = mediumType;
    mediafile["medium"] = medium;
    QList< QMap<QString, QByteArray> > listMediafiles;
    listMediafiles.append(mediafile);

    QByteArray data = Choqok::MediaManager::createMultipartFormData(formdata, listMediafiles);

    KIO::StoredTransferJob *job = KIO::storedHttpPost(data, url, KIO::HideProgressInfo) ;
    if ( !job ) {
        kError() << "Cannot create a http POST request!";
        return;
    }
    job->addMetaData( "content-type", "Content-Type: multipart/form-data; boundary=AaB03x" );
    mUrlMap[job] = localUrl;
    connect( job, SIGNAL( result( KJob* ) ),
             SLOT( slotUpload(KJob*)) );
    job->start();
}

void Twitpic::slotUpload(KJob* job)
{
    kDebug();
    KUrl localUrl = mUrlMap.take(job);
    if ( job->error() ) {
        kError() << "Job Error: " << job->errorString();
        emit uploadingFailed(localUrl, job->errorString());
        return;
    } else {
        QDomDocument doc;
        QByteArray buffer = qobject_cast<KIO::StoredTransferJob*>(job)->data();
//         kDebug()<<buffer;
        buffer.replace('&', "&amp;");
        doc.setContent(buffer);
        QDomElement element = doc.documentElement();
        if( element.tagName() == "rsp" ) {
            QString result;
            if(element.hasAttribute("stat") )
                result = element.attribute("stat" , "fail");
            else if(element.hasAttribute("status"))
                result = element.attribute("status" , "fail");
            else {
                emit uploadingFailed(localUrl, i18n("Malformed response"));
                kError()<<"Twitpic uploading failed: There isn't any \"stat\" or \"status\" attribute. Buffer:\n" << buffer;
                return;
            }
            if( result == "ok" ) {
                QDomNode node = element.firstChild();
                while( !node.isNull() ){
                    element = node.toElement();
                    if(element.tagName() == "mediaurl") {
                        emit mediumUploaded(localUrl, element.text());
                    }
                    node = node.nextSibling();
                }
                return;
            } else {
                QDomNode node = element.firstChild();
                while( !node.isNull() ){
                    element = node.toElement();
                    if(element.tagName() == "err") {
                        QString err = element.attribute( "msg", i18n("Unrecognized result.") );
                        kDebug()<<"Server Error: "<<err<<" Buffer: "<<buffer;
                        emit uploadingFailed(localUrl, err);
                    }
                    node = node.nextSibling();
                }
                return;
            }
        } else {
            kError()<<"There isn't any \"rsp\" tag. Buffer:\n"<<buffer;
            emit uploadingFailed(localUrl, i18n("Malformed response"));
        }
    }
}
