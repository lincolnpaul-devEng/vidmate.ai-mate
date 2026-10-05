#include "run_ledger.h"
#include <fstream>
#include <sstream>

#if defined(QT_CORE_LIB) || defined(QT_VERSION)
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QFile>
#endif

namespace agent_cpp {

std::string RunLedger::to_json() const {
#if defined(QT_CORE_LIB) || defined(QT_VERSION)
    QJsonObject root;
    root[QStringLiteral("run_id")] = QString::fromStdString(run_id);
    root[QStringLiteral("goal")] = QString::fromStdString(goal);
    root[QStringLiteral("created_at_ms")] = static_cast<qint64>(created_at_ms);
    QJsonArray arr;
    for (const auto& entry : entries) {
        QJsonObject obj;
        obj[QStringLiteral("tool_call_id")] = QString::fromStdString(entry.tool_call_id);
        obj[QStringLiteral("tool_name")] = QString::fromStdString(entry.tool_name);
        obj[QStringLiteral("arguments")] = QString::fromStdString(entry.arguments);
        obj[QStringLiteral("result")] = QString::fromStdString(entry.result);
        obj[QStringLiteral("status")] = QString::fromStdString(entry.status);
        obj[QStringLiteral("timestamp_ms")] = static_cast<qint64>(entry.timestamp_ms);
        obj[QStringLiteral("turn_index")] = static_cast<qint64>(entry.turn_index);
        arr.append(obj);
    }
    root[QStringLiteral("entries")] = arr;
    QJsonDocument doc(root);
    return doc.toJson(QJsonDocument::Indented).toStdString();
#else
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"run_id\": \"" << run_id << "\",\n";
    ss << "  \"goal\": \"" << goal << "\",\n";
    ss << "  \"created_at_ms\": " << created_at_ms << ",\n";
    ss << "  \"entries\": [\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        ss << "    {\n";
        ss << "      \"tool_call_id\": \"" << e.tool_call_id << "\",\n";
        ss << "      \"tool_name\": \"" << e.tool_name << "\",\n";
        ss << "      \"arguments\": \"" << e.arguments << "\",\n";
        ss << "      \"result\": \"" << e.result << "\",\n";
        ss << "      \"status\": \"" << e.status << "\",\n";
        ss << "      \"timestamp_ms\": " << e.timestamp_ms << ",\n";
        ss << "      \"turn_index\": " << e.turn_index << "\n";
        ss << "    }" << (i + 1 < entries.size() ? "," : "") << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
#endif
}

RunLedger RunLedger::from_json(const std::string& json_str) {
    RunLedger ledger;
#if defined(QT_CORE_LIB) || defined(QT_VERSION)
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json_str));
    if (doc.isObject()) {
        QJsonObject root = doc.object();
        if (root.contains(QStringLiteral("run_id"))) ledger.run_id = root[QStringLiteral("run_id")].toString().toStdString();
        if (root.contains(QStringLiteral("goal"))) ledger.goal = root[QStringLiteral("goal")].toString().toStdString();
        if (root.contains(QStringLiteral("created_at_ms"))) ledger.created_at_ms = root[QStringLiteral("created_at_ms")].toInteger();
        if (root.contains(QStringLiteral("entries")) && root[QStringLiteral("entries")].isArray()) {
            QJsonArray arr = root[QStringLiteral("entries")].toArray();
            for (const auto& val : arr) {
                if (val.isObject()) {
                    QJsonObject obj = val.toObject();
                    RunLedgerEntry entry;
                    if (obj.contains(QStringLiteral("tool_call_id"))) entry.tool_call_id = obj[QStringLiteral("tool_call_id")].toString().toStdString();
                    if (obj.contains(QStringLiteral("tool_name"))) entry.tool_name = obj[QStringLiteral("tool_name")].toString().toStdString();
                    if (obj.contains(QStringLiteral("arguments"))) entry.arguments = obj[QStringLiteral("arguments")].toString().toStdString();
                    if (obj.contains(QStringLiteral("result"))) entry.result = obj[QStringLiteral("result")].toString().toStdString();
                    if (obj.contains(QStringLiteral("status"))) entry.status = obj[QStringLiteral("status")].toString().toStdString();
                    if (obj.contains(QStringLiteral("timestamp_ms"))) entry.timestamp_ms = obj[QStringLiteral("timestamp_ms")].toInteger();
                    if (obj.contains(QStringLiteral("turn_index"))) entry.turn_index = static_cast<size_t>(obj[QStringLiteral("turn_index")].toInteger());
                    ledger.entries.push_back(entry);
                }
            }
        }
    }
#endif
    return ledger;
}

bool RunLedger::save_to_file(const std::string& file_path) const {
#if defined(QT_CORE_LIB) || defined(QT_VERSION)
    QFile file(QString::fromStdString(file_path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    std::string json_data = to_json();
    file.write(json_data.c_str(), static_cast<qint64>(json_data.size()));
    file.close();
    return true;
#else
    std::ofstream out(file_path);
    if (!out.is_open()) return false;
    out << to_json();
    return true;
#endif
}

std::optional<RunLedger> RunLedger::load_from_file(const std::string& file_path) {
#if defined(QT_CORE_LIB) || defined(QT_VERSION)
    QFile file(QString::fromStdString(file_path));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }
    QByteArray data = file.readAll();
    file.close();
    return from_json(data.toStdString());
#else
    std::ifstream in(file_path);
    if (!in.is_open()) return std::nullopt;
    std::stringstream buffer;
    buffer << in.rdbuf();
    return from_json(buffer.str());
#endif
}

void RunLedger::replay_into_messages(std::vector<common_chat_msg>& messages) const {
    for (const auto& entry : entries) {
        common_chat_msg tool_msg;
        tool_msg.role = "tool";
        tool_msg.content = entry.result;
        tool_msg.tool_call_id = entry.tool_call_id;
        tool_msg.tool_name = entry.tool_name;
        messages.push_back(tool_msg);
    }
}

} // namespace agent_cpp
