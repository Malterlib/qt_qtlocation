/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtPositioning module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/
#include <qgeopositioninfosource.h>
#include "qgeopositioninfosource_p.h"
#include "qgeopositioninfosourcefactory.h"

#include <QFile>
#include <QPluginLoader>
#include <QStringList>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QtCore/private/qfactoryloader_p.h>
#include <QtCore/private/qthread_p.h>

#include <algorithm>

QT_BEGIN_NAMESPACE

Q_GLOBAL_STATIC_WITH_ARGS(QFactoryLoader, loader,
        ("org.qt-project.qt.position.sourcefactory/6.0",
         QLatin1String("/position")))

/*!
    \class QGeoPositionInfoSource
    \inmodule QtPositioning
    \ingroup QtPositioning-positioning
    \since 5.2

    \brief The QGeoPositionInfoSource class is an abstract base class for the distribution of positional updates.

    The static function QGeoPositionInfoSource::createDefaultSource() creates a default
    position source that is appropriate for the platform, if one is available.
    Otherwise, QGeoPositionInfoSource will check for available plugins that
    implement the QGeoPositionInfoSourceFactory interface.

    Users of a QGeoPositionInfoSource subclass can request the current position using
    requestUpdate(), or start and stop regular position updates using
    startUpdates() and stopUpdates(). When an update is available,
    positionUpdated() is emitted. The last known position can be accessed with
    lastKnownPosition().

    If regular position updates are required, setUpdateInterval() can be used
    to specify how often these updates should be emitted. If no interval is
    specified, updates are simply provided whenever they are available.
    For example:

    \code
        // Emit updates every 10 seconds if available
        QGeoPositionInfoSource *source = QGeoPositionInfoSource::createDefaultSource(0);
        if (source)
            source->setUpdateInterval(10000);
    \endcode

    To remove an update interval that was previously set, call
    setUpdateInterval() with a value of 0.

    Note that the position source may have a minimum value requirement for
    update intervals, as returned by minimumUpdateInterval().
*/

/*!
    \enum QGeoPositionInfoSource::PositioningMethod
    Defines the types of positioning methods.

    \value NoPositioningMethods None of the positioning methods.
    \value SatellitePositioningMethods Satellite-based positioning methods such as GPS or GLONASS.
    \value NonSatellitePositioningMethods Other positioning methods such as 3GPP cell identifier or WiFi based positioning.
    \value AllPositioningMethods Satellite-based positioning methods as soon as available. Otherwise non-satellite based methods.
*/

QGeoPositionInfoSourcePrivate::~QGeoPositionInfoSourcePrivate()
{

}

QGeoPositionInfoSourceFactory *QGeoPositionInfoSourcePrivate::loadFactory(const QJsonObject &meta)
{
    const int idx = static_cast<int>(meta.value(QStringLiteral("index")).toDouble());
    if (idx < 0)
        return nullptr;
    QObject *instance = loader()->instance(idx);
    if (!instance)
        return nullptr;
    return qobject_cast<QGeoPositionInfoSourceFactory *>(instance);
}

QMultiHash<QString, QJsonObject> QGeoPositionInfoSourcePrivate::plugins(bool reload)
{
    static QMultiHash<QString, QJsonObject> plugins;
    static bool alreadyDiscovered = false;

    if (reload == true)
        alreadyDiscovered = false;

    if (!alreadyDiscovered) {
        loadPluginMetadata(plugins);
        alreadyDiscovered = true;
    }
    return plugins;
}

static bool pluginComparator(const QJsonObject &p1, const QJsonObject &p2)
{
    const QString prio = QStringLiteral("Priority");
    if (p1.contains(prio) && !p2.contains(prio))
        return true;
    if (!p1.contains(prio) && p2.contains(prio))
        return false;
    if (p1.value(prio).isDouble() && !p2.value(prio).isDouble())
        return true;
    if (!p1.value(prio).isDouble() && p2.value(prio).isDouble())
        return false;
    return (p1.value(prio).toDouble() > p2.value(prio).toDouble());
}

