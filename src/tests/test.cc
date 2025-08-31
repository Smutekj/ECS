#include <gtest/gtest.h>

#include <EntityWorld.h>
#include <type_traits>


struct CompA : public CompTag<CompA>
{
    int a;
};

struct CompB : public CompTag<CompB>
{
    double x;
};

struct CompC : public CompTag<CompC>
{
    char x;
};

struct CompD : public CompTag<CompD>
{
    int x;
    int y;
};


struct CompFunction : public CompTag<CompFunction>
{
    inline static int CompFunctionCount = 0;
    
    CompFunction()
    {
        func = [](int a){return a;};
        ptr = std::make_shared<std::array<int, 500>>();
        CompFunctionCount++;
        // std::cout << "CONSTRUCTING COMPONENT" << std::endl;
    }
    ~CompFunction()
    {
        CompFunctionCount--;
        // std::cout << "DESTROYING COMPONENT" << std::endl;
    }
    CompFunction(const CompFunction& other)
    {
        ptr = other.ptr;
        func = other.func;
        CompFunctionCount++;
    }
    CompFunction& operator = (const CompFunction& other)
    {
        ptr = other.ptr;
        func = other.func;
        return *this;
    }
    CompFunction(CompFunction&& other)
    {
        ptr = std::move(other.ptr);
        func = std::move(other.func);
        CompFunctionCount++;
    }
    CompFunction& operator =(CompFunction&& other)
    {
        ptr = std::move(other.ptr);
        func = std::move(other.func);
        return *this;
    }
    
    std::shared_ptr<std::array<int, 500>> ptr;
    std::function<int(int)> func = [](int a){return a;};
}; 

struct CompSharedPtr : public CompTag<CompSharedPtr>
{
    int* func;
}; 




namespace  //! THIS NAMESPACE NEEDS TO BE ANONYMOUS FOR SOME REASON???
{


    TEST(Registration, ComponentTests)
    {

        EXPECT_EQ(CompA::id, 0);
        EXPECT_EQ(CompB::id, 1);
        EXPECT_EQ(CompC::id, 2);

        auto ti = makeTypeinfo<CompB>();
        
    }
    
    TEST(EntityInsertion, BasicTests)
    {
        EntityWorld world;
        
        auto e0 = world.addEntity(CompA{}, CompB{}, CompC{});
        auto e1 = world.addEntity(CompA{}, CompC{});
        auto e2 = world.addEntity(CompA{}, CompC{});
        auto e3 = world.addEntity(CompA{}, CompB{}, CompC{});
        
        EXPECT_EQ(e0.id, 0);
        EXPECT_EQ(e1.id, 1);
        EXPECT_EQ(e2.id, 2);
        EXPECT_EQ(e3.id, 3);
        
        world.removeEntity(e1.id);
        auto e4 = world.addEntity(CompC{}, CompA{});
        EXPECT_EQ(e4.id, e1.id); //! deleted id should have been used
        
        auto e5 = world.addEntity(CompA{});
        EXPECT_EQ(e5.id, 4); //! deleted id should have been used
        
        auto ex = world.addEntity(CompSharedPtr{});
    }
    TEST(EntityInsertionNewChunk, BasicTests)
    {
        EntityWorld world;
        
        auto e_last = world.addEntity(CompA{.a=5}, CompB{.x=69}, CompC{.x='6'});
        auto& archetype = world.m_archetypes.at(e_last.comp_ids);
        
        EXPECT_EQ(archetype.chunkCount(), 1);
        
        int entity_count = COMPONENT_CHUNK_SIZE / archetype.m_total_size; 
        for(int i = 0; i < entity_count; ++i)
        {
            e_last = world.addEntity(CompA{.a=5}, CompB{.x=69}, CompC{.x='6'});
        }

        EXPECT_EQ(archetype.chunkCount(), 2);
        EXPECT_EQ(world.get<CompC>(e_last.id).x, '6');
        EXPECT_EQ(world.get<CompA>(e_last.id).a, 5);
    }
    
