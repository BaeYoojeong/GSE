#pragma once

#include "SceneGraph.h"
#include "WorldActor.h"
#include "WorldSociety.h"
#include "WorldMonitor.h"
#include "ChapterTwo.h"
#include "RegionDatabase.h"
#include "PlanetWorld.h"
#include "FarSimulation.h"
#include <memory>
#include <map>
#include <set>
#include <string>

namespace Ashen
{
    class FarSimulation;
    class AcceptanceChecks;

    class Game
    {
      public:
        bool Initialize();
        bool headless = false;
        std::unique_ptr<FarSimulation> farSimulation;

        std::wstring PlanetStatus() const
        {
            return PlanetWorld::Describe(Absolute(hero->position));
        }

        std::size_t CachedRegionCount() const
        {
            return regions.size();
        }

        std::uint64_t StoredRegionCount() const
        {
            return regionDatabase.StoredRegions();
        }

        void Update(float seconds);
        void Interact();
        void Give();
        void ChooseEmotion(Emotion emotion);
        void ContinueLife();
        void ClearInput();
        bool Save();
        bool CombatTarget(const WorldActor& actor) const;
        void Trade(bool buy);
        void TogglePK();
        WorldSociety society;
        ChapterTwo chapter;
        bool LevelOneComplete() const;
        bool pkEnabled = false;
        bool inputBlocked = false;
        bool Load();
        void Shutdown();
        void Notify(const std::wstring& text);

        const SceneGraph& Scene() const
        {
            return scene;
        }

        const WorldActor& Hero() const
        {
            return *hero;
        }

        WorldActor* Nearest() const;
        WorldActor* AimTarget() const;
        float MaxHealth() const;
        float ShotDamage() const;
        float ShotCooldown() const;
        float PickupRange() const;
        int ExperienceNeeded() const;
        std::wstring Lesson() const;
        static constexpr float AttackRange = 360;
        static constexpr float EnemyRespawnSeconds = 6;
        static constexpr float EnemyMaximumHealth = 72;
        static constexpr int MaxLevel = 30;
        static constexpr int MaxWeapon = 20;
        const Region* RegionAt(ChunkKey key) const;
        const Memory* MemoryAt(std::uint64_t id) const;

        Vec2 Camera() const
        {
            return camera;
        }

        bool Dead() const
        {
            return health <= 0;
        }

        bool StorageHealthy() const
        {
            return storageHealthy;
        }

        const std::wstring& StoragePath() const
        {
            return savePath;
        }

        std::uint64_t Hash(ChunkKey key, unsigned salt = 0) const;
        unsigned Biome(ChunkKey key) const;
        bool EnemySlotEnabled(ChunkKey key, int slot) const;
        bool Lit(ChunkKey key) const;
        std::wstring NatureName(Nature nature) const;
        std::wstring EmotionName(Emotion emotion) const;
        std::wstring Description(const WorldActor& actor) const;
        std::wstring LegacyDirection() const;
        std::wstring VillageDirection(int targetTown = -1) const;

        bool arrows[4] = {};
        bool attack = false;
        bool sprint = false, jumpRequested = false;
        float health = 100;
        float elapsed = 0;
        int supplies = 3;
        LifeRecord life;
        Progression growth;
        std::map<std::uint64_t, Memory> memories;
        std::wstring notice;
        std::wstring dialogue;
        ActorId conversation = 0;
        bool choiceMade = false;

      private:
        friend class FarSimulation;
        friend class AcceptanceChecks;
        friend class WorldActor;
        friend class WorldSociety;
        friend class ChapterTwo;
        friend class BellKeeperActor;
        friend class WorldMonitor;
        friend class CitizenActor;
        friend class HerbActor;
        friend class FireActor;
        friend class SettlerActor;
        friend class GraveActor;
        friend class PlayerActor;
        friend class EnemyActor;
        friend class HeirActor;
        friend class ProjectileActor;
        friend class DropActor;
        void Stream();
        void BuildChunk(ChunkKey key, ActorId root);
        void SpawnSettlement(ChunkKey key, ActorId root);
        WorldActor& Spawn(Kind kind, ChunkKey chunk, ActorId parent, Vec2 position, int slot = 0);
        void SpawnMemory(const Memory& memory);
        void Die();
        void Attack();
        void Defeat(WorldActor& enemy, bool playerReward = true);
        void UpdateEnemyRespawns(float seconds);
        void ClearProjectiles();
        void AddExperience(int amount);
        void SpawnLoot(ChunkKey key, int source);
        void CollectDrop(WorldActor& drop);
        Vec2 FarmingPosition(ChunkKey key, int slot) const;
        Vec2 EnemyPosition(ChunkKey key, int slot) const;
        bool Walkable(Vec2 point, float radius = 12) const;
        void Move(WorldActor& actor, Vec2 delta);
        bool ClearPath(Vec2 from, Vec2 to) const;
        Vec2 Relative(const Location& location) const;
        Location Absolute(Vec2 point) const;
        void Rebase();
        bool BeginConversation(WorldActor& target);
        void CloseDialogue();
        void CloseStorage();

        SceneGraph scene;
        WorldMonitor monitor;
        WorldActor* hero = nullptr;
        std::map<ChunkKey, ActorId> chunks;
        mutable std::map<ChunkKey, Region> regions;
        mutable RegionDatabase regionDatabase;
        void TrimRegions();
        std::vector<ActorId> solids;
        ChunkKey origin;
        Location checkpoint;
        Vec2 camera;
        std::uint64_t seed = 20260922;
        float attackTimer = 0;
        float immunity = 0;
        float restTimer = 0;
        float saveTimer = 0;
        float noticeTimer = 0;
        bool storageHealthy = true;
        mutable bool regionReadFailed = false;
        bool writable = true;
        bool initialized = false;
        bool dirty = false;
        std::wstring savePath;
        void* profileLock = nullptr;
    };

} // namespace Ashen
