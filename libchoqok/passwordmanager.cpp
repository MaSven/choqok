/*
This file is part of Choqok, the KDE micro-blogging client

Copyright (C) 2008-2012 Mehrdad Momeny <mehrdad.momeny@gmail.com>

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

#include "passwordmanager.h"

#include <QApplication>
#include <qt5keychain/keychain.h>
#include <qt5keychain/qkeychain_export.h>
#include <KMessageBox>
#include "choqokbehaviorsettings.h"
#include "choqokuiglobal.h"
#include "libchoqokdebug.h"

namespace Choqok {
class PasswordManager::Private {
public:
  Private() : conf(nullptr), cfg(nullptr) {}

  ~Private() {
    if (cfg) {
      cfg->sync();
    }
    delete conf;
    delete cfg;
  }

  
  void syncConfigs() { cfg->sync(); }
  KConfigGroup *conf;

private:
  KConfig *cfg;
};

PasswordManager::PasswordManager() : QObject(qApp), d(new Private) {
  qCDebug(CHOQOK);
}

PasswordManager::~PasswordManager() { delete d; }

PasswordManager *PasswordManager::mSelf = nullptr;

PasswordManager *PasswordManager::self() {
  if (!mSelf) {
    mSelf = new PasswordManager;
  }
  return mSelf;
}

QString PasswordManager::readPassword(const QString &alias) {
    QKeychain::ReadPasswordJob readPasswordJob(QLatin1String("choqok"));
    readPasswordJob.setKey(alias);
    qCDebug(CHOQOK) << "Start Event Loop to read password from wallet";
    QEventLoop loop;
    readPasswordJob.connect(&readPasswordJob,SIGNAL(finished(QKeychain::Job*)),&loop,SLOT(quit()));
    readPasswordJob.start();
    loop.exec();
    if(readPasswordJob.error()){
     qCDebug(CHOQOK) << "Error on readin password";   
     return QString();
    }
    else{
     qCDebug(CHOQOK) << "Successfuly read password";
     return readPasswordJob.textData();
    }
}

bool PasswordManager::writePassword(const QString &alias,
                                    const QString &password) {
  QKeychain::WritePasswordJob writePasswordJob(QLatin1String("choqok"));
  writePasswordJob.setKey(alias);
  writePasswordJob.setTextData(password);
  QEventLoop loop;
  writePasswordJob.connect(&writePasswordJob,SIGNAL(finished(QKeychain::Job*)),&loop,SLOT(quit()));
  writePasswordJob.start();
  loop.exec();
  if(writePasswordJob.error()){
      qCDebug(CHOQOK) << "Error while trying to write password" << writePasswordJob.errorString();
      return false;
  }else{
   qCDebug(CHOQOK) << "Successfuly wrote password";
   return true;
  }
  
}

bool PasswordManager::removePassword(const QString &alias) {
  QKeychain::DeletePasswordJob deletePasswordJob(QLatin1String("choqok"));
  deletePasswordJob.setKey(alias);
  QEventLoop loop;
  deletePasswordJob.connect(&deletePasswordJob,SIGNAL(finished(QKeychain::Job*)),&loop,SLOT(quit()));
  deletePasswordJob.start();
  loop.exec();
  if(deletePasswordJob.error()){
      qCDebug(CHOQOK) << "Could not delete password because of " << deletePasswordJob.errorString();
      return false;
  }
    return true;
  
}

} // namespace Choqok
