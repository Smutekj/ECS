#pragma once

#include "Archetype.h"

#include <iostream>
#include <bitset>
#include <cstring>
#include <algorithm>

#ifndef MAX_ENTITY_COUNT
constexpr int MAX_ENTITY_COUNT = 20000;
#endif

#ifndef MAX_COMPONENT_COUNT
constexpr int MAX_COMPONENT_COUNT = 64;
#endif


using ArchetypeId = std::bitset<MAX_COMPONENT_COUNT>;

struct Entity
{
    std::size_t id;
    std::size_t archetype_id;
    ArchetypeId comp_ids;
};

//! this operator means: first IS CONTAINED in second
//! for instance Archetype: AB IS CONTAINED in ABCD and ABD but not in AD
//! when this is true then action with ArchetypeId second should be called when ArchetypeId first is called 
bool operator<=(const ArchetypeId &first, const ArchetypeId &second);

struct EntityWorld
{
    template <Component... Comps>
    ArchetypeId getId() const;

    template <Component Comp>
    bool has(std::size_t entity_id) const;

    void removeEntity(std::size_t id);

    template <typename Callable>
    void forEach(Callable &&callable);

    template <Component Comp>
    Comp &get(std::size_t entity_id);

    template <Component... Comps>
    Entity addEntity(Comps... comps);

    template <Component Comp>
    void addComponent(int entity_id, Comp comp);

    template <Component Comp>
    void removeComponent(int entity_id);

private:
    template <typename C, typename R, class... Comps>
    void forEachHelper(C &&callable, const std::function<R(Comps...)> &);

    //! TODO: This is stupid AF, make somehow not retarded
    void registerToActions(ArchetypeId new_id);

    std::size_t getNewId();

private:
    //! remembers which action get called for each ID.
    //! For example: If we have archetypes A, AB, and ABC and action AB, then AB and ABC should be called
    std::unordered_map<ArchetypeId, std::unordered_set<ArchetypeId>> m_id2action_ids;

    std::unordered_map<ArchetypeId, Archetype> m_archetypes; //!< holds all archetype, which hold all components

    std::array<Entity, MAX_ENTITY_COUNT> m_entities; //!< entity storage
    std::size_t m_entity_count = 0;                  //!< number of existing entities
    std::vector<std::size_t> m_free_entity_ids;      //!< entity id free-list
};

template <Component... Comps>
ArchetypeId EntityWorld::getId() const
{
    ArchetypeId id;
    ((id[Comps::id] = true), ...);
    return id;
}

template <Component Comp>
bool EntityWorld::has(std::size_t entity_id) const
{
    return m_entities.at(entity_id).comp_ids[Comp::id];
}

template <typename C, typename R, class... Comps>
void EntityWorld::forEachHelper(C &&callable, const std::function<R(Comps...)> &)
{

    static_assert(std::is_same_v<void, R>);

    auto id = getId<std::remove_reference_t<Comps>...>();
    //! if the action is new we need to register it
    // if (!m_id2action_ids.contains(id))
    {
        registerToActions(id);
    }

    //! go through all archetypes whose id fully contains actions id
    //! TODO: each action should probably be stored and it should hold it's corresponding ids
    for (auto &corresponding_id : m_id2action_ids.at(id))
    {
        if (m_archetypes.contains(corresponding_id))
        {
            m_archetypes.at(corresponding_id).template forEach2<C, std::remove_reference_t<Comps>...>(std::forward<C>(callable));
        }
    }
}

template <typename Callable>
void EntityWorld::forEach(Callable &&callable)
{
    using std_function_type = decltype(std::function{std::forward<Callable>(callable)});
    forEachHelper(std::forward<Callable>(callable), std_function_type{});
}

template <Component Comp>
Comp &EntityWorld::get(std::size_t entity_id)
{
    return m_archetypes.at(m_entities.at(entity_id).comp_ids).get2<Comp>(entity_id);
}

template <Component... Comps>
Entity EntityWorld::addEntity(Comps... comps)
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
void EntityWorld::addComponent(int entity_id, Comp comp)
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
        if (!m_id2action_ids.contains(entity.comp_ids))
        {
            m_id2action_ids[entity.comp_ids] = {};
        }
    }

    // auto old_size = component_data.size();
    //! resize buffer to fit the new component
    // component_data.resize(component_data.size() + sizeof(Comp));
    // std::vector<std::byte> new_component(new_archetype.m_total_size);

    auto &new_archetype = m_archetypes.at(entity.comp_ids);
    //! construct components in new archetype
    std::byte *new_entity_buffer = new_archetype.allocateNewEntity(entity_id);
    //! construct the new component in newly created buffer
    int offset = m_archetypes.at(entity.comp_ids).m_type2offsets.at(Comp::id);
    std::construct_at(std::launder(reinterpret_cast<Comp *>(new_entity_buffer + offset)), comp);
    //! move rest of the objects from the old buffer
    const auto &old_offsets = archetype.m_type2offsets;
    const auto &new_offsets = new_archetype.m_type2offsets;
    for (auto &rtti : archetype.m_type_info)
    {
        assert(new_offsets.at(rtti.id) != offset); //! no object is build in added  component spot!
        rtti.move(new_entity_buffer + new_offsets.at(rtti.id), component_data.data() + old_offsets.at(rtti.id));
    }
}

template <Component Comp>
void EntityWorld::removeComponent(int entity_id)
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
        if (!m_id2action_ids.contains(entity.comp_ids))
        {
            m_id2action_ids[entity.comp_ids] = {};
        }
    }
    auto &new_archetype = m_archetypes.at(entity.comp_ids);

    auto component_data = archetype.removeEntityAndGetData(entity_id);

    std::byte *new_entity_buffer = new_archetype.allocateNewEntity(entity_id);
    //! move all components from component_data to new buffer
    auto &offsets = archetype.m_type2offsets;
    auto &new_offsets = new_archetype.m_type2offsets;
    for (auto &rtti : new_archetype.m_type_info)
    {
        assert(rtti.id != Comp::id); //! none of the resting components can be the remove one!
        rtti.move(new_entity_buffer + new_offsets.at(rtti.id), component_data.data() + offsets.at(rtti.id));
    }
    //! destroy the removed component
    std::destroy_at(std::launder(reinterpret_cast<Comp *>(component_data.data() + offsets.at(Comp::id))));
}