QList<QJsonObject> QGeoPositionInfoSourcePrivate::pluginsSorted()
{
    QList<QJsonObject> list = plugins().values();
    std::stable_sort(list.begin(), list.end(), pluginComparator);
    return list;
}

void QGeoPositionInfoSourcePrivate::loadPluginMetadata(QMultiHash<QString, QJsonObject> &plugins)
{
    QFactoryLoader *l = loader();
    QList<QJsonObject> meta = l->metaData();
    for (int i = 0; i < meta.size(); ++i) {
        QJsonObject obj = meta.at(i).value(QStringLiteral("MetaData")).toObject();
        const QString testableKey = QStringLiteral("Testable");
        if (obj.contains(testableKey) && !obj.value(testableKey).toBool()) {
            static bool inTest = qEnvironmentVariableIsSet("QT_QTESTLIB_RUNNING");
            if (inTest)
                continue;
        }
        obj.insert(QStringLiteral("index"), i);
        plugins.insert(obj.value(QStringLiteral("Provider")).toString(), obj);
    }
}

/*!
    Creates a position source with the specified \a parent.
*/

QGeoPositionInfoSource::QGeoPositionInfoSource(QObject *parent)
        : QObject(*new QGeoPositionInfoSourcePrivate, parent)
{
    qRegisterMetaType<QGeoPositionInfo>();
    Q_D(QGeoPositionInfoSource);
    d->interval = 0;
    d->methods = {};
}

/*!
    Destroys the position source.
*/
QGeoPositionInfoSource::~QGeoPositionInfoSource()
{
}

/*!
    \property QGeoPositionInfoSource::sourceName
    \brief This property holds the unique name of the position source
           implementation in use.

    This is the same name that can be passed to createSource() in order to
    create a new instance of a particular position source implementation.
*/
QString QGeoPositionInfoSource::sourceName() const
{
    Q_D(const QGeoPositionInfoSource);
    return d->sourceName;
}

/*!
    Sets the backend-specific property named \a name to \a value.
    Returns \c true on success, \c false otherwise.
    Backend-specific properties can be used to configure the positioning subsystem behavior
    at runtime.
    Supported backend-specific properties are listed and described in
    \l {Qt Positioning plugins#Default plugins}.

    \sa backendProperty
    \since Qt 5.14
*/
bool QGeoPositionInfoSource::setBackendProperty(const QString &name, const QVariant &value)
{
    Q_UNUSED(name)
    Q_UNUSED(value)
    return false;
}

/*!
    Returns the value of the backend-specific property named \a name, if present.
    Otherwise, the returned value will be invalid.
    Supported backend-specific properties are listed and described in
    \l {Qt Positioning plugins#Default plugins}.

    \sa setBackendProperty
    \since Qt 5.14
*/
QVariant QGeoPositionInfoSource::backendProperty(const QString &name) const
{
    Q_UNUSED(name)
    return QVariant();
}

/*!
    \property QGeoPositionInfoSource::updateInterval
    \brief This property holds the requested interval in milliseconds between each update.

    If the update interval is not set (or is set to 0) the
    source will provide updates as often as necessary.

    If the update interval is set, the source will provide updates at an
    interval as close to the requested interval as possible. If the requested
    interval is less than the minimumUpdateInterval(),
    the minimum interval is used instead.

    Changes to the update interval will happen as soon as is practical, however the
    time the change takes may vary between implementations.  Whether or not the elapsed
    time from the previous interval is counted as part of the new interval is also
    implementation dependent.

    The default value for this property is 0.

    Note: Subclass implementations must call the base implementation of
    setUpdateInterval() so that updateInterval() returns the correct value.
*/
void QGeoPositionInfoSource::setUpdateInterval(int msec)
{
    Q_D(QGeoPositionInfoSource);
    d->interval = msec;
}

