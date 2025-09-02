#include "EntityWorld.h"

#include <cmath>

struct CompA : public ecs::CompTag<CompA>
{
    float x;
    float y;
};

struct CompB : public ecs::CompTag<CompB>
{
    float x;
    float y;
    float z;
};

struct CompC : public ecs::CompTag<CompC>
{
    float vx;
    float vy;
    float max_vel;
};

struct CompD : public ecs::CompTag<CompD>
{
    float vx;
    float vy;
    float max_vel;
};

REGISTER(CompA)
REGISTER(CompB)
REGISTER(CompC)
REGISTER(CompD)



int main(int argc, char **argv)
{
    using namespace ecs;

    EntityWorld world;

    for(int i = 0; i < 2500; ++i)
    {
        world.addEntity(CompA{.x = rand()%100, .y = rand()%500});
    }

    for(int i = 0; i < 5000; ++i)
    {
        world.addEntity(CompA{.x = rand()%100, .y = rand()%500}, CompC{.vx = rand()%50, .vy = rand()%69, .max_vel=100.f});
    }

    float dist = 0.f;
    auto action1 = [&dist](CompA &pos){ 
        dist += std::sqrt(pos.x*pos.x + pos.y * pos.y);
    };  
    
    auto action2 = [](CompA &pos, CompC& vel){
        pos.x += vel.vx;  
        pos.y += vel.vy;  
    };
    auto action3 = [](CompC& vel){
        float speed = std::sqrt(vel.vx*vel.vx + vel.vy*vel.vy);
        if(speed > vel.max_vel)
        {
            vel.vx *= vel.max_vel / speed;
            vel.vy *= vel.max_vel / speed;
        }

    };

    for(int i = 0; i < 50000; ++i)
    {
        world.forEach(action1);
        world.forEach(action3);
        world.forEach(action2);
    }

    return (int)dist;
}
