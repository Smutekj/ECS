#pragma once

#include <vector>
#include <cstddef>
#include <cassert>
#include <functional>
#include <unordered_set>
#include <unordered_map>

#include "Component.h"

struct CompTypeInfo
{
	int id;
	unsigned long align;
	std::size_t size;

	void (*copy)(void *dest, const void *src);
	void (*dtor)(void *obj);
	void (*move)(void *dest, void *src);

	bool operator==(const CompTypeInfo &rhs)
	{
		return align == rhs.align && id == rhs.id && size == rhs.size;
	}
	//! order primary by aling, secondary by id (which is unique so the ordering is unique)
	bool operator<(const CompTypeInfo &rhs)
	{
		return std::tie(align, id) > std::tie(rhs.align, rhs.id);
	}
};

template <typename Comp>
CompTypeInfo makeTypeinfo()
{
	return {
		.id = Comp::id,
		.align = alignof(Comp),
		.size = sizeof(Comp),
		// copy constructor
		.copy = [](void *dest, const void *src)
		{ std::construct_at(reinterpret_cast<Comp *>(dest),
							*reinterpret_cast<const Comp *>(src)); },
		// destructor
		.dtor = [](void *obj)
		{ std::destroy_at(reinterpret_cast<Comp *>(obj)); },
		// move constructor + destroy source
		.move = [](void *dest, void *src)
		{
			std::construct_at(reinterpret_cast<Comp *>(dest),
							  std::move(*reinterpret_cast<Comp *>(src)));
			std::destroy_at(reinterpret_cast<Comp *>(src)); }};
}

constexpr int COMPONENT_CHUNK_SIZE = 2048;

struct Archetype
{
	~Archetype()
	{
		//! call all dtors
		for (int comp_i = 0; comp_i < m_count; ++comp_i)
		{
			auto &chunk = m_buffer_stable.at(getArrayIndex(comp_i));
			auto entity_offset = getIndexInArray(comp_i);
			for (auto &rtti : m_type_info)
			{
				auto obj_p = chunk.data() + entity_offset + m_type2offsets.at(rtti.id);
				rtti.dtor(obj_p);
			}
		}
	}

	void registerComps(std::vector<CompTypeInfo> type_info)
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

	template <Component... Comps>
	void registerComps()
	{
		m_type_info.resize(sizeof...(Comps));

		int i = 0;
		((m_type_info.at(i++) = makeTypeinfo<Comps>()), ...);

		//! largest alignements go first in component blocks -> if the first is aligned then so are the others
		std::sort(m_type_info.begin(), m_type_info.end());

		std::size_t offset = 0;
		for (auto &[id, align, size, a, b, c] : m_type_info)
		{
			m_type2offsets[id] = offset;
			offset += size;
		}
		auto max_align = m_type_info[0].align;
		m_padding = (max_align - (offset % max_align)) % max_align;

		m_total_size = (sizeof(Comps) + ... + (0));
		m_total_size += m_padding;
	}

	template <Component Comp>
	Comp &get2(std::size_t entity_id)
	{
		auto comp_i = m_entities.at(entity_id);
		auto comp_offset = m_type2offsets.at(Comp::id);
		auto entity_offset = getIndexInArray(comp_i);

		auto &chunk = m_buffer_stable.at(getArrayIndex(comp_i));
		return *std::launder(reinterpret_cast<Comp *>(chunk.data() + entity_offset + comp_offset));
	}

	template <Component... Comps>
	void forEach2(std::function<void(Comps &...)> action)
	{
		std::array<std::size_t, sizeof...(Comps)> offsets;
		int k = sizeof...(Comps);
		(..., (offsets.at(--k) = m_type2offsets.at(Comps::id)));

		for (int comp_i = 0; comp_i < m_count; ++comp_i)
		{
			auto entity_offset = getIndexInArray(comp_i);
			auto &chunk = m_buffer_stable.at(getArrayIndex(comp_i));
			int i = 0;
			action(
				(*std::launder(reinterpret_cast<Comps *>(chunk.data() + entity_offset + offsets.at(i++))))...);
		}
	}