int QGeoPositionInfoSource::updateInterval() const
{
    Q_D(const QGeoPositionInfoSource);
    return d->interval;
}

/*!
    Sets the preferred positioning methods for this source to \a methods.

    If \a methods includes a method that is not supported by the source, the
    unsupported method will be ignored.

    If \a methods does not include a single method available/supported by the source, the
    preferred methods will be set to the set of methods which the source has available.
    If the source has no method availabe (e.g. because its Location service is turned off
    or it does not offer a Location service), the passed \a methods are accepted as they are.

    \b {Note:} When reimplementing this method, subclasses must call the
    base method implementation to ensure preferredPositioningMethods() returns the correct value.

    \sa supportedPositioningMethods()
*/
void QGeoPositionInfoSource::setPreferredPositioningMethods(PositioningMethods methods)
{
    Q_D(QGeoPositionInfoSource);
    if (supportedPositioningMethods() != QGeoPositionInfoSource::NoPositioningMethods) {
        d->methods = methods & supportedPositioningMethods();
        if (d->methods == 0) {
            d->methods = supportedPositioningMethods();
        }
    } else { // avoid that turned of Location service blocks any changes to d->methods
        d->methods = methods;
    }
}

/*!
    Returns the positioning methods set by setPreferredPositioningMethods().
*/
QGeoPositionInfoSource::PositioningMethods QGeoPositionInfoSource::preferredPositioningMethods() const
{
    Q_D(const QGeoPositionInfoSource);
    return d->methods;
}

QGeoPositionInfoSource *QGeoPositionInfoSourcePrivate::createSourceReal(const QJsonObject &meta, const QVariantMap &parameters, QObject *parent)
{
    QGeoPositionInfoSource *s = nullptr;
    auto factory = QGeoPositionInfoSourcePrivate::loadFactory(meta);
    if (factory)
        s = factory->positionInfoSource(parent, parameters);
    if (s)
        s->d_func()->sourceName = meta.value(QStringLiteral("Provider")).toString();

    return s;
}

/*!
    Creates and returns a position source with the given \a parent that
    reads from the system's default sources of location data, or the plugin
    with the highest available priority.

    Returns \c nullptr if the system has no default position source, no valid
    plugins could be found or the user does not have the permission to access
    the current position.
*/
QGeoPositionInfoSource *QGeoPositionInfoSource::createDefaultSource(QObject *parent)
{
    return createDefaultSource(QVariantMap(), parent);
}

/*!
    Creates and returns a position source with the given \a parent that
    reads from the system's default sources of location data, or the plugin
    with the highest available priority.

    Returns \c nullptr if the system has no default position source, no valid plugins
    could be found or the user does not have the permission to access the current position.

    This method passes \a parameters to the factory to configure the source.

    \since Qt 5.14
*/
QGeoPositionInfoSource *QGeoPositionInfoSource::createDefaultSource(const QVariantMap &parameters, QObject *parent)
{
    QList<QJsonObject> plugins = QGeoPositionInfoSourcePrivate::pluginsSorted();
    foreach (const QJsonObject &obj, plugins) {
        if (obj.value(QStringLiteral("Position")).isBool()
                && obj.value(QStringLiteral("Position")).toBool()) {
            QGeoPositionInfoSource *source = QGeoPositionInfoSourcePrivate::createSourceReal(obj, parameters, parent);
            if (source)
                return source;
        }
    }
    return nullptr;
}

/*!
    Creates and returns a position source with the given \a parent,
    by loading the plugin named \a sourceName.

    Returns \c nullptr if the plugin cannot be found.
*/
QGeoPositionInfoSource *QGeoPositionInfoSource::createSource(const QString &sourceName, QObject *parent)
{
    return createSource(sourceName, QVariantMap(), parent);
}

