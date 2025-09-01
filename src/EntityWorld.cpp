#include "EntityWorld.h"


EntityWorld::EntityWorld() : m_entities(){};

bool operator<=(const ArchetypeId &first, const ArchetypeId &second)
{
    return (first & second) == first;
};

//! TODO: This is stupid AF, make somehow not retarded
void EntityWorld::registerToActions(ArchetypeId new_id)
{
    //! if no such action has been registered yet, then register it!
    if (!m_id2action_ids.contains(new_id))
    {
        m_id2action_ids[new_id] = {};
    }

    //! each archetype that is contained in the new action id gets added
    for (auto &[id, archetype] : m_archetypes)
    {
        if (id <= new_id)
        {
            m_id2action_ids.at(id).insert(new_id);
        }
        if (new_id <= id)
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

std::size_t EntityWorld::getNewId()
{
    if (m_free_entity_ids.size() == 0)
    {
        return m_entity_count;
    }
    std::size_t new_id = m_free_entity_ids.back();
    m_free_entity_ids.pop_back();
    return new_id;
}

void EntityWorld::removeEntity(std::size_t id)
{
    auto &entity = m_entities.at(id);
    m_archetypes.at(entity.comp_ids).removeEntity2(id);

    m_free_entity_ids.push_back(id);
    m_entity_count--;
}