    TEST(ComponentInsertion, ComponentTests)
    {
        EntityWorld world;
        
        auto e0 = world.addEntity(CompA{}, CompB{}, CompC{}, CompD{});
        auto e1 = world.addEntity(CompD{.x = 6969, .y=9000}, CompA{.a=100}, CompC{.x='5'});
        
        EXPECT_EQ(world.get<CompD>(e1.id).x, 6969); //! value should not have changed
        EXPECT_EQ(world.get<CompD>(e1.id).y, 9000); //! value should not have changed
        EXPECT_EQ(world.get<CompC>(e1.id).x, '5'); //! value should not have changed
        EXPECT_EQ(world.get<CompA>(e1.id).a, 100); //! value should not have changed
        
        world.addComponent(e1.id, CompB{.x = 69}); //! nice
        EXPECT_EQ(world.get<CompB>(e1.id).x, 69);  //! nice
        EXPECT_EQ(world.get<CompC>(e1.id).x, '5'); //! value should not have changed
        EXPECT_EQ(world.get<CompA>(e1.id).a, 100); //! value should not have changed

        CompFunction function_comp;
        function_comp.func = [a=69](int i){return i + a;}; 
        world.addComponent(e1.id, function_comp); 
        EXPECT_EQ(world.get<CompFunction>(e1.id).func(5), 69+5); 
        
        world.removeComponent<CompB>(e1.id);    
        EXPECT_EQ(CompFunction::CompFunctionCount, 2);
        world.addComponent(e1.id, CompB{.x=68});    
        EXPECT_EQ(CompFunction::CompFunctionCount, 2);
        world.removeComponent<CompFunction>(e1.id);
        EXPECT_EQ(CompFunction::CompFunctionCount, 1);
    }
    
    TEST(ComponentDeletionLastInChunk, ComponentTests)
    {
        EntityWorld world;
        
        Entity last_e;
        for(int i = 0; i<=COMPONENT_CHUNK_SIZE / 16; ++i)
        {
            last_e = world.addEntity(CompA{.a=1}, CompB{.x=2}, CompC{.x='c'}); 
        }   
        
        world.removeEntity(last_e.id);
        EXPECT_EQ(world.get<CompA>(127).a, 1);
        EXPECT_EQ(world.get<CompB>(127).x, 2);
        EXPECT_EQ(world.get<CompC>(127).x, 'c');
        last_e = world.addEntity(CompA{.a=11}, CompB{.x=22}, CompC{.x='d'}); 
        EXPECT_EQ(world.get<CompA>(last_e.id).a, 11);
        EXPECT_EQ(world.get<CompB>(last_e.id).x, 22);
        EXPECT_EQ(world.get<CompC>(last_e.id).x, 'd');
    }

    TEST(ComponentInsertionDeletion, ComponentTests)
    {
        EntityWorld world;
        
        auto e1 = world.addEntity(CompA{}, CompC{.x=5});
        
        world.addComponent<CompB>(e1.id, CompB{.x = 69}); //! nice
        EXPECT_FLOAT_EQ(world.get<CompB>(e1.id).x, 69); //! value of C component has not changed!
        world.removeComponent<CompB>(e1.id); 
        EXPECT_FALSE(world.has<CompB>(e1.id));

        EXPECT_EQ(world.get<CompC>(e1.id).x, 5); //! value of C component has not changed!
    }


