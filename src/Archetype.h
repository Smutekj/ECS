#pragma once

#include <vector>
#include <cstddef>
#include <cassert>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <concepts>
#include <algorithm>

#include "Component.h"

namespace ecs
{

#ifndef MEMORY_CHUNK_SIZE
#define MEMORY_CHUNK_SIZE 100000
#endif

	constexpr int COMPONENT_CHUNK_SIZE = MEMORY_CHUNK_SIZE;

	using EntityId = std::size_t;

	struct CompTypeInfo
	{
		int id;
		std::size_t size;
		unsigned long align;

		template <class Comp>
		static void destroy_s(void *obj)
		{
			std::destroy_at(reinterpret_cast<Comp *>(obj));
		}

		template <class Comp>
		static void copy_s(void *dest, const void *src)
		{
			std::construct_at(reinterpret_cast<Comp *>(dest),
							  *reinterpret_cast<const Comp *>(src));
		}

		template <class Comp>
		static void move_s(void *dest, void *src)
		{
			std::construct_at(reinterpret_cast<Comp *>(dest),
							  std::move(*reinterpret_cast<Comp *>(src)));
			std::destroy_at(reinterpret_cast<Comp *>(src));
		}

		struct VTable
		{
			void (*copy)(void *dest, const void *src);
			void (*dtor)(void *obj);
			void (*move)(void *dest, void *src);
		};

		template <class Comp>
		constexpr static VTable v_table_temp{
			.copy = &copy_s<Comp>,
			.dtor = &destroy_s<Comp>,
			.move = &move_s<Comp>};

		const VTable *v_table = nullptr;
		template <class Comp>
		CompTypeInfo(Comp c) : id(Comp::id), size(sizeof(Comp)), align(alignof(Comp)),
								   v_table(&v_table_temp<Comp>)
		{
		}

		CompTypeInfo(const CompTypeInfo& from)
			: id(from.id), size(from.size), align(from.align)
		{
			v_table = from.v_table;
		}

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


	struct Archetype
	{
		~Archetype();

		void registerComps(std::vector<CompTypeInfo> type_info);

		template <Component... Comps>
		void registerComps();

		template <Component Comp>
		Comp &get2(std::size_t entity_id);

		template <class Callable, Component... Comps>
		void forEach2(Callable action);

		std::byte *allocateNewEntity(std::size_t entity_id);
		void addEntity2(std::size_t entity_id, std::vector<std::byte> data);

		//! adds components of the entity entity_id at the end of the m_buffer by copy constructing them
		template <Component... Comps>
		void addEntity2(std::size_t entity_id, Comps&&... data);

		std::vector<std::byte> removeEntityAndGetData(int entity_id);

		void removeEntity2(int entity_id);

		bool empty() const;

		std::size_t chunkCount() const;

		//! saved components info
		std::size_t m_total_size = 0; //! size in bytes of a single component block
		std::size_t m_padding = 0;	  //! size in bytes of padding at the end of a component block
		std::vector<CompTypeInfo> m_type_info;
		std::unordered_map<int, std::size_t> m_type2offsets;

	private:
		std::size_t getBlocksPerChunk() const;
		std::size_t getArrayIndex(std::size_t comp_index) const;
		std::size_t getIndexInArray(std::size_t comp_index) const;
		bool needsAnotherChunk() const;


		struct ByteChunk
		{
			alignas(std::max_align_t) std::vector<std::byte> buffer;

			ByteChunk() : buffer(COMPONENT_CHUNK_SIZE) {}

			std::byte *data()
			{
				return buffer.data();
			}
		};

		struct Column
		{
			Column()
			{
				
			};

			std::size_t entity_offset;
			ByteChunk* p_chunk = nullptr;
		};


		std::size_t m_count = 0;				   //! total number of stored entities (i.e. component blocks)
		std::size_t m_count_last_chunk = 0;		   //! total number of stored entities (i.e. component blocks)
		std::vector<ByteChunk> m_buffer_stable{1}; //! buffer for all component blocks (starts with one chunk)

		std::vector<EntityId> m_buffer2entity_id;			  //! entity ids of each component block
		std::unordered_map<EntityId, std::size_t> m_entities; //! component block id of each entity
	};

	template <Component... Comps>
	void Archetype::registerComps()
	{
		// m_type_info.resize(sizeof...(Comps));

		int i = 0;
		((m_type_info.emplace_back(Comps{}), i++), ...);

		//! largest alignements go first in component blocks -> if the first is aligned then so are the others
		std::sort(m_type_info.begin(), m_type_info.end());

		std::size_t offset = 0;
		for (auto &[id, size, align, v_table] : m_type_info)
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
	Comp &Archetype::get2(std::size_t entity_id)
	{
		auto comp_i = m_entities.at(entity_id);
		auto comp_offset = m_type2offsets.at(Comp::id);
		auto entity_offset = getIndexInArray(comp_i);

		auto &chunk = m_buffer_stable.at(getArrayIndex(comp_i));
		return *std::launder(reinterpret_cast<Comp *>(chunk.data() + entity_offset + comp_offset));
	}

	template <class Callable, typename... Comps, std::size_t... Is>
	void callActionWithOffsets(
		Callable action, std::byte *args_data,
		const std::array<std::size_t, sizeof...(Comps)> &offsets,
		std::index_sequence<Is...>)
	{
		action((*std::launder(reinterpret_cast<Comps *>(args_data + offsets[Is])))...);
	}

	template <class Callable, Component... Comps>
	void Archetype::forEach2(Callable action)
	{
		constexpr std::size_t comps_count = sizeof...(Comps);

		std::array<std::size_t, comps_count> offsets;
		int k = 0;
		(..., (offsets.at(k) = m_type2offsets.at(Comps::id), k++)); //! the pack expansion is opposite of what i need???

		std::size_t chunk_count = getArrayIndex(m_count - 1) + 1;
		for (int chunk_i = 0; chunk_i < chunk_count - 1; ++chunk_i)
		{
			auto &chunk = m_buffer_stable.at(chunk_i);
			std::size_t comps_per_chunk = getBlocksPerChunk();
			for (int comp_i = 0; comp_i < comps_per_chunk; ++comp_i)
			{
				int entity_offset = comp_i * m_total_size;
				callActionWithOffsets<Callable, Comps...>(action, chunk.data() + entity_offset, offsets, std::index_sequence_for<Comps...>{});
			}
		}

		//! last chunk is not full so we iterate separately
		auto &last_chunk = m_buffer_stable.at(chunk_count - 1);
		for (int comp_i = 0; comp_i < m_count_last_chunk; ++comp_i)
		{
			int entity_offset = comp_i * m_total_size;
			callActionWithOffsets<Callable, Comps...>(action, last_chunk.data() + entity_offset, offsets, std::index_sequence_for<Comps...>{});
		}
	}

	//! adds components of the entity entity_id at the end of the m_buffer by copy constructing them
	template <Component... Comps>
	void Archetype::addEntity2(std::size_t entity_id, Comps&&... data)
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
				std::construct_at(std::launder(reinterpret_cast<Comps *>(chunk.data() + offset)), std::forward<Comps>(data))),
			...);

		m_entities[entity_id] = m_buffer2entity_id.size();
		m_buffer2entity_id.push_back(entity_id);

		m_count++;
		m_count_last_chunk++;
		assert(getIndexInArray(m_count - 1) == (m_count_last_chunk - 1) * m_total_size);
	}

} // namespace ecs