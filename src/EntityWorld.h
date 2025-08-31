#pragma once

#include "Archetype.h"

#include <iostream>
#include <bitset>
#include <cstring>

constexpr int MAX_ENTITY_COUNT = 20000;
constexpr int MAX_COMPONENT_COUNT = 64;

using ArchetypeId = std::bitset<MAX_COMPONENT_COUNT>;
struct Entity
{
    std::size_t id;
    std::size_t archetype_id;
    ArchetypeId comp_ids;
};

inline bool operator<=(const ArchetypeId &first, const ArchetypeId &second)
{
    return (first & second) == first;
};

struct EntityWorld
{
    template <Component... Comps>
    ArchetypeId getId() const
    {
        ArchetypeId id;
        ((id[Comps::id] = true), ...);
        return id;
    }

    template <Component... Comps>
    ArchetypeId getOrderedId() const
    {
        ArchetypeId id;
        ((id[Comps::id] = true), ...);
        return id;
    }

    template <Component Comp>
    bool has(std::size_t entity_id) const
    {
        return m_entities.at(entity_id).comp_ids[Comp::id];
    }

    std::size_t getNewId()
    {
        if (m_free_entity_ids.size() == 0)
        {
            return m_entity_count;
        }
        std::size_t new_id = m_free_entity_ids.back();
        m_free_entity_ids.pop_back();
        return new_id;
    }

    void removeEntity(std::size_t id)
    {
        auto &entity = m_entities.at(id);
        m_archetypes.at(entity.comp_ids).removeEntity2(id);

        m_free_entity_ids.push_back(id);
        m_entity_count--;
    }

    template <Component... Comps>
    void forEach(std::function<void(Comps &...)> action)
    {
        auto id = getId<Comps...>();
        //! if the action is new we need to register it
        // if (!m_id2action_ids.contains(id))
        {
            registerToActions(id);
        }


        //! go through all archetypes whose id fully contains actions id
        //! TODO: each action should probably be stored and it should hold it's corresponding ids
        for (auto &corresponding_id : m_id2action_ids.at(id))
        {
            if(m_archetypes.contains(corresponding_id))
            {
                m_archetypes.at(corresponding_id).forEach2(action);
            }
        }
    }

    template <Component Comp>
    Comp &get(std::size_t entity_id)
    {
        return m_archetypes.at(m_entities.at(entity_id).comp_ids).get2<Comp>(entity_id);
    }

    void registerToActions(ArchetypeId new_id)
    {
        //! if no such action has been registered yet, then register it!
        if(!m_id2action_ids.contains(new_id))
        {
            m_id2action_ids[new_id] = {};
        }

        //! each archetype that is contained in the new action id gets added
        for (auto &[id, archetype] : m_archetypes)
        {
            if(id <= new_id)
            {
                m_id2action_ids.at(id).insert(new_id);
            }
            if(new_id <= id)
            {
                m_id2action_ids.at(new_id).insert(id);
            }
        }

        for (auto &[id, other_ids] : m_id2action_ids)
        {
            if (id <= new_id)
            {
                other_ids.insert(new_id);
            }
            else if (new_id <= id)
            {
                m_id2action_ids.at(new_id).insert(id);
            }
        }
    }

    template <Component... Comps>
    Entity addEntity(Comps... comps)
    {
        Entity new_entity;
        new_entity.id = getNewId();
        m_entity_count++;
        new_entity.comp_ids = getId<Comps...>();

        if (!m_archetypes.contains(new_entity.comp_ids))
        {
            m_archetypes[new_entity.comp_ids].registerComps<Comps...>();
            registerToActions(new_entity.comp_ids);
        }

        m_archetypes.at(new_entity.comp_ids).addEntity2(new_entity.id, comps...);

        m_entities.at(new_entity.id) = new_entity;

        return new_entity;
    };

