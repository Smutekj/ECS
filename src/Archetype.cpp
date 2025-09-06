#include "Archetype.h"

namespace ecs
{
    Archetype::~Archetype()
    {
        //! call all dtors
        for (int comp_i = 0; comp_i < m_count; ++comp_i)
        {
            auto &chunk = m_buffer_stable.at(getArrayIndex(comp_i));
            auto entity_offset = getIndexInArray(comp_i);
            for (auto &rtti : m_type_info)
            {
                auto obj_p = chunk.data() + entity_offset + m_type2offsets.at(rtti.id);
                rtti.v_table->dtor(obj_p);
            }
        }
    }

    void Archetype::registerComps(std::vector<CompTypeInfo> type_info)
    {
        m_type_info = type_info;

        std::size_t offset = 0;
        for (auto &comp_rtti : m_type_info)
        {
            m_type2offsets[comp_rtti.id] = offset;
            offset += comp_rtti.size;
            m_total_size += comp_rtti.size;
        }

        auto max_align = m_type_info[0].align;
        m_padding = (max_align - (offset % max_align)) % max_align;
        m_total_size += m_padding;
    }

    std::byte *Archetype::allocateNewEntity(std::size_t entity_id)
    {
        if (needsAnotherChunk())
        {
            m_buffer_stable.emplace_back(); //! create new chunk
            m_count_last_chunk = 0;
        }

        auto &chunk = m_buffer_stable.at(getArrayIndex(m_count));
        auto entity_offset = getIndexInArray(m_count);

        //! bookkeeping
        m_entities[entity_id] = m_buffer2entity_id.size();
        m_buffer2entity_id.push_back(entity_id);

        m_count++;
        m_count_last_chunk++;
        return chunk.data() + entity_offset;
    }
    void Archetype::addEntity2(std::size_t entity_id, std::vector<std::byte> data)
    {
        assert(!m_entities.contains(entity_id));
        assert(data.size() == m_total_size);

        if (needsAnotherChunk())
        {
            m_buffer_stable.emplace_back(); //! create new chunk
            m_count_last_chunk = 0;
        }

        auto &chunk = m_buffer_stable.at(getArrayIndex(m_count));
        auto entity_offset = getIndexInArray(m_count);

        //! move all components construct in their chunk
        for (auto &type : m_type_info)
        {
            auto dest_p = chunk.data() + entity_offset + m_type2offsets.at(type.id);
            auto src_p = data.data() + m_type2offsets.at(type.id);
            type.v_table->move(dest_p, src_p);
        }

        m_entities[entity_id] = m_buffer2entity_id.size();
        m_buffer2entity_id.push_back(entity_id);

        m_count++;
        m_count_last_chunk++;
        assert(getIndexInArray(m_count - 1) == (m_count_last_chunk - 1) * m_total_size);
    }

    std::vector<std::byte> Archetype::removeEntityAndGetData(int entity_id)
    {
        assert(m_count > 0);

        auto comp_i = m_entities.at(entity_id);

        auto entity_offset = getIndexInArray(comp_i);
        auto &chunk = m_buffer_stable.at(getArrayIndex(comp_i));
        assert(m_count_last_chunk > 0);

        std::vector<std::byte> components(m_total_size);
        //! move the removed comps into returned buffer components (this calls their destructors)
        for (auto &type : m_type_info)
        {
            auto dest_p = components.data() + m_type2offsets.at(type.id);
            auto comp_p = chunk.data() + entity_offset + m_type2offsets.at(type.id);
            type.v_table->move(dest_p, comp_p);
        }

        bool is_last_in_chunk = getIndexInArray(m_count - 1) == getIndexInArray(comp_i);
        bool is_last_chunk = getArrayIndex(m_count - 1) == getArrayIndex(comp_i);
        //! if removing last component, we do not swap!
        if (!(is_last_in_chunk && is_last_chunk))
        {
            assert(getIndexInArray(m_count - 1) == (m_count_last_chunk - 1) * m_total_size);
            auto &last_chunk = m_buffer_stable.at(getArrayIndex(m_count - 1));
            auto start_comp_p = chunk.data() + entity_offset;
            auto last_comp_p = last_chunk.data() + (m_count_last_chunk - 1) * m_total_size;
            //! move from end to created spot
            for (auto &type : m_type_info)
            {
                type.v_table->move(start_comp_p + m_type2offsets.at(type.id), last_comp_p + m_type2offsets.at(type.id));
            }
        }

        //! book keeping
        auto last_entity_id = m_buffer2entity_id.at(m_count - 1);
        m_entities.at(last_entity_id) = comp_i;
        m_buffer2entity_id.at(comp_i) = last_entity_id;

        //! pop
        m_buffer2entity_id.pop_back();
        m_entities.erase(entity_id);
        m_count--;
        m_count_last_chunk--;
        if (m_count_last_chunk == 0)
        {
            m_count_last_chunk = getBlocksPerChunk(); //! next BYTE_CHUNK will be used next time
        }
        return components;
    }

