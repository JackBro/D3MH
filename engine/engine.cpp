#include "engine.h"

#include "process/memoryreader.h"

Engine::~Engine()
{
    delete navMesh;
}

Engine *Engine::getInstance()
{
    static Engine instance;
    return &instance;
}

void Engine::update()
{
    memoryReader->read(&localData, (void *)Addr_LocalData, sizeof(D3::LocalData));
    memoryReader->read(&ApplicationLoopCount, (void *)Addr_ApplicationLoopCount, sizeof(int));

    if (isInGame()) {
        if (nav_mesh_timer.start_or_elapsed() > 0.3) {
            navMesh->update();
            nav_mesh_timer.start();
        }
    }
    else {
        navMesh->clear();
    }
}

bool Engine::isInGame()
{
    return localData.x04_IsNotInGame != 0xCD && localData.x00_IsActorCreated == 1;
}

Engine::Engine():
    navMesh(new D3::NavMesh),
    memoryReader(MemoryReader::instance())
{
}

