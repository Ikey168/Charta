// Test for the input remapping core - Milestone 9, #60.
//
// Verifies the headless action/binding model behind the input panel: registration
// and immediate rebinding, reverse lookup, conflict detection, reset to defaults,
// the binding string round-trip, and persistence to disk (bindings surviving
// across "sessions").
//
// Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_input_map.cpp -o test_input_map

#include "core/InputMap.h"

#include <cstdio>
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

// Arbitrary codes stand in for the engine's ImGui key / button values.
static constexpr int KEY_W = 100;
static constexpr int KEY_S = 101;
static constexpr int KEY_SPACE = 200;

static bool listHas(const std::vector<std::string>& v, const std::string& s) {
    for (const std::string& x : v) {
        if (x == s) return true;
    }
    return false;
}

static void testRegisterAndRebind() {
    InputMap map;
    map.addAction("MoveForward", {InputDevice::Keyboard, KEY_W});
    map.addAction("Jump", {InputDevice::Keyboard, KEY_SPACE});
    map.addAction("Fire", {InputDevice::Mouse, 0});

    CHECK(map.actionCount() == 3);
    CHECK(map.hasAction("Jump"));
    CHECK(!map.hasAction("Nope"));
    CHECK(map.actionName(0) == "MoveForward"); // registration order preserved

    CHECK(map.binding("MoveForward") == (InputMapBinding{InputDevice::Keyboard, KEY_W}));

    // Rebinding takes effect immediately.
    map.rebind("MoveForward", {InputDevice::Keyboard, KEY_S});
    CHECK(map.binding("MoveForward") == (InputMapBinding{InputDevice::Keyboard, KEY_S}));

    // Unbind clears it; unbound never matches a reverse lookup.
    map.unbind("Fire");
    CHECK(!map.binding("Fire").bound());
    CHECK(map.actionFor({InputDevice::Mouse, 0}).empty());

    // Duplicate registration is ignored.
    map.addAction("Jump", {InputDevice::Keyboard, 999});
    CHECK(map.actionCount() == 3);
    CHECK(map.binding("Jump") == (InputMapBinding{InputDevice::Keyboard, KEY_SPACE}));
}

static void testReverseLookup() {
    InputMap map;
    map.addAction("A", {InputDevice::Keyboard, KEY_W});
    map.addAction("B", {InputDevice::Mouse, 1});
    CHECK(map.actionFor({InputDevice::Keyboard, KEY_W}) == "A");
    CHECK(map.actionFor({InputDevice::Mouse, 1}) == "B");
    CHECK(map.actionFor({InputDevice::Gamepad, 3}).empty());
}

static void testConflictDetection() {
    InputMap map;
    map.addAction("MoveForward", {InputDevice::Keyboard, KEY_W});
    map.addAction("Accelerate", {InputDevice::Keyboard, KEY_W}); // same as MoveForward
    map.addAction("Jump", {InputDevice::Keyboard, KEY_SPACE});
    map.addAction("Crouch"); // unbound

    CHECK(map.anyConflicts());
    CHECK(map.hasConflict("MoveForward"));
    CHECK(map.hasConflict("Accelerate"));
    CHECK(!map.hasConflict("Jump"));
    CHECK(listHas(map.conflictsWith("MoveForward"), "Accelerate"));
    CHECK(map.conflictsWith("Jump").empty());

    // Two unbound actions do not conflict.
    map.addAction("Extra"); // also unbound
    CHECK(!map.hasConflict("Crouch"));
    CHECK(!map.hasConflict("Extra"));

    // Rebinding away resolves the conflict.
    map.rebind("Accelerate", {InputDevice::Keyboard, 555});
    CHECK(!map.hasConflict("MoveForward"));
    CHECK(!map.anyConflicts());
}

static void testResetToDefaults() {
    InputMap map;
    map.addAction("Jump", {InputDevice::Keyboard, KEY_SPACE});
    map.rebind("Jump", {InputDevice::Gamepad, 0});
    CHECK(map.binding("Jump") == (InputMapBinding{InputDevice::Gamepad, 0}));
    map.resetToDefaults();
    CHECK(map.binding("Jump") == (InputMapBinding{InputDevice::Keyboard, KEY_SPACE}));
}

static void testBindingStringRoundTrip() {
    const InputMapBinding key{InputDevice::Keyboard, 65};
    const InputMapBinding mouse{InputDevice::Mouse, 2};
    const InputMapBinding pad{InputDevice::Gamepad, 7};
    const InputMapBinding none{};

    CHECK(bindingFromString(toString(key)) == key);
    CHECK(bindingFromString(toString(mouse)) == mouse);
    CHECK(bindingFromString(toString(pad)) == pad);
    CHECK(toString(none) == "none");
    CHECK(!bindingFromString("none").bound());
    CHECK(!bindingFromString("garbage").bound()); // malformed -> unbound
}

static void testSerializeRoundTrip() {
    InputMap a;
    a.addAction("MoveForward", {InputDevice::Keyboard, KEY_W});
    a.addAction("Fire", {InputDevice::Mouse, 0});
    a.addAction("Menu", {InputDevice::Gamepad, 4});
    a.rebind("Fire", {InputDevice::Mouse, 1});

    const std::string text = a.serialize();

    // A fresh map with the same actions loads the saved bindings over its defaults.
    InputMap b;
    b.addAction("MoveForward");
    b.addAction("Fire");
    b.addAction("Menu");
    b.load(text);
    CHECK(b.binding("MoveForward") == (InputMapBinding{InputDevice::Keyboard, KEY_W}));
    CHECK(b.binding("Fire") == (InputMapBinding{InputDevice::Mouse, 1}));
    CHECK(b.binding("Menu") == (InputMapBinding{InputDevice::Gamepad, 4}));

    // Unknown actions in the text are ignored.
    InputMap c;
    c.addAction("MoveForward");
    c.load("MoveForward=keyboard:5\nUnknownAction=mouse:3\n");
    CHECK(c.binding("MoveForward") == (InputMapBinding{InputDevice::Keyboard, 5}));
    CHECK(!c.hasAction("UnknownAction"));
}

static void testPersistAcrossSessions() {
    const std::string path = "ikore_input_test.ini";

    InputMap first;
    first.addAction("Jump", {InputDevice::Keyboard, KEY_SPACE});
    first.addAction("Fire", {InputDevice::Mouse, 0});
    first.rebind("Jump", {InputDevice::Gamepad, 1});
    CHECK(first.saveToFile(path));

    // A new "session" registers the same actions and loads the saved bindings.
    InputMap second;
    second.addAction("Jump", {InputDevice::Keyboard, KEY_SPACE});
    second.addAction("Fire", {InputDevice::Mouse, 0});
    CHECK(second.loadFromFile(path));
    CHECK(second.binding("Jump") == (InputMapBinding{InputDevice::Gamepad, 1}));
    CHECK(second.binding("Fire") == (InputMapBinding{InputDevice::Mouse, 0}));

    CHECK(!second.loadFromFile("no_such_input_file_54321.ini"));

    std::remove(path.c_str());
}

int main() {
    testRegisterAndRebind();
    testReverseLookup();
    testConflictDetection();
    testResetToDefaults();
    testBindingStringRoundTrip();
    testSerializeRoundTrip();
    testPersistAcrossSessions();

    if (g_failures == 0) {
        std::printf("All input map tests passed.\n");
        return 0;
    }
    std::printf("%d input map test(s) failed.\n", g_failures);
    return 1;
}
