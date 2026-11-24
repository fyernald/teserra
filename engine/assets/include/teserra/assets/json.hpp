#pragma once

// A read-only view of JSON documents (ADR-0025): what loaders of JSON source formats (Tiled maps, sidecars,
// sprite sheet files) read. The parser stays private to teserra_assets.
//
//   TESERRA_TRY_ASSIGN(const JsonDocument document, JsonDocument::parse(bytes));
//   const JsonValue root = document.root();
//   const std::optional<i64> width = root["width"].asInt();   // nullopt if missing or not an integer
//   root["layers"].forEach([&](JsonValue layer) { ... });

#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace teserra::assets {

/// One value of a JsonDocument, or a missing one (a member that is not there). Valid while its document
/// lives. Accessors never fail: a missing value or one of another type gives std::nullopt, false or an
/// empty result, so loaders report problems in their own words.
class JsonValue {
public:
    /// A missing value.
    JsonValue() noexcept = default;

    /// False for a missing value; true for every value in the document, null included.
    [[nodiscard]] bool exists() const noexcept { return m_value != nullptr; }
    [[nodiscard]] bool isNull() const noexcept;
    [[nodiscard]] bool isBool() const noexcept;
    /// An integer or a real number.
    [[nodiscard]] bool isNumber() const noexcept;
    /// A number written without a fraction or exponent.
    [[nodiscard]] bool isInteger() const noexcept;
    [[nodiscard]] bool isString() const noexcept;
    [[nodiscard]] bool isArray() const noexcept;
    [[nodiscard]] bool isObject() const noexcept;

    /// "missing", "null", "boolean", "integer", "number", "string", "array" or "object", for messages.
    [[nodiscard]] std::string_view typeName() const noexcept;

    [[nodiscard]] std::optional<bool> asBool() const noexcept;
    /// Integers that fit an i64; nullopt for reals, even 2.0.
    [[nodiscard]] std::optional<i64> asInt() const noexcept;
    /// Any number, integers included.
    [[nodiscard]] std::optional<f64> asNumber() const noexcept;
    /// The string's bytes (UTF-8), valid while the document lives.
    [[nodiscard]] std::optional<std::string_view> asString() const noexcept;

    /// The member `key` of an object; a missing value if there is none or this is not an object.
    [[nodiscard]] JsonValue operator[](std::string_view key) const noexcept;

    /// The number of elements of an array or members of an object; 0 otherwise.
    [[nodiscard]] usize size() const noexcept;

    /// Calls f(JsonValue element) for each element of an array, in order; nothing for other values. If f
    /// returns bool, false stops the iteration.
    template <typename F>
    void forEach(F&& f) const {
        forEachElement(
            &f, [](void* context, JsonValue element) { return visit(*static_cast<F*>(context), element); });
    }

    /// Calls f(std::string_view key, JsonValue value) for each member of an object, in document order;
    /// nothing for other values. If f returns bool, false stops the iteration.
    template <typename F>
    void forEachMember(F&& f) const {
        forEachMemberImpl(&f, [](void* context, std::string_view key, JsonValue value) {
            return visit(*static_cast<F*>(context), key, value);
        });
    }

private:
    friend class JsonDocument;

    explicit JsonValue(void* value) noexcept
        : m_value(value) {}

    template <typename F, typename... Args>
    static bool visit(F& f, Args... args) {
        if constexpr (std::is_same_v<std::invoke_result_t<F&, Args...>, bool>) {
            return f(args...);
        } else {
            f(args...);
            return true;
        }
    }

    void forEachElement(void* context, bool (*visitor)(void*, JsonValue)) const;
    void forEachMemberImpl(void* context, bool (*visitor)(void*, std::string_view, JsonValue)) const;

    void* m_value = nullptr;
};

/// A parsed JSON document, which owns the memory its JsonValues view. Move-only.
class JsonDocument {
public:
    /// Parses `bytes` (UTF-8 JSON). Errors: InvalidData with the line, the column and what is wrong, e.g.
    /// "line 3, column 14: unexpected character".
    [[nodiscard]] static Result<JsonDocument> parse(std::span<const std::byte> bytes);
    /// Like parse(bytes), for text.
    [[nodiscard]] static Result<JsonDocument> parse(std::string_view text);

    JsonDocument(JsonDocument&& other) noexcept;
    JsonDocument& operator=(JsonDocument&& other) noexcept;
    JsonDocument(const JsonDocument&) = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;
    ~JsonDocument();

    /// The top-level value.
    [[nodiscard]] JsonValue root() const noexcept;

private:
    explicit JsonDocument(void* document) noexcept
        : m_document(document) {}

    void* m_document = nullptr;
};

} // namespace teserra::assets
