#pragma once

#include <QObject>
#include <QVariantMap>
#include <functional>
#include <memory>

#include "capabilities.h"
#include "catalog.h"
#include "changecoordinator.h"
#include "commandrunner.h"
#include "compat.h"
#include "hypromodule.h"
#include "iconthemeprovider.h"

class LauncherEditor;

struct ReadResult {
    QString status = "unknown";  // ok, unknown, custom, unavailable
    QString value;
    QString source;
    QString reason;
    bool isOverride = false;
    QVariantMap extra;  // Structure a setting has beyond one value, such as who agrees on it.
};

struct ChangePlan {
    bool ok = false;
    QString error;
    QString normalized;
    QString confirmText;
    bool needsOwnership = false;
    QStringList effects;
    QStringList files;
    QString recovery;
    QString restart;
    QVariantMap toVariant() const;
    QJsonObject toJson() const;
};

// Reads, validates, applies, and verifies the typed settings in the catalog. Each
// value crosses into a command as one argument and never through a shell.
class SettingsEngine : public QObject {
    Q_OBJECT

public:
    using ReadCallback = std::function<void(const ReadResult &)>;
    using ChoicesCallback = std::function<void(const QVariantList &)>;
    using ApplyCallback = std::function<void(const Transaction &)>;

    SettingsEngine(const Catalog *catalog, CommandRunner *runner, Capabilities *capabilities,
                   ChangeCoordinator *coordinator, Compat *compat = nullptr, QObject *parent = nullptr);

    // Icon packs need more than a command or a file can describe, and the same path has
    // to serve the window and the CLI, so the shared provider is plugged in here.
    void setIconThemes(IconThemeProvider *icons) { m_icons = icons; }
    void setLaunchers(LauncherEditor *launchers) { m_launchers = launchers; }

    void read(const QString &id, ReadCallback callback);
    void readChoices(const QString &id, ChoicesCallback callback);

    // Pure check of a proposed value against the entry's type and limits. Choices
    // from a live list are checked against `knownChoices`.
    ChangePlan plan(const QString &id, const QString &value, const QVariantList &knownChoices = {}) const;

    void apply(const QString &id, const QString &value, ApplyCallback callback, const QString &kind = "set");
    void reset(const QString &id, ApplyCallback callback);
    void undo(const QString &transactionId, ApplyCallback callback);

    QString displayValue(const CatalogEntry &entry, const QString &value, const QVariantList &choices = {}) const;

    static QString parseOutput(const CommandResult &result, const QJsonObject &spec, bool *ok);
    static QString parseHyprOption(const QByteArray &json, bool *ok);
    static bool valuesMatch(const CatalogEntry &entry, const QString &a, const QString &b);

private:
    void readFiles(const CatalogEntry &entry, ReadCallback callback);
    void readBlur(const CatalogEntry *entry, ReadCallback callback);
    // Reads Hyprland options one after another, stopping at the first that fails.
    void hyprOptions(const QStringList &names, std::function<void(const QHash<QString, QString> &, const QString &)> done);
    void applyCommand(const CatalogEntry &entry, const QJsonObject &write, const QString &value, const QString &before,
                      const QString &target, ChangeCoordinator::Done done);
    QStringList substitute(const QJsonArray &argv, const QString &value, const CatalogEntry *entry = nullptr, const QString &target = {}) const;
    static QStringList targeted(const QStringList &argv, const CatalogEntry &entry, const QString &target);
    QStringList expandArgv(const QStringList &argv) const;
    QString configuredHyprValue(const CatalogEntry &entry, const QStringList &path, QString *source) const;
    void verifyValue(const QString &id, const QString &expected, int attempts, ChangeCoordinator::Done done);
    void submitHypr(const CatalogEntry &entry, const QString &value, const QString &before, bool beforeWasDefault, bool remove,
                    const QString &kind, ApplyCallback callback);
    void submitJson(const CatalogEntry &entry, const QString &value, const QString &before, bool beforeWasDefault, bool remove,
                    const QString &kind, ApplyCallback callback);
    void reloadHyprland(ChangeCoordinator::Done done, const QStringList &baseline, bool checkErrors);
    void confirmRead(const QString &id, std::shared_ptr<QString> settled, ChangeCoordinator::Done done);

    const Catalog *m_catalog;
    CommandRunner *m_runner;
    Capabilities *m_capabilities;
    ChangeCoordinator *m_coordinator;
    Compat *m_compat;
    IconThemeProvider *m_icons = nullptr;
    LauncherEditor *m_launchers = nullptr;
    QHash<QString, QVariantList> m_lastChoices;
};
