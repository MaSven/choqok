/*
    This file is part of Choqok, the KDE micro-blogging client

    Copyright (C) 2008-2009 Mehrdad Momeny <mehrdad.momeny@gmail.com>

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
#include "account.h"
#include "microblog.h"
#include <kglobal.h>
#include <ksharedptr.h>
#include <ksharedconfig.h>
#include "passwordmanager.h"
#include <KDebug>

namespace Choqok
{

class Account::Private
{
public:
    Private(Choqok::MicroBlog* parent, const QString& mAlias)
        : alias(mAlias), blog(parent)
    {}
    ~Private()
    {}
    QString username;
    QString password;
    QString alias;
    MicroBlog *blog;
    KConfigGroup *configGroup;
    uint priority;
    bool readonly;
};

Account::Account(Choqok::MicroBlog* parent, const QString& alias)
    : QObject(parent), d(new Private(parent, alias))
{
    kDebug();
    d->configGroup = new KConfigGroup(KGlobal::config(), QString::fromLatin1( "Account_%1" ).arg( d->alias ));
    if(!d->configGroup)
        kError()<<"ERROR, cannot create a config group";
    d->username = d->configGroup->readEntry("Username", QString());
    d->priority = d->configGroup->readEntry("Priority", (uint)0);
    d->readonly = d->configGroup->readEntry("ReadOnly", false);
    d->password = PasswordManager::self()->readPassword(d->alias);
}

Account::~Account()
{
    kDebug()<<alias();
//     writeConfig();
    delete d->configGroup;
    delete d;
}

void Account::writeConfig()
{
    d->configGroup->writeEntry( "Alias", d->alias );
    d->configGroup->writeEntry( "Username", d->username );
    d->configGroup->writeEntry( "Priority", d->priority );
    d->configGroup->writeEntry( "ReadOnly", d->readonly );
    d->configGroup->writeEntry( "MicroBlog", microblog()->pluginName() );
    PasswordManager::self()->writePassword( d->alias, password() );
    d->configGroup->sync();
}

QString Account::username() const
{
    return d->username;
}

void Account::setUsername( const QString & name )
{
    d->username = name;
}

QString Account::password() const
{
    return d->password;
}

void Account::setPassword( const QString & pass )
{
    d->password = pass;
}

QString Account::alias() const
{
    return d->alias;
}

void Account::setAlias( const QString & alias )
{
    d->alias = alias;
    d->configGroup->deleteGroup();
    delete d->configGroup;
    d->configGroup = new KConfigGroup(KGlobal::config(), QString::fromLatin1( "Account_%1" ).arg( d->alias ));
    writeConfig();
}

bool Account::isReadOnly() const
{
    return d->readonly;
}
void Account::setReadOnly(bool readonly /*= true*/)
{
    d->readonly = readonly;
}

MicroBlog *Account::microblog() const
{
    return d->blog;
}

void Account::setPriority( uint priority )
{
    d->priority = priority;
//     d->configGroup->writeEntry( "Priority", d->priority );
}

uint Account::priority() const
{
    return d->priority;
}


KConfigGroup* Account::configGroup() const
{
    return d->configGroup;
}

}
