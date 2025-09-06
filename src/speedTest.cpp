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

#include <benchmark/benchmark.h>
#include <unordered_set>
using namespace ecs;

static void BM_EntityCreation(benchmark::State &state)
{

    std::vector<int> ids;
    ids.reserve(5009);
    for (auto _ : state)
    {
        EntityWorld world;

        for (int i = 0; i < state.range(0); ++i)
        {
            world.addEntity(CompA{}, CompB{.x = 5});
        }
        ids.clear();
    }
}
BENCHMARK(BM_EntityCreation)->Range(100, 9999);

static void BM_action1(benchmark::State &state)
{

    EntityWorld world;
    for (int i = 0; i < 3000; ++i)
    {
        world.addEntity(CompA{.x = rand() % 100, .y = rand() % 500});
    }

    for (int i = 0; i < state.range(0); ++i)
    {
        world.addEntity(CompA{.x = rand() % 100, .y = rand() % 500}, CompC{.vx = rand() % 50, .vy = rand() % 69, .max_vel = 100.f});
    }
    float dist = 0.f;
    auto action1 = [&dist](CompA &pos)
    {
        dist += std::sqrt(pos.x * pos.x + pos.y * pos.y);
    };

    for (auto _ : state)
    {
        world.forEach(action1);
    }
}
BENCHMARK(BM_action1)->Range(100, 6000);

static void BM_action2(benchmark::State &state)
{

    EntityWorld world;

    for (int i = 0; i < state.range(0); ++i)
    {
        world.addEntity(CompA{.x = rand() % 100, .y = rand() % 500}, CompC{.vx = rand() % 50, .vy = rand() % 69, .max_vel = 100.f});
    }
    float dist = 0.f;
    auto action2 = [](CompA &pos, CompC &vel)
    {
        pos.x += vel.vx;
        pos.y += vel.vy;
    };
    for (auto _ : state)
    {
        world.forEach(action2);
    }
}
BENCHMARK(BM_action2)->Range(100, 6000);

static void BM_action3(benchmark::State &state)
{

    EntityWorld world;

    for (int i = 0; i <  6000; ++i)
    {
        world.addEntity(CompA{.x = rand() % 100, .y = rand() % 500}, CompC{.vx = rand() % 50, .vy = rand() % 69, .max_vel = 100.f});
    }
    float dist = 0.f;

    auto action3 = [](CompC &vel)
    {
        float speed = std::sqrt(vel.vx * vel.vx + vel.vy * vel.vy);
        float ratio = speed / vel.max_vel;
        // if (speed > vel.max_vel)
        {
            vel.vx = ratio > 1.f ? vel.vx / ratio : vel.vx;
            vel.vy = ratio > 1.f ? vel.vy / ratio : vel.vy;
        }
    };

    for (auto _ : state)
    {
        world.forEach(action3);
    }
}
BENCHMARK(BM_action3);

static void BM_action4(benchmark::State &state)
{

    EntityWorld world;

    for (int i = 0; i <  6000; ++i)
    {
        world.addEntity(CompA{.x = rand() % 100, .y = rand() % 500}, CompC{.vx = rand() % 50, .vy = rand() % 69, .max_vel = 100.f});
    }
    float dist = 0.f;

    auto action3 = [](CompC &vel)
    {
        float speed2 = vel.vx * vel.vx + vel.vy * vel.vy;
        float ratio = speed2 / (vel.max_vel * vel.max_vel);
        if (ratio > 1.f)
        {
            vel.vx /= std::sqrt(ratio);
            vel.vy /= std::sqrt(ratio);
        }
    };

    for (auto _ : state)
    {
        world.forEach(action3);
    }
}
BENCHMARK(BM_action4);

BENCHMARK_MAIN();

// int main(int argc, char **argv)
// {

//     EntityWorld world;

//     for(int i = 0; i < 3500; ++i)
//     {
//         world.addEntity(CompA{.x = rand()%100, .y = rand()%500});
//     }

//     for(int i = 0; i < 5000; ++i)
//     {
//         world.addEntity(CompA{.x = rand()%100, .y = rand()%500}, CompC{.vx = rand()%50, .vy = rand()%69, .max_vel=100.f});
//     }

//     float dist = 0.f;
//     auto action1 = [&dist](CompA &pos){
//         dist += std::sqrt(pos.x*pos.x + pos.y * pos.y);
//     };

//     auto action2 = [](CompA &pos, CompC& vel){
//         pos.x += vel.vx;
//         pos.y += vel.vy;
//     };
//     auto action3 = [](CompC& vel){
//         float speed = std::sqrt(vel.vx*vel.vx + vel.vy*vel.vy);
//         if(speed > vel.max_vel)
//         {
//             vel.vx *= vel.max_vel / speed;
//             vel.vy *= vel.max_vel / speed;
//         }

//     };

//     // for(int i = 0; i < 50000; ++i)
//     // {
//     //     world.forEach(action1);
//     //     world.forEach(action3);
//     //     world.forEach(action2);
//     // }

//     return (int)dist;
// }
