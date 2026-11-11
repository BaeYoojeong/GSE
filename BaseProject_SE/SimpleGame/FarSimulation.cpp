#include "FarSimulation.h"
#include "DarkAgeGame.h"
#include "WorldActors.h"
#include "RenderProfiler.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <set>

namespace Ashen
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        struct Hash
        {
            std::uint64_t value = 14695981039346656037ull;

            template <class T> void Add(const T& data)
            {
                const auto* bytes = reinterpret_cast<const unsigned char*>(&data);
                for (size_t i = 0; i < sizeof(data); ++i)
                {
                    value = (value ^ bytes[i]) * 1099511628211ull;
                }
            }
        };

        std::uint64_t Fingerprint(const CitizenRecord& n)
        {
            Hash h;
#define FIELD(member) h.Add(n.member)
            FIELD(id);
            FIELD(originLife);
            FIELD(village);
            FIELD(position);
            FIELD(profession);
            FIELD(ambition);
            FIELD(activity);
            FIELD(raidWeight);
            FIELD(health);
            FIELD(maximumHealth);
            FIELD(coins);
            FIELD(goods);
            FIELD(insight);
            FIELD(actionTimer);
            FIELD(attackTimer);
            FIELD(hostileSeconds);
            FIELD(target);
            FIELD(lastGiftLife);
            FIELD(lastTalkLife);
            FIELD(intention);
            FIELD(raidCooldown);
            FIELD(reviveProtection);
            FIELD(deaths);
            FIELD(houseLot);
            FIELD(journeyKind);
            FIELD(destinationTown);
            FIELD(pendingLot);
            FIELD(mailId);
            FIELD(migrationCooldown);
            FIELD(transitAge);
            FIELD(decisionTimer);
            FIELD(intentionAge);
            FIELD(quarrySlot);
            FIELD(goalActivity);
            FIELD(activityGoal);
            FIELD(decisionScores);
            FIELD(attackVisualSeconds);
            FIELD(attackVisualTarget);
            FIELD(hitVisualSeconds);
            FIELD(journey);
            FIELD(huntingTrip);
            FIELD(travelCommitSeconds);
            FIELD(destinationsReached);
            FIELD(successfulRaids);
            FIELD(stolenCoins);
            FIELD(lastRaidTarget);
            FIELD(lastRaidAmount);
            FIELD(raidResultSeconds);
            FIELD(goalDwellSeconds);
            FIELD(marketDuty);
            FIELD(marketCooldown);
            FIELD(marketVisitSeconds);
            FIELD(marketTripSeconds);
#undef FIELD
            return h.value;
        }

        std::uint64_t Fingerprint(const HuntingGround& g)
        {
            Hash h;
            h.Add(g.village);
            h.Add(g.slot);
            h.Add(g.health);
            h.Add(g.position);
            h.Add(g.spawnVariant);
            h.Add(g.placementPending);
            h.Add(g.spawnDeferred);
            return h.value;
        }

        std::uint64_t Fingerprint(const TownNews& n)
        {
            Hash h;
            h.Add(n.authored);
            h.Add(n.safety);
            h.Add(n.wealth);
            h.Add(n.trade);
            h.Add(n.learning);
            h.Add(n.population);
            h.Add(n.murders);
            h.Add(n.raids);
            return h.value;
        }

        std::uint64_t Fingerprint(const TownLetter& letter)
        {
            Hash h;
            h.Add(letter.id);
            h.Add(letter.carrier);
            h.Add(letter.source);
            h.Add(letter.destination);
            h.Add(letter.sent);
            for (const auto& report : letter.reports)
            {
                h.Add(Fingerprint(report));
            }
            return h.value;
        }
    } // namespace

    struct FarSimulation::Impl
    {
        struct Job
        {
            std::unique_ptr<WorldSociety> before, after;
            std::vector<std::pair<std::uint64_t, float>> residents;
            std::map<std::uint64_t, Memory> memories;
            ChunkKey origin;
            Vec2 player;
            LifeRecord life;
            std::uint64_t seed = 0;
            double milliseconds = 0;
            bool good = false;
            std::string failure;
            std::vector<RenderProfiler::Diagnostic> diagnostics;
        };

        std::mutex mutex;
        std::condition_variable condition;
        std::unique_ptr<Job> queued, ready;
        bool working = false;
        std::atomic<bool> stopping{false};
        std::thread thread;
        Statistics statistics;
        std::uint64_t lastScheduled = 0;
    };

    FarSimulation::FarSimulation() : impl(std::make_unique<Impl>())
    {
        impl->thread = std::thread([this] { Run(); });
    }

    FarSimulation::~FarSimulation()
    {
        impl->stopping = true;
        impl->condition.notify_all();
        if (impl->thread.joinable())
        {
            impl->thread.join();
        }
    }

    FarSimulation::Statistics FarSimulation::Stats() const
    {
        return impl->statistics;
    }

    bool FarSimulation::WaitReady(unsigned milliseconds)
    {
        std::unique_lock<std::mutex> lock(impl->mutex);
        return impl->condition.wait_for(lock, std::chrono::milliseconds(milliseconds),
                                        [&] { return impl->ready || (!impl->working && !impl->queued); });
    }

    void FarSimulation::Submit(Game& game)
    {
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            if (impl->working || impl->queued || impl->ready || impl->stopping)
            {
                return;
            }
        }
        auto job = std::make_unique<Impl::Job>();
        auto it = game.society.citizens.upper_bound(impl->lastScheduled);
        for (size_t visited = 0; visited < game.society.citizens.size(); ++visited)
        {
            if (it == game.society.citizens.end())
            {
                it = game.society.citizens.begin();
            }
            const auto& pair = *it++;
            const auto& npc = pair.second;
            if (npc.simulationDebt >= 1 &&
                !game.chunks.count(WorldSociety::Locate(npc.village, npc.position).chunk))
            {
                job->residents.emplace_back(npc.id, (std::min)(1.0f, npc.simulationDebt));
                impl->lastScheduled = npc.id;
                if (job->residents.size() >= 64)
                {
                    break;
                }
            }
        }
        if (job->residents.empty())
        {
            return;
        }
        job->before = std::make_unique<WorldSociety>(game.society);
        job->memories = game.memories;
        job->origin = game.origin;
        job->player = game.hero->position;
        job->life = game.life;
        job->seed = game.seed;
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->queued = std::move(job);
        }
        impl->condition.notify_one();
    }

    void FarSimulation::Run()
    {
        for (;;)
        {
            std::unique_ptr<Impl::Job> job;
            {
                std::unique_lock<std::mutex> lock(impl->mutex);
                impl->condition.wait(lock, [&] { return impl->queued || impl->stopping; });
                if (impl->stopping)
                {
                    return;
                }
                job = std::move(impl->queued);
                impl->working = true;
            }
            const auto started = Clock::now();
            RenderProfiler::BeginCapture(job->diagnostics);
            try
            {
                Game worker;
                worker.headless = true;
                worker.seed = job->seed;
                worker.origin = job->origin;
                worker.life = job->life;
                worker.memories = job->memories;
                worker.health =
                    0; // Worker cannot perform interactions with the player's authoritative state.
                worker.hero = &worker.scene.Create<PlayerActor>(0, worker);
                worker.hero->position = job->player;
                worker.society = *job->before;
                worker.society.backgroundSimulation = true;
                worker.society.backgroundEvents.clear();
                worker.society.nearbyEnemies.clear();
                for (const auto& step : job->residents)
                {
                    if (impl->stopping)
                    {
                        break;
                    }
                    worker.society.Think(worker, worker.society.citizens.at(step.first), step.second);
                }
                job->after = std::make_unique<WorldSociety>(std::move(worker.society));
                job->good = !impl->stopping;
            }
            catch (const std::exception& error)
            {
                job->good = false;
                job->failure = error.what();
            }
            RenderProfiler::EndCapture();
            job->milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
            {
                std::lock_guard<std::mutex> lock(impl->mutex);
                impl->ready = std::move(job);
                impl->working = false;
            }
            impl->condition.notify_all();
        }
    }

    bool FarSimulation::Poll(Game& game)
    {
        std::unique_ptr<Impl::Job> job;
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            job = std::move(impl->ready);
        }
        if (!job)
        {
            return false;
        }
        const auto started = Clock::now();
        impl->statistics.workerMilliseconds += job->milliseconds;
        auto& live = game.society;
        bool valid = job->good;
        std::string rejection = job->good ? "" : ("cancelled_or_exception:" + job->failure);
        if (valid)
        {
            const auto& before = *job->before;
            const auto& after = *job->after;
            for (const auto& step : job->residents)
            {
                const auto current = live.citizens.find(step.first);
                if (current == live.citizens.end() ||
                    game.chunks.count(
                        WorldSociety::Locate(current->second.village, current->second.position).chunk))
                {
                    valid = false;
                    rejection = "scheduled_resident_became_visible:" + std::to_string(step.first);
                    break;
                }
            }
            for (const auto& pair : after.citizens)
            {
                const auto original = before.citizens.find(pair.first);
                const auto current = live.citizens.find(pair.first);
                if (original == before.citizens.end() || current == live.citizens.end())
                {
                    valid = false;
                    rejection = "resident_missing:" + std::to_string(pair.first);
                    break;
                }
                if (Fingerprint(original->second) == Fingerprint(pair.second))
                {
                    continue;
                }
                if (Fingerprint(current->second) != Fingerprint(original->second))
                {
                    valid = false;
                    rejection = "resident_changed:" + std::to_string(pair.first);
                    break;
                }
                const auto location = WorldSociety::Locate(pair.second.village, pair.second.position);
                if (game.chunks.count(location.chunk) && !game.Walkable(game.Relative(location), 14))
                {
                    valid = false;
                    rejection = "loaded_collision:" + std::to_string(pair.first);
                    break;
                }
            }
            for (const auto& pair : after.hunting)
            {
                const auto& original = before.hunting.at(pair.first);
                const bool changed = Fingerprint(original) != Fingerprint(pair.second) ||
                                     original.attackTimer != pair.second.attackTimer;
                if (!changed)
                {
                    continue;
                }
                const auto current = live.hunting.find(pair.first);
                const auto location = WorldSociety::Locate(pair.second.village, pair.second.position);
                if (current == live.hunting.end() || Fingerprint(current->second) != Fingerprint(original) ||
                    game.chunks.count(location.chunk))
                {
                    valid = false;
                    rejection = "hunting_ground_changed_or_visible";
                    break;
                }
            }
            for (unsigned a = 0; a < TownWorld::Count; ++a)
            {
                for (unsigned b = 0; b < TownWorld::Count; ++b)
                {
                    if (Fingerprint(before.knowledge[a][b]) != Fingerprint(after.knowledge[a][b]) &&
                        Fingerprint(before.knowledge[a][b]) != Fingerprint(live.knowledge[a][b]))
                    {
                        valid = false;
                        rejection = "town_knowledge_changed";
                    }
                }
            }
            for (const auto& pair : before.letters)
            {
                const auto next = after.letters.find(pair.first);
                if (next != after.letters.end() && Fingerprint(next->second) == Fingerprint(pair.second))
                {
                    continue;
                }
                const auto current = live.letters.find(pair.first);
                if (current == live.letters.end() || Fingerprint(current->second) != Fingerprint(pair.second))
                {
                    valid = false;
                    rejection = "letter_changed:" + std::to_string(pair.first);
                }
            }
        }
        if (!valid)
        {
            ++impl->statistics.rejected;
            RenderProfiler::Event("npc_worker_rejected", rejection + "; retained_debt=1", 0,
                                  job->milliseconds);
            return false;
        }
        const auto& before = *job->before;
        const auto& after = *job->after;
        for (const auto& pair : after.citizens)
        {
            if (Fingerprint(pair.second) == Fingerprint(before.citizens.at(pair.first)))
            {
                continue;
            }
            auto& current = live.citizens.at(pair.first);
            const float debt = current.simulationDebt;
            const Vec2 origin = current.motionOrigin, velocity = current.motionVelocity;
            current = pair.second;
            current.simulationDebt = debt;
            current.motionOrigin = origin;
            current.motionVelocity = velocity;
        }
        for (const auto& step : job->residents)
        {
            auto& current = live.citizens.at(step.first);
            current.simulationDebt = (std::max)(0.0f, current.simulationDebt - step.second);
        }
        for (const auto& pair : after.hunting)
        {
            const auto& previous = before.hunting.at(pair.first);
            if (Fingerprint(previous) != Fingerprint(pair.second) ||
                previous.attackTimer != pair.second.attackTimer)
            {
                live.hunting.at(pair.first) = pair.second;
            }
        }
        for (unsigned a = 0; a < TownWorld::Count; ++a)
        {
            for (unsigned b = 0; b < TownWorld::Count; ++b)
            {
                if (Fingerprint(before.knowledge[a][b]) != Fingerprint(after.knowledge[a][b]))
                {
                    live.knowledge[a][b] = after.knowledge[a][b];
                }
            }
        }
        for (const auto& pair : before.letters)
        {
            if (!after.letters.count(pair.first))
            {
                live.letters.erase(pair.first);
            }
        }
        live.hunts += after.hunts - before.hunts;
        live.raids += after.raids - before.raids;
        live.trades += after.trades - before.trades;
        live.lettersDelivered += after.lettersDelivered - before.lettersDelivered;
        live.migrationsCompleted += after.migrationsCompleted - before.migrationsCompleted;
        live.attacksSinceReport += after.attacksSinceReport - before.attacksSinceReport;
        live.monsterKillsSinceReport += after.monsterKillsSinceReport - before.monsterKillsSinceReport;
        live.raidsSinceReport += after.raidsSinceReport - before.raidsSinceReport;
        std::map<std::uint64_t, std::uint64_t> eventIds;
        for (const auto& event : after.backgroundEvents)
        {
            const auto source =
                eventIds.count(event.sourceEvent) ? eventIds[event.sourceEvent] : event.sourceEvent;
            const auto id = live.RecordEvent(
                event.type.c_str(), event.actor, event.originalTarget, event.amount, event.originalMessage,
                event.hasVillage ? &event.village : nullptr, event.witnessCount, source);
            eventIds[event.id] = id;
            live.recentEvents.push_front(event.message);
            while (live.recentEvents.size() > 4)
            {
                live.recentEvents.pop_back();
            }
        }
        for (const auto& event : job->diagnostics)
        {
            if (event.category != "society")
            {
                RenderProfiler::Event(event.category.c_str(), event.detail, event.bytes, event.milliseconds);
            }
        }
        live.farSteps += static_cast<unsigned>(job->residents.size());
        ++impl->statistics.accepted;
        impl->statistics.residents += job->residents.size();
        const double commit = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        impl->statistics.commitMilliseconds += commit;
        RenderProfiler::Event("npc_worker_batch",
                              "residents=" + std::to_string(job->residents.size()) + " worker_ms=" +
                                  std::to_string(job->milliseconds) + " commit_ms=" + std::to_string(commit),
                              0, job->milliseconds);
        game.dirty = true;
        return true;
    }
} // namespace Ashen