/*!
    Creates and returns a position source with the given \a parent,
    by loading the plugin named \a sourceName.

    Returns \c nullptr if the plugin cannot be found.

    This method passes \a parameters to the factory to configure the source.

    \since Qt 5.14
*/
QGeoPositionInfoSource *QGeoPositionInfoSource::createSource(const QString &sourceName, const QVariantMap &parameters, QObject *parent)
{
    auto plugins = QGeoPositionInfoSourcePrivate::plugins();
    if (plugins.contains(sourceName))
        return QGeoPositionInfoSourcePrivate::createSourceReal(plugins.value(sourceName), parameters, parent);
    return nullptr;
}

/*!
    Returns a list of available source plugins. This includes any default backend
    plugin for the current platform.
*/
QStringList QGeoPositionInfoSource::availableSources()
{
    QStringList plugins;
    const auto meta = QGeoPositionInfoSourcePrivate::plugins();
    for (auto it = meta.cbegin(), end = meta.cend(); it != end; ++it) {
        if (it.value().value(QStringLiteral("Position")).isBool()
                && it.value().value(QStringLiteral("Position")).toBool()) {
            plugins << it.key();
        }
    }

    return plugins;
}

QGeoPositionInfoSource::QGeoPositionInfoSource(QGeoPositionInfoSourcePrivate &dd, QObject *parent)
    : QObject(dd, parent)
{
    qRegisterMetaType<QGeoPositionInfo>();
    Q_D(QGeoPositionInfoSource);
    d->interval = 0;
    d->methods = NoPositioningMethods;
}

/*!
    \fn QGeoPositionInfo QGeoPositionInfoSource::lastKnownPosition(bool fromSatellitePositioningMethodsOnly = false) const = 0;

    Returns an update containing the last known position, or a null update
    if none is available.

    If \a fromSatellitePositioningMethodsOnly is true, this returns the last
    known position received from a satellite positioning method; if none
    is available, a null update is returned.
*/

/*!
    \fn virtual PositioningMethods QGeoPositionInfoSource::supportedPositioningMethods() const = 0;

    Returns the positioning methods available to this source. Availability is defined as being usable
    at the time of calling this function. Therefore user settings like turned off location service or
    limitations to Satellite-based position providers are reflected by this function. Runtime notifications
    when the status changes can be obtained via \l supportedPositioningMethodsChanged().

    Not all platforms distinguish the different positioning methods or communicate the current user
    configuration of the device. The following table provides an overview of the current platform situation:

    \table
    \header
        \li Platform
        \li Brief Description
    \row
        \li Android
        \li Individual provider status and general Location service state are known and communicated
            when location service is active.
    \row
        \li GeoClue
        \li Hardcoced to always return AllPositioningMethods.
    \row
        \li GeoClue2
        \li Individual providers are not distinguishable but disabled Location services reflected.
    \row
        \li iOS/tvOS
        \li Hardcoced to always return AllPositioningMethods.
    \row
        \li macOS
        \li Hardcoced to always return AllPositioningMethods.
    \row
        \li Windows (UWP)
        \li Individual providers are not distinguishable but disabled Location services reflected.
    \endtable

    \sa supportedPositioningMethodsChanged(), setPreferredPositioningMethods()
*/


/*!
    \property QGeoPositionInfoSource::minimumUpdateInterval
    \brief This property holds the minimum time (in milliseconds) required to retrieve a position update.

    This is the minimum value accepted by setUpdateInterval() and
    requestUpdate().
*/


