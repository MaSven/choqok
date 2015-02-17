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

#include "pluginmanager.h"

#include <QApplication>
#include <QRegExp>
#include <QtGlobal>
#include <QTimer>
#include <QStack>

#include <KConfig>
#include "libchoqokdebug.h"
#include <KPluginInfo>
#include <KServiceTypeTrader>
#include <KSharedConfig>
#include <KStandardDirs>
#include <QUrl>

#include "accountmanager.h"
#include "plugin.h"

namespace Choqok
{

class PluginManagerPrivate
{
public:
    PluginManagerPrivate() : shutdownMode( StartingUp ), isAllPluginsLoaded(false)
    {
        plugins = KPluginInfo::fromServices( KServiceTypeTrader::self()->query( QLatin1String( "Choqok/Plugin" ),
                                            QString("[X-Choqok-Version] == %1").arg(CHOQOK_PLUGIN_VERSION) ) );
    }

    ~PluginManagerPrivate()
    {
        if ( shutdownMode != DoneShutdown ) {
            qCWarning(CHOQOK) << "Destructing plugin manager without going through the shutdown process! Backtrace is: " << endl;
        }

        // Clean up loadedPlugins manually, because PluginManager can't access our global
        // static once this destructor has started.
        while ( !loadedPlugins.empty() )
        {
            InfoToPluginMap::ConstIterator it = loadedPlugins.constBegin();
            qCWarning(CHOQOK) << "Deleting stale plugin '" << it.value()->objectName() << "'";
            KPluginInfo info = it.key();
            Plugin *plugin = it.value();
            loadedPlugins.remove(info);
            plugin->disconnect(&instance, SLOT(slotPluginDestroyed(QObject*)));
            plugin->deleteLater();;
        }
    }

    // All available plugins, regardless of category, and loaded or not
    QList<KPluginInfo> plugins;

    // Dict of all currently loaded plugins, mapping the KPluginInfo to
    // a plugin
    typedef QMap<KPluginInfo, Plugin *> InfoToPluginMap;
    InfoToPluginMap loadedPlugins;

    // The plugin manager's mode. The mode is StartingUp until loadAllPlugins()
    // has finished loading the plugins, after which it is set to Running.
    // ShuttingDown and DoneShutdown are used during Choqok shutdown by the
    // async unloading of plugins.
    enum ShutdownMode { StartingUp, Running, ShuttingDown, DoneShutdown };
    ShutdownMode shutdownMode;

    // Plugins pending for loading
    QStack<QString> pluginsToLoad;

