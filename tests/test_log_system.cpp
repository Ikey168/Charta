// Test for the structured logging core - Milestone 9, #63.
//
// Verifies levels and categories, the level threshold (dropping sub-threshold
// messages cheaply), the bounded viewer ring vs cumulative totals, per-level
// counts, category listing, filtering, sinks (the console-feed mechanism), and the
// readable file sink.
//
// Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_log_system.cpp -o test_log_system

#include "core/LogSystem.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace IKore;

static int g_failures = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond);  \
            ++g_failures;                                          \
        }                                                          \
    } while (0)

static std::size_t countLines(const std::string& s) {
    std::size_t n = 0;
    for (char c : s) {
        if (c == '\n') ++n;
    }
    return n;
}

static void testLevelsCategoriesAndFormat() {
    LogSystem log;
    log.info("engine", "started");
    log.warning("render", "shader cache miss");
    log.error("audio", "device open failed");

    CHECK(log.records().size() == 3);
    CHECK(log.totalLogged() == 3);
    CHECK(log.records()[0].level == LogViewerLevel::Info);
    CHECK(log.records()[0].category == "engine");
    CHECK(log.records()[0].message == "started");
    CHECK(log.records()[0].index == 0);
    CHECK(log.records()[2].index == 2);

    CHECK(log.records()[0].format() == "[INFO] [engine] started");
    CHECK(log.records()[1].format() == "[WARN] [render] shader cache miss");

    // Level names.
    CHECK(std::string(logLevelName(LogViewerLevel::Error)) == "ERROR");
    CHECK(std::string(logLevelName(LogViewerLevel::Trace)) == "TRACE");
}

static void testMinLevelThreshold() {
    LogSystem log;
    log.setMinLevel(LogViewerLevel::Warning);
    log.trace("x", "t");
    log.debug("x", "d");
    log.info("x", "i");     // all three below Warning -> dropped
    log.warning("x", "w");
    log.error("x", "e");

    CHECK(log.records().size() == 2);
    CHECK(log.totalLogged() == 2); // dropped messages are not counted
    CHECK(log.records()[0].level == LogViewerLevel::Warning);
    CHECK(log.records()[1].level == LogViewerLevel::Error);
}

static void testRingCapacityVsTotal() {
    LogSystem log(3); // ring holds only the last 3
    for (int i = 0; i < 5; ++i) log.info("n", "m" + std::to_string(i));

    CHECK(log.records().size() == 3);
    CHECK(log.totalLogged() == 5);
    CHECK(log.records().front().message == "m2"); // oldest surviving
    CHECK(log.records().back().message == "m4");
    CHECK(log.records().back().index == 4); // indices stay monotonic

    // clear() empties the ring but not the cumulative total.
    log.clear();
    CHECK(log.records().empty());
    CHECK(log.totalLogged() == 5);
    log.info("n", "again");
    CHECK(log.records().back().index == 5); // continues past clear
}

static void testCountsAndCategories() {
    LogSystem log;
    log.info("a", "1");
    log.info("b", "2");
    log.warning("a", "3");
    log.error("c", "4");

    CHECK(log.countAtLeast(LogViewerLevel::Warning) == 2); // warning + error
    CHECK(log.countAtLeast(LogViewerLevel::Error) == 1);
    CHECK(log.countAtLeast(LogViewerLevel::Trace) == 4);   // everything

    const std::vector<std::string> cats = log.categories();
    CHECK(cats.size() == 3);
    CHECK(cats[0] == "a" && cats[1] == "b" && cats[2] == "c"); // sorted
}

static void testFilter() {
    LogSystem log;
    log.info("render", "i1");
    log.warning("render", "w1");
    log.warning("audio", "w2");
    log.error("render", "e1");

    // By level only.
    CHECK(log.filter(LogViewerLevel::Warning).size() == 3);
    // By level and category.
    const std::vector<LogRecord> renderWarnPlus = log.filter(LogViewerLevel::Warning, "render");
    CHECK(renderWarnPlus.size() == 2);
    CHECK(renderWarnPlus[0].message == "w1" && renderWarnPlus[1].message == "e1");
    // Category with a low threshold picks up everything in that category.
    CHECK(log.filter(LogViewerLevel::Trace, "render").size() == 3);
}

static void testSinksFeedConsole() {
    LogSystem log;
    std::vector<std::string> consoleLines; // stands in for the debug console (#54)
    log.addSink([&consoleLines](const LogRecord& r) { consoleLines.push_back(r.format()); });

    log.info("engine", "hello");
    log.setMinLevel(LogViewerLevel::Warning);
    log.info("engine", "dropped"); // below threshold: sink must NOT fire
    log.error("engine", "boom");

    CHECK(consoleLines.size() == 2);
    CHECK(consoleLines[0] == "[INFO] [engine] hello");
    CHECK(consoleLines[1] == "[ERROR] [engine] boom");
}

static void testFileSink() {
    const std::string path = "ikore_log_test.txt";
    {
        LogSystem log;
        CHECK(log.openFile(path));
        CHECK(log.fileOpen());
        log.info("engine", "line one");
        log.warning("render", "line two");
        log.error("audio", "line three");
        log.closeFile();
        CHECK(!log.fileOpen());
    }

    std::ifstream in(path);
    CHECK(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string content = ss.str();
    in.close();

    CHECK(countLines(content) == 3);
    CHECK(content.find("[INFO] [engine] line one") != std::string::npos);
    CHECK(content.find("[WARN] [render] line two") != std::string::npos);
    CHECK(content.find("[ERROR] [audio] line three") != std::string::npos);

    std::remove(path.c_str());
}

int main() {
    testLevelsCategoriesAndFormat();
    testMinLevelThreshold();
    testRingCapacityVsTotal();
    testCountsAndCategories();
    testFilter();
    testSinksFeedConsole();
    testFileSink();

    if (g_failures == 0) {
        std::printf("All log system tests passed.\n");
        return 0;
    }
    std::printf("%d log system test(s) failed.\n", g_failures);
    return 1;
}