/*!
    \fn virtual void QGeoPositionInfoSource::startUpdates() = 0;

    Starts emitting updates at regular intervals as specified by setUpdateInterval().

    If setUpdateInterval() has not been called, the source will emit updates
    as soon as they become available.

    An errorOccurred() signal with the \l {QGeoPositionInfoSource::Error::}
    {UpdateTimeoutError} will be emitted if this QGeoPositionInfoSource subclass
    determines that it will not be able to provide regular updates. This could
    happen if a satellite fix is lost or if a hardware error is detected.
    Position updates will recommence if the data becomes available later on.
    The \l {QGeoPositionInfoSource::Error::}{UpdateTimeoutError} error will not
    be emitted again until after the periodic updates resume.

    \note Since Qt6 this method always resets the last error to
    \l {QGeoPositionInfoSource::Error::}{NoError} before starting the updates.

    On iOS, starting from version 8, Core Location framework requires additional
    entries in the application's Info.plist with keys NSLocationAlwaysUsageDescription or
    NSLocationWhenInUseUsageDescription and a string to be displayed in the authorization prompt.
    The key NSLocationWhenInUseUsageDescription is used when requesting permission
    to use location services while the app is in the foreground.
    The key NSLocationAlwaysUsageDescription is used when requesting permission
    to use location services whenever the app is running (both the foreground and the background).
    If both entries are defined, NSLocationWhenInUseUsageDescription has a priority in the
    foreground mode.
*/

/*!
    \fn virtual void QGeoPositionInfoSource::stopUpdates() = 0;

    Stops emitting updates at regular intervals.
*/

/*!
    \fn virtual void QGeoPositionInfoSource::requestUpdate(int timeout = 0);

    Attempts to get the current position and emit positionUpdated() with
    this information. If the current position cannot be found within the given \a timeout
    (in milliseconds) or if \a timeout is less than the value returned by
    minimumUpdateInterval(), an errorOccurred() signal with the
    \l {QGeoPositionInfoSource::Error::}{UpdateTimeoutError} is emitted.

    If the timeout is zero, the timeout defaults to a reasonable timeout
    period as appropriate for the source.

    This does nothing if another update request is in progress. However
    it can be called even if startUpdates() has already been called and
    regular updates are in progress.

    If the source uses multiple positioning methods, it tries to get the
    current position from the most accurate positioning method within the
    given timeout.

    \note Since Qt6 this method always resets the last error to
    \l {QGeoPositionInfoSource::Error::}{NoError} before requesting
    the position.
*/

/*!
     \fn virtual QGeoPositionInfoSource::Error QGeoPositionInfoSource::error() const;

     Returns the type of error that last occurred.

    \note Since Qt6 the last error is always reset when calling startUpdates()
    or requestUpdate().
*/

/*!
    \fn void QGeoPositionInfoSource::positionUpdated(const QGeoPositionInfo &update);

    If startUpdates() or requestUpdate() is called, this signal is emitted
    when an update becomes available.

    The \a update value holds the value of the new update.
*/

/*!
    \fn void QGeoPositionInfoSource::errorOccurred(QGeoPositionInfoSource::Error positioningError)

    This signal is emitted after an error occurred. The \a positioningError
    parameter describes the type of error that occurred.
*/

/*!
    \enum QGeoPositionInfoSource::Error

    The Error enumeration represents the errors which can occur.

    \value AccessError The connection setup to the remote positioning backend failed because the
        application lacked the required privileges.
    \value ClosedError  The remote positioning backend closed the connection, which happens for example in case
        the user is switching location services to off. As soon as the location service is re-enabled
        regular updates will resume.
    \value NoError No error has occurred.
    \value UnknownSourceError An unidentified error occurred.
    \value [since 6.2] UpdateTimeoutError If requestUpdate() was called, this
        error indicates that the current position could not be retrieved within
        the specified timeout. If startUpdates() was called, this error
        indicates that this QGeoPositionInfoSource subclass determined that it
        will not be able to provide further regular updates. In the latter case
        the error would not be emitted again until after the regular updates
        resume.
 */

/*!
    \fn void QGeoPositionInfoSource::supportedPositioningMethodsChanged()

    This signal is emitted when the supported positioning methods changed. The cause for a change could be
    a user turning Location services on/off or restricting Location services to certain types (e.g. GPS only).
    Note that changes to the supported positioning methods cannot be detected on all platforms.
    \l supportedPositioningMethods() provides an overview of the current platform support.

    \since Qt 5.12
*/

QT_END_NAMESPACE
