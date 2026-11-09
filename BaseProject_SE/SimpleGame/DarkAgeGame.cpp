#include "DarkAgeGame.h"
#include "WorldActors.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace Ashen
{
    bool Game::Initialize()
    {
        if (initialized)
        {
            Shutdown();
        }

        scene.Clear();
        hero = nullptr;
        chunks.clear();
        regions.clear();
        memories.clear();
        society = {};
        chapter = {};
        pkEnabled = false;
        inputBlocked = false;
        seed = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
        origin = {};
        checkpoint = {};
        life = {};
        growth = {};
        health = MaxHealth();
        supplies = 3;
        camera = {};
        writable = storageHealthy = true;
        regionReadFailed = false;
        elapsed = attackTimer = immunity = restTimer = saveTimer = noticeTimer = 0;
        dialogue.clear();
        CloseDialogue();
        ClearInput();
        hero = &scene.Create<PlayerActor>(0, *this);
        hero->position = checkpoint.local;
        regions[{}].lit = true;
        Load();
        society.Initialize(memories);
        chapter.Update(*this);
        camera = hero->position;
        Stream();
        initialized = true;
        try
        {
            farSimulation = std::make_unique<FarSimulation>();
        }
        catch (const std::exception& error)
        {
            farSimulation.reset();
            RenderProfiler::Event("npc_worker_unavailable",
                                  std::string(error.what()) + "; using_main_thread_fallback=1");
        }
        dirty = true;

        if (!headless)
        {
            monitor.Start(*this);
        }

        if (writable)
        {
            Save();
        }

        Notify(Dead()               ? L"한 생명이 끝났고, 그 삶을 물려받은 누군가가 태어났습니다."
               : LevelOneComplete() ? chapter.Objective()
                                    : L"마을에서 깨어났습니다. V 주민 현황 · E 대화 · B/N 거래 · P PK 전환");
        return true;
    }

    void Game::Notify(const std::wstring& text)
    {
        notice = text;
        noticeTimer = 6;
    }

    void Game::ClearInput()
    {
        for (bool& key : arrows)
        {
            key = false;
        }

        attack = false;
        sprint = jumpRequested = false;

        if (hero)
        {
            hero->moving = false;
        }
    }

    void Game::CloseDialogue()
    {
        dialogue.clear();
        conversation = 0;
        choiceMade = false;
    }

    void Game::Update(float seconds)
    {
        const float societySeconds = (std::max)(0.0f, seconds);
        seconds = (std::max)(0.0f, (std::min)(seconds, .05f));
        elapsed += seconds;
        noticeTimer -= seconds;

        if (noticeTimer <= 0)
        {
            notice.clear();
        }

        saveTimer += seconds;
        const bool wasAlive = !Dead();
        immunity = (std::max)(0.0f, immunity - seconds);
        society.Update(*this, societySeconds);
        UpdateEnemyRespawns((std::min)(societySeconds, .5f));
        if (wasAlive && Dead())
        {
            Die();
        }

        if (Dead())
        {
            scene.Update(seconds);
            if (!headless)
            {
                monitor.Update(*this);
            }
            if (dirty && saveTimer > 5)
            {
                Save();
            }

            return;
        }

        if (!dialogue.empty() || inputBlocked)
        {
            ClearInput();
        }

        attackTimer = (std::max)(0.0f, attackTimer - seconds);
        restTimer = (std::max)(0.0f, restTimer - seconds);

        if (growth.magnetSeconds > 0)
        {
            growth.magnetSeconds = (std::max)(0.0f, growth.magnetSeconds - seconds);
            dirty = true;
        }

        scene.Update(seconds);

        if (health <= 0)
        {
            Die();
        }

        chapter.Update(*this);
        const float amount = 1 - std::exp(-seconds * 9);
        camera.x += (hero->position.x - camera.x) * amount;
        camera.y += (hero->position.y - camera.y) * amount;

        if (dirty && saveTimer >= 5)
        {
            Save();
        }
        if (!headless)
        {
            monitor.Update(*this);
        }
    }

    void Game::Die()
    {
        health = 0;
        ClearProjectiles();
        chapter.ResetBattle(*this, "death");

        if (!memories.count(life.id))
        {
            Memory memory;
            memory.life = life;
            memory.home = Absolute(hero->position);
            memory.nature = life.gifts >= 2                              ? Nature::Giver
                            : life.kills >= 2                            ? Nature::Guardian
                            : (life.fear > life.hope || life.rests >= 3) ? Nature::Cautious
                                                                         : Nature::Wanderer;
            memory.emotion = life.fear > life.hope && life.fear >= life.anger ? Emotion::Fear
                             : life.anger > life.hope                         ? Emotion::Anger
                                                                              : Emotion::Hope;
            memories.emplace(life.id, memory);
            society.AddHeir(memory);
            SpawnMemory(memory);
        }

        ClearInput();
        CloseDialogue();
        dirty = true;
        Save();
        Notify(L"당신의 삶을 이어받은 누군가가 이곳에서 눈을 떴습니다.");
    }

    void Game::ContinueLife()
    {
        if (!Dead())
        {
            return;
        }

        // Death and its NPC must be durably committed before another life can begin.
        if (!Save())
        {
            Notify(L"전승 기록을 저장하지 못했습니다. 저장 위치를 확인한 뒤 R로 다시 시도하세요.");
            return;
        }

        const std::uint64_t next = life.id + 1;
        life = {};
        life.id = next;
        health = MaxHealth();
        pkEnabled = false;
        hero->motion = hero->previousMotion = WorldActor::Motion::Idle;
        hero->motionSeconds = hero->previousMotionSeconds = hero->jumpRemaining = hero->turnRemaining = 0;
        hero->blendSeconds = 1;
        origin = checkpoint.chunk;
        hero->position = checkpoint.local;
        camera = hero->position;
        immunity = 3;
        attackTimer = 0;
        restTimer = 0;
        ClearInput();
        Stream();
        dirty = true;
        Save();
        Notify(L"새 여행이 시작됩니다. 이전 삶에서 태어난 사람과, 되살린 불씨는 세계에 남아 있습니다.");
    }

    void Game::Shutdown()
    {
        if (farSimulation)
        {
            farSimulation->WaitReady(250);
            farSimulation->Poll(*this);
            farSimulation.reset();
        }
        monitor.Stop();
        if (initialized)
        {
            Save();
        }

        initialized = false;
        scene.Clear();
        chunks.clear();
        solids.clear();
        hero = nullptr;
        CloseStorage();
    }
} // namespace Ashen
