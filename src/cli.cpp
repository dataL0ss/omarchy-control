#include "cli.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>

#include "appcore.h"
#include "coverage.h"
#include "displayprovider.h"
#include "fileops.h"
#include "iconlayer.h"
#include "helperlauncher.h"
#include "hypromodule.h"
#include "pluginsprovider.h"
#include "shortcutsprovider.h"
#include "iconthemeimporter.h"
#include "updater.h"
#include "windowrulesprovider.h"

namespace {

QTextStream &out() {
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err() {
    static QTextStream stream(stderr);
    return stream;
}

void printJson(const QJsonValue &value) {
    const QJsonDocument document = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    out() << document.toJson(QJsonDocument::Indented);
    out().flush();
}

QJsonObject txJson(const Transaction &tx) {
    return tx.toJson();
}

const char Usage[] =
    "Usage: omarchy-control <command> [options]\n\n"
    "  open [--page <id>] [--setting <id>]   Open the window\n"
    "  catalog [--json]                      List every setting and workflow\n"
    "  search <text> [--json]                Search the catalog; never runs anything\n"
    "  get <id> [--json]                     Read a setting's current value\n"
    "  plan <id> <value> [--json]            Validate a change without applying it\n"
    "  apply <id> <value> [--yes] [--json]   Apply and verify a change\n"
    "  reset <id> [--yes] [--json]           Return a setting to its default\n"
    "  history [--json]                      Show recorded changes\n"
    "  undo <transaction> [--json]           Undo a recorded change if nothing edited it since\n"
    "  doctor [--json]                       Check compatibility and read every setting\n"
    "  coverage [--json] [--inventory <f>]   Report coverage; --live re-reads Omarchy's commands\n"
    "  compat [accept]                       Show commands changed since testing; resume them\n"
    "  launch <id> [--yes]                   Open a helper route from the catalog\n"
    "  plugin enable|disable <id>            Turn a shell plugin on or off\n"
    "  widget move <id> <sec> <i> <from> <i> Move a bar widget\n"
    "  shortcut add|remove ...               Manage shortcuts owned by this app\n"
    "  rule add|remove|list ...              Manage window rules owned by this app\n"
    "  display preview <out> <x> <y> [--keep|--revert]   Move a display under the safety timer\n"
    "  integrate menu [--remove]             Add this app to the Omarchy menu\n"
    "  selftest display                      Prove display recovery on this machine\n"
    "  skill install                         Install the agent skill\n"
    "  icons list [--json]                   List installed icon packs, and any problems reading them\n"
    "  icons import <folder|archive> [--yes] [--json]  Install an icon pack from a folder or a .zip, .tar, or .7z\n"
    "  icons reapply [theme]                 Reapply your icon pack choice; the theme hook runs this\n"
    "  icons remove-integration              Remove the hooks, saved choice, and launcher icons, leaving the icons\n"
    "  apps launcher list [--modified|--hidden] [--json]   Every app's launcher, and what was changed\n"
    "  apps launcher set <id> [--name N] [--icon NAME|FILE] [--hide|--show]   Rename, re-icon, or hide an app\n"
    "  apps launcher reset <id>              Put an app's launcher back as its package made it\n"
    "  apps launcher update <id>             Take a package's newer launcher, with the same changes\n"
    "  icons remove <id> [--yes]             Move an icon pack you installed to the Trash; undo brings it back\n"
    "  icons layer status [--json]           What the launcher icon layer covers, and why it may be idle\n"
    "  icons layer sync [--if-stale]         Rebuild the launcher icon layer; the update hook runs this\n"
    "  icons iconic [status|retire|restore]  Hand Iconic's icon files over to this app, or put them back\n"
    "  update [check] [--json]               Check GitHub for a newer version\n"
    "  update install [--yes]                Download, verify, and install it with pacman\n";

bool popFlag(QStringList &args, const QString &flag) {
    return args.removeAll(flag) > 0;
}

QString popOption(QStringList &args, const QString &flag) {
    const int at = args.indexOf(flag);
    if (at < 0 || at + 1 >= args.size())
        return {};
    const QString value = args[at + 1];
    args.remove(at, 2);
    return value;
}

template <typename Fn>
Transaction waitFor(Fn start) {
    // A callback may run before start() returns, when a value needs no process,
    // and quitting a loop that has not started yet would leave it waiting forever.
    Transaction result;
    QEventLoop loop;
    bool done = false;
    start([&](const Transaction &tx) {
        result = tx;
        done = true;
        loop.quit();
    });
    if (!done)
        loop.exec();
    return result;
}

ReadResult readBlocking(AppCore &core, const QString &id) {
    ReadResult result;
    QEventLoop loop;
    bool done = false;
    core.engine.read(id, [&](const ReadResult &r) {
        result = r;
        done = true;
        loop.quit();
    });
    if (!done)
        loop.exec();
    return result;
}

QVariantList choicesBlocking(AppCore &core, const QString &id) {
    QVariantList result;
    QEventLoop loop;
    bool done = false;
    core.engine.readChoices(id, [&](const QVariantList &choices) {
        result = choices;
        done = true;
        loop.quit();
    });
    if (!done)
        loop.exec();
    return result;
}

int cmdUpdate(const Env &env, const QStringList &args, bool yes, bool json) {
    const QString action = args.value(1, QStringLiteral("check"));
    if (action != QLatin1String("check") && action != QLatin1String("install")) {
        err() << Usage;
        return 1;
    }
    CommandRunner runner(env);
    // A copy a package repository installed updates with the system; a GitHub copy, here.
    const Updater::Source source = Updater::sourceBlocking(runner);
    if (source.fromRepository()) {
        const QString current = Updater::currentVersion();
        const bool waiting = Updater::isNewer(source.version, current);
        if (json) {
            printJson(QJsonObject{{"current", current}, {"repository", source.repository}, {"repositoryVersion", source.version}, {"updateAvailable", waiting}});
        } else {
            out() << "This copy of Omarchy Control came from the " << source.repository << " package repository and updates with the system.\n";
            if (waiting)
                out() << "Version " << source.version << " is waiting there. You have " << current << ".\n";
            out() << "Update it with: omarchy update\n";
            out().flush();
        }
        return action == QLatin1String("install") ? 1 : 0;
    }
    const Updater::Check check = Updater::fetchBlocking(runner);
    if (check.outcome == Updater::Check::Failed) {
        err() << check.error << "\n";
        return 1;
    }
    const QString current = Updater::currentVersion();
    const QString latest = check.release.version;
    const bool available = check.outcome == Updater::Check::Found && Updater::isNewer(latest, current);
    if (action == QLatin1String("check") && json) {
        printJson(QJsonObject{{"current", current}, {"latest", latest}, {"updateAvailable", available}, {"url", check.release.pageUrl}, {"notes", check.release.notes}});
        return 0;
    }
    if (check.outcome == Updater::Check::NoReleases) {
        out() << "No releases have been published yet.\n";
        out().flush();
        return 0;
    }
    if (!available) {
        out() << "Omarchy Control " << current << " is up to date.\n";
        out().flush();
        return 0;
    }
    if (action == QLatin1String("check")) {
        out() << "Version " << latest << " is available. You have " << current << ".\nRun: omarchy-control update install\n";
        out().flush();
        return 0;
    }
    QString error;
    const bool installed = Updater::install(runner, check.release, yes, &error, [](const QString &line) {
        out() << line << "\n";
        out().flush();
    });
    if (!installed) {
        err() << error << "\n";
        return 1;
    }
    out() << "Updated to " << latest << ". Restart Omarchy Control to use it.\n";
    out().flush();
    return 0;
}

int cmdCatalog(AppCore &core, bool json) {
    QJsonArray rows;
    for (const auto &entry : core.catalog.entries()) {
        const auto availability = core.capabilities.availability(entry);
        QJsonObject row;
        row["id"] = entry.id;
        row["page"] = entry.page;
        row["section"] = entry.section;
        row["label"] = entry.label;
        row["type"] = entry.type;
        row["integration"] = entry.integration;
        row["available"] = availability.available;
        if (!availability.available)
            row["reason"] = availability.reason;
        rows.append(row);
    }
    if (json) {
        printJson(rows);
    } else {
        for (const auto &value : rows) {
            const auto row = value.toObject();
            out() << row.value("id").toString().leftJustified(42) << row.value("type").toString().leftJustified(8) << row.value("label").toString() << "\n";
        }
    }
    return 0;
}

int cmdSearch(AppCore &core, const QString &query, bool json) {
    QJsonArray rows;
    for (const auto &hit : core.catalog.search(query, 15)) {
        QJsonObject row;
        row["id"] = hit.entry->id;
        row["label"] = hit.entry->label;
        row["page"] = hit.entry->page;
        row["score"] = hit.score;
        rows.append(row);
    }
    if (json)
        printJson(rows);
    else
        for (const auto &value : rows)
            out() << value.toObject().value("id").toString().leftJustified(42) << value.toObject().value("label").toString() << "\n";
    return 0;
}

int cmdGet(AppCore &core, const QString &id, bool json) {
    const CatalogEntry *entry = core.catalog.find(id);
    if (!entry || !entry->isSetting()) {
        err() << "Unknown setting: " << id << "\n";
        return 1;
    }
    const ReadResult result = readBlocking(core, id);
    const QVariantList choices = choicesBlocking(core, id);
    QJsonObject row;
    row["id"] = id;
    row["status"] = result.status;
    row["value"] = result.value;
    row["display"] = core.engine.displayValue(*entry, result.value, choices);
    if (!result.source.isEmpty())
        row["source"] = result.source;
    if (!result.reason.isEmpty())
        row["reason"] = result.reason;
    if (!result.extra.isEmpty())
        row["details"] = QJsonObject::fromVariantMap(result.extra);
    if (json)
        printJson(row);
    else if (result.status == QLatin1String("ok"))
        out() << row.value("display").toString() << "\n";
    else
        err() << result.status << ": " << result.reason << "\n";
    return result.status == QLatin1String("ok") || result.status == QLatin1String("custom") ? 0 : 1;
}

int cmdPlan(AppCore &core, const QString &id, const QString &value, bool json) {
    const ChangePlan plan = core.engine.plan(id, value, choicesBlocking(core, id));
    if (json) {
        printJson(plan.toJson());
    } else if (plan.ok) {
        out() << "OK: " << id << " = " << plan.normalized << "\n";
        for (const auto &effect : plan.effects)
            out() << "  " << effect << "\n";
        if (!plan.confirmText.isEmpty())
            out() << "  Needs --yes: " << plan.confirmText << "\n";
    }
    if (!plan.ok && !json)
        err() << plan.error << "\n";
    return plan.ok ? 0 : 1;
}

int report(const Transaction &tx, bool json) {
    const bool ok = tx.phase == QLatin1String("committed");
    if (json)
        printJson(txJson(tx));
    else if (ok)
        out() << tx.label << ": " << tx.before << " -> " << tx.after << " (verified, transaction " << tx.id << ")\n";
    if (!ok && !json)
        err() << tx.phase << ": " << tx.message << "\n";
    return ok ? 0 : 1;
}

int cmdApply(AppCore &core, const QString &id, const QString &value, bool yes, bool json) {
    const ChangePlan plan = core.engine.plan(id, value, choicesBlocking(core, id));
    if (!plan.ok) {
        err() << plan.error << "\n";
        return 1;
    }
    if (!plan.confirmText.isEmpty() && !yes) {
        err() << plan.confirmText << "\nRe-run with --yes to go ahead.\n";
        return 1;
    }
    core.capabilities.scanServicesBlocking();
    const Transaction tx = waitFor([&](auto done) { core.engine.apply(id, value, done); });
    return report(tx, json);
}

int cmdHistory(AppCore &core, bool json) {
    QJsonArray rows;
    for (const auto &tx : core.coordinator.history())
        rows.append(tx.toJson());
    if (json) {
        printJson(rows);
        return 0;
    }
    for (const auto &value : rows) {
        const auto row = value.toObject();
        out() << row.value("id").toString() << "  " << row.value("at").toString() << "  " << row.value("phase").toString().leftJustified(12)
              << row.value("label").toString() << ": " << row.value("before").toString() << " -> " << row.value("after").toString() << "\n";
    }
    return 0;
}

int cmdDoctor(AppCore &core, bool json) {
    core.capabilities.scanServicesBlocking();
    const QString version = core.capabilities.versions().value("omarchy").toString();
    QJsonObject report;
    report["omarchy"] = version;
    report["hyprland"] = core.capabilities.versions().value("hyprland").toString();
    report["compatible"] = version.startsWith("4.5");
    report["changedCommands"] = QJsonArray::fromStringList(core.compat.changedCommands(core.capabilities.versions().value("hyprland").toString()));
    report["capabilities"] = QJsonObject::fromVariantMap(core.capabilities.values());
    const auto moduleState = HyprModule(core.env).state();
    report["ownedLuaModule"] = moduleState == HyprModule::Absent ? "absent" : moduleState == HyprModule::Ours ? "ours" : "modified-by-user";
    report["ownedLuaImport"] = HyprModule(core.env).importInstalled();

    QJsonArray reads;
    int ok = 0, unavailable = 0, failed = 0;
    for (const auto &entry : core.catalog.entries()) {
        if (!entry.isSetting())
            continue;
        const ReadResult result = readBlocking(core, entry.id);
        QJsonObject row;
        row["id"] = entry.id;
        row["status"] = result.status;
        if (!result.reason.isEmpty())
            row["reason"] = result.reason;
        reads.append(row);
        if (result.status == QLatin1String("ok") || result.status == QLatin1String("custom"))
            ++ok;
        else if (result.status == QLatin1String("unavailable"))
            ++unavailable;
        else
            ++failed;
    }
    report["reads"] = reads;
    report["readSummary"] = QJsonObject{{"ok", ok}, {"unavailable", unavailable}, {"failed", failed}};

    // Resolves every route to an executable. Nothing here launches anything.
    QJsonArray missing;
    const bool hasUwsm = core.capabilities.hasCommand("uwsm-app");
    int routes = 0;
    for (const auto &entry : core.catalog.entries()) {
        if (!entry.isAction() || entry.launch.value("kind").toString() == QLatin1String("page"))
            continue;
        ++routes;
        const QStringList argv = HelperLauncher::resolve(entry.launch, core.env, hasUwsm);
        if (argv.isEmpty() || !core.capabilities.hasCommand(argv.first()))
            missing.append(QJsonObject{{"id", entry.id}, {"command", argv.value(0)}});
    }
    report["routes"] = QJsonObject{{"checked", routes}, {"unresolved", missing}};

    QByteArray check;
    FileOps::readFile(core.env.appStateDir + "/display-recovery-check.json", &check);
    report["displayRecoveryCheck"] = QJsonDocument::fromJson(check).object();

    int interrupted = 0;
    for (const auto &tx : core.coordinator.history())
        if (!tx.finished())
            ++interrupted;
    report["unfinishedTransactions"] = interrupted;

    if (json) {
        printJson(report);
    } else {
        out() << "Omarchy " << version << (version.startsWith("4.5") ? " (tested baseline)\n" : " (newer than the tested baseline)\n");
        out() << "Hyprland " << report["hyprland"].toString() << ", shell " << (core.capabilities.has("shell") ? "responding" : "not responding") << "\n";
        out() << "Settings read: " << ok << " ok, " << unavailable << " not applicable here, " << failed << " failed\n";
        for (const auto &value : reads)
            if (value.toObject().value("status").toString() == QLatin1String("unknown"))
                out() << "  " << value.toObject().value("id").toString() << ": " << value.toObject().value("reason").toString() << "\n";
        out() << "Commands changed since testing: " << report["changedCommands"].toArray().size() << "\n";
        out() << "Routes resolved: " << routes - missing.size() << "/" << routes << " (none launched)\n";
        out() << "Owned Lua module: " << report["ownedLuaModule"].toString() << ", import " << (report["ownedLuaImport"].toBool() ? "present" : "not added") << "\n";
        out() << "Display recovery check: " << (report["displayRecoveryCheck"].toObject().value("passed").toBool() ? "passed" : "not passed; display editing locked") << "\n";
    }
    return failed == 0 && version.startsWith("4.5") ? 0 : 1;
}

int cmdCoverage(AppCore &core, QStringList args, bool json) {
    const bool live = popFlag(args, "--live");
    QString inventoryPath = popOption(args, "--inventory");
    Coverage::Result result;
    if (live) {
        const auto command = core.runner.runBlocking({"omarchy", "commands", "--json", "--all"}, CommandOptions{30000});
        if (!command.ok()) {
            err() << command.describe("omarchy") << "\n";
            return 1;
        }
        result = Coverage::reconcileLive(core.catalog, QJsonDocument::fromJson(command.out).object().value("commands").toArray());
    } else if (!inventoryPath.isEmpty()) {
        QByteArray contents;
        if (!FileOps::readFile(inventoryPath, &contents)) {
            err() << "Cannot read " << inventoryPath << "\n";
            return 1;
        }
        result = Coverage::reconcile(core.catalog, QJsonDocument::fromJson(contents).object());
    }
    QJsonObject evidence;
    const QString evidencePath = popOption(args, "--evidence");
    if (!evidencePath.isEmpty()) {
        QByteArray contents;
        if (FileOps::readFile(evidencePath, &contents))
            evidence = QJsonDocument::fromJson(contents).object();
    }
    QHash<QString, int> byMode;
    for (const auto &entry : core.catalog.entries())
        byMode[entry.integration]++;
    QJsonObject modes;
    for (auto it = byMode.constBegin(); it != byMode.constEnd(); ++it)
        modes[it.key()] = it.value();
    QJsonObject report;
    report["entries"] = int(core.catalog.entries().size());
    report["modes"] = modes;
    report["claimedCommands"] = result.claimedCommands;
    report["excludedCommands"] = result.excludedCommands;
    report["claimedMenu"] = result.claimedMenu;
    report["excludedMenu"] = result.excludedMenu;
    report["unclassifiedCommands"] = QJsonArray::fromStringList(result.unclassifiedCommands);
    report["unclassifiedMenu"] = QJsonArray::fromStringList(result.unclassifiedMenu);
    report["exclusions"] = core.catalog.exclusions();
    report["ledger"] = Coverage::ledger(core.catalog, evidence);
    if (json) {
        printJson(report);
    } else {
        out() << "Catalog entries: " << report["entries"].toInt() << "\n";
        for (auto it = modes.constBegin(); it != modes.constEnd(); ++it)
            out() << "  " << it.key().leftJustified(18) << it.value().toInt() << "\n";
        out() << "Commands: " << result.claimedCommands << " routed, " << result.excludedCommands << " excluded with a reason, " << result.unclassifiedCommands.size() << " unclassified\n";
        out() << "Menu rows: " << result.claimedMenu << " routed, " << result.excludedMenu << " excluded with a reason, " << result.unclassifiedMenu.size() << " unclassified\n";
        for (const auto &route : result.unclassifiedCommands)
            out() << "  unclassified: " << route << "\n";
        for (const auto &id : result.unclassifiedMenu)
            out() << "  unclassified: " << id << "\n";
    }
    return result.unclassifiedCommands.isEmpty() && result.unclassifiedMenu.isEmpty() ? 0 : 1;
}

int cmdIntegrateMenu(AppCore &core, bool remove) {
    const QString path = core.env.configHome + "/omarchy/extensions/omarchy-menu.jsonc";
    QByteArray contents;
    if (!FileOps::readFile(path, &contents))
        FileOps::readFile(core.env.omarchyPath + "/config/omarchy/extensions/omarchy-menu.jsonc", &contents);
    if (contents.trimmed().isEmpty())
        contents = "{\n}\n";
    QString error;
    const QStringList key{"omarchy-control"};
    const bool edited = remove ? FileOps::jsonRemove(&contents, key, &error)
                               : FileOps::jsonSet(&contents, key, QJsonObject{{"icon", QString::fromUtf8("\xef\x80\x93")}, {"label", "Control Center"}, {"description", "Omarchy Control"}, {"action", "omarchy-control open"}}, &error);
    if (!edited || !FileOps::atomicWrite(path, contents, &error, 0644)) {
        err() << error << "\n";
        return 1;
    }
    core.runner.runBlocking({"omarchy", "menu", "refresh"}, CommandOptions{3000});
    out() << (remove ? "Removed the Omarchy menu entry.\n" : "Added Control Center to the Omarchy menu. Your other entries are unchanged.\n");
    return 0;
}

int cmdSelftestDisplay(AppCore &core, bool json) {
    core.capabilities.scanServicesBlocking();
    if (!core.capabilities.has("hyprland")) {
        err() << "Hyprland is not reachable from here.\n";
        return 1;
    }
    DisplayProvider provider(&core.runner, &core.coordinator);
    bool passed = false;
    QString detail;
    QEventLoop loop;
    QObject::connect(&provider, &DisplayProvider::checkFinished, &loop, [&](bool ok, const QString &text) {
        passed = ok;
        detail = text;
        loop.quit();
    });
    QObject::connect(&provider, &DisplayProvider::changed, &loop, [&] {
        if (provider.status() == QLatin1String("ok") && provider.checkState() == QLatin1String("idle"))
            provider.runRecoveryCheck();
    });
    QTimer::singleShot(30000, &loop, [&] {
        detail = "The check timed out.";
        loop.quit();
    });
    provider.refresh();
    loop.exec();
    if (json)
        printJson(QJsonObject{{"passed", passed}, {"detail", detail}});
    else
        (passed ? out() : err()) << detail << "\n";
    return passed ? 0 : 1;
}

int cmdCompat(AppCore &core, const QStringList &args, bool json) {
    core.capabilities.scanServicesBlocking();
    const QString hyprland = core.capabilities.versions().value("hyprland").toString();
    if (args.value(1) == QLatin1String("accept")) {
        core.compat.acceptAll(hyprland);
        out() << "Changes resumed for every command that changed. Check the results of what you change next.\n";
        return 0;
    }
    const QStringList changed = core.compat.changedCommands(hyprland);
    if (json) {
        printJson(QJsonObject{{"changed", QJsonArray::fromStringList(changed)}, {"baseline", core.compat.baseline()}});
    } else if (changed.isEmpty()) {
        out() << "Every command behind a setting matches what was tested (" << core.compat.baseline() << ").\n";
    } else {
        out() << "Changed since testing; these settings read normally, but changes are paused:\n";
        for (const auto &name : changed)
            out() << "  " << name << "\n";
        out() << "Run `omarchy-control compat accept` after reviewing.\n";
    }
    return changed.isEmpty() ? 0 : 1;
}

// Waits for a provider's next message, which is how these report an outcome.
template <typename Provider, typename Start>
int providerResult(Provider &provider, Start start, bool json) {
    bool isError = true;
    QString text = QStringLiteral("Timed out.");
    QEventLoop loop;
    bool answered = false;
    QObject::connect(&provider, &Provider::message, &loop, [&](const QString &message, bool error) {
        text = message;
        isError = error;
        answered = true;
        loop.quit();
    });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    start();
    // A refusal can arrive before start() returns, and quitting a loop that has not
    // started would leave it waiting out the whole timeout.
    if (!answered)
        loop.exec();
    if (json)
        printJson(QJsonObject{{"ok", !isError}, {"message", text}});
    else
        (isError ? err() : out()) << text << "\n";
    return isError ? 1 : 0;
}

int cmdShortcut(AppCore &core, const QStringList &args, bool json, bool yes) {
    ShortcutsProvider provider(&core.runner, &core.coordinator);
    const QString verb = args.value(1);
    if (verb == QLatin1String("add") && args.size() >= 5) {
        // Collisions need the current binds, so read them before deciding.
        QEventLoop loop;
        QObject::connect(&provider, &ShortcutsProvider::changed, &loop, &QEventLoop::quit);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        provider.refresh();
        loop.exec();
        return providerResult(provider, [&] { provider.addBinding(args[2], args[3], args[4], yes); }, json);
    }
    if (verb == QLatin1String("remove") && args.size() >= 3)
        return providerResult(provider, [&] { provider.removeBinding(args[2]); }, json);
    err() << "Usage: omarchy-control shortcut add <keys> <description> <command> [--yes] | remove <keys>\n";
    return 1;
}

int cmdRule(AppCore &core, const QStringList &args, bool json) {
    WindowRulesProvider provider(&core.runner, &core.coordinator);
    const QString verb = args.value(1);
    if (verb == QLatin1String("add") && args.size() >= 4)
        return providerResult(provider, [&] { provider.addRule(args[2], args[3], args.value(4)); }, json);
    if (verb == QLatin1String("remove") && args.size() >= 3)
        return providerResult(provider, [&] { provider.removeRule(args[2].toInt()); }, json);
    if (verb == QLatin1String("list")) {
        printJson(QJsonArray::fromVariantList(provider.rules()));
        return 0;
    }
    err() << "Usage: omarchy-control rule add <class> <float|tile|workspace|opacity|noblur> [value] | remove <index> | list\n";
    return 1;
}

// A headless preview, for scripts and for proving recovery. Without --keep or --revert
// it does nothing further, which is the point: the watchdog restores on its own.
int cmdDisplayPreview(AppCore &core, const QStringList &args, bool json) {
    if (args.value(1) != QLatin1String("preview") || args.size() < 5) {
        err() << "Usage: omarchy-control display preview <output> <x> <y> [--keep|--revert]\n";
        return 1;
    }
    core.capabilities.scanServicesBlocking();
    DisplayProvider provider(&core.runner, &core.coordinator);
    QEventLoop ready;
    QObject::connect(&provider, &DisplayProvider::changed, &ready, [&] {
        if (provider.status() != QLatin1String("loading") && provider.editingUnlocked())
            ready.quit();
    });
    QTimer::singleShot(4000, &ready, &QEventLoop::quit);
    provider.refresh();
    ready.exec();
    if (!provider.editingUnlocked()) {
        err() << "Run `omarchy-control selftest display` first.\n";
        return 1;
    }
    QVariantList layout = provider.monitors();
    bool found = false;
    for (auto &row : layout) {
        auto map = row.toMap();
        if (map.value("output").toString() == args[2]) {
            map["x"] = args[3].toInt();
            map["y"] = args[4].toInt();
            row = map;
            found = true;
        }
    }
    const QString problem = found ? provider.validateLayout(layout) : QStringLiteral("No such display.");
    if (!problem.isEmpty()) {
        err() << problem << "\n";
        return 1;
    }
    const bool keep = args.contains("--keep"), revert = args.contains("--revert");
    bool isError = false;
    QString text;
    QEventLoop loop;
    QObject::connect(&provider, &DisplayProvider::previewChanged, &loop, [&] {
        if (provider.previewing() && keep)
            provider.keep();
        else if (provider.previewing() && revert)
            provider.revert();
    });
    bool answered = false;
    QObject::connect(&provider, &DisplayProvider::message, &loop, [&](const QString &message, bool error) {
        text = message;
        isError = error;
        answered = true;
        loop.quit();
    });
    QTimer::singleShot(40000, &loop, &QEventLoop::quit);
    provider.preview(layout);
    if (!answered)
        loop.exec();
    if (json)
        printJson(QJsonObject{{"ok", !isError}, {"message", text}});
    else
        (isError ? err() : out()) << text << "\n";
    return isError ? 1 : 0;
}

int cmdPlugin(AppCore &core, const QStringList &args, bool json) {
    PluginsProvider provider(&core.runner, &core.coordinator);
    const QString verb = args.value(1);
    if ((verb == QLatin1String("enable") || verb == QLatin1String("disable")) && args.size() >= 3)
        return providerResult(provider, [&] { provider.setEnabled(args[2], verb == QLatin1String("enable")); }, json);
    err() << "Usage: omarchy-control plugin enable|disable <id>\n";
    return 1;
}

int cmdWidget(AppCore &core, const QStringList &args, bool json) {
    if (args.value(1) != QLatin1String("move") || args.size() < 7) {
        err() << "Usage: omarchy-control widget move <id> <section> <index> <from-section> <from-index>\n";
        return 1;
    }
    PluginsProvider provider(&core.runner, &core.coordinator);
    return providerResult(provider, [&] { provider.moveWidget(args[2], args[3], args[4].toInt(), args[5], args[6].toInt()); }, json);
}

// Opens a catalog route. Reports only that it opened; what happens next is the
// helper's business, and a consequential route needs --yes first.
int cmdLaunch(AppCore &core, const QString &id, bool yes, bool json) {
    const CatalogEntry *entry = core.catalog.find(id);
    if (!entry || !entry->isAction()) {
        err() << "Unknown route: " << id << "\n";
        return 1;
    }
    if (!entry->confirm.isEmpty() && !yes) {
        err() << entry->confirmText << "\nRe-run with --yes to go ahead.\n";
        return 1;
    }
    HelperLauncher launcher(&core.runner, &core.capabilities);
    HelperLauncher::Outcome outcome;
    QEventLoop loop;
    bool done = false;
    launcher.launch(*entry, [&](const HelperLauncher::Outcome &o) {
        outcome = o;
        done = true;
        loop.quit();
    });
    if (!done)
        loop.exec();
    if (json)
        printJson(QJsonObject{{"opened", outcome.opened}, {"message", outcome.message}});
    else
        (outcome.opened ? out() : err()) << outcome.message << "\n";
    return outcome.opened ? 0 : 1;
}

int cmdIcons(AppCore &core, const QStringList &args, bool yes, bool json) {
    const QString action = args.value(1);
    if (action == QLatin1String("list")) {
        QStringList diagnostics;
        const QVariantList packs = core.icons.listJson(&diagnostics);
        if (json) {
            QJsonObject row;
            row["packs"] = QJsonArray::fromVariantList(packs);
            row["problems"] = QJsonArray::fromStringList(diagnostics);
            printJson(row);
            return 0;
        }
        for (const auto &pack : packs)
            out() << pack.toMap().value("id").toString() << "\t" << pack.toMap().value("name").toString() << "\n";
        for (const auto &problem : diagnostics)
            err() << problem << "\n";
        return 0;
    }
    if (action == QLatin1String("reapply")) {
        // Runs from a hook with no one watching, so a changed Omarchy pauses it just as it
        // pauses the setting in the window.
        if (const CatalogEntry *entry = core.catalog.find("appearance.icons")) {
            const QString changed = core.compat.changedFor(*entry, {});
            if (!changed.isEmpty()) {
                err() << "Not reapplying: Omarchy's " << changed << " changed since this was tested.\n";
                return 0;
            }
        }
        QString message;
        const int code = core.icons.reapply(&message, args.value(2));
        if (!message.isEmpty())
            err() << message << "\n";
        return code;
    }
    if (action == QLatin1String("remove") && args.size() >= 3) {
        const IconThemeProvider::Removal plan = core.icons.planRemoval(args[2]);
        if (!plan.ok) {
            err() << plan.reason << "\n";
            return 1;
        }
        if (!yes) {
            err() << plan.name << " (" << plan.path << ") would move to the Trash. Re-run with --yes to go ahead.\n";
            return 1;
        }
        Transaction result;
        QEventLoop loop;
        core.icons.removePack(args[2], [&](const Transaction &tx) {
            result = tx;
            loop.quit();
        });
        if (result.phase.isEmpty())
            loop.exec();
        const bool ok = result.phase == QLatin1String("committed");
        if (json)
            printJson(QJsonObject{{"ok", ok}, {"id", result.id}, {"message", result.message}});
        else
            (ok ? out() : err()) << (ok ? "Moved " + plan.name + " to the Trash. Undo with: omarchy-control undo " + result.id : result.message) << "\n";
        return ok ? 0 : 1;
    }
    if (action == QLatin1String("layer")) {
        const QString verb = args.value(2, QStringLiteral("status"));
        if (verb == QLatin1String("sync")) {
            QString message;
            const int code = core.icons.syncLayer(args.contains(QStringLiteral("--if-stale")), &message);
            if (!message.isEmpty())
                err() << message << "\n";
            return code;
        }
        if (verb != QLatin1String("status")) {
            err() << Usage;
            return 1;
        }
        const QVariantMap state = core.icons.layerState();
        if (json) {
            QJsonObject row = QJsonObject::fromVariantMap(state);
            row["folder"] = IconLayer::folder(core.env);
            row["reason"] = IconThemeProvider::layerReason(state);
            printJson(row);
            return 0;
        }
        out() << "Launcher icons: " << (state.value("enabled").toBool() ? "on" : "off") << "\n";
        if (state.value("built").toBool())
            out() << state.value("pack").toString() << " covers " << state.value("fromPack").toInt() << " of " << state.value("total").toInt()
                  << " app icons; " << state.value("own").toInt() << " use the app's own, " << state.value("missing").toInt() << " have none.\n";
        else
            out() << "No layer is built.\n";
        const QString reason = IconThemeProvider::layerReason(state);
        if (!reason.isEmpty())
            out() << reason << "\n";
        return 0;
    }
    if (action == QLatin1String("iconic")) {
        const QString verb = args.value(2, QStringLiteral("status"));
        QString message;
        int code = 0;
        if (verb == QLatin1String("retire")) {
            code = core.icons.retireIconic(&message);
        } else if (verb == QLatin1String("restore")) {
            code = core.icons.restoreIconic(&message);
        } else if (verb == QLatin1String("status")) {
            const QStringList paths = IconLayer::iconicPaths(core.env);
            if (json) {
                printJson(QJsonObject{{"present", QJsonArray::fromStringList(paths)}, {"retired", IconLayer::iconicRetired(core.env)}});
                return 0;
            }
            message = paths.isEmpty() ? QStringLiteral("Iconic's files are not here.") : "Iconic's files: " + paths.join(", ");
            if (IconLayer::iconicRetired(core.env))
                message += "\nAn earlier hand-over is kept and can be restored.";
        } else {
            err() << Usage;
            return 1;
        }
        (code == 0 ? out() : err()) << message << "\n";
        return code;
    }
    if (action == QLatin1String("remove-integration")) {
        QString message;
        const int code = core.icons.removeIntegration(&message);
        out() << message << "\n";
        return code;
    }
    if (action == QLatin1String("import") && args.size() >= 3) {
        for (const auto &message : core.coordinator.recoverInterrupted())
            err() << message << "\n";
        IconThemeImporter importer(core.env, &core.coordinator);
        for (const auto &message : importer.recoverStaging())
            err() << message << "\n";
        QEventLoop loop;
        QObject::connect(&importer, &IconThemeImporter::finished, &loop, &QEventLoop::quit);
        // An archive only knows what it holds once unpacked, so the question can come later.
        bool declined = false;
        const auto answer = [&] {
            if (importer.state() != QLatin1String("confirm"))
                return;
            if (yes) {
                importer.confirm();
                return;
            }
            err() << importer.message() << "\nRe-run with --yes to go ahead.\n";
            declined = true;
            importer.cancel();
            loop.quit();
        };
        QObject::connect(&importer, &IconThemeImporter::changed, &loop, answer, Qt::QueuedConnection);
        importer.choosePath(args[2]);
        answer();
        if (declined)
            return 1;
        if (importer.busy() || importer.state() == QLatin1String("confirm"))
            loop.exec();
        if (declined)
            return 1;
        const bool ok = importer.state() == QLatin1String("imported");
        if (json) {
            QJsonObject row;
            row["ok"] = ok;
            row["id"] = importer.themeId();
            row["name"] = importer.themeName();
            row["message"] = importer.message();
            printJson(row);
        } else if (ok) {
            out() << importer.message() << "\n";
        } else {
            err() << importer.message() << "\n";
        }
        return ok ? 0 : 1;
    }
    err() << Usage;
    return 1;
}

// Waits for a change the coordinator runs, and reports it as every other command does.
int finishChange(AppCore &core, const std::function<void(ChangeCoordinator::Finished)> &start, bool json) {
    Q_UNUSED(core);
    Transaction result;
    QEventLoop loop;
    start([&](const Transaction &tx) {
        result = tx;
        loop.quit();
    });
    if (result.phase.isEmpty())
        loop.exec();
    const bool ok = result.phase == QLatin1String("committed");
    if (json)
        printJson(QJsonObject{{"ok", ok}, {"id", result.id}, {"message", result.message}});
    else
        (ok ? out() : err()) << (ok ? "Done. Undo with: omarchy-control undo " + result.id : result.message) << "\n";
    return ok ? 0 : 1;
}

int cmdApps(AppCore &core, QStringList args, bool json) {
    if (args.value(1) != QLatin1String("launcher")) {
        err() << Usage;
        return 1;
    }
    const QString verb = args.value(2, QStringLiteral("list"));
    const QString id = args.value(3);
    if (verb == QLatin1String("list")) {
        const bool modified = args.contains(QStringLiteral("--modified")), hidden = args.contains(QStringLiteral("--hidden"));
        QJsonArray rows;
        for (const LauncherEditor::Entry &entry : core.launchers.scan()) {
            if ((modified && !entry.modified) || (hidden && entry.visible))
                continue;
            if (json) {
                rows << QJsonObject{{"id", entry.id}, {"name", entry.name}, {"icon", entry.icon}, {"kind", entry.kind}, {"visible", entry.visible},
                                    {"modified", entry.modified}, {"outdated", entry.outdated}, {"omarchyHidden", entry.omarchyHidden}, {"path", entry.path}};
                continue;
            }
            QStringList notes;
            if (entry.modified)
                notes << "changed";
            if (entry.outdated)
                notes << "out of date";
            if (!entry.visible)
                notes << "hidden";
            out() << entry.id << "\t" << entry.name << (notes.isEmpty() ? QString() : "\t(" + notes.join(", ") + ")") << "\n";
        }
        if (json)
            printJson(QJsonObject{{"launchers", rows}});
        return 0;
    }
    if (id.isEmpty()) {
        err() << Usage;
        return 1;
    }
    if (verb == QLatin1String("set")) {
        LauncherEditor::Edit edit;
        const auto option = [&args](const QString &flag) -> std::optional<QString> {
            const qsizetype at = args.indexOf(flag);
            if (at < 0 || at + 1 >= args.size())
                return std::nullopt;
            return args[at + 1];
        };
        edit.name = option(QStringLiteral("--name"));
        if (const auto icon = option(QStringLiteral("--icon"))) {
            // A file is copied in first, so the launcher keeps it if the original moves.
            if (icon->contains('/')) {
                QString error;
                const QString copy = core.launchers.importIcon(QFileInfo(*icon).absoluteFilePath(), &error);
                if (copy.isEmpty()) {
                    err() << error << "\n";
                    return 1;
                }
                edit.icon = copy;
            } else {
                edit.icon = *icon;
            }
        }
        if (args.contains(QStringLiteral("--hide")))
            edit.hidden = true;
        else if (args.contains(QStringLiteral("--show")))
            edit.hidden = false;
        if (!edit.name && !edit.icon && !edit.hidden) {
            err() << "Nothing to change. Use --name, --icon, --hide, or --show.\n";
            return 1;
        }
        return finishChange(core, [&](ChangeCoordinator::Finished done) { core.launchers.apply(id, edit, done); }, json);
    }
    if (verb == QLatin1String("reset"))
        return finishChange(core, [&](ChangeCoordinator::Finished done) { core.launchers.reset(id, done); }, json);
    if (verb == QLatin1String("update"))
        return finishChange(core, [&](ChangeCoordinator::Finished done) { core.launchers.update(id, done); }, json);
    err() << Usage;
    return 1;
}

int cmdSkillInstall(AppCore &core) {
    QByteArray skill;
    if (!FileOps::readFile(":/data/skill.md", &skill)) {
        err() << "The bundled skill is missing.\n";
        return 1;
    }
    const QString path = core.env.home + "/.claude/skills/omarchy-control/SKILL.md";
    QString error;
    if (!FileOps::atomicWrite(path, skill, &error, 0644)) {
        err() << error << "\n";
        return 1;
    }
    out() << "Installed " << path << "\n";
    return 0;
}

}  // namespace

namespace Cli {

bool isCliCommand(const QString &command) {
    static const QStringList commands{"catalog", "search", "get", "plan", "apply", "reset", "history", "undo", "doctor", "coverage", "compat", "shortcut", "rule", "display", "plugin", "widget", "launch", "integrate", "selftest", "skill", "display-watchdog", "icons", "apps", "help", "--help", "-h", "--version"};
    return commands.contains(command);
}

int run(const Env &env, const QStringList &arguments) {
    QStringList args = arguments;
    const bool json = popFlag(args, "--json");
    const bool yes = popFlag(args, "--yes");
    const QString command = args.value(0);

    if (command == QLatin1String("--version")) {
        out() << "omarchy-control " << QCoreApplication::applicationVersion() << "\n";
        return 0;
    }
    if (command.isEmpty() || command == QLatin1String("help") || command == QLatin1String("--help") || command == QLatin1String("-h")) {
        out() << Usage;
        return 0;
    }
    if (command == QLatin1String("display-watchdog"))
        return DisplayProvider::runWatchdog(env, args.value(1));
    if (command == QLatin1String("update"))
        return cmdUpdate(env, args, yes, json);

    AppCore core(env);
    if (command == QLatin1String("catalog"))
        return cmdCatalog(core, json);
    if (command == QLatin1String("search"))
        return cmdSearch(core, args.mid(1).join(' '), json);
    if (command == QLatin1String("doctor"))
        return cmdDoctor(core, json);
    if (command == QLatin1String("coverage"))
        return cmdCoverage(core, args.mid(1), json);
    if (command == QLatin1String("history"))
        return cmdHistory(core, json);
    if (command == QLatin1String("skill") && args.value(1) == QLatin1String("install"))
        return cmdSkillInstall(core);
    if (command == QLatin1String("integrate") && args.value(1) == QLatin1String("menu"))
        return cmdIntegrateMenu(core, args.contains("--remove"));
    if (command == QLatin1String("compat"))
        return cmdCompat(core, args, json);
    if (command == QLatin1String("icons"))
        return cmdIcons(core, args, yes, json);
    if (command == QLatin1String("apps"))
        return cmdApps(core, args, json);
    if (command == QLatin1String("plugin"))
        return cmdPlugin(core, args, json);
    if (command == QLatin1String("widget"))
        return cmdWidget(core, args, json);
    if (command == QLatin1String("shortcut"))
        return cmdShortcut(core, args, json, yes);
    if (command == QLatin1String("rule"))
        return cmdRule(core, args, json);
    if (command == QLatin1String("display"))
        return cmdDisplayPreview(core, args, json);
    if (command == QLatin1String("selftest") && args.value(1) == QLatin1String("display"))
        return cmdSelftestDisplay(core, json);

    core.capabilities.scanServicesBlocking();
    // Anything that changes state settles earlier interruptions first, and the window
    // is not the only way in.
    if (command == QLatin1String("apply") || command == QLatin1String("reset") || command == QLatin1String("undo"))
        for (const auto &message : core.coordinator.recoverInterrupted())
            err() << message << "\n";
    if (command == QLatin1String("launch") && args.size() >= 2)
        return cmdLaunch(core, args[1], yes, json);
    if (command == QLatin1String("get") && args.size() >= 2)
        return cmdGet(core, args[1], json);
    if (command == QLatin1String("plan") && args.size() >= 3)
        return cmdPlan(core, args[1], args[2], json);
    if (command == QLatin1String("apply") && args.size() >= 3)
        return cmdApply(core, args[1], args[2], yes, json);
    if (command == QLatin1String("reset") && args.size() >= 2) {
        const CatalogEntry *entry = core.catalog.find(args[1]);
        if (!entry) {
            err() << "Unknown setting: " << args[1] << "\n";
            return 1;
        }
        return report(waitFor([&](auto done) { core.engine.reset(args[1], done); }), json);
    }
    if (command == QLatin1String("undo") && args.size() >= 2)
        return report(waitFor([&](auto done) { core.engine.undo(args[1], done); }), json);

    err() << Usage;
    return 1;
}

}  // namespace Cli