    TEST(SingleAction, ActionTests)
    {
        EntityWorld world;
        
        auto e0 = world.addEntity(CompA{.a=1}, CompB{.x=5}, CompC{.x='C'});
        auto e1 = world.addEntity(CompA{.a=1}, CompB{.x=5});
        auto e2 = world.addEntity(CompA{.a=1}, CompC{.x='C'});
        auto e3 = world.addEntity(CompA{.a=1}, CompB{.x=5}, CompC{.x='C'});

        int call_count = 0;
        std::function<void(CompA&, CompB&)> action = [&call_count](CompA& a, CompB& b)
        {
            EXPECT_EQ(a.a, 1);
            EXPECT_FLOAT_EQ(b.x, 5);
            call_count++;
        };
        world.forEach(action);
        EXPECT_EQ(call_count, 3); //AB and ABC archetypes get used
        call_count = 0;
        std::function<void(CompA&, CompB&, CompC&)> action2 = [&call_count](CompA& a, CompB& b, CompC& c)
        {
            EXPECT_EQ(a.a, 1);
            EXPECT_FLOAT_EQ(b.x, 5);
            EXPECT_EQ(c.x, 'C');
            call_count++;
        };
        world.forEach(action2);
        EXPECT_EQ(call_count, 2);

        world.removeComponent<CompC>(e0.id);
        call_count = 0;
        world.forEach(action);
        EXPECT_EQ(call_count, 3); //AB and ABC archetypes get used
        call_count = 0;
        world.forEach(action2);
        EXPECT_EQ(call_count, 1); //AB and ABC archetypes get used
    }
    TEST(SwappedParametersAction, ActionTests)
    {
        EntityWorld world;
        
        auto e0 = world.addEntity(CompA{.a=1}, CompB{.x=5}, CompC{.x='C'});
        auto e1 = world.addEntity(CompA{.a=1}, CompB{.x=5});
        auto e2 = world.addEntity(CompA{.a=1}, CompC{.x='C'});
        auto e3 = world.addEntity(CompA{.a=1}, CompB{.x=5}, CompC{.x='C'});

        int call_count = 0;
        std::function<void(CompA&, CompB&)> action = [&call_count](CompA& a, CompB& b)
        {
            EXPECT_EQ(a.a, 1);
            EXPECT_FLOAT_EQ(b.x, 5);
            call_count++;
        };
        std::function<void(CompB&, CompA&)> action_swapped = [&call_count](CompB& b, CompA& a)
        {
            EXPECT_EQ(a.a, 1);
            EXPECT_FLOAT_EQ(b.x, 5);
            call_count++;
        };
        world.forEach(action);
        EXPECT_EQ(call_count, 3); //AB and ABC archetypes get used
        call_count = 0;    
        world.forEach(action_swapped);
        EXPECT_EQ(call_count, 3); //AB and ABC archetypes get used
    }

    TEST(AdditionRemoval, RandomShit)
    {
        EntityWorld world;
        
        auto e0 = world.addEntity(CompA{.a=1}, CompB{.x=5}, CompC{.x='x'});

        std::unordered_set<std::size_t> added_ids; 
        for(int i = 0; i < 5009; ++i)
        {
            added_ids.insert(world.addEntity(CompFunction{}, CompC{.x='x'}, CompB{.x=5}).id);
        }

        //! remove randomly
        int b_count = 5010;
        int c_count = 5010;
        int bc_count = 5010;
        for(int i = 1; i <= 2500; ++i)
        {
            auto id = *std::next(added_ids.begin(), rand() % added_ids.size());
            int a = rand()%3;
            if(a == 0)
            {
                c_count--;
                world.removeComponent<CompC>(id);
            }else if(a == 1)
            {
                b_count--;
                world.removeComponent<CompB>(id);
            }else{
                b_count--;
                c_count--;
                world.removeEntity(id);
            }
            bc_count--;
            added_ids.erase(id);
        }

        int i = 0;
        std::function<void(CompB&, CompC&)> actionbc = [&bc_count, &i](CompB& b, CompC& c)
        {
            EXPECT_EQ(c.x, 'x');
            EXPECT_FLOAT_EQ(b.x, 5);
            bc_count--;
        };
        std::function<void(CompB&)> actionb = [&b_count](CompB& b)
        {
            EXPECT_FLOAT_EQ(b.x, 5);
            b_count--;
        };
        std::function<void(CompC&)> actionc = [&c_count](CompC& c)
        {
            EXPECT_EQ(c.x, 'x');
            c_count--;
        };
    
        world.forEach(actionbc);
        world.forEach(actionb);
        world.forEach(actionc);


        //! actions got called just the right amount of times
        EXPECT_EQ(c_count, 0);
        EXPECT_EQ(b_count, 0);
        EXPECT_EQ(bc_count, 0);
        //! 
    }
}