#include "InventoryInterface.h"
#include "DarkAgeGame.h"
#include "DarkAgeRenderer.h"
#include <iomanip>
#include <sstream>

namespace InventoryInterface
{
    namespace
    {
        enum class Mode
        {
            Closed,
            Inventory,
            Shop
        };
        Mode mode = Mode::Closed;
        Ashen::ActorId shop = 0;
        unsigned selection = 0;
        const AshenRenderer::Color Gold = {.91f, .70f, .40f, 1};
        const AshenRenderer::Color Muted = {.55f, .58f, .63f, 1};

        std::wstring Number(float value)
        {
            std::wostringstream out;
            out << std::fixed << std::setprecision(1) << value;
            return out.str();
        }

        void Prepare(Ashen::Game& game)
        {
            if (!game.dialogue.empty())
            {
                game.Interact();
            }
            game.ClearInput();
            game.inputBlocked = true;
        }

        unsigned Quantity(const Ashen::Game& game, Ashen::ItemId id)
        {
            return id == Ashen::ItemId::Supplies ? static_cast<unsigned>(game.supplies)
                                                 : game.inventory.Quantity(id);
        }
    } // namespace

    bool IsOpen()
    {
        return mode != Mode::Closed;
    }

    void Close(Ashen::Game& game)
    {
        mode = Mode::Closed;
        shop = 0;
        game.requestedShop = game.tradingMerchant = 0;
        game.inputBlocked = false;
        game.ClearInput();
    }

    void OpenInventory(Ashen::Game& game)
    {
        if (game.Dead())
        {
            return;
        }
        Prepare(game);
        mode = Mode::Inventory;
        shop = game.tradingMerchant = game.requestedShop = 0;
        game.requestedMap = -1;
        selection = 0;
    }

    void OpenShop(Ashen::Game& game)
    {
        if (game.Dead())
        {
            return;
        }
        const auto requested = game.requestedShop;
        game.requestedShop = 0;
        if (!game.Shopkeeper(requested))
        {
            game.Notify(L"시장의 상인 NPC와 E로 상호작용하세요.");
            return;
        }
        Prepare(game);
        mode = Mode::Shop;
        shop = requested;
        game.tradingMerchant = requested;
        selection = static_cast<unsigned>(Ashen::ItemId::IronStaff);
    }

    void Update(Ashen::Game& game)
    {
        if (game.Dead())
        {
            if (IsOpen())
            {
                Close(game);
            }
            return;
        }
        if (game.requestedShop)
        {
            OpenShop(game);
        }
        if (mode == Mode::Shop && (game.tradingMerchant != shop || !game.Shopkeeper(shop)))
        {
            Close(game);
            game.Notify(L"상인에게서 멀어졌거나 거래할 수 없어 창을 닫았습니다.");
        }
    }

    bool Arrow(WorldScene::SpecialKey key, bool down)
    {
        if (!IsOpen())
        {
            return false;
        }
        using Special = WorldScene::SpecialKey;
        if (key < Special::Left || key > Special::Down)
        {
            return false;
        }
        if (down && (key == Special::Up || key == Special::Down))
        {
            const auto count = static_cast<unsigned>(Ashen::ItemTypeCount);
            selection = (selection + (key == Special::Down ? 1 : count - 1)) % count;
        }
        return true;
    }

    bool Key(Ashen::Game& game, unsigned char key, bool down)
    {
        if (!IsOpen())
        {
            return false;
        }
        if (!down)
        {
            return true;
        }
        if (key == 27)
        {
            Close(game);
        }
        else if (key == 'i' || key == 'I')
        {
            if (mode == Mode::Inventory)
            {
                Close(game);
            }
            else
            {
                OpenInventory(game);
            }
        }
        else if (key >= '1' && key < '1' + Ashen::ItemTypeCount)
        {
            selection = static_cast<unsigned>(key - '1');
        }
        else if (key == '\r' || key == 'e' || key == 'E')
        {
            const auto item = static_cast<Ashen::ItemId>(selection);
            if (mode == Mode::Shop)
            {
                game.BuyItem(shop, item);
            }
            else
            {
                game.UseItem(item);
            }
        }
        else if ((key == 'n' || key == 'N') && mode == Mode::Shop)
        {
            game.SellSupplies(shop);
        }
        else if (key == 'h' || key == 'H')
        {
            game.UseItem(Ashen::ItemId::HealingPotion);
        }
        // Consume every gameplay key while a panel owns input, including Space and movement releases.
        return true;
    }

