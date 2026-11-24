// The JSON view: parsing with positioned errors, typed accessors that never fail, and iteration.

#include <teserra/assets/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::assets;

TEST_CASE("JSON: values of every type", "[assets][json]") {
    const Result<JsonDocument> document = JsonDocument::parse(std::string_view(R"({
        "name": "level", "width": 64, "scale": 1.5, "big": 18446744073709551615, "negative": -3,
        "visible": true, "nothing": null, "layers": [1, 2, 3], "tile": {"id": 7}
    })"));
    REQUIRE(document.has_value());
    const JsonValue root = document->root();
    CHECK(root.isObject());
    CHECK(root.size() == 9);

    CHECK(root["name"].asString() == "level");
    CHECK(root["width"].asInt() == 64);
    CHECK(root["width"].asNumber() == 64.0);
    CHECK(root["width"].isInteger());
    CHECK(root["scale"].asNumber() == 1.5);
    CHECK_FALSE(root["scale"].asInt().has_value()); // reals are not integers
    CHECK_FALSE(root["big"].asInt().has_value());   // does not fit an i64
    CHECK(root["negative"].asInt() == -3);
    CHECK(root["visible"].asBool() == true);
    CHECK(root["nothing"].isNull());
    CHECK(root["nothing"].exists());
    CHECK(root["tile"]["id"].asInt() == 7);

    CHECK(root["name"].typeName() == "string");
    CHECK(root["width"].typeName() == "integer");
    CHECK(root["scale"].typeName() == "number");
    CHECK(root["layers"].typeName() == "array");
    CHECK(root["tile"].typeName() == "object");
    CHECK(root["visible"].typeName() == "boolean");
    CHECK(root["nothing"].typeName() == "null");
}

TEST_CASE("JSON: missing members and wrong types give nothing, not errors", "[assets][json]") {
    const JsonDocument document =
        JsonDocument::parse(std::string_view(R"({"width": "wide", "list": [1]})")).value();
    const JsonValue root = document.root();
    CHECK_FALSE(root["height"].exists());
    CHECK(root["height"].typeName() == "missing");
    CHECK_FALSE(root["width"].asInt().has_value());
    CHECK_FALSE(root["height"]["deeper"].exists()); // a member of a missing value is missing
    CHECK_FALSE(root["list"]["key"].exists());      // arrays have no members
    CHECK(root["width"].size() == 0);
    int visits = 0;
    root["width"].forEach([&](JsonValue) { ++visits; });
    root["missing"].forEachMember([&](std::string_view, JsonValue) { ++visits; });
    CHECK(visits == 0);
}

TEST_CASE("JSON: iteration in document order, with early stop", "[assets][json]") {
    const JsonDocument document =
        JsonDocument::parse(std::string_view(R"({"b": 1, "a": 2, "list": [10, 20, 30, 40]})")).value();
    std::vector<std::string> keys;
    document.root().forEachMember([&](std::string_view key, JsonValue) { keys.emplace_back(key); });
    CHECK(keys == std::vector<std::string>{"b", "a", "list"});

    std::vector<i64> values;
    document.root()["list"].forEach([&](JsonValue element) {
        values.push_back(element.asInt().value_or(0));
        return values.size() < 2; // stop after two
    });
    CHECK(values == std::vector<i64>{10, 20});
}

TEST_CASE("JSON: malformed documents fail with the line and column", "[assets][json]") {
    const Result<JsonDocument> broken =
        JsonDocument::parse(std::string_view("{\n  \"width\": 64,\n  \"height\": ]\n}"));
    REQUIRE_FALSE(broken.has_value());
    CHECK(broken.error().code == ErrorCode::InvalidData);
    CHECK(broken.error().module == Module::Assets);
    CHECK(broken.error().message.starts_with("line 3, column 13: "));

    CHECK_FALSE(JsonDocument::parse(std::string_view("")).has_value());
    CHECK_FALSE(JsonDocument::parse(std::string_view("{\"a\": 1,}")).has_value()); // no trailing commas

    // A moved document keeps its values.
    JsonDocument first = JsonDocument::parse(std::string_view("[1]")).value();
    JsonDocument second = std::move(first);
    CHECK(second.root().size() == 1);
}