    bool isAllPluginsLoaded;
    PluginManager instance;
};

Q_GLOBAL_STATIC(PluginManagerPrivate, _kpmp)

PluginManager* PluginManager::self()
{
    return &_kpmp->instance;
}

PluginManager::PluginManager() : QObject( )
{
    connect(QCoreApplication::instance(), SIGNAL(aboutToQuit()),
            this, SLOT(slotAboutToQuit()));
}

PluginManager::~PluginManager()
{
    qCDebug(CHOQOK);
}

QList<KPluginInfo> PluginManager::availablePlugins( const QString &category ) const
{
    if ( category.isEmpty() )
        return _kpmp->plugins;

    QList<KPluginInfo> result;
    QList<KPluginInfo>::ConstIterator it;
    for ( it = _kpmp->plugins.constBegin(); it != _kpmp->plugins.constEnd(); ++it )
    {
        if ( it->category() == category && !(*it).service()->noDisplay() )
            result.append( *it );
    }

    return result;
}

PluginList PluginManager::loadedPlugins( const QString &category ) const
{
    PluginList result;

    for ( PluginManagerPrivate::InfoToPluginMap::ConstIterator it = _kpmp->loadedPlugins.constBegin();
          it != _kpmp->loadedPlugins.constEnd(); ++it )
    {
        if ( category.isEmpty() || it.key().category() == category )
            result.append( it.value() );
    }

    return result;
}

KPluginInfo PluginManager::pluginInfo( const Plugin *plugin ) const
{
    for ( PluginManagerPrivate::InfoToPluginMap::ConstIterator it = _kpmp->loadedPlugins.constBegin();
          it != _kpmp->loadedPlugins.constEnd(); ++it )
    {
        if ( it.value() == plugin )
            return it.key();
    }
    return KPluginInfo();
}

void PluginManager::shutdown()
{
    qCDebug(CHOQOK);
    if(_kpmp->shutdownMode != PluginManagerPrivate::Running)
    {
        qCDebug(CHOQOK) << "called when not running.  / state = " << _kpmp->shutdownMode;
        return;
    }

    _kpmp->shutdownMode = PluginManagerPrivate::ShuttingDown;

    // Remove any pending plugins to load, we're shutting down now :)
    _kpmp->pluginsToLoad.clear();

    // Ask all plugins to unload
    for ( PluginManagerPrivate::InfoToPluginMap::ConstIterator it = _kpmp->loadedPlugins.constBegin();
          it != _kpmp->loadedPlugins.constEnd(); /* EMPTY */ )
    {
        // Plugins could emit their ready for unload signal directly in response to this,
        // which would invalidate the current iterator. Therefore, we copy the iterator
        // and increment it beforehand.
        PluginManagerPrivate::InfoToPluginMap::ConstIterator current( it );
        ++it;
        // FIXME: a much cleaner approach would be to just delete the plugin now. if it needs
        //  to do some async processing, it can grab a reference to the app itself and create
        //  another object to do it.
        current.value()->aboutToUnload();
    }

    // When running under valgrind, don't enable the timer because it will almost
    // certainly fire due to valgrind's much slower processing
#if defined(HAVE_VALGRIND_H) && !defined(NDEBUG) && defined(__i386__)
    if ( RUNNING_ON_VALGRIND )
        qCDebug(CHOQOK) << "Running under valgrind, disabling plugin unload timeout guard";
    else
#endif
        QTimer::singleShot( 3000, this, SLOT( slotShutdownTimeout() ) );
}

void PluginManager::slotPluginReadyForUnload()
{
    qCDebug(CHOQOK);
    // Using QObject::sender() is on purpose here, because otherwise all
    // plugins would have to pass 'this' as parameter, which makes the API
    // less clean for plugin authors
    // FIXME: I don't buy the above argument. Add a Choqok::Plugin::emitReadyForUnload(void),
    //        and make readyForUnload be passed a plugin. - Richard
    Plugin *plugin = dynamic_cast<Plugin *>( const_cast<QObject *>( sender() ) );
    if ( !plugin )
    {
        qCWarning(CHOQOK) << "Calling object is not a plugin!";
        return;
    }
    qCDebug(CHOQOK) << plugin->pluginId() << "ready for unload";
    _kpmp->loadedPlugins.remove(_kpmp->loadedPlugins.key(plugin));
    plugin->deleteLater();
    plugin = 0L;
    if(_kpmp->loadedPlugins.count() < 1){
        slotShutdownDone();
    }
}

void PluginManager::slotShutdownTimeout()
{
    qCDebug(CHOQOK);
    // When we were already done the timer might still fire.
    // Do nothing in that case.
    if ( _kpmp->shutdownMode == PluginManagerPrivate::DoneShutdown )
        return;

    QStringList remaining;
    for ( PluginManagerPrivate::InfoToPluginMap::ConstIterator it = _kpmp->loadedPlugins.constBegin();
          it != _kpmp->loadedPlugins.constEnd(); ++it )
        remaining.append( it.value()->pluginId() );

    qCWarning(CHOQOK) << "Some plugins didn't shutdown in time!" << endl
        << "Remaining plugins: " << remaining << endl
        << "Forcing Choqok shutdown now." << endl;

    slotShutdownDone();
}

void PluginManager::slotShutdownDone()
{
    qCDebug(CHOQOK) ;
    _kpmp->shutdownMode = PluginManagerPrivate::DoneShutdown;
}

void PluginManager::loadAllPlugins()
{
    qCDebug(CHOQOK);
    KSharedConfig::Ptr config = KSharedConfig::openConfig();
    if ( config->hasGroup( QLatin1String( "Plugins" ) ) )
    {
        QMap<QString, bool> pluginsMap;

        QMap<QString, QString> entries = config->entryMap( QLatin1String( "Plugins" ) );
        QMap<QString, QString>::Iterator it;
        for ( it = entries.begin(); it != entries.end(); ++it )
        {
            QString key = it.key();
            if ( key.endsWith( QLatin1String( "Enabled" ) ) )
                pluginsMap.insert( key.left(key.length() - 7), (it.value() == QLatin1String( "true" )) );
        }

        QList<KPluginInfo> plugins = availablePlugins( QString::null );    //krazy:exclude=nullstrassign for old broken gcc
        QList<KPluginInfo>::ConstIterator it2 = plugins.constBegin();
        QList<KPluginInfo>::ConstIterator end = plugins.constEnd();
        for ( ; it2 != end; ++it2 )
        {
            if ( it2->category() == QLatin1String( "MicroBlogs" ) ||
                 it2->category() == QLatin1String( "Shorteners" ) )
                continue;

            QString pluginName = it2->pluginName();
            if ( pluginsMap.value( pluginName, it2->isPluginEnabledByDefault() ) )
            {
                if ( !plugin( pluginName ) )
                    _kpmp->pluginsToLoad.push( pluginName );
            }
            else
            {
                //This happens if the user unloaded plugins with the config plugin page.
                // No real need to be assync because the user usually unload few plugins
                // compared tto the number of plugin to load in a cold start. - Olivier
                if ( plugin( pluginName ) )
                    unloadPlugin( pluginName );
            }
        }
    }
    else
    {
        // we had no config, so we load any plugins that should be loaded by default.
        QList<KPluginInfo> plugins = availablePlugins( QString::null );    //krazy:exclude=nullstrassign for old broken gcc
        QList<KPluginInfo>::ConstIterator it = plugins.constBegin();
        QList<KPluginInfo>::ConstIterator end = plugins.constEnd();
        for ( ; it != end; ++it )
        {
            if ( it->category() == QLatin1String( "MicroBlogs" ) ||
                it->category() == QLatin1String( "Shorteners" ) )
                continue;
            if ( it->isPluginEnabledByDefault() )
                _kpmp->pluginsToLoad.push( it->pluginName() );
        }
    }
    // Schedule the plugins to load
    QTimer::singleShot( 0, this, SLOT( slotLoadNextPlugin() ) );
}

void PluginManager::slotLoadNextPlugin()
{
    qCDebug(CHOQOK);
    if ( _kpmp->pluginsToLoad.isEmpty() )
    {
        if ( _kpmp->shutdownMode == PluginManagerPrivate::StartingUp )
        {
            _kpmp->shutdownMode = PluginManagerPrivate::Running;
            _kpmp->isAllPluginsLoaded = true;
            qCDebug(CHOQOK)<<"All plugins loaded...";
            Q_EMIT allPluginsLoaded();
        }
        return;
    }

    QString key = _kpmp->pluginsToLoad.pop();
    loadPluginInternal( key );

    // Schedule the next run unconditionally to avoid code duplication on the
    // allPluginsLoaded() signal's handling. This has the added benefit that
    // the signal is delayed one event loop, so the accounts are more likely
    // to be instantiated.
    QTimer::singleShot( 0, this, SLOT( slotLoadNextPlugin() ) );
}

Plugin * PluginManager::loadPlugin( const QString &_pluginId, PluginLoadMode mode /* = LoadSync */ )
{
    QString pluginId = _pluginId;

    // Try to find legacy code
    // FIXME: Find any cases causing this, remove them, and remove this too - Richard
    if ( pluginId.endsWith( QLatin1String( ".desktop" ) ) )
    {
        qCWarning(CHOQOK) << "Trying to use old-style API!" << endl;
        pluginId = pluginId.remove( QRegExp( QLatin1String( ".desktop$" ) ) );
    }

    if ( mode == LoadSync )
    {
        return loadPluginInternal( pluginId );
    }
    else
    {
        _kpmp->pluginsToLoad.push( pluginId );
        QTimer::singleShot( 0, this, SLOT( slotLoadNextPlugin() ) );
        return 0L;
    }
}

Plugin *PluginManager::loadPluginInternal( const QString &pluginId )
{
    qCDebug(CHOQOK) << "Loading Plugin: " << pluginId;

    KPluginInfo info = infoForPluginId( pluginId );
    if ( !info.isValid() )
    {
        qCWarning(CHOQOK) << "Unable to find a plugin named '" << pluginId << "'!";
        return 0L;
    }

    if ( _kpmp->loadedPlugins.contains( info ) )
        return _kpmp->loadedPlugins[ info ];

    QString error;
    Plugin *plugin = KServiceTypeTrader::createInstanceFromQuery<Plugin>( QString::fromLatin1( "Choqok/Plugin" ), QString::fromLatin1( "[X-KDE-PluginInfo-Name]=='%1'" ).arg( pluginId ), this, QVariantList(), &error );

    if ( plugin )
    {
        _kpmp->loadedPlugins.insert( info, plugin );
        info.setPluginEnabled( true );

        connect( plugin, SIGNAL( destroyed( QObject * ) ), this, SLOT( slotPluginDestroyed( QObject * ) ) );
        connect( plugin, SIGNAL( readyForUnload() ), this, SLOT( slotPluginReadyForUnload() ) );

        qCDebug(CHOQOK) << "Successfully loaded plugin '" << pluginId << "'";

        if( plugin->pluginInfo().category() != "MicroBlogs" && plugin->pluginInfo().category() != "Shorteners" ){
            qCDebug(CHOQOK)<<"Emitting pluginLoaded()";
            Q_EMIT pluginLoaded( plugin );
        }

//         Protocol* protocol = dynamic_cast<Protocol*>( plugin );
//         if ( protocol )
//             emit protocolLoaded( protocol );
    }
    else
    {
        qCDebug(CHOQOK) << "Loading plugin " << pluginId << " failed, KServiceTypeTrader reported error: " << error ;
    }

    return plugin;
}

bool PluginManager::unloadPlugin( const QString &spec )
{
    qCDebug(CHOQOK) << spec;
    if( Plugin *thePlugin = plugin( spec ) )
    {
        qCDebug(CHOQOK)<<"Unloading "<<spec;
        thePlugin->aboutToUnload();
        return true;
    }
    else
        return false;
}

void PluginManager::slotPluginDestroyed( QObject *plugin )
{
    qCDebug(CHOQOK);
    for ( PluginManagerPrivate::InfoToPluginMap::Iterator it = _kpmp->loadedPlugins.begin();
          it != _kpmp->loadedPlugins.end(); ++it )
    {
        if ( it.value() == plugin )
        {
            QString pluginName = it.key().pluginName();
            _kpmp->loadedPlugins.erase( it );
            Q_EMIT pluginUnloaded( pluginName );
            break;
        }
    }

    if ( _kpmp->shutdownMode == PluginManagerPrivate::ShuttingDown && _kpmp->loadedPlugins.isEmpty() )
    {
        // Use a timer to make sure any pending deleteLater() calls have
        // been handled first
        QTimer::singleShot( 0, this, SLOT( slotShutdownDone() ) );
    }
}

Plugin* PluginManager::plugin( const QString &_pluginId ) const
{
    // Hack for compatibility with Plugin::pluginId(), which returns
    // classname() instead of the internal name. Changing that is not easy
    // as it invalidates the config file, the contact list, and most likely
    // other code as well.
    // For now, just transform FooProtocol to choqok_foo.
    // FIXME: In the future we'll need to change this nevertheless to unify
    //        the handling - Martijn
    QString pluginId = _pluginId;
    if ( pluginId.endsWith( QLatin1String( "Protocol" ) ) )
        pluginId = QLatin1String( "choqok_" ) + _pluginId.toLower().remove( QString::fromLatin1( "protocol" ) );
    // End hack

    KPluginInfo info = infoForPluginId( pluginId );
    if ( !info.isValid() )
        return 0L;

    if ( _kpmp->loadedPlugins.contains( info ) )
        return _kpmp->loadedPlugins[ info ];
    else
        return 0L;
}

KPluginInfo PluginManager::infoForPluginId( const QString &pluginId ) const
{
    QList<KPluginInfo>::ConstIterator it;
    for ( it = _kpmp->plugins.constBegin(); it != _kpmp->plugins.constEnd(); ++it )
    {
        if ( it->pluginName() == pluginId )
            return *it;
    }

    return KPluginInfo();
}

bool PluginManager::setPluginEnabled( const QString &_pluginId, bool enabled /* = true */ )
{
    QString pluginId = _pluginId;

    KConfigGroup config(KSharedConfig::openConfig(), "Plugins");

    // FIXME: What is this for? This sort of thing is kconf_update's job - Richard
    if ( !pluginId.startsWith( QLatin1String( "choqok_" ) ) )
        pluginId.prepend( QLatin1String( "choqok_" ) );

    if ( !infoForPluginId( pluginId ).isValid() )
        return false;

    config.writeEntry( pluginId + QLatin1String( "Enabled" ), enabled );
    config.sync();

    return true;
}

bool PluginManager::isAllPluginsLoaded() const
{
    return _kpmp->isAllPluginsLoaded;
}

void PluginManager::slotAboutToQuit()
{
    shutdown();
}

} //END namespace Choqok