    void Render(const Ashen::Game& game)
    {
        if (!IsOpen() || game.Dead())
        {
            return;
        }
        using namespace AshenRenderer;
        using namespace Ashen;
        Panel(60, 165, 880, 452, {.025f, .033f, .045f, .98f});
        Panel(60, 165, 4, 452, Gold);
        Text(82, 184, mode == Mode::Shop ? L"상점 · 무기와 회복 물약" : L"인벤토리 · 장비와 소모품", 24,
             Gold);
        Text(82, 220,
             L"동전 " + std::to_wstring(game.society.playerCoins) + L"  ·  무기 강화 +" +
                 std::to_wstring(game.growth.weapon) + L"  ·  HP " +
                 std::to_wstring(static_cast<int>(game.health)) + L"/" +
                 std::to_wstring(static_cast<int>(game.MaxHealth())),
             16);
        for (unsigned index = 0; index < ItemTypeCount; ++index)
        {
            const auto& item = Items()[index];
            const float y = 254 + index * 47.0f;
            const unsigned quantity = Quantity(game, item.id);
            Panel(82, y - 4, 454, 40,
                  index == selection ? Color{.17f, .20f, .25f, 1} : Color{.055f, .07f, .09f, 1});
            const Color tint = quantity > 0 ? Gold : Muted;
            const bool equipped =
                item.category == ItemCategory::Weapon && game.inventory.Equipped() == item.id;
            Text(94, y, std::to_wstring(index + 1) + L". " + item.name, 18, tint);
            Text(340, y + 2, equipped ? L"[장착 중]" : L"x" + std::to_wstring(quantity), 16, tint);
        }
        const auto& item = Items()[selection];
        Text(563, 258, item.name, 21, Gold);
        Text(563, 298, item.description, 14);
        if (item.category == ItemCategory::Weapon)
        {
            Text(563, 335, L"기본 공격력 " + Number(item.attack), 17);
            Text(563, 365, L"현재 성장 포함 " + Number(game.DamageForWeapon(item.id)), 17, Gold);
            Text(563, 400, mode == Mode::Shop ? L"구매한 무기는 바로 장착합니다." : L"Enter / E로 무기 장착",
                 14);
        }
        else if (item.id == ItemId::HealingPotion)
        {
            Text(563, 335, L"회복량 " + Number(game.MaxHealth() * .35f), 17, Gold);
            Text(563, 365, L"재사용 대기 2초 · H 빠른 사용", 15);
            Text(563, 400, L"가득 찬 체력에는 소모하지 않습니다.", 13);
        }
        if (mode == Mode::Shop)
        {
            Text(563, 446,
                 item.price > 0 ? L"구매 가격 " + std::to_wstring(item.price) + L" 동전"
                                : L"기본 지급 무기 · 판매하지 않음",
                 17, Gold);
            Text(563, 478, L"N 물자 1개 판매 · 동전 +2", 15);
        }
        Text(82, 510,
             L"장착: " + std::wstring(FindItem(game.inventory.Equipped())->name) +
                 L"  ·  세계 시간은 계속 흐릅니다.",
             15, Gold);
        if (!game.notice.empty())
        {
            Text(82, 540, game.notice, 14, Gold);
        }
        Text(82, 580,
             mode == Mode::Shop ? L"↑↓ / 1~5 선택 · Enter/E 구매 · I 인벤토리 · Esc 닫기"
                                : L"↑↓ / 1~5 선택 · Enter/E 장착·사용 · I/Esc 닫기",
             15);
    }
} // namespace InventoryInterface
