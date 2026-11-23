#include <teserra/ecs/commands.hpp>

#include <algorithm>

namespace teserra::ecs {

void Commands::apply() {
    // m_next advances only after a command succeeded, so if one throws (test assert handlers do), clear()
    // still discards it and everything after it.
    while (m_next < m_records.size()) {
        Record& record = m_records[m_next];
        switch (record.operation) {
            case Operation::Create:
                m_world->commitReserved(record.entity);
                break;
            case Operation::Destroy:
                m_world->destroy(record.entity);
                break;
            case Operation::Add:
            case Operation::Remove:
                record.apply(*m_world, record.entity, record.payload);
                if (record.discard != nullptr) {
                    record.discard(record.payload);
                    record.discard = nullptr;
                }
                break;
        }
        ++m_next;
    }
    clear();
}

void Commands::clear() {
    for (usize index = m_next; index < m_records.size(); ++index) {
        Record& record = m_records[index];
        if (record.discard != nullptr) {
            record.discard(record.payload);
        }
        if (record.operation == Operation::Create && m_world->isReserved(record.entity)) {
            m_world->releaseReserved(record.entity);
        }
    }
    m_records.clear();
    m_next = 0;
    m_chunk = 0;
    m_offset = 0;
}

void* Commands::allocate(usize size, usize alignment) {
    while (m_chunk < m_chunks.size()) {
        Chunk& chunk = m_chunks[m_chunk];
        const usize start = (m_offset + alignment - 1) & ~(alignment - 1);
        if (start + size <= chunk.size) {
            m_offset = start + size;
            return chunk.memory.get() + start;
        }
        ++m_chunk;
        m_offset = 0;
    }
    // Chunks are aligned to kMaxAlignment, so a payload at offset 0 is aligned.
    const usize chunkSize = std::max(kChunkSize, size);
    m_chunks.push_back({.memory = std::unique_ptr<std::byte, ChunkDeleter>(static_cast<std::byte*>(
                            ::operator new(chunkSize, std::align_val_t{kMaxAlignment}))),
                        .size = chunkSize});
    m_chunk = m_chunks.size() - 1;
    m_offset = size;
    return m_chunks.back().memory.get();
}

} // namespace teserra::ecs