    void Archetype::removeEntity2(int entity_id)
    {
        assert(m_count > 0);

        auto comp_i = m_entities.at(entity_id);
        auto entity_offset = getIndexInArray(comp_i);

        auto &chunk = m_buffer_stable.at(getArrayIndex(comp_i));

        //! destroy the removed comps
        for (auto &type : m_type_info)
        {
            auto comp_p = chunk.data() + entity_offset;
            type.v_table->dtor(comp_p + m_type2offsets.at(type.id));
        }

        bool is_last_in_chunk = getIndexInArray(m_count - 1) == getIndexInArray(comp_i);
        bool is_last_chunk = getArrayIndex(m_count - 1) == getArrayIndex(comp_i);
        //! if removing last component, we do not swap!
        if (!(is_last_in_chunk && is_last_chunk))
        {
            assert(getIndexInArray(m_count - 1) == (m_count_last_chunk - 1) * m_total_size);
            auto &last_chunk = m_buffer_stable.at(getArrayIndex(m_count - 1));
            auto start_comp_p = chunk.data() + entity_offset;
            auto last_comp_p = last_chunk.data() + (m_count_last_chunk - 1) * m_total_size;
            //! move from end to created hole
            for (auto &type : m_type_info)
            {
                type.v_table->move(start_comp_p + m_type2offsets.at(type.id), last_comp_p + m_type2offsets.at(type.id));
            }
        }

        //! book keeping
        auto last_entity_id = m_buffer2entity_id.at(m_count - 1);
        m_entities.at(last_entity_id) = comp_i;
        m_buffer2entity_id.at(comp_i) = last_entity_id;

        m_buffer2entity_id.pop_back();
        m_entities.erase(entity_id);
        m_count--;
        m_count_last_chunk--;
        if (m_count_last_chunk == 0)
        {
            m_count_last_chunk = getBlocksPerChunk(); //! next BYTE_CHUNK will be used next time
        }
    }

    bool Archetype::empty() const
    {
        return m_count == 0;
    }

    std::size_t Archetype::chunkCount() const
    {
        return m_buffer_stable.size();
    }

    std::size_t Archetype::getBlocksPerChunk() const
    {
        return COMPONENT_CHUNK_SIZE / m_total_size;
    }

    std::size_t Archetype::getArrayIndex(std::size_t comp_index) const
    {
        //! end of component block must be inside CHUNK_SIZE
        int blocks_per_chunk = getBlocksPerChunk();
        return (comp_index) / blocks_per_chunk;
    }
    std::size_t Archetype::getIndexInArray(std::size_t comp_index) const
    {
        int blocks_per_chunk = getBlocksPerChunk();
        return ((comp_index) % blocks_per_chunk) * m_total_size;
    }
    bool Archetype::needsAnotherChunk() const
    {
        //! if next inserted component block is beyond CHUNK_SIZE we need another chunk
        return (m_count_last_chunk + 1) * m_total_size > COMPONENT_CHUNK_SIZE;
    }

} // namespace ecs