    template <Component Comp>
    void addComponent(int entity_id, Comp comp)
    {
        auto &entity = m_entities.at(entity_id);
        auto &archetype = m_archetypes.at(entity.comp_ids);

        //! remove the entity from it's current archetype and add the component to its new data_block
        auto component_data = archetype.removeEntityAndGetData(entity_id);

        //! set the right bit in ArchetypeId;
        entity.comp_ids[Comp::id] = true;
        
        if (!m_archetypes.contains(entity.comp_ids)) //! create the archetype if it is new
        {
            //! correct type info
            auto comp_type_info = archetype.m_type_info;
            CompTypeInfo new_info = makeTypeinfo<Comp>();
            
            auto it = std::lower_bound(comp_type_info.begin(), comp_type_info.end(), new_info);
            comp_type_info.insert(it, new_info);
            m_archetypes[entity.comp_ids].registerComps(comp_type_info);
            if(!m_id2action_ids.contains(entity.comp_ids)){
                m_id2action_ids[entity.comp_ids] = {};
            }
        }

        // auto old_size = component_data.size();
        //! resize buffer to fit the new component
        // component_data.resize(component_data.size() + sizeof(Comp));
        // std::vector<std::byte> new_component(new_archetype.m_total_size);
        
        auto &new_archetype = m_archetypes.at(entity.comp_ids);
        //! construct components in new archetype
        std::byte* new_entity_buffer = new_archetype.allocateNewEntity(entity_id);
        //! construct the new component in newly created buffer
        int offset = m_archetypes.at(entity.comp_ids).m_type2offsets.at(Comp::id);
        std::construct_at(std::launder(reinterpret_cast<Comp *>(new_entity_buffer + offset)), comp);
        //! move rest of the objects from the old buffer
        const auto& old_offsets = archetype.m_type2offsets;
        const auto& new_offsets = new_archetype.m_type2offsets;
        for(auto& rtti : archetype.m_type_info)
        {
            assert(new_offsets.at(rtti.id) != offset); //! no object is build in added  component spot!
            rtti.move(new_entity_buffer + new_offsets.at(rtti.id), component_data.data() + old_offsets.at(rtti.id));
        }


        // //! move existing bytes to make space for the new component
        // auto copy_start_p = component_data.data() + offset;
        // auto copy_dest_p = copy_start_p + sizeof(Comp);
        // std::size_t copy_count = old_size - offset;          //! THIS HAS TO BE AFTER RESIZE DUE TO REALLOCATIONS
        // std::memmove(copy_dest_p, copy_start_p, copy_count); //! UB??!!
        

        // new_archetype.addEntity2(entity_id, component_data);
    }

    template <Component Comp>
    void removeComponent(int entity_id)
    {
        assert(has<Comp>(entity_id)); 

        auto &entity = m_entities.at(entity_id);
        auto &archetype = m_archetypes.at(entity.comp_ids);

        
        entity.comp_ids[Comp::id] = false;
        if (!m_archetypes.contains(entity.comp_ids)) //! create the archetype if it is new
        {
            //! erase removed component from rtti_info and add use it to register a new archetype
            auto type_info = archetype.m_type_info;
            type_info.erase(std::remove_if(type_info.begin(), type_info.end(), [id = Comp::id](auto &info)
                                           { return info.id == id; }),
                            type_info.end());
            assert(type_info.size() == archetype.m_type_info.size() - 1); //! only on id should have existed

            m_archetypes[entity.comp_ids].registerComps(type_info);
            if(!m_id2action_ids.contains(entity.comp_ids)){
                m_id2action_ids[entity.comp_ids] = {};
            }
        }
        auto &new_archetype = m_archetypes.at(entity.comp_ids);

        auto component_data = archetype.removeEntityAndGetData(entity_id);
        
        std::byte* new_entity_buffer = new_archetype.allocateNewEntity(entity_id);
        //! move all components from component_data to new buffer
        auto& offsets = archetype.m_type2offsets;
        auto& new_offsets = new_archetype.m_type2offsets;
        for(auto& rtti : new_archetype.m_type_info)
        {
            assert(rtti.id != Comp::id);//! none of the resting components can be the remove one!
            rtti.move(new_entity_buffer + new_offsets.at(rtti.id), component_data.data() + offsets.at(rtti.id));
        }
        //! destroy the removed component
        std::destroy_at(std::launder(reinterpret_cast<Comp*>(component_data.data() + offsets.at(Comp::id))));
    }

    std::unordered_map<ArchetypeId, std::unordered_set<ArchetypeId>> m_id2action_ids; //
    std::unordered_map<ArchetypeId, Archetype> m_archetypes;                          //!

    std::array<Entity, MAX_ENTITY_COUNT> m_entities; //!< entity storage
    std::size_t m_entity_count = 0;                  //!< number of existing entities
    std::vector<std::size_t> m_free_entity_ids;      //!< entity id free-list
};
