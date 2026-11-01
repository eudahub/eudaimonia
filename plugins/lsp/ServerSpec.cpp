// SPDX-License-Identifier: GPL-3.0-only
#include "ServerSpec.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace eudaimonia::lsp {

bool ServerSpec::matches(const QString& file) const {
    const QFileInfo info(file);
    return fileNames.contains(info.fileName()) || suffixes.contains(info.suffix().toLower());
}

QString ServerSpec::languageIdOf(const QString& file) const {
    const QString suffix = QFileInfo(file).suffix().toLower();
    for (const QString& pair : languageIds)
        if (pair.section(QLatin1Char('='), 0, 0) == suffix) return pair.section(QLatin1Char('='), 1);
    return languageId;
}

QList<ServerSpec> builtInServers() {
    QList<ServerSpec> servers;
    {
        ServerSpec clangd;
        clangd.name = QStringLiteral("clangd");
        clangd.languages = QStringLiteral("C and C++");
        clangd.program = QStringLiteral("clangd");
        // compile_commands.json from the build directory; an index of the
        // whole project built in the background.
        clangd.arguments = {QStringLiteral("--compile-commands-dir=${buildDir}"), QStringLiteral("--background-index")};
        clangd.suffixes = {"c", "h", "cc", "cpp", "cxx", "c++", "hh", "hpp", "hxx", "h++", "ipp", "tpp"};
        clangd.languageId = QStringLiteral("cpp");
        clangd.languageIds = {QStringLiteral("c=c")};
        clangd.installHint = QStringLiteral("sudo apt install clangd");
        servers << clangd;
    }
    {
        ServerSpec cmake;
        cmake.name = QStringLiteral("neocmakelsp");
        cmake.languages = QStringLiteral("CMake");
        cmake.program = QStringLiteral("neocmakelsp");
        // As nvim-lspconfig starts it; older releases took "--stdio".
        cmake.arguments = {QStringLiteral("stdio")};
        cmake.suffixes = {"cmake"};
        cmake.fileNames = {QStringLiteral("CMakeLists.txt")};
        cmake.languageId = QStringLiteral("cmake");
        cmake.installHint = QStringLiteral("cargo install neocmakelsp");
        servers << cmake;
    }
    {
        ServerSpec rust;
        rust.name = QStringLiteral("rust-analyzer");
        rust.languages = QStringLiteral("Rust");
        rust.program = QStringLiteral("rust-analyzer");
        rust.suffixes = {"rs"};
        rust.languageId = QStringLiteral("rust");
        rust.installHint = QStringLiteral("rustup component add rust-analyzer");
        servers << rust;
    }
    {
        ServerSpec python;
        python.name = QStringLiteral("pylsp");
        python.languages = QStringLiteral("Python");
        python.program = QStringLiteral("pylsp");
        python.suffixes = {"py"};
        python.languageId = QStringLiteral("python");
        python.installHint = QStringLiteral("sudo apt install python3-pylsp");
        servers << python;
    }
    return servers;
}

void applyServerConfig(QList<ServerSpec>& servers, const QByteArray& json) {
    const QJsonArray entries = QJsonDocument::fromJson(json).array();
    auto strings = [](const QJsonValue& value) {
        QStringList result;
        for (const QJsonValue& item : value.toArray()) result << item.toString();
        return result;
    };
    for (const QJsonValue& value : entries) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) continue;
        auto existing = std::find_if(servers.begin(), servers.end(), [&](const ServerSpec& s) { return s.name == name; });
        if (object.contains(QStringLiteral("program")) && object.value(QStringLiteral("program")).toString().isEmpty()) {
            if (existing != servers.end()) servers.erase(existing);  // "program": "" removes it
            continue;
        }
        ServerSpec spec = existing != servers.end() ? *existing : ServerSpec{};
        spec.name = name;
        if (object.contains(QStringLiteral("languages"))) spec.languages = object.value(QStringLiteral("languages")).toString();
        if (object.contains(QStringLiteral("program"))) spec.program = object.value(QStringLiteral("program")).toString();
        if (object.contains(QStringLiteral("arguments"))) spec.arguments = strings(object.value(QStringLiteral("arguments")));
        if (object.contains(QStringLiteral("suffixes"))) spec.suffixes = strings(object.value(QStringLiteral("suffixes")));
        if (object.contains(QStringLiteral("fileNames"))) spec.fileNames = strings(object.value(QStringLiteral("fileNames")));
        if (object.contains(QStringLiteral("languageId"))) spec.languageId = object.value(QStringLiteral("languageId")).toString();
        if (object.contains(QStringLiteral("languageIds"))) spec.languageIds = strings(object.value(QStringLiteral("languageIds")));
        if (object.contains(QStringLiteral("installHint")))
            spec.installHint = object.value(QStringLiteral("installHint")).toString();
        if (existing != servers.end()) *existing = spec;
        else servers << spec;
    }
}

QList<ServerSpec> knownServers() {
    QList<ServerSpec> servers = builtInServers();
    const QString config = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                               .filePath(QStringLiteral("language-servers.json"));
    for (const QString& path : {config, qEnvironmentVariable("EUDAIMONIA_LSP_SERVERS")}) {
        if (path.isEmpty()) continue;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) applyServerConfig(servers, file.readAll());
    }
    return servers;
}

} // namespace eudaimonia::lsp
