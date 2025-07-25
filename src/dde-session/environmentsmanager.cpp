// SPDX-FileCopyrightText: 2021 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "environmentsmanager.h"
#include "utils/utils.h"
#include "org_freedesktop_systemd1_Manager.h"
#include "org_freedesktop_DBus.h"

#include <QProcess>
#include <QDebug>
#include <QDBusInterface>

EnvironmentsManager::EnvironmentsManager()
{
    createGeneralEnvironments();
    createDBusEnvironments();
}

void EnvironmentsManager::init()
{
    qInfo() << "environments manager init";

    // clear env
    QByteArray sessionType = qgetenv("XDG_SESSION_TYPE");
    bool ret = false;
    if (sessionType == "x11") {
        // 从wayland切换到x11后，此环境变量还存在
        // 部分应用以此环境变量是否存在来判断当前是否是wayland环境, 为避免导致的一系列问题，启动后清除它
        ret = unsetEnv("WAYLAND_DISPLAY");
        ret = unsetEnv("QT_WAYLAND_SHELL_INTEGRATION");

    } else if (sessionType == "wayland") {
        ret = true;
    }
    qDebug() << "clear env, result: " << ret;

    // setup general envs
    for (auto it : m_envMap.keys()) {
        qputenv(it.toStdString().data(), m_envMap[it].toStdString().data());
    }

    {
        QStringList envs;
        for (auto it : m_envMap.keys()) {
            envs.append(it + "=" + m_envMap[it]);
        }

        // systemd1
        org::freedesktop::systemd1::Manager systemd1("org.freedesktop.systemd1", "/org/freedesktop/systemd1", QDBusConnection::sessionBus());
        QDBusPendingReply<void> replySystemd1 = systemd1.SetEnvironment(envs);
        replySystemd1.waitForFinished();
        if (replySystemd1.isError()) {
            qWarning() << "failed to set systemd1 envs: " << envs;
        }

        // dbus
        org::freedesktop::DBus dbus("org.freedesktop.DBus", "/org/freedesktop/DBus", QDBusConnection::sessionBus());
        EnvInfoList envInfos;
        for (auto it : m_envMap.keys()) {
            envInfos.insert(it, m_envMap[it]);
        }

        QDBusPendingReply<void> replyDbus = dbus.UpdateActivationEnvironment(envInfos);
        replyDbus.waitForFinished();
        if (replyDbus.isError()) {
            qWarning() << "failed to update dbus envs:" << envInfos;
        }
    }

    qInfo() << "environments manager init finished";
}

void EnvironmentsManager::createGeneralEnvironments()
{
    double scaleFactor = 1.0;
    QDBusInterface dbusInterface("org.deepin.dde.XSettings1", "/org/deepin/dde/XSettings1",
        "org.deepin.dde.XSettings1", QDBusConnection::sessionBus());
    if (dbusInterface.isValid()) {
        QDBusReply<double> scaleFactorReply = dbusInterface.call("GetScaleFactor");
        if (scaleFactorReply.isValid()) {
            scaleFactor = scaleFactorReply.value();
        }
    }

    auto envs = QProcessEnvironment::systemEnvironment();
    auto keys = envs.keys();

    for (const auto& key : keys) {
        m_envMap.insert(key, envs.value(key));
    }

    m_envMap.insert("GNOME_DESKTOP_SESSION_ID", "this-is-deprecated");
    m_envMap.insert("XDG_CURRENT_DESKTOP", "DDE");
    m_envMap.insert("QT_DBL_CLICK_DIST", QString::number(15 * scaleFactor));

    QByteArray sessionType = qgetenv("XDG_SESSION_TYPE");
    if (sessionType == "x11") {
        m_envMap.insert("QT_QPA_PLATFORM", "dxcb;xcb");
    } else if (sessionType == "wayland") {
        m_envMap.insert("QT_QPA_PLATFORM", "wayland;xcb");
        m_envMap.insert("QT_WAYLAND_RECONNECT", "1");
        m_envMap.insert("QT_WAYLAND_SHELL_INTEGRATION", "xdg-shell;wl-shell;ivi-shell;qt-shell;");
        m_envMap.insert("XCURSOR_SIZE", "24");
    }

}

void EnvironmentsManager::createDBusEnvironments()
{
    QByteArrayList additionalEnvs = {"LANG", "LANGUAGE"};
    for (auto env : additionalEnvs) {
        m_envMap.insert(env, qgetenv(env));
    }

    QByteArray sessionType = qgetenv("XDG_SESSION_TYPE");
    if (sessionType == "wayland") {
        QFile localeFile(QStandardPaths::locate(QStandardPaths::ConfigLocation, "locale.conf"));
        if (localeFile.open(QIODevice::ReadOnly)) {
            QTextStream in(&localeFile);

            while (!in.atEnd()) {
                QString line = in.readLine();
                if (line.startsWith("LANG=")) {
                    m_envMap.insert("LANG", line.mid(strlen("LANG=")));
                } else if (line.startsWith("LANGUAGE=")) {
                    m_envMap.insert("LANGUAGE", line.mid(strlen("LANGUAGE=")));
                }
            }
        }
    }
}

bool EnvironmentsManager::unsetEnv(QString env)
{
    // dbus
    org::freedesktop::DBus dbus("org.freedesktop.DBus", "/org/freedesktop/DBus", QDBusConnection::sessionBus());
    EnvInfoList envs;
    envs.insert(env, "");
    QDBusPendingReply<void> replyDbus = dbus.UpdateActivationEnvironment(envs);
    replyDbus.waitForFinished();
    if (replyDbus.isError()) {
        qWarning() << "unset dbus env failed:" << env;
        return false;
    }

    // systemd1
    org::freedesktop::systemd1::Manager systemd1("org.freedesktop.systemd1", "/org/freedesktop/systemd1", QDBusConnection::sessionBus());
    QDBusPendingReply<void> replySystemd1 = systemd1.UnsetEnvironment(QStringList(env));
    replySystemd1.waitForFinished();
    if (replySystemd1.isError()) {
        qWarning() << "unset systemd1 env failed:" << env;
        return false;
    }

    return true;
}