	std::byte *allocateNewEntity(std::size_t entity_id)
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
	void addEntity2(std::size_t entity_id, std::vector<std::byte> data)
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
			type.move(dest_p, src_p);
		}

		m_entities[entity_id] = m_buffer2entity_id.size();
		m_buffer2entity_id.push_back(entity_id);

		m_count++;
		m_count_last_chunk++;
		assert(getIndexInArray(m_count - 1) == (m_count_last_chunk-1)*m_total_size);

	}

	//! adds components of the entity entity_id at the end of the m_buffer by copy constructing them
	template <Component... Comps>
	void addEntity2(std::size_t entity_id, Comps... data)
	{
		assert(!m_entities.contains(entity_id));

		if (needsAnotherChunk())
		{
			m_buffer_stable.emplace_back(); //! create new chunk
			m_count_last_chunk = 0;
		}
		auto &chunk = m_buffer_stable.at(getArrayIndex(m_count));
		auto entity_offset = getIndexInArray(m_count);
		assert(entity_offset + m_total_size <= COMPONENT_CHUNK_SIZE); //! NO DATA OUTSIDE OF THE CHUNK!

		//! fold expression to construct all Comps... data at their respective offsets
		std::size_t offset = 0;
		(
			(
				offset = entity_offset + m_type2offsets.at(Comps::id),
				std::construct_at(std::launder(reinterpret_cast<Comps *>(chunk.data() + offset)), data)),
			...);

		m_entities[entity_id] = m_buffer2entity_id.size();
		m_buffer2entity_id.push_back(entity_id);

		m_count++;
		m_count_last_chunk++;
		assert(getIndexInArray(m_count - 1) == (m_count_last_chunk-1)*m_total_size);
	}

	std::vector<std::byte> removeEntityAndGetData(int entity_id)
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
			type.move(dest_p, comp_p);
		}

		bool is_last_in_chunk = getIndexInArray(m_count - 1) == getIndexInArray(comp_i);  
		bool is_last_chunk = getArrayIndex(m_count - 1) == getArrayIndex(comp_i);  
		//! if removing last component, we do not swap!
		if ( !(is_last_in_chunk && is_last_chunk))
		{
			assert(getIndexInArray(m_count - 1) == (m_count_last_chunk-1)*m_total_size);
			auto &last_chunk = m_buffer_stable.at(getArrayIndex(m_count-1));
			auto start_comp_p = chunk.data() + entity_offset;
			auto last_comp_p = last_chunk.data() + (m_count_last_chunk - 1) * m_total_size;
			//! move from end to created spot
			for (auto &type : m_type_info)
			{
				type.move(start_comp_p + m_type2offsets.at(type.id), last_comp_p + m_type2offsets.at(type.id));
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
		if(m_count_last_chunk == 0)
		{
			m_count_last_chunk = COMPONENT_CHUNK_SIZE / m_total_size; //! next BYTE_CHUNK will be used next time
		}
		return components;
	}

	void removeEntity2(int entity_id)
	{
		assert(m_count > 0);

		auto comp_i = m_entities.at(entity_id);
		auto entity_offset = getIndexInArray(comp_i);

		auto &chunk = m_buffer_stable.at(getArrayIndex(comp_i));

		//! destroy the removed comps
		for (auto &type : m_type_info)
		{
			auto comp_p = chunk.data() + entity_offset;
			type.dtor(comp_p + m_type2offsets.at(type.id));
		}

		bool is_last_in_chunk = getIndexInArray(m_count - 1) == getIndexInArray(comp_i);  
		bool is_last_chunk = getArrayIndex(m_count - 1) == getArrayIndex(comp_i);  
		//! if removing last component, we do not swap!
		if ( !(is_last_in_chunk && is_last_chunk))
		{
			assert(getIndexInArray(m_count - 1) == (m_count_last_chunk-1)*m_total_size);
			auto &last_chunk = m_buffer_stable.at(getArrayIndex(m_count-1));
			auto start_comp_p = chunk.data() + entity_offset;
			auto last_comp_p = last_chunk.data() + (m_count_last_chunk - 1) * m_total_size;
			//! move from end to created hole
			for (auto &type : m_type_info)
			{
				type.move(start_comp_p + m_type2offsets.at(type.id), last_comp_p + m_type2offsets.at(type.id));
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
		if(m_count_last_chunk == 0)
		{
			m_count_last_chunk = COMPONENT_CHUNK_SIZE / m_total_size; //! next BYTE_CHUNK will be used next time
		}
	}

	bool empty() const
	{
		return m_count == 0;
	}

	std::size_t chunkCount() const
	{
		return m_buffer_stable.size();
	}

	using EntityId = std::size_t;

	//! saved components info
	std::size_t m_total_size = 0; //! size in bytes of a single component block
	std::size_t m_padding = 0;	  //! size in bytes of padding at the end of a component block
	std::vector<CompTypeInfo> m_type_info;
	std::unordered_map<int, std::size_t> m_type2offsets;

private:
	std::size_t getArrayIndex(std::size_t comp_index) const
	{
		//! end of component block must be inside CHUNK_SIZE
		int blocks_per_chunk = COMPONENT_CHUNK_SIZE / m_total_size;
		return (comp_index) / blocks_per_chunk;
	}
	std::size_t getIndexInArray(std::size_t comp_index) const
	{
		int blocks_per_chunk = COMPONENT_CHUNK_SIZE / m_total_size;
		return ((comp_index) % blocks_per_chunk)*m_total_size;
	}
	bool needsAnotherChunk() const
	{
		//! if next inserted component block is beyond CHUNK_SIZE we need another chunk
		return (m_count_last_chunk + 1) * m_total_size > COMPONENT_CHUNK_SIZE;
	}

	struct ByteChunk
	{
		alignas(std::max_align_t) std::vector<std::byte>  buffer;

		ByteChunk() : buffer(COMPONENT_CHUNK_SIZE){}

		std::byte *data()
		{
			return buffer.data();
		}
	};

	std::size_t m_count = 0;				   //! total number of stored entities (i.e. component blocks)
	std::size_t m_count_last_chunk = 0;		   //! total number of stored entities (i.e. component blocks)
	std::vector<ByteChunk> m_buffer_stable{1}; //! buffer for all component blocks (starts with one chunk)

	std::vector<EntityId> m_buffer2entity_id;			  //! entity ids of each component block
	std::unordered_map<EntityId, std::size_t> m_entities; //! component block id of each entity
};
