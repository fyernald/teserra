#include <teserra/debug_ui/log_console.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace teserra::debugui {

namespace {

[[nodiscard]] bool containsIgnoringCase(std::string_view text, std::string_view part) noexcept {
    if (part.empty()) {
        return true;
    }
    const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return std::ranges::search(text, part, {}, lower, lower).begin() != text.end();
}

[[nodiscard]] const char* levelName(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:
            return "trace";
        case LogLevel::Debug:
            return "debug";
        case LogLevel::Info:
            return "info";
        case LogLevel::Warn:
            return "warn";
        case LogLevel::Error:
            return "error";
        case LogLevel::Critical:
            return "critical";
        case LogLevel::Off:
            break;
    }
    return "off";
}

[[nodiscard]] ImVec4 levelColor(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:
            return {0.55F, 0.55F, 0.55F, 1.0F};
        case LogLevel::Debug:
            return {0.55F, 0.75F, 0.95F, 1.0F};
        case LogLevel::Info:
            return {0.9F, 0.9F, 0.9F, 1.0F};
        case LogLevel::Warn:
            return {1.0F, 0.85F, 0.3F, 1.0F};
        case LogLevel::Error:
            return {1.0F, 0.4F, 0.35F, 1.0F};
        case LogLevel::Critical:
        case LogLevel::Off:
            break;
    }
    return {1.0F, 0.35F, 0.9F, 1.0F};
}

constexpr std::array kLevels{LogLevel::Trace, LogLevel::Debug, LogLevel::Info,
                             LogLevel::Warn,  LogLevel::Error, LogLevel::Critical};

} // namespace

LogBuffer::LogBuffer(usize capacity)
    : m_ring(std::max(capacity, usize{1})) {
}

void LogBuffer::push(const log::LogRecord& record) {
    const f64 seconds = std::chrono::duration<f64>(std::chrono::steady_clock::now() - m_start).count();
    const std::lock_guard lock(m_mutex);
    LogEntry& entry = m_ring[m_next % m_ring.size()];
    entry.sequence = m_next;
    entry.level = record.level;
    entry.category = record.category;
    entry.message.assign(record.message);
    entry.seconds = seconds;
    ++m_next;
}

u64 LogBuffer::copyFrom(u64 first, std::deque<LogEntry>& out) const {
    const std::lock_guard lock(m_mutex);
    const u64 kept = std::min<u64>(m_next, m_ring.size());
    for (u64 sequence = std::max(first, m_next - kept); sequence < m_next; ++sequence) {
        out.push_back(m_ring[sequence % m_ring.size()]);
    }
    return m_next;
}

bool LogFilter::passes(const LogEntry& entry) const {
    if (entry.level < minimumLevel) {
        return false;
    }
    const auto category = static_cast<usize>(entry.category);
    if (category < hiddenCategories.size() && hiddenCategories.test(category)) {
        return false;
    }
    return containsIgnoringCase(entry.message, text) ||
           containsIgnoringCase(log::categoryName(entry.category), text);
}

LogConsole::LogConsole(usize capacity)
    : m_buffer(capacity), m_sink([this](const log::LogRecord& record) { m_buffer.push(record); }) {
}

void LogConsole::update() {
    m_nextSequence = m_buffer.copyFrom(m_nextSequence, m_entries);
    while (m_entries.size() > m_buffer.capacity()) {
        m_entries.pop_front();
    }
}

void LogConsole::draw() {
    const usize before = m_entries.empty() ? 0 : m_entries.back().sequence + 1;
    update();
    const bool received = !m_entries.empty() && m_entries.back().sequence + 1 != before;

    // Filters: level, categories, text; then Clear.
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0F);
    if (ImGui::BeginCombo("Level", levelName(m_filter.minimumLevel))) {
        for (const LogLevel level : kLevels) {
            if (ImGui::Selectable(levelName(level), level == m_filter.minimumLevel)) {
                m_filter.minimumLevel = level;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
    if (ImGui::BeginCombo("Categories", m_filter.hiddenCategories.none() ? "all" : "some")) {
        for (usize value = 0; value < log::kMaxCategories; ++value) {
            const std::string_view name = log::categoryName(static_cast<LogCategory>(value));
            if (name == "Unknown") {
                continue; // not registered
            }
            bool shown = !m_filter.hiddenCategories.test(value);
            ImGui::PushID(static_cast<int>(value));
            if (ImGui::Checkbox("##shown", &shown)) {
                m_filter.hiddenCategories.set(value, !shown);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(name.data(), name.data() + name.size());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    std::array<char, 128> text{};
    m_filter.text.copy(text.data(), text.size() - 1);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0F);
    if (ImGui::InputTextWithHint("##text", "Filter text", text.data(), text.size())) {
        m_filter.text = text.data();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        clear();
    }
    ImGui::Separator();

    m_shown.clear();
    for (usize i = 0; i < m_entries.size(); ++i) {
        if (m_filter.passes(m_entries[i])) {
            m_shown.push_back(static_cast<u32>(i));
        }
    }

    if (ImGui::BeginChild("##messages", ImVec2(0.0F, 0.0F), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        // Following the newest message stops when the user scrolls up, and resumes at the bottom.
        m_followNewest = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0F;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_shown.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const LogEntry& entry = m_entries[m_shown[static_cast<usize>(row)]];
                const std::string_view category = log::categoryName(entry.category);
                ImGui::TextDisabled("%8.3f", entry.seconds);
                ImGui::SameLine();
                ImGui::TextColored(levelColor(entry.level), "%-8s", levelName(entry.level));
                ImGui::SameLine();
                ImGui::TextDisabled("[%.*s]", static_cast<int>(category.size()), category.data());
                ImGui::SameLine();
                ImGui::TextUnformatted(entry.message.data(), entry.message.data() + entry.message.size());
            }
        }
        if (received && m_followNewest) {
            ImGui::SetScrollHereY(1.0F);
        }
    }
    ImGui::EndChild();
}

} // namespace teserra::debugui
