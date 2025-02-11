/*  This file was part of the KDE libraries

    SPDX-FileCopyrightText: 2021 Tomaz Canabrava <tcanabrava@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "sshmanagermodel.h"

#include <QStandardItem>

#include <KLocalizedString>

#include <KConfig>
#include <KConfigGroup>

#include <QDebug>
#include <QFile>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTextStream>

#include "profile/ProfileManager.h"
#include "sshconfigurationdata.h"

Q_LOGGING_CATEGORY(SshManagerPlugin, "org.kde.konsole.plugin.sshmanager")

namespace
{
const QString SshDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/.ssh/");
}

SSHManagerModel::SSHManagerModel(QObject *parent)
    : QStandardItemModel(parent)
{
    load();
    if (!m_sshConfigTopLevelItem) {
        // this also sets the m_sshConfigTopLevelItem if the text is `SSH Config`.
        addTopLevelItem(i18n("SSH Config"));
    }
    if (invisibleRootItem()->rowCount() == 0) {
        addTopLevelItem(i18n("Default"));
    }

    m_sshConfigWatcher.addPath(SshDir + QStringLiteral("config"));
    connect(&m_sshConfigWatcher, &QFileSystemWatcher::fileChanged, this, [this] {
        startImportFromSshConfig();
    });
    startImportFromSshConfig();
}

SSHManagerModel::~SSHManagerModel() noexcept
{
    save();
}

QStandardItem *SSHManagerModel::addTopLevelItem(const QString &name)
{
    for (int i = 0, end = invisibleRootItem()->rowCount(); i < end; i++) {
        if (invisibleRootItem()->child(i)->text() == name) {
            return nullptr;
        }
    }

    auto *newItem = new QStandardItem();
    newItem->setText(name);
    newItem->setToolTip(i18n("%1 is a folder for SSH entries", name));
    invisibleRootItem()->appendRow(newItem);
    invisibleRootItem()->sortChildren(0);

    if (name == i18n("SSH Config")) {
        m_sshConfigTopLevelItem = newItem;
    }

    return newItem;
}

void SSHManagerModel::addChildItem(const SSHConfigurationData &config, const QString &parentName)
{
    QStandardItem *parentItem = nullptr;
    for (int i = 0, end = invisibleRootItem()->rowCount(); i < end; i++) {
        if (invisibleRootItem()->child(i)->text() == parentName) {
            parentItem = invisibleRootItem()->child(i);
            break;
        }
    }

    if (!parentItem) {
        parentItem = addTopLevelItem(parentName);
    }

    auto newChild = new QStandardItem();
    newChild->setData(QVariant::fromValue(config), SSHRole);
    newChild->setText(config.name);
    newChild->setToolTip(i18n("Host: %1", config.host));
    parentItem->appendRow(newChild);
    parentItem->sortChildren(0);
}

bool SSHManagerModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    const bool ret = QStandardItemModel::setData(index, value, role);
    invisibleRootItem()->sortChildren(0);
    return ret;
}

void SSHManagerModel::editChildItem(const SSHConfigurationData &config, const QModelIndex &idx)
{
    QStandardItem *item = itemFromIndex(idx);
    item->setData(QVariant::fromValue(config), SSHRole);
    item->setData(config.name, Qt::DisplayRole);
    item->parent()->sortChildren(0);
}

QStringList SSHManagerModel::folders() const
{
    QStringList retList;
    for (int i = 0, end = invisibleRootItem()->rowCount(); i < end; i++) {
        retList.push_back(invisibleRootItem()->child(i)->text());
    }
    return retList;
}

bool SSHManagerModel::hasHost(const QString &host) const
{
    // runs in O(N), should be ok for the amount of data peophe have.
    for (int i = 0, end = invisibleRootItem()->rowCount(); i < end; i++) {
        QStandardItem *iChild = invisibleRootItem()->child(i);
        for (int e = 0, end = iChild->rowCount(); e < end; e++) {
            QStandardItem *eChild = iChild->child(e);
            const auto data = eChild->data(SSHManagerModel::Roles::SSHRole).value<SSHConfigurationData>();
            if (data.host == host) {
                return true;
            }
        }
    }
    return false;
}

void SSHManagerModel::load()
{
    auto config = KConfig(QStringLiteral("konsolesshconfig"), KConfig::OpenFlag::SimpleConfig);
    for (const QString &groupName : config.groupList()) {
        KConfigGroup group = config.group(groupName);
        addTopLevelItem(groupName);
        for (const QString &sessionName : group.groupList()) {
            SSHConfigurationData data;
            KConfigGroup sessionGroup = group.group(sessionName);
            data.host = sessionGroup.readEntry("hostname");
            data.name = sessionGroup.readEntry("identifier");
            data.port = sessionGroup.readEntry("port");
            data.profileName = sessionGroup.readEntry("profileName");
            data.username = sessionGroup.readEntry("username");
            data.sshKey = sessionGroup.readEntry("sshkey");
            data.useSshConfig = sessionGroup.readEntry<bool>("useSshConfig", false);
            data.importedFromSshConfig = sessionGroup.readEntry<bool>("importedFromSshConfig", false);
            addChildItem(data, groupName);
        }
    }
}

void SSHManagerModel::save()
{
    auto config = KConfig(QStringLiteral("konsolesshconfig"), KConfig::OpenFlag::SimpleConfig);
    for (const QString &groupName : config.groupList()) {
        config.deleteGroup(groupName);
    }

    for (int i = 0, end = invisibleRootItem()->rowCount(); i < end; i++) {
        QStandardItem *groupItem = invisibleRootItem()->child(i);
        const QString groupName = groupItem->text();
        KConfigGroup baseGroup = config.group(groupName);
        for (int e = 0, rend = groupItem->rowCount(); e < rend; e++) {
            QStandardItem *sshElement = groupItem->child(e);
            const auto data = sshElement->data(SSHRole).value<SSHConfigurationData>();
            KConfigGroup sshGroup = baseGroup.group(data.name.trimmed());
            sshGroup.writeEntry("hostname", data.host.trimmed());
            sshGroup.writeEntry("identifier", data.name.trimmed());
            sshGroup.writeEntry("port", data.port.trimmed());
            sshGroup.writeEntry("profileName", data.profileName.trimmed());
            sshGroup.writeEntry("sshkey", data.sshKey.trimmed());
            sshGroup.writeEntry("useSshConfig", data.useSshConfig);
            sshGroup.writeEntry("username", data.username);
            sshGroup.writeEntry("importedFromSshConfig", data.importedFromSshConfig);
            sshGroup.sync();
        }
        baseGroup.sync();
    }
    config.sync();
}

Qt::ItemFlags SSHManagerModel::flags(const QModelIndex &index) const
{
    if (indexFromItem(invisibleRootItem()) == index.parent()) {
        return QStandardItemModel::flags(index);
    } else {
        return QStandardItemModel::flags(index) & ~Qt::ItemIsEditable;
    }
}

void SSHManagerModel::removeIndex(const QModelIndex &idx)
{
    if (idx.data(Qt::DisplayRole) == i18n("SSH Config")) {
        m_sshConfigTopLevelItem = nullptr;
    }

    removeRow(idx.row(), idx.parent());
}

void SSHManagerModel::startImportFromSshConfig()
{
    importFromSshConfigFile(SshDir + QStringLiteral("config"));
}

void SSHManagerModel::importFromSshConfigFile(const QString &file)
{
    QFile sshConfig(file);
    if (!sshConfig.open(QIODevice::ReadOnly)) {
        qCDebug(SshManagerPlugin) << "Can't open config file";
    }
    QTextStream stream(&sshConfig);
    QString line;

    SSHConfigurationData data;

    // If we hit a *, we ignore till the next Host.
    bool ignoreEntry = false;
    while (stream.readLineInto(&line)) {
        // ignore comments
        if (line.startsWith(QStringLiteral("#"))) {
            continue;
        }

        QStringList lists = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        // ignore lines that are not "Type Value"
        if (lists.count() != 2) {
            continue;
        }

        if (lists.at(0) == QStringLiteral("Import")) {
            if (lists.at(1).contains(QLatin1Char('*'))) {
                // TODO: We don't handle globbing yet.
                continue;
            }

            importFromSshConfigFile(SshDir + lists.at(1));
            continue;
        }

        if (lists.at(0) == QStringLiteral("Host")) {
            if (line.contains(QLatin1Char('*'))) {
                // Panic, ignore everything until the next Host appears.
                ignoreEntry = true;
                continue;
            } else {
                ignoreEntry = false;
            }

            // When we hit this, that means that we just finished reading the
            // *previous* host. so we need to add it to the list, if we can,
            // and read the next value.
            if (!data.host.isEmpty() && !hasHost(data.host)) {
                // We already registered this entity.

                if (data.name.isEmpty()) {
                    data.name = data.host;
                }
                data.useSshConfig = true;
                data.importedFromSshConfig = true;
                data.profileName = Konsole::ProfileManager::instance()->defaultProfile()->name();
                addChildItem(data, i18n("SSH Config"));
            }

            data = {};
            data.host = lists.at(1);
        }

        if (ignoreEntry) {
            continue;
        }

        if (lists.at(0) == QStringLiteral("HostName")) {
            // hostname is always after Host, so this will be true.
            const QString currentHost = data.host;
            data.host = lists.at(1).trimmed();
            data.name = currentHost.trimmed();
        } else if (lists.at(0) == QStringLiteral("IdentityFile")) {
            data.sshKey = lists.at(1).trimmed();
        } else if (lists.at(0) == QStringLiteral("Port")) {
            data.port = lists.at(1).trimmed();
        } else if (lists.at(0) == QStringLiteral("User")) {
            data.username = lists.at(1).trimmed();
        }
    }

    // the last possible read
    if (data.host.count()) {
        if (!hasHost(data.host)) {
            if (data.name.isEmpty()) {
                data.name = data.host.trimmed();
            }
            data.useSshConfig = true;
            data.importedFromSshConfig = true;
            addChildItem(data, i18n("SSH Config"));
        }
    }
}
