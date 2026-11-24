#include <teserra/assets/json.hpp>

#include <yyjson.h>

#include <limits>
#include <utility>

namespace teserra::assets {

namespace {

yyjson_val* val(void* value) noexcept {
    return static_cast<yyjson_val*>(value);
}

// The 1-based line and column (in bytes) of byte `position` of `text`.
std::pair<usize, usize> lineAndColumn(std::string_view text, usize position) noexcept {
    usize line = 1;
    usize column = 1;
    for (usize i = 0; i < position && i < text.size(); ++i) {
        if (text[i] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    return {line, column};
}

} // namespace

bool JsonValue::isNull() const noexcept {
    return yyjson_is_null(val(m_value));
}

bool JsonValue::isBool() const noexcept {
    return yyjson_is_bool(val(m_value));
}

bool JsonValue::isNumber() const noexcept {
    return yyjson_is_num(val(m_value));
}

bool JsonValue::isInteger() const noexcept {
    return yyjson_is_int(val(m_value));
}

bool JsonValue::isString() const noexcept {
    return yyjson_is_str(val(m_value));
}

bool JsonValue::isArray() const noexcept {
    return yyjson_is_arr(val(m_value));
}

bool JsonValue::isObject() const noexcept {
    return yyjson_is_obj(val(m_value));
}

std::string_view JsonValue::typeName() const noexcept {
    if (!exists()) {
        return "missing";
    }
    switch (yyjson_get_type(val(m_value))) {
        case YYJSON_TYPE_NULL:
            return "null";
        case YYJSON_TYPE_BOOL:
            return "boolean";
        case YYJSON_TYPE_NUM:
            return isInteger() ? "integer" : "number";
        case YYJSON_TYPE_STR:
            return "string";
        case YYJSON_TYPE_ARR:
            return "array";
        case YYJSON_TYPE_OBJ:
            return "object";
        default:
            return "unknown";
    }
}

std::optional<bool> JsonValue::asBool() const noexcept {
    return isBool() ? std::optional<bool>(yyjson_get_bool(val(m_value))) : std::nullopt;
}

std::optional<i64> JsonValue::asInt() const noexcept {
    yyjson_val* value = val(m_value);
    if (yyjson_is_sint(value)) {
        return yyjson_get_sint(value);
    }
    if (yyjson_is_uint(value)) {
        const u64 unsignedValue = yyjson_get_uint(value);
        if (unsignedValue <= static_cast<u64>(std::numeric_limits<i64>::max())) {
            return static_cast<i64>(unsignedValue);
        }
    }
    return std::nullopt;
}

std::optional<f64> JsonValue::asNumber() const noexcept {
    return isNumber() ? std::optional<f64>(yyjson_get_num(val(m_value))) : std::nullopt;
}

std::optional<std::string_view> JsonValue::asString() const noexcept {
    yyjson_val* value = val(m_value);
    if (!yyjson_is_str(value)) {
        return std::nullopt;
    }
    return std::string_view(yyjson_get_str(value), yyjson_get_len(value));
}

JsonValue JsonValue::operator[](std::string_view key) const noexcept {
    return JsonValue(yyjson_obj_getn(val(m_value), key.data(), key.size()));
}

usize JsonValue::size() const noexcept {
    yyjson_val* value = val(m_value);
    if (yyjson_is_arr(value)) {
        return yyjson_arr_size(value);
    }
    if (yyjson_is_obj(value)) {
        return yyjson_obj_size(value);
    }
    return 0;
}

void JsonValue::forEachElement(void* context, bool (*visitor)(void*, JsonValue)) const {
    yyjson_arr_iter iterator{};
    if (!yyjson_arr_iter_init(val(m_value), &iterator)) {
        return;
    }
    while (yyjson_val* element = yyjson_arr_iter_next(&iterator)) {
        if (!visitor(context, JsonValue(element))) {
            return;
        }
    }
}

void JsonValue::forEachMemberImpl(void* context, bool (*visitor)(void*, std::string_view, JsonValue)) const {
    yyjson_obj_iter iterator{};
    if (!yyjson_obj_iter_init(val(m_value), &iterator)) {
        return;
    }
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator)) {
        const std::string_view name(yyjson_get_str(key), yyjson_get_len(key));
        if (!visitor(context, name, JsonValue(yyjson_obj_iter_get_val(key)))) {
            return;
        }
    }
}

Result<JsonDocument> JsonDocument::parse(std::span<const std::byte> bytes) {
    return parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

Result<JsonDocument> JsonDocument::parse(std::string_view text) {
    yyjson_read_err error{};
    // Without YYJSON_READ_INSITU the input is only read; the API takes a mutable pointer for in-situ parsing.
    yyjson_doc* document =
        yyjson_read_opts(const_cast<char*>(text.data()), text.size(), YYJSON_READ_NOFLAG, nullptr, &error);
    if (document == nullptr) {
        const auto [line, column] = lineAndColumn(text, error.pos);
        return fail(ErrorCode::InvalidData, Module::Assets, "line {}, column {}: {}", line, column,
                    error.msg != nullptr ? error.msg : "invalid JSON");
    }
    return JsonDocument(document);
}

JsonDocument::JsonDocument(JsonDocument&& other) noexcept
    : m_document(std::exchange(other.m_document, nullptr)) {
}

JsonDocument& JsonDocument::operator=(JsonDocument&& other) noexcept {
    if (this != &other) {
        yyjson_doc_free(static_cast<yyjson_doc*>(m_document));
        m_document = std::exchange(other.m_document, nullptr);
    }
    return *this;
}

JsonDocument::~JsonDocument() {
    yyjson_doc_free(static_cast<yyjson_doc*>(m_document));
}

JsonValue JsonDocument::root() const noexcept {
    return JsonValue(yyjson_doc_get_root(static_cast<yyjson_doc*>(m_document)));
}

} // namespace teserra::assets
