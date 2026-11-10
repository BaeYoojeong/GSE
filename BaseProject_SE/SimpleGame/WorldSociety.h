#pragma once

#include "WorldTypes.h"
#include "TownWorld.h"
#include <array>
#include <cstddef>
#include <deque>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace Ashen
{
    class Game;
    class WorldActor;

    enum class Profession
    {
        Hunter,
        Raider,
        Merchant,
        Seeker
    };
    enum class Ambition
    {
        Vitality,
        Wealth,
        Insight
    };
    enum class Activity
    {
        Rest,
        Hunt,
        Raid,
        Trade,
        Study,
        Return,
        Dead
    };

    enum class JourneyKind
    {
        None,
        Courier,
        Returning,
        Migration
    };

    struct TownNews
    {
        double authored = -1;
        float safety = 0;
        float wealth = 0;
        float trade = 0;
        float learning = 0;
        unsigned population = 0;
        std::uint64_t murders = 0, raids = 0;
    };

    struct TownLetter
    {
        std::uint64_t id = 0;
        std::uint64_t carrier = 0;
        int source = 0;
        int destination = 0;
        double sent = 0;
        std::array<TownNews, TownWorld::Count> reports;
    };

    struct CitizenRecord
    {
        std::uint64_t id = 0;
        std::uint64_t originLife = 0;
        ChunkKey village;
        Vec2 position = {384, 420};
        Profession profession = Profession::Hunter;
        Ambition ambition = Ambition::Wealth;
        Activity activity = Activity::Rest;
        float raidWeight = 0;
        float health = 90;
        float maximumHealth = 90;
        int coins = 24;
        int goods = 2;
        int insight = 0;
        float actionTimer = 0;
        float attackTimer = 0;
        float hostileSeconds = 0;
        std::uint64_t target = 0;
        std::uint64_t lastGiftLife = 0;
        std::uint64_t lastTalkLife = 0;
        Activity intention = Activity::Rest;
        float raidCooldown = 0;
        float reviveProtection = 0;
        std::uint64_t deaths = 0;
        unsigned houseLot = 0;
        JourneyKind journeyKind = JourneyKind::None;
        int destinationTown = -1;
        unsigned pendingLot = 0;
        std::uint64_t mailId = 0;
        float migrationCooldown = 180;
        float transitAge = 0;
        float simulationDebt = 0;
        // Derived decision diagnostics, not authoritative save data.
        float decisionTimer = 0;
        float intentionAge = 0;
        int quarrySlot = -1;
        Activity goalActivity = Activity::Dead;
        Vec2 activityGoal = {};
        std::array<float, 5> decisionScores = {};
        // Presentation follows real attacks; these observations are not saved.
        float attackVisualSeconds = 0;
        Vec2 attackVisualTarget = {};
        float hitVisualSeconds = 0;
        unsigned journey = 0;
        unsigned huntingTrip = 0;
        float travelCommitSeconds = 0;
        unsigned destinationsReached = 0;
        // Session observations: only actual transfers increment these counters.
        std::uint64_t successfulRaids = 0;
        std::uint64_t stolenCoins = 0;
        std::uint64_t lastRaidTarget = 0;
        int lastRaidAmount = 0;
        float raidResultSeconds = 0;
        float goalDwellSeconds = 0;
        bool marketDuty = false;
        Vec2 motionVelocity = {};
        Vec2 motionOrigin = {};
    };

    struct HuntingGround
    {
        ChunkKey village;
        int slot = 0;
        static constexpr float MaximumHealth = 90;
        float health = MaximumHealth;
        float respawnSeconds = 0;
        float attackTimer = 0;
        // Runtime placement is rebuilt on load; economic and cooldown save data stay unchanged.
        Vec2 position = {};
        unsigned spawnVariant = 0;
        bool placementPending = true;
        bool spawnDeferred = false;
        float hitVisualSeconds = 0;
    };

    // Runtime observations do not alter the authoritative world save format.
    struct SocietyEvent
    {
        std::uint64_t id = 0;
        double timeSeconds = 0;
        std::string type;
        std::string severity;
        std::string category;
        bool hasVillage = false;
        ChunkKey village;
        std::uint64_t actor = 0;
        std::uint64_t target = 0;
        int amount = 0;
        std::wstring message;
        bool confirmed = true;
        unsigned witnessCount = 0;
        std::uint64_t sourceEvent = 0;
        std::uint64_t originalTarget = 0;
        std::wstring originalMessage;
    };

    struct IncidentSummary
    {
        std::uint64_t murders = 0;
        std::uint64_t raids = 0;
        std::uint64_t trades = 0;
        double lastIncidentSeconds = -1;
    };

    // Persistent simulation owns facts; visible actors only mirror them.
    // Loaded residents update at 10 Hz; distant residents retain records and advance at 1 Hz.
    class WorldSociety
    {
      public:
        static constexpr int HuntingSlots = 6;
        static constexpr float QuarryRespawnSeconds = 4;
        static constexpr float AttackVisualDuration = .45f;
        static constexpr float ReviveProtectionSeconds = 3;
        static constexpr std::size_t EventCapacity = 512;
        static constexpr int RecentEventSeconds = 120;

        bool ExpandedWorld() const
        {
            return expandedWorld;
        }

        static const wchar_t* JourneyName(JourneyKind kind);
        std::array<std::array<TownNews, TownWorld::Count>, TownWorld::Count> knowledge;
        std::map<std::uint64_t, TownLetter> letters;
        std::uint64_t lettersDelivered = 0;
        std::uint64_t migrationsCompleted = 0;
        void Initialize(const std::map<std::uint64_t, Memory>& memories);
        void AddHeir(const Memory& memory);
        void Update(Game& game, float seconds);
        void SpawnVillage(Game& game, ChunkKey village, ActorId parent);
        void SyncActor(WorldActor& actor) const;
        bool Read(std::istream& in, std::uint64_t currentLife);
        void Write(std::ostream& out) const;
        bool DamageCitizen(Game& game, std::uint64_t id, float damage);
        bool DamageQuarry(Game& game, ChunkKey village, int slot, float damage);
        bool Trade(Game& game, std::uint64_t id, bool buy);
        bool Give(Game& game, std::uint64_t id);
        bool IsHostile(std::uint64_t id) const;
        const CitizenRecord* Find(std::uint64_t id) const;
        const HuntingGround* Quarry(ChunkKey village, int slot) const;
        std::wstring Describe(std::uint64_t id) const;
        std::wstring Ledger(unsigned page) const;
        std::wstring Summary() const;
        unsigned PageCount() const;
        IncidentSummary RecentIncidents(ChunkKey village) const;
        bool IsMarketKeeper(const CitizenRecord& npc) const;
        static Location Locate(ChunkKey home, Vec2 point);
        static Vec2 InFrame(ChunkKey from, Vec2 point, ChunkKey to);
        static Vec2 MarketPosition(unsigned site);
        bool IsVillage(ChunkKey chunk) const;
        bool IsAtMarket(const CitizenRecord& npc) const;
        static bool IsFoundingVillage(ChunkKey chunk);
        static Vec2 QuarryPosition(int slot, unsigned variant = 0);
        static const wchar_t* RaidPhase(const CitizenRecord& npc);
        static const wchar_t* JobName(Profession profession);
        static const wchar_t* GoalName(Ambition ambition);
        static const wchar_t* ActivityName(Activity activity);

        std::map<std::uint64_t, CitizenRecord> citizens;
        std::map<std::pair<ChunkKey, int>, HuntingGround> hunting;
        int playerCoins = 20;
        int notoriety = 0;
        double simulationSeconds = 0;
        std::uint64_t trades = 0;
        std::uint64_t raids = 0;
        std::uint64_t hunts = 0;
        std::deque<std::wstring> recentEvents;
        std::deque<SocietyEvent> monitorEvents;

      private:
        friend class FarSimulation;
        friend class WorldMonitor;
        bool backgroundSimulation = false;
        std::vector<SocietyEvent> backgroundEvents;
        bool expandedWorld = false;
        bool readingLegacyTowns = false;

        int IndexDuringRead(ChunkKey key) const
        {
            return readingLegacyTowns ? TownWorld::LegacyIndex(key) : TownWorld::Index(key);
        }

        ChunkKey CenterDuringRead(unsigned index) const
        {
            return readingLegacyTowns ? TownWorld::LegacyCenter(index) : TownWorld::Towns()[index].center;
        }

        double nextNetworkUpdate = 0;
        std::uint64_t nextLetter = 1;
        unsigned networkRound = 0;
        unsigned nearSteps = 0, farSteps = 0;
        unsigned VacantLot(ChunkKey town) const;
        void ExpandTowns();
        void NetworkTick();
        bool TravelMission(Game& game, CitizenRecord& npc, float dt);
        void CancelMission(CitizenRecord& npc);
        bool ReadNetwork(std::istream& in);
        void WriteNetwork(std::ostream& out) const;
        void SyncHomes(Game& game);
        void Tick(Game& game, float dt);
        void Think(Game& game, CitizenRecord& npc, float dt);
        void EnsureGrounds(ChunkKey village);
        void SelectActivity(CitizenRecord& npc, bool playerCanBeRobbed);
        HuntingGround* FindQuarry(CitizenRecord& npc, bool reserve = true);
        CitizenRecord* FindRaidTarget(const CitizenRecord& npc);
        CitizenRecord* FindTradePartner(const CitizenRecord& npc, bool& buying, bool requireMarket = true);
        void Revive(CitizenRecord& npc, const char* reason);
        void Log(const char* event, std::uint64_t actor, std::uint64_t target, int amount,
                 const std::wstring& text, const ChunkKey* village = nullptr);
        std::uint64_t RecordEvent(const char* type, std::uint64_t actor, std::uint64_t target, int amount,
                                  const std::wstring& message, const ChunkKey* village = nullptr,
                                  unsigned witnesses = 0, std::uint64_t sourceEvent = 0);
        void Kill(Game& game, CitizenRecord& victim, CitizenRecord* killer, bool player,
                  std::uint64_t attacker = 0);
        void SetDestination(CitizenRecord& npc, Vec2 goal, Activity activity);
        void RecordRaid(CitizenRecord& npc, std::uint64_t target, int amount);
        bool Travel(Game& game, CitizenRecord& npc, Vec2 goal, float dt);
        Vec2 ActivityPosition(const CitizenRecord& npc, Activity activity) const;
        Vec2 HuntingPosition(const CitizenRecord& npc, const HuntingGround& ground) const;
        void MoveCitizen(Game& game, CitizenRecord& npc, Vec2 delta);
        void ResolveCrowding(Game& game, float dt);
        bool PlaceQuarry(Game& game, HuntingGround& ground, bool respawn);
        void ReportSpacing(Game& game) const;
        bool Exchange(CitizenRecord& seller, CitizenRecord& buyer, int price);
        bool DefendAgainstMonsters(Game& game, CitizenRecord& npc);
        void ShowAttack(CitizenRecord& npc, Vec2 target);
        void SyncActors(Game& game);
        static float CitizenDamage(const CitizenRecord& npc);
        static float CitizenCooldown(const CitizenRecord& npc);

        std::vector<WorldActor*> nearbyEnemies;
        std::uint64_t attacksSinceReport = 0;
        std::uint64_t monsterKillsSinceReport = 0;
        std::uint64_t raidsSinceReport = 0;

        struct IncidentBucket
        {
            double second = -1;
            IncidentSummary summary;
        };

        // Kept separately from the event ring so frequent hunting cannot erase village incident counts.
        std::map<ChunkKey, std::array<IncidentBucket, RecentEventSeconds>> incidentBuckets;
        std::uint64_t lastMonitorEvent = 0;
        float accumulator = 0;
        float reportTimer = 0;
        double updateMilliseconds = 0;
        double peakUpdateMilliseconds = 0;
    };
} // namespace Ashen
