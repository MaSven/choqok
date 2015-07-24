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
#include "accountmanager.h"

#include <QUrl>
#include <QStandardPaths>

#include <KConfig>
#include <KConfigGroup>
#include <KIO/DeleteJob>
#include <KIO/StatJob>
#include <KJobWidgets>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KWallet>

#include "libchoqokdebug.h"
#include "microblog.h"
#include "passwordmanager.h"
#include "pluginmanager.h"

namespace Choqok
{

static QList<Account *> sortAccountsByPriority(QList<Account *> &accounts)
{
    qCDebug(CHOQOK) << accounts.count();
    QList<Account *> result;
    Q_FOREACH (Account *ac, accounts) {
        bool inserted = false;
        int i = 0;
        while (i < result.count()) {
            if (ac->priority() < result[i]->priority()) {
                result.insert(i, ac);
                inserted = true;
                break;
            }
            ++i;
        }
        if (!inserted) {
            result.insert(i, ac);
        }
    }
    return result;
}

class AccountManager::Private
{
public:
    Private()
        : conf(0)
    {}
    QList<Account *> accounts;
    KSharedConfig::Ptr conf;
    QString lastError;
};

AccountManager::AccountManager()
    : QObject(qApp), d(new Private)
{
    qCDebug(CHOQOK);
    d->conf = KSharedConfig::openConfig();
}

AccountManager::~AccountManager()
{
    qCDebug(CHOQOK);
    mSelf = 0L;
    d->conf->sync();
    delete d;
}

AccountManager *AccountManager::mSelf = 0L;

AccountManager *AccountManager::self()
{
    if (!mSelf) {
        mSelf = new AccountManager;
    }
    return mSelf;
}

const QList< Account * > &AccountManager::accounts() const
{
    return d->accounts;
}

Account *AccountManager::findAccount(const QString &alias)
{
    qCDebug(CHOQOK) << "Finding: " << alias;
    int count = d->accounts.count();
    for (int i = 0; i < count; ++i) {
        if (d->accounts[i]->alias() == alias) {
            return d->accounts[i];
        }
    }
    d->lastError = i18n("There is no account with alias %1.", alias);
    return 0L;
}

bool AccountManager::removeAccount(const QString &alias)
{
    qCDebug(CHOQOK) << "Removing " << alias;
    int count = d->accounts.count();
    d->conf->deleteGroup(QStringLiteral("Account_%1").arg(alias));
    d->conf->sync();
    for (int i = 0; i < count; ++i) {
        if (d->accounts[i]->alias() == alias) {
            Choqok::Account *a = d->accounts.takeAt(i);
            if (!a) {
                return false;
            }
            QStringList names = a->timelineNames();
            while (!names.isEmpty()) {
                QString tmpFile;
                tmpFile = QStandardPaths::locate(QStandardPaths::DataLocation,
                                                generatePostBackupFileName(a->alias(), names.takeFirst()));
                qCDebug(CHOQOK) << "Will remove " << tmpFile;
                const QUrl path(tmpFile);
                KIO::StatJob *job = KIO::stat(path, KIO::StatJob::SourceSide, 1);
                KJobWidgets::setWindow(job, UI::Global::mainWindow());
                job->exec();
                KIO::DeleteJob *delJob = KIO::del(path);
                KJobWidgets::setWindow(delJob, UI::Global::mainWindow());
                delJob->exec();
            }
            a->deleteLater();
            PasswordManager::self()->removePassword(alias);
            Q_EMIT accountRemoved(alias);
            return true;
        }
    }
    d->lastError = i18n("There is no account with alias %1.", alias);
    return false;
}

Account *AccountManager::registerAccount(Account *account)
{
    qCDebug(CHOQOK) << "Adding: " << account->alias();

    if (!account || d->accounts.contains(account) || account->alias().isEmpty()) {
        return 0L;
    }

    // If this account already exists, do nothing
    QListIterator<Account *> it(d->accounts);
    while (it.hasNext()) {
        Account *curracc = it.next();
        if (account->alias() == curracc->alias()) {
            d->lastError = i18n("An account with this alias already exists: a unique alias has to be specified.");
            qCDebug(CHOQOK) << "An account with this alias already exists: a unique alias has to be specified.";
            return 0L;
        }
    }
    d->accounts.append(account);
    d->accounts = sortAccountsByPriority(d->accounts);

    Q_EMIT accountAdded(account);
    return account;
}

void AccountManager::loadAllAccounts()
{
    qCDebug(CHOQOK);
    Q_FOREACH (Account *ac, d->accounts) {
        ac->deleteLater();
    }
    d->accounts.clear();
    const QStringList accountGroups = d->conf->groupList().filter(QRegExp(QLatin1String("^Account_")));
    qCDebug(CHOQOK) << accountGroups;
    Q_FOREACH (const QString &grp, accountGroups) {
        qCDebug(CHOQOK) << grp;
        KConfigGroup cg(d->conf, grp);
//         KConfigGroup pluginConfig( d->conf, QLatin1String("Plugins") );

        QString blog = cg.readEntry("MicroBlog", QString());
        Choqok::MicroBlog *mBlog = 0;
        if (!blog.isEmpty() && cg.readEntry("Enabled", true)) {
            mBlog = qobject_cast<MicroBlog *>(PluginManager::self()->loadPlugin(blog));
        }
        if (mBlog) {
            const QString alias = cg.readEntry("Alias", QString());
            if (alias.isEmpty()) {
                continue;    ///Unknown alias
            }
            Account *acc = mBlog->createNewAccount(alias);
            if (acc) {
                d->accounts.append(acc);
            }
        }
    }
    qCDebug(CHOQOK) << d->accounts.count() << " accounts loaded.";
    d->accounts = sortAccountsByPriority(d->accounts);
    Q_EMIT allAccountsLoaded();
}

QString AccountManager::generatePostBackupFileName(const QString &alias, const QString &name)
{
    return QString(alias + QLatin1Char('_') + name + QLatin1String("_backuprc"));
}

QString AccountManager::lastError() const
{
    return d->lastError;
}

}
