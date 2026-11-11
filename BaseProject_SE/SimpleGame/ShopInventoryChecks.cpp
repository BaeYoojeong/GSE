#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "AcceptanceChecks.h"
#include "DarkAgeGame.h"
#include <filesystem>
#include <sstream>

namespace Ashen
{
    void AcceptanceChecks::InventoryAndMarkets(Game& game,
                                               const std::function<void(bool, const char*)>& require)
    {
        Inventory bag;
        require(bag.Quantity(ItemId::TravelerStaff) == 1 && !bag.Equip(ItemId::EmberStaff),
                "starting_weapon_exists_and_unowned_equipment_is_rejected");
        require(bag.Add(ItemId::IronStaff) && bag.Equip(ItemId::IronStaff) && bag.WeaponDamage() > 16 &&
                    !bag.Add(ItemId::IronStaff),
                "unique_weapons_have_different_damage");
        require(bag.Add(ItemId::HealingPotion, 99) && !bag.Add(ItemId::HealingPotion) &&
                    !bag.Add(ItemId::Supplies) && !bag.Equip(ItemId::HealingPotion),
                "inventory_limits_and_item_categories_are_enforced");
        std::ostringstream encoded;
        bag.Write(encoded);
        Inventory restored;
        std::istringstream decoded(encoded.str());
        require(restored.Read(decoded) && restored.Equipped() == bag.Equipped() &&
                    restored.Quantity(ItemId::HealingPotion) == 99,
                "inventory_roundtrip");
        for (const char* invalid : {"INVENTORY_V1 2 1 0 0 1 0", "INVENTORY_V1 0 1 0 0 100 0",
                                    "INVENTORY_V1 0 1 0 0 1 1", "INVENTORY_V1 0 2 0 0 0 0"})
        {
            std::istringstream input(invalid);
            require(!restored.Read(input) && restored.Equipped() == bag.Equipped() &&
                        restored.Quantity(ItemId::HealingPotion) == 99,
                    "invalid_inventory_does_not_partially_replace_contents");
        }

        WorldSociety fixture;
        CitizenRecord keeper;
        keeper.id = 1;
        keeper.profession = Profession::Merchant;
        keeper.coins = 80;
        keeper.goods = 12;
        keeper.position = WorldSociety::MarketPosition(0);
        keeper.marketDuty = true;
        fixture.citizens.emplace(keeper.id, keeper);
        for (unsigned index = 0; index < 6; ++index)
        {
            CitizenRecord visitor;
            visitor.id = 2 + index;
            const Vec2 market = WorldSociety::MarketPosition(index / 3);
            visitor.position = {market.x + 50, market.y + static_cast<float>(index % 3) * 10};
            visitor.activityGoal = visitor.position;
            visitor.intention = visitor.goalActivity = Activity::Trade;
            fixture.citizens.emplace(visitor.id, visitor);
        }
        CitizenRecord outsider;
        outsider.id = 9;
        outsider.goods = 4;
        fixture.citizens.emplace(outsider.id, outsider);
        require(fixture.MarketVisitors({}, 0) == WorldSociety::MarketVisitorLimit &&
                    fixture.MarketVisitors({}, 1) == WorldSociety::MarketVisitorLimit &&
                    !fixture.HasMarketVacancy(fixture.citizens.at(9)),
                "market_visitor_reservations_are_bounded");
        fixture.SelectActivity(fixture.citizens.at(9), false);
        require(fixture.citizens.at(9).intention != Activity::Trade,
                "crowded_markets_choose_another_activity");
        auto& visitor = fixture.citizens.at(2);
        fixture.FinishMarketVisit(visitor, "acceptance");
        visitor.travelCommitSeconds = 40;
        visitor.goalActivity = Activity::Trade;
        visitor.activityGoal = {visitor.position.x + 100, visitor.position.y};
        fixture.SelectActivity(visitor, false);
        require(visitor.marketCooldown >= 35 && visitor.intention != Activity::Trade &&
                    visitor.travelCommitSeconds == 0,
                "trade_cooldown_overrides_old_travel_commitment");
        fixture.SelectActivity(fixture.citizens.at(1), false);
        require(fixture.citizens.at(1).intention == Activity::Trade,
                "market_keeper_can_serve_while_customers_leave");

        const auto originalSociety = game.society;
        const auto originalGrowth = game.growth;
        const int originalSupplies = game.supplies;
        const float originalHealth = game.health;
        game.society = fixture;
        game.inventory = {};
        game.society.playerCoins = 100;
        require(game.society.PurchaseItem(game, 1, ItemId::IronStaff) && game.society.playerCoins == 82 &&
                    game.society.citizens.at(1).coins == 98 && game.inventory.Equipped() == ItemId::IronStaff,
                "shop_purchase_transfers_coins_and_equips_weapon");
        const int balance = game.society.playerCoins;
        require(!game.society.PurchaseItem(game, 1, ItemId::IronStaff) && game.society.playerCoins == balance,
                "duplicate_weapon_purchase_does_not_charge");
        game.society.playerCoins = 0;
        require(!game.society.PurchaseItem(game, 1, ItemId::HealingPotion) &&
                    game.inventory.Quantity(ItemId::HealingPotion) == 0,
                "insufficient_funds_do_not_grant_items");
        game.society.playerCoins = 100;
        game.society.citizens.at(1).coins = 1000000;
        require(!game.society.PurchaseItem(game, 1, ItemId::HealingPotion) && game.society.playerCoins == 100,
                "merchant_coin_limit_preserves_player_money");
        game.society.citizens.at(1).coins = 98;
        require(game.society.PurchaseItem(game, 1, ItemId::EmberStaff) &&
                    game.society.PurchaseItem(game, 1, ItemId::HealingPotion) &&
                    game.society.PurchaseItem(game, 1, ItemId::HealingPotion),
                "weapon_and_potion_catalog_purchases");
        const Inventory purchased = game.inventory;
        game.inventory.Add(ItemId::HealingPotion, 97);
        const int fullBalance = game.society.playerCoins;
        require(!game.society.PurchaseItem(game, 1, ItemId::HealingPotion) &&
                    game.society.playerCoins == fullBalance,
                "full_bag_purchase_does_not_charge");
        game.inventory = purchased;
        game.society = originalSociety;
        game.growth = originalGrowth;
        game.supplies = originalSupplies;
        game.health = game.MaxHealth();
        game.healingCooldown = 0;
        require(!game.UseItem(ItemId::HealingPotion) && game.inventory.Quantity(ItemId::HealingPotion) == 2,
                "full_health_does_not_consume_potion");
        game.health -= 20;
        require(game.UseItem(ItemId::HealingPotion) && game.health == game.MaxHealth() &&
                    game.inventory.Quantity(ItemId::HealingPotion) == 1,
                "potion_consumption_heals_without_exceeding_maximum");
        game.health -= 10;
        require(!game.UseItem(ItemId::HealingPotion) && game.inventory.Quantity(ItemId::HealingPotion) == 1,
                "potion_reuse_cooldown_prevents_burst_consumption");
        game.health = originalHealth;
        game.healingCooldown = 0;
        for (const auto& item : Items())
        {
            if (item.category == ItemCategory::Weapon)
            {
                const auto growth = game.growth;
                game.growth.level = Game::MaxLevel;
                game.growth.weapon = Game::MaxWeapon;
                require(game.DamageForWeapon(item.id) * 2 < Game::EnemyMaximumHealth,
                        "all_weapon_types_keep_full_health_enemies_above_two_hit_damage");
                game.growth = growth;
            }
        }
        const Vec2 previous = game.hero->position;
        auto merchant = game.society.citizens.begin();
        while (merchant != game.society.citizens.end() && merchant->second.profession != Profession::Merchant)
        {
            ++merchant;
        }
        require(merchant != game.society.citizens.end(), "merchant_fixture_available");
        const auto merchantBefore = merchant->second;
        merchant->second.village = {};
        merchant->second.position = WorldSociety::MarketPosition(0);
        merchant->second.health = merchant->second.maximumHealth;
        merchant->second.hostileSeconds = 0;
        merchant->second.activity = merchant->second.intention = Activity::Trade;
        merchant->second.journeyKind = JourneyKind::None;
        auto& actor = game.Spawn(Kind::Citizen, {}, 0,
                                 game.Relative(WorldSociety::Locate({}, merchant->second.position)));
        actor.memory = merchant->first;
        game.hero->position = actor.WorldPosition();
        for (Actor* base : game.scene.Snapshot())
        {
            const auto* stall = dynamic_cast<const WorldActor*>(base);
            if (stall && stall->kind == Kind::MarketStall)
            {
                require(game.Shopkeeper(stall->Id()) == 0, "stall_object_is_not_a_remote_shopkeeper");
                break;
            }
        }
        const int beforeSession = game.society.playerCoins;
        require(game.Shopkeeper(actor.Id()) == merchant->first &&
                    !game.BuyItem(actor.Id(), ItemId::HealingPotion) &&
                    game.society.playerCoins == beforeSession,
                "nearby_merchant_requires_an_interaction_session");
        require(actor.Interact() && game.requestedShop == actor.Id(), "merchant_interaction_requests_shop");
        game.tradingMerchant = actor.Id();
        game.hero->position = {100000, 100000};
        require(!game.BuyItem(actor.Id(), ItemId::HealingPotion) && game.society.playerCoins == beforeSession,
                "remote_merchant_purchase_is_rejected");
        game.requestedShop = game.tradingMerchant = 0;
        game.scene.Destroy(actor.Id());
        merchant->second = merchantBefore;
        game.hero->position = previous;
        require(game.Save(), "inventory_profile_saved_before_migration_check");

        const auto directory = std::filesystem::path(game.StoragePath()).parent_path() / L"migration-v6";
        std::filesystem::create_directories(directory);
        const auto database = directory / L"World_v1.txt.sqlite3";
        require(game.regionDatabase.Backup(database.wstring()), "sqlite_backup_preserves_core_and_regions");
        RegionDatabase legacy;
        std::string core;
        bool found = false;
        require(legacy.Open(database.wstring()) && legacy.ReadCore(core, found) && found,
                "backup_database_opens");
        core.replace(0, std::string("ASHEN_WORLD_V8").size(), "ASHEN_WORLD_V6");
        const auto start = core.find("INVENTORY_V1 ");
        require(start != std::string::npos, "inventory_section_exists_in_v8");
        const auto end = core.find('\n', start);
        core.erase(start, end + 1 - start);
        const auto portalStart = core.find("WAYPOINTS_V1 ");
        require(portalStart != std::string::npos, "waypoint_section_exists_in_v8");
        core.erase(portalStart, core.find('\n', portalStart) + 1 - portalStart);
        require(legacy.Commit(core, {}, game.society.simulationSeconds), "legacy_v6_fixture_created");
        legacy.Close();
        wchar_t oldDirectory[32768] = {};
        GetEnvironmentVariableW(L"ASHEN_PROFILE_DIRECTORY", oldDirectory, 32768);

        struct RestoreDirectory
        {
            const wchar_t* value;

            ~RestoreDirectory()
            {
                SetEnvironmentVariableW(L"ASHEN_PROFILE_DIRECTORY", *value ? value : nullptr);
            }
        } restoreDirectory{oldDirectory};

        SetEnvironmentVariableW(L"ASHEN_PROFILE_DIRECTORY", directory.c_str());
        Game migrated;
        migrated.headless = true;

        struct StopGame
        {
            Game& game;

            ~StopGame()
            {
                game.Shutdown();
            }
        } stop{migrated};

        require(migrated.Initialize() && migrated.StorageHealthy() &&
                    migrated.inventory.Equipped() == ItemId::TravelerStaff &&
                    migrated.inventory.Quantity(ItemId::HealingPotion) == 0 &&
                    migrated.growth.level == game.growth.level &&
                    migrated.growth.weapon == game.growth.weapon &&
                    migrated.society.citizens.size() == game.society.citizens.size(),
                "v6_profile_migrates_to_inventory_without_losing_progress");
        require(std::filesystem::is_regular_file(directory / L"World_v1.txt.pre-inventory-v7.sqlite3"),
                "legacy_profile_is_backed_up_before_v7_save");
    }
} // namespace Ashen
