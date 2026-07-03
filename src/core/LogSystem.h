#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <vector>

/**
 * @file LogSystem.h
 * @brief Structured logging with levels, categories, sinks, and a viewer ring (#63).
 *
 * Milestone 9 (In-Engine Editor & Tooling). The headless core behind the in-engine
 * log viewer: log messages with a level (Trace..Error) and a category, keep a
 * bounded in-memory ring for the viewer's real-time tail, optionally mirror to a
 * readable file sink, and fan out to registered sinks (e.g. the debug console, #54).
 * Sub-threshold messages are dropped on a single branch, and each accepted message
 * is an O(1) ring push plus a buffered file write, so real-time logging does not
 * impact the frame rate.
 *
 * Records carry a monotonic index instead of a wall-clock timestamp, so the core is
 * deterministic and unit-testable without a clock. Header-only, std only.
 */
namespace IKore {

enum class LogViewerLevel { Trace, Debug, Info, Warning, Error };

inline const char* logLevelName(LogViewerLevel level) {
    switch (level) {
        case LogViewerLevel::Trace: return "TRACE";
        case LogViewerLevel::Debug: return "DEBUG";
        case LogViewerLevel::Info: return "INFO";
        case LogViewerLevel::Warning: return "WARN";
        case LogViewerLevel::Error: return "ERROR";
    }
    return "?";
}

struct LogRecord {
    std::uint64_t index{0};
    LogViewerLevel level{LogViewerLevel::Info};
    std::string category;
    std::string message;

    /// Readable one-line form: "[LEVEL] [category] message" (category omitted if empty).
    std::string format() const {
        std::string out = "[";
        out += logLevelName(level);
        out += "]";
        if (!category.empty()) {
            out += " [";
            out += category;
            out += "]";
        }
        out += " ";
        out += message;
        return out;
    }
};

class LogSystem {
public:
    using Sink = std::function<void(const LogRecord&)>;

    explicit LogSystem(std::size_t capacity = 1000)
        : m_capacity(capacity == 0 ? 1 : capacity) {}

    void setMinLevel(LogViewerLevel level) { m_minLevel = level; }
    LogViewerLevel minLevel() const { return m_minLevel; }

    /// Log a message. Dropped on one branch if below the minimum level.
    void log(LogViewerLevel level, const std::string& category, const std::string& message) {
        if (static_cast<int>(level) < static_cast<int>(m_minLevel)) return;

        LogRecord record{m_nextIndex++, level, category, message};
        m_records.push_back(record);
        while (m_records.size() > m_capacity) m_records.pop_front();
        if (!category.empty()) m_categories.insert(category);
        ++m_levelCounts[static_cast<int>(level)];

        if (m_file.is_open()) {
            m_file << record.format() << "\n";
            if (level == LogViewerLevel::Error) m_file.flush(); // errors persist immediately
        }
        for (const Sink& sink : m_sinks) {
            if (sink) sink(record);
        }
    }

    void trace(const std::string& category, const std::string& message) {
        log(LogViewerLevel::Trace, category, message);
    }
    void debug(const std::string& category, const std::string& message) {
        log(LogViewerLevel::Debug, category, message);
    }
    void info(const std::string& category, const std::string& message) {
        log(LogViewerLevel::Info, category, message);
    }
    void warning(const std::string& category, const std::string& message) {
        log(LogViewerLevel::Warning, category, message);
    }
    void error(const std::string& category, const std::string& message) {
        log(LogViewerLevel::Error, category, message);
    }

    /// Bounded ring of recent records, oldest first (the viewer's tail).
    const std::deque<LogRecord>& records() const { return m_records; }
    /// Cumulative accepted records this session (monotonic; survives clear()).
    std::uint64_t totalLogged() const { return m_nextIndex; }

    /// Cumulative count of records at or above @p level (a cheap summary stat).
    std::size_t countAtLeast(LogViewerLevel level) const {
        std::size_t total = 0;
        for (int i = static_cast<int>(level); i <= static_cast<int>(LogViewerLevel::Error); ++i) {
            total += m_levelCounts[i];
        }
        return total;
    }

    /// Distinct categories seen so far, sorted.
    std::vector<std::string> categories() const {
        return std::vector<std::string>(m_categories.begin(), m_categories.end());
    }

    /// Snapshot of ring records at or above @p minLevel, optionally one category.
    std::vector<LogRecord> filter(LogViewerLevel minLevel, const std::string& category = "") const {
        std::vector<LogRecord> out;
        for (const LogRecord& r : m_records) {
            if (static_cast<int>(r.level) < static_cast<int>(minLevel)) continue;
            if (!category.empty() && r.category != category) continue;
            out.push_back(r);
        }
        return out;
    }

    /// Empty the viewer ring and category set (totals and file are unaffected).
    void clear() {
        m_records.clear();
        m_categories.clear();
    }

    /// Register a sink called for each accepted record; returns its id.
    int addSink(Sink sink) {
        m_sinks.push_back(std::move(sink));
        return static_cast<int>(m_sinks.size()) - 1;
    }

    /// Open a file sink (truncating). Returns false if it cannot be opened.
    bool openFile(const std::string& path) {
        m_file.close();
        m_file.clear();
        m_file.open(path, std::ios::trunc);
        return m_file.is_open();
    }
    void closeFile() {
        if (m_file.is_open()) {
            m_file.flush();
            m_file.close();
        }
    }
    bool fileOpen() const { return m_file.is_open(); }

private:
    std::deque<LogRecord> m_records;
    std::set<std::string> m_categories;
    std::vector<Sink> m_sinks;
    std::ofstream m_file;
    std::size_t m_capacity;
    std::uint64_t m_nextIndex{0};
    LogViewerLevel m_minLevel{LogViewerLevel::Trace};
    std::size_t m_levelCounts[5]{0, 0, 0, 0, 0};
};

} // namespace IKore
