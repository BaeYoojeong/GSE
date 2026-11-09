"use strict";

(() => {
    const POLL_INTERVAL_MS = 1000;
    const REQUEST_TIMEOUT_MS = 3000;
    const STALE_AFTER_MS = 5000;
    const EVENT_DISPLAY_LIMIT = 200;
    const byId = (id) => document.getElementById(id);
    const numberFormat = new Intl.NumberFormat("ko-KR", { maximumFractionDigits: 0 });
    const decimalFormat = new Intl.NumberFormat("ko-KR", { maximumFractionDigits: 1 });
    const labels = {
        rest: "휴식", hunt: "사냥", raid: "약탈", trade: "거래", study: "학습", return: "이동", dead: "사망",
        hunter: "사냥꾼", raider: "약탈자", merchant: "상인", seeker: "구도자",
        vitality: "생존과 건강", wealth: "부의 축적", insight: "깨달음",
        murder: "살인", murder_rumor: "살인 소문", death: "사망", revive: "부활", raid: "약탈",
        trade: "거래", hunt: "사냥", injury: "부상", player_death: "플레이어 사망", player_revive: "플레이어 부활",
        letter_dispatched: "편지 출발", letter_delivered: "편지 도착", letter_lost: "편지 유실",
        migration_started: "이사 출발", migration_arrived: "이사 완료", migration_cancelled: "이사 중단",
        player_murder: "플레이어 살인", player_raid: "플레이어 약탈", heir: "전승", npc_birth: "주민 탄생",
        load_revive: "저장 주민 부활", death_loot: "사망 약탈", rob_player: "플레이어 약탈", player_hit: "플레이어 공격",
        player_hunt: "플레이어 사냥", player_buy: "플레이어 구매", player_sell: "플레이어 판매", insight: "깨달음",
        warning: "경고", critical: "심각", danger: "위험", error: "오류", info: "일반", notice: "알림", rumor: "소문"
    };
    const moodLabels = {
        peaceful: "선량한 마을", raider: "약탈자 마을", raiding: "약탈자 마을", hostile: "위험한 마을",
        hunting: "사냥 마을", trading: "상업 마을", learning: "학문 마을", mixed: "다양한 마을", unsettled: "불안한 마을"
    };

    const state = {
        snapshot: null,
        session: null,
        sequence: null,
        lastReceived: 0,
        lastChanged: 0,
        village: "",
        selectedNpc: "",
        sort: "id",
        sortDirection: 1,
        page: 0,
        pageSize: 25,
        eventDisplayLimit: EVENT_DISPLAY_LIMIT,
        seenEvents: new Set(),
        newEvents: new Set(),
        sessionNoticeUntil: 0,
        fetchFailed: false
    };

    function text(value, fallback = "—") {
        return value === null || value === undefined || value === "" ? fallback : String(value);
    }

    function numeric(value, fallback = 0) {
        const result = Number(value);
        return Number.isFinite(result) ? result : fallback;
    }

    function number(value) {
        return numberFormat.format(numeric(value));
    }

    function decimal(value) {
        return decimalFormat.format(numeric(value));
    }

    function ratio(value) {
        return Math.max(0, Math.min(1, numeric(value)));
    }

    function percentage(value) {
        return `${Math.round(ratio(value) * 100)}%`;
    }

    function label(value, supplied) {
        return text(supplied, labels[value] || text(value));
    }

    function setText(id, value) {
        byId(id).textContent = value;
    }

    function element(tag, className, content) {
        const node = document.createElement(tag);
        if (className) node.className = className;
        if (content !== undefined) node.textContent = String(content);
        return node;
    }

    function badge(content, tone = "") {
        return element("span", `badge${tone ? ` ${tone}` : ""}`, content);
    }

    function villageById(id) {
        return state.snapshot?.villages.find((village) => String(village.id) === String(id));
    }

    function villageName(id) {
        if (id === null || id === undefined || id === "") return "월드";
        const village = villageById(id);
        return text(village?.name, `마을 ${String(id)}`);
    }

    function locationText(location, fallback = "위치 미수신") {
        if (!location?.chunk || !location?.local) return fallback;
        return `청크 (${text(location.chunk.x)}, ${text(location.chunk.y)}) · 좌표 (${decimal(location.local.x)}, ${decimal(location.local.y)})`;
    }

    function clock(seconds) {
        const elapsed = Math.max(0, Math.floor(numeric(seconds)));
        const hours = Math.floor(elapsed / 3600);
        const minutes = Math.floor((elapsed % 3600) / 60);
        const remainder = elapsed % 60;
        return `${String(hours).padStart(2, "0")}:${String(minutes).padStart(2, "0")}:${String(remainder).padStart(2, "0")}`;
    }

    function relativeTime(seconds) {
        const delta = Math.max(0, numeric(state.snapshot?.simulation_seconds) - numeric(seconds));
        if (delta < 1) return "방금";
        if (delta < 60) return `${Math.floor(delta)}초 전`;
        if (delta < 3600) return `${Math.floor(delta / 60)}분 전`;
        return `${Math.floor(delta / 3600)}시간 전`;
    }

    function moodTone(mood) {
        const type = String(mood?.type || "").toLowerCase();
        if (["raider", "raiding", "hostile", "dangerous", "conflict"].includes(type)) return "red";
        if (["unsettled", "trading", "merchant", "unrest"].includes(type)) return "amber";
        if (["peaceful", "hunting", "hunter"].includes(type)) return "green";
        return "purple";
    }

    function activityTone(activity) {
        if (activity === "raid" || activity === "dead") return "red";
        if (activity === "hunt") return "green";
        if (activity === "trade") return "amber";
        if (activity === "study") return "purple";
        return "";
    }

    function avatar(npc) {
        const known = ["hunter", "raider", "merchant", "seeker"].includes(npc.profession) ? npc.profession : "seeker";
        return element("span", `npc-avatar ${known}`, label(npc.profession, npc.profession_label).slice(0, 1));
    }

    function healthMeter(npc, detailed = false) {
        const fraction = numeric(npc.max_health) > 0 ? ratio(numeric(npc.health) / numeric(npc.max_health)) : 0;
        const container = element("div", detailed ? "detail-health" : "table-health");
        const value = `${decimal(npc.health)} / ${decimal(npc.max_health)}`;
        if (detailed) {
            const title = element("div", "detail-health-top");
            title.append(element("span", "", "체력"), element("span", "", value));
            container.append(title);
        } else {
            container.append(element("span", "", value));
        }
        const track = element("span", "health-track");
        const fill = element("span", `health-fill${fraction < 0.4 ? " low" : ""}`);
        fill.style.width = percentage(fraction);
        track.append(fill);
        container.append(track);
        return container;
    }

    function npcBadges(npc) {
        const result = [];
        if (numeric(npc.health) <= 0) result.push(badge("사망", "red"));
        if (npc.hostile) result.push(badge("적대", "red"));
        if (numeric(npc.revive_protection) > 0) result.push(badge("부활 보호", "purple"));
        if (npc.away_from_home) result.push(badge("외출 중", "purple"));
        if (numeric(npc.health) > 0 && numeric(npc.health) < numeric(npc.max_health) * 0.7) result.push(badge("부상", "amber"));
        if (result.length === 0) result.push(badge("안정", "green"));
        return result;
    }

    function updateConnection() {
        const now = Date.now();
        const hasSnapshot = Boolean(state.snapshot);
        const stale = hasSnapshot && (now - state.lastChanged > STALE_AFTER_MS ||
            (numeric(state.snapshot.generated_at_ms) > 0 && now - numeric(state.snapshot.generated_at_ms) > STALE_AFTER_MS));
        const dot = byId("connection-dot");
        dot.className = "connection-dot";
        byId("connection-bar").classList.toggle("stale", stale && !state.fetchFailed);
        if (state.fetchFailed) {
            dot.classList.add("disconnected");
            setText("connection-label", "게임 연결 끊김");
            setText("connection-description", hasSnapshot ? "마지막 수신 상태를 표시합니다. 자동으로 다시 연결합니다." : "게임을 실행하면 자동으로 연결합니다.");
        } else if (!hasSnapshot) {
            setText("connection-label", "게임에 연결하는 중");
            setText("connection-description", "첫 JSON 메시지를 기다리고 있습니다.");
        } else if (stale || state.snapshot.game?.running === false) {
            setText("connection-label", "상태 업데이트 대기");
            setText("connection-description", "새 메시지가 도착하지 않았습니다. 게임 상태를 확인해 주세요.");
        } else {
            dot.classList.add("connected");
            setText("connection-label", "실시간 연결됨");
            setText("connection-description", "로컬 게임의 상태를 수신하고 있습니다.");
        }
        if (hasSnapshot) {
            setText("world-time", `월드 시간 ${clock(state.snapshot.simulation_seconds)}`);
            const age = Math.max(0, Math.floor((now - state.lastReceived) / 1000));
            setText("updated-at", age === 0 ? "방금 수신" : `${age}초 전 수신`);
            setText("sequence", `메시지 #${text(state.snapshot.sequence)}`);
        }
        byId("session-notice").hidden = now >= state.sessionNoticeUntil;
    }

    function renderOverview() {
        const snapshot = state.snapshot;
        const totals = snapshot.totals || {};
        const recentWindow = numeric(snapshot.event_window?.recent_seconds, 120);
        const recent = snapshot.events.filter((event) => numeric(snapshot.simulation_seconds) - numeric(event.time_seconds) <= recentWindow);
        const murders = recent.filter((event) => event.type === "murder" || event.type === "player_murder").length;
        const rumors = recent.filter((event) => event.category === "rumor" || event.type === "murder_rumor").length;
        setText("metric-npcs", number(totals.npc_total));
        setText("metric-npcs-description", `생존 ${number(totals.npc_alive)}명 · 전체 주민 ${number(totals.npc_total)}명`);
        setText("metric-visible", `화면에 로드됨 ${number(totals.npc_visible)}명`);
        setText("metric-monsters", number(totals.monster_alive_known));
        setText("metric-monsters-description", `현재 관측 지역 기준 · 화면에 로드된 일반 몬스터 ${number(totals.ordinary_monster_loaded)}마리`);
        setText("metric-quarries", `사냥터 ${number(totals.quarry_alive)} / ${number(totals.quarry_slots)} · 일반 ${number(totals.ordinary_monster_alive_known)}`);
        setText("metric-villages", number(totals.village_total));
        setText("metric-chunks", `활성 청크 ${number(totals.loaded_chunks)}개`);
        setText("metric-events", number(recent.length));
        setText("metric-events-description", `최근 ${number(recentWindow)}초의 보관된 사건`);
        setText("metric-murders", `살인 ${number(murders)} · 소문 ${number(rumors)}`);
        setText("total-trades", number(totals.trades));
        setText("total-raids", number(totals.raids));
        setText("total-hunts", number(totals.hunts));
        const streaming = snapshot.world_streaming;
        setText("streaming-status", streaming ? `행성 구획 (${number(streaming.planet_x)}, ${number(streaming.planet_y)}) · 저장된 지역 ${number(streaming.stored_regions)} · 메모리 지역 ${number(streaming.cached_regions)} · 이웃 마을 보행 ${decimal(streaming.center_travel_seconds)}초` : "이 게임 버전에는 처리 정보가 없습니다.");
        setText("worker-status", streaming ? `원거리 주민 계산: 반영 ${number(streaming.worker_batches)}회 · 상태 변경으로 재계산 ${number(streaming.worker_conflicts)}회 · 누적 계산 ${decimal(streaming.worker_cpu_ms)}ms · 반영 ${decimal(streaming.worker_commit_ms)}ms` : "");
        const dead = Boolean(snapshot.game?.player_dead);
        const pk = Boolean(snapshot.game?.pk_enabled);
        setText("player-state", dead ? "플레이어 사망" : "플레이어 생존");
        byId("player-state").className = `badge ${dead ? "red" : "green"}`;
        setText("pk-state", `PK ${pk ? "허용" : "비활성"}`);
        byId("pk-state").className = `badge${pk ? " amber" : ""}`;
        const warnings = byId("warnings");
        warnings.replaceChildren();
        for (const warning of snapshot.warnings || []) warnings.append(element("p", "", text(warning)));
        warnings.hidden = warnings.childElementCount === 0;
    }

    function renderVillages() {
        const container = byId("village-grid");
        const fragment = document.createDocumentFragment();
        for (const village of state.snapshot.villages) {
            const selected = state.village === String(village.id);
            const card = element("button", `village-card${selected ? " selected" : ""}`);
            card.type = "button";
            card.setAttribute("aria-pressed", String(selected));
            const population = village.population || {};
            const monsters = village.monsters || {};
            const mood = village.mood || {};
            const economy = village.economy || {};
            const recent = village.recent || {};
            const top = element("div", "village-card-top");
            const heading = element("div", "");
            heading.append(element("h3", "", text(village.name, `마을 ${village.id}`)), element("div", "village-coordinate", `청크 (${text(village.chunk?.x)}, ${text(village.chunk?.y)})`));
            top.append(heading, element("span", village.loaded ? "loaded-indicator" : "village-coordinate", village.loaded ? "● 활성" : "오프스크린"));
            card.append(top);
            const moodLine = element("div", "village-mood");
            const safety = element("span", "village-coordinate", `안전 ${number(Math.max(0, Math.min(100, numeric(mood.safety_score))))}/100`);
            safety.title = `안전 지수 ${decimal(mood.safety_score)} · 폭력 지수 ${decimal(mood.violence_score)} (0–100)`;
            moodLine.append(badge(text(mood.label, moodLabels[mood.type] || text(mood.type)), moodTone(mood)), safety);
            card.append(moodLine, element("p", "mood-description", text(mood.description, "마을 주민들의 활동을 관측하고 있습니다.")));
            const stats = element("div", "village-stats");
            for (const [name, value] of [["주민", population.total], ["몬스터", numeric(monsters.quarry_alive) + numeric(monsters.ordinary_alive)], ["적대 주민", population.hostile]]) {
                const stat = element("div", "village-stat");
                stat.append(element("span", "", name), element("strong", "", number(value)));
                stats.append(stat);
            }
            card.append(stats);
            const facts = element("dl", "village-facts");
            const villageFields = [
                ["주민 상태", `생존 ${number(population.alive)} · 부상 ${number(population.injured)} · 보호 ${number(population.protected)}`],
                ["주민 위치", `마을 내 ${number(population.present)} · 외출 ${number(population.away)} · 방문객 ${number(population.visitors)}`],
                ["마을 자산", `재화 ${number(economy.coins)} · 물품 ${number(economy.goods)} · 깨달음 ${number(economy.insight)}`],
                [`최근 ${number(state.snapshot.event_window?.recent_seconds ?? 120)}초`, `살인 ${number(recent.murders)} · 약탈 ${number(recent.raids)} · 거래 ${number(recent.trades)}`]
            ];
            for (const [name, value] of villageFields) {
                const fact = element("div", `village-fact${name.startsWith("최근") ? " incidents" : ""}`);
                fact.append(element("dt", "", name), element("dd", "", value));
                facts.append(fact);
            }
            card.append(facts);
            const activities = village.activities || {};
            const sum = Object.values(activities).reduce((total, value) => total + Math.max(0, numeric(value)), 0);
            const activity = element("div", "village-activity");
            activity.setAttribute("aria-label", "마을 주민 행동 분포");
            const activitySummary = [];
            for (const key of ["rest", "hunt", "raid", "trade", "study", "return"]) {
                const count = Math.max(0, numeric(activities[key]));
                if (count === 0) continue;
                const segment = element("span", `activity-${key}`);
                segment.style.flexGrow = String(count);
                segment.title = `${labels[key]} ${number(count)}명`;
                activity.append(segment);
                activitySummary.push(`${labels[key]} ${number(count)}`);
            }
            activity.title = activitySummary.join(" · ");
            if (sum === 0) activity.append(element("span", ""));
            const activityLabel = element("div", "village-activity-label");
            activityLabel.append(element("span", "", activitySummary.slice(0, 3).join(" · ") || "활동 대기"), element("span", "", `약탈 성향 ${percentage(mood.avg_raid_weight)}`));
            card.append(activity, activityLabel);
            card.addEventListener("click", () => selectVillage(selected ? "" : String(village.id)));
            fragment.append(card);
        }
        if (state.snapshot.villages.length === 0) fragment.append(element("div", "empty-state initial-empty", "아직 알려진 마을이 없습니다."));
        container.replaceChildren(fragment);
        byId("clear-village").hidden = state.village === "";
        setText("selected-village-label", state.village ? villageName(state.village) : "전체 마을");
    }

    function updateOptions(select, choices, initialLabel) {
        const previous = select.value;
        const values = [...new Map(choices.map(([value, title]) => [String(value), title])).entries()];
        const signature = JSON.stringify(values);
        if (select.dataset.optionSignature === signature) return;
        select.dataset.optionSignature = signature;
        const fragment = document.createDocumentFragment();
        const first = element("option", "", initialLabel);
        first.value = "";
        fragment.append(first);
        for (const [value, title] of values) {
            const option = element("option", "", title);
            option.value = value;
            fragment.append(option);
        }
        select.replaceChildren(fragment);
        select.value = values.some(([value]) => value === previous) ? previous : "";
    }

    function renderFilterOptions() {
        updateOptions(byId("profession-filter"), state.snapshot.npcs.map((npc) => [npc.profession, label(npc.profession, npc.profession_label)]), "모든 직업");
        updateOptions(byId("event-type-filter"), state.snapshot.events.map((event) => [event.type, label(event.type)]), "모든 사건");
        updateOptions(byId("event-severity-filter"), state.snapshot.events.map((event) => [event.severity, label(event.severity)]), "모든 중요도");
        updateOptions(byId("event-village-filter"), state.snapshot.villages.map((village) => [village.id, village.name]), "모든 마을");
    }

    function selectVillage(id) {
        state.village = id;
        state.page = 0;
        state.eventDisplayLimit = EVENT_DISPLAY_LIMIT;
        byId("event-village-filter").value = id;
        renderVillages();
        renderNpcs();
        renderEvents();
    }

    function compareIds(a, b) {
        const first = String(a);
        const second = String(b);
        if (/^\d+$/.test(first) && /^\d+$/.test(second)) {
            const normalizedFirst = first.replace(/^0+(?=\d)/, "");
            const normalizedSecond = second.replace(/^0+(?=\d)/, "");
            if (normalizedFirst.length !== normalizedSecond.length) return normalizedFirst.length - normalizedSecond.length;
            return normalizedFirst.localeCompare(normalizedSecond);
        }
        return first.localeCompare(second, "ko");
    }

    function filteredNpcs() {
        const query = byId("npc-search").value.trim().toLocaleLowerCase("ko");
        const profession = byId("profession-filter").value;
        const status = byId("npc-state-filter").value;
        return state.snapshot.npcs.filter((npc) => {
            if (state.village && String(npc.village_id) !== state.village) return false;
            if (profession && npc.profession !== profession) return false;
            if (status === "hostile" && !npc.hostile) return false;
            if (status === "injured" && !(numeric(npc.health) < numeric(npc.max_health) * 0.7)) return false;
            if (status === "protected" && numeric(npc.revive_protection) <= 0) return false;
            if (status === "visible" && !npc.visible) return false;
            const haystack = [npc.id, npc.origin_life, npc.profession, npc.profession_label, npc.ambition, npc.ambition_label, npc.activity_label, villageName(npc.village_id), locationText(npc.location), npc.away_from_home ? "외출 중" : "마을 내"].join(" ").toLocaleLowerCase("ko");
            return !query || haystack.includes(query);
        }).sort((first, second) => {
            let order;
            if (state.sort === "id") order = compareIds(first.id, second.id);
            else if (state.sort === "profession") order = label(first.profession, first.profession_label).localeCompare(label(second.profession, second.profession_label), "ko");
            else if (state.sort === "activity") order = label(first.activity, first.activity_label).localeCompare(label(second.activity, second.activity_label), "ko");
            else order = numeric(first[state.sort]) - numeric(second[state.sort]);
            return order === 0 ? compareIds(first.id, second.id) : order * state.sortDirection;
        });
    }

    function selectNpc(id) {
        state.selectedNpc = id;
        renderNpcs();
    }

    function renderNpcs() {
        const focusedNpc = document.activeElement?.closest("tr")?.dataset.npcId;
        const npcs = filteredNpcs();
        const pages = Math.max(1, Math.ceil(npcs.length / state.pageSize));
        state.page = Math.min(Math.max(state.page, 0), pages - 1);
        const start = state.page * state.pageSize;
        const shown = npcs.slice(start, start + state.pageSize);
        const fragment = document.createDocumentFragment();
        for (const npc of shown) {
            const row = element("tr", String(npc.id) === state.selectedNpc ? "selected" : "");
            row.dataset.npcId = String(npc.id);
            row.tabIndex = 0;
            row.setAttribute("aria-selected", String(String(npc.id) === state.selectedNpc));
            row.setAttribute("aria-label", `주민 ${npc.id}, ${label(npc.profession, npc.profession_label)}, ${label(npc.activity, npc.activity_label)}`);
            const identityCell = element("td", "");
            const identity = element("div", "npc-identity");
            const name = element("div", "");
            name.append(element("div", "npc-id", `NPC ${text(npc.id)}`), element("div", "npc-village", `소속: ${villageName(npc.village_id)}`), element("div", "npc-village", `현재: ${locationText(npc.location)}`));
            identity.append(avatar(npc), name);
            identityCell.append(identity);
            const professionCell = element("td", "", label(npc.profession, npc.profession_label));
            const activityCell = element("td", "");
            activityCell.append(badge(label(npc.activity, npc.activity_label), activityTone(npc.activity)));
            if (npc.intention === "raid" || numeric(npc.raid_cooldown_seconds) > 0) {
                activityCell.append(element("div", "muted", text(npc.raid_phase_label)));
            }
            const healthCell = element("td", "");
            healthCell.append(healthMeter(npc));
            const coinsCell = element("td", "", number(npc.coins));
            coinsCell.title = `재화 ${number(npc.coins)} · 물품 ${number(npc.goods)} · 깨달음 ${number(npc.insight)}`;
            const raidCell = element("td", `raid-value${numeric(npc.raid_weight) >= 0.6 ? " high" : ""}`, percentage(npc.raid_weight));
            const statuses = element("td", "");
            const statusContainer = element("div", "status-badges");
            statusContainer.append(...npcBadges(npc));
            statuses.append(statusContainer);
            row.append(identityCell, professionCell, activityCell, healthCell, coinsCell, raidCell, statuses);
            row.addEventListener("click", () => selectNpc(String(npc.id)));
            row.addEventListener("keydown", (event) => {
                if (event.key === "Enter" || event.key === " ") {
                    event.preventDefault();
                    selectNpc(String(npc.id));
                }
            });
            fragment.append(row);
        }
        if (shown.length === 0) {
            const row = element("tr", "empty-row");
            const cell = element("td", "table-empty", "조건에 맞는 주민이 없습니다.");
            cell.colSpan = 7;
            row.append(cell);
            fragment.append(row);
        }
        byId("npc-rows").replaceChildren(fragment);
        if (focusedNpc) {
            const focusedRow = [...byId("npc-rows").rows].find((row) => row.dataset.npcId === focusedNpc);
            focusedRow?.focus({ preventScroll: true });
        }
        setText("npc-count", number(npcs.length));
        setText("npc-page-summary", npcs.length === 0 ? "0명의 주민" : `${number(npcs.length)}명 중 ${number(start + 1)}–${number(start + shown.length)}명`);
        setText("page-indicator", `${state.page + 1} / ${pages}`);
        byId("previous-page").disabled = state.page === 0;
        byId("next-page").disabled = state.page >= pages - 1;
        renderNpcDetails();
    }

    function renderNpcDetails() {
        if (!state.selectedNpc) return;
        const npc = state.snapshot.npcs.find((entry) => String(entry.id) === state.selectedNpc);
        const container = byId("npc-details");
        if (!npc) {
            state.selectedNpc = "";
            const placeholder = element("div", "detail-placeholder");
            placeholder.append(element("h3", "", "주민 정보가 갱신되었습니다"), element("p", "", "목록에서 다른 주민을 선택해 주세요."));
            container.replaceChildren(placeholder);
            return;
        }
        const fragment = document.createDocumentFragment();
        const heading = element("div", "detail-heading");
        const title = element("div", "");
        title.append(element("h3", "", `NPC ${text(npc.id)}`), element("p", "detail-subtitle", `${label(npc.profession, npc.profession_label)} · 소속: ${villageName(npc.village_id)}`));
        heading.append(avatar(npc), title);
        fragment.append(heading, healthMeter(npc, true));
        const statuses = element("div", "detail-status");
        statuses.append(...npcBadges(npc), badge(npc.visible ? "화면에 로드됨" : "오프스크린"));
        fragment.append(statuses);
        const grid = element("dl", "detail-grid");
        const source = text(npc.origin_life, "0") === "0" ? "최초 주민" : `삶 ${text(npc.origin_life)} 전승`;
        const destinationLabel = { quarry: "사냥감 위치", target: "약탈 대상 위치", patrol: "야외 탐색 목적지", activity: "활동 목적지" }[npc.destination_kind] || "활동 목적지";
        const fields = [
            ["목표", label(npc.ambition, npc.ambition_label)], ["현재 행동", label(npc.activity, npc.activity_label)],
            ["선택한 의도", label(npc.intention, npc.intention_label)], ["약탈 성향", percentage(npc.raid_weight)],
            ["재화", number(npc.coins)], ["물품", number(npc.goods)], ["깨달음", number(npc.insight)], ["사망 · 부활", text(npc.deaths, "0")],
            ["집 주소", `${number(npc.house_number)}호 · ${locationText(npc.house_location)}`],
            ["여행", text(npc.travel_label)], ["여행 목적지", npc.destination_village ? villageName(npc.destination_village) : "없음"],
            ["운반 편지", text(npc.mail_id, "0") === "0" ? "없음" : `#${text(npc.mail_id)}`],
            ["이동 경과", `${decimal(npc.transit_seconds)}초`],
            ["소속 마을", villageName(npc.village_id)], ["현재 위치", locationText(npc.location)],
            [destinationLabel, locationText(npc.destination, "미배정")], ["이동 계획 갱신", number(npc.journey)],
            ["남은 체류 시간", `${decimal(npc.goal_dwell_seconds)}초`], ["생명의 기원", source],
            ["지역", npc.in_wilderness ? "야외" : "마을"],
            ["목적지 유지", `${decimal(npc.travel_commit_seconds)}초`],
            ["도착 횟수 · 이번 실행", number(npc.destinations_reached_session)],
            ["약탈 진행", text(npc.raid_phase_label, "다른 활동 중")],
            ["다음 약탈까지", `${decimal(npc.raid_cooldown_seconds)}초`],
            ["약탈 성공 · 이번 실행", text(npc.successful_raids_session, "0")],
            ["빼앗은 동전 · 이번 실행", text(npc.stolen_coins_session, "0")],
            ["최근 약탈", numeric(npc.last_raid_amount) > 0
                ? `${text(npc.last_raid_target, "0") === "0" ? "플레이어" : `NPC ${text(npc.last_raid_target)}`} · ${number(npc.last_raid_amount)} 동전`
                : "이번 실행 기록 없음"],
            ["행동 대상", text(npc.target, "0") === "0" ? "없음" : `NPC ${text(npc.target)}`], ["사냥터", numeric(npc.quarry_slot, -1) >= 0 ? `${numeric(npc.quarry_slot) + 1}번 구역` : "미배정"]
        ];
        for (const [name, value] of fields) {
            const field = element("div", "");
            field.append(element("dt", "", name), element("dd", "", value));
            grid.append(field);
        }
        fragment.append(grid, element("h4", "detail-section-title", "행동 선택 점수"));
        const scores = npc.scores || {};
        const maximum = Math.max(1, ...["rest", "hunt", "raid", "trade", "study"].map((key) => Math.max(0, numeric(scores[key]))));
        for (const key of ["rest", "hunt", "raid", "trade", "study"]) {
            const value = numeric(scores[key]);
            const row = element("div", `score-row${npc.intention === key ? " current" : ""}`);
            const track = element("span", "score-track");
            const fill = element("span", "score-fill");
            fill.style.width = percentage(Math.max(0, value) / maximum);
            track.append(fill);
            row.append(element("span", "", labels[key]), track, element("span", "score-number", value <= -999 ? "불가" : decimal(value)));
            fragment.append(row);
        }
        fragment.append(element("p", "score-caption", "마지막 행동 결정 시점의 점수입니다. 성향, 목표, 체력과 거래·사냥 조건에 따라 달라집니다."));
        if (numeric(npc.revive_protection) > 0) fragment.append(element("p", "score-caption", `부활 보호 ${decimal(npc.revive_protection)}초 남음`));
        container.replaceChildren(fragment);
    }

    function eventTone(event) {
        if (event.category === "rumor" || event.type === "murder_rumor") return "caution";
        if (["murder", "player_murder", "death", "player_death"].includes(event.type) || ["critical", "danger", "error"].includes(event.severity)) return "danger";
        if (["warning", "warn"].includes(event.severity) || event.type === "raid" || event.type === "injury") return "caution";
        return "";
    }

    function eventIcon(event) {
        const type = text(event.type, "");
        if (event.category === "rumor" || event.type === "murder_rumor") return "?";
        if (type.includes("murder") || type.includes("death")) return "†";
        if (type.includes("revive")) return "+";
        if (event.type === "trade") return "⇄";
        if (event.type === "hunt") return "◇";
        if (event.type === "raid") return "!";
        return "·";
    }

    function renderEvents() {
        const type = byId("event-type-filter").value;
        const severity = byId("event-severity-filter").value;
        const village = byId("event-village-filter").value;
        const query = byId("event-search").value.trim().toLocaleLowerCase("ko");
        const events = state.snapshot.events.filter((event) => {
            if (type && event.type !== type) return false;
            if (severity && event.severity !== severity) return false;
            if (village && String(event.village_id) !== village) return false;
            const haystack = [event.message, event.actor_id, event.target_id, label(event.type), villageName(event.village_id)].join(" ").toLocaleLowerCase("ko");
            return !query || haystack.includes(query);
        }).slice().reverse();
        const fragment = document.createDocumentFragment();
        for (const event of events.slice(0, state.eventDisplayLimit)) {
            const row = element("article", `event-row ${eventTone(event)}${state.newEvents.has(String(event.id)) ? " new-event" : ""}`);
            row.id = `event-${String(event.id)}`;
            const time = element("div", "event-time", relativeTime(event.time_seconds));
            time.title = `월드 시간 ${clock(event.time_seconds)}`;
            const icon = element("span", "event-icon", eventIcon(event));
            icon.setAttribute("aria-hidden", "true");
            const content = element("div", "");
            content.append(element("div", "event-description", text(event.message, label(event.type))));
            const metadata = element("div", "event-metadata");
            metadata.append(badge(label(event.type), eventTone(event) === "danger" ? "red" : eventTone(event) === "caution" ? "amber" : "purple"));
            if (event.category === "rumor" || event.type === "murder_rumor") metadata.append(element("span", "", "미확인 소문"));
            else if (event.confirmed === false) metadata.append(element("span", "", "미확인 사건"));
            else metadata.append(element("span", "", "확인된 사건"));
            if (numeric(event.witness_count) > 0) metadata.append(element("span", "", `목격자 ${number(event.witness_count)}명`));
            if (text(event.actor_id, "0") !== "0") {
                const actor = element("button", "event-source-button", `NPC ${event.actor_id}`);
                actor.type = "button";
                actor.addEventListener("click", () => {
                    selectNpc(String(event.actor_id));
                    byId("npcs-heading").scrollIntoView({ behavior: "smooth", block: "start" });
                });
                metadata.append(actor);
            }
            if (text(event.target_id, "0") !== "0") metadata.append(element("span", "", `대상 NPC ${event.target_id}`));
            if (event.source_event_id) metadata.append(element("span", "", `원본 사건 #${event.source_event_id}`));
            metadata.append(element("span", "", `#${text(event.id)}`));
            content.append(metadata);
            row.append(time, icon, content, element("div", "event-village", villageName(event.village_id)));
            fragment.append(row);
        }
        if (events.length === 0) fragment.append(element("div", "empty-state", state.snapshot.events.length > 0 ? "조건에 맞는 사건이 없습니다." : "아직 관측된 사건이 없습니다."));
        byId("event-feed").replaceChildren(fragment);
        setText("event-count", number(events.length));
        setText("event-window-label", `최근 ${number(state.snapshot.event_window?.retained ?? state.snapshot.events.length)} / ${number(state.snapshot.event_window?.capacity ?? 512)}개 보관`);
        const shown = Math.min(events.length, state.eventDisplayLimit);
        setText("event-limit-label", events.length > shown ? `최근 ${shown}개 표시 · 아래에서 이전 사건을 확인하세요` : "새로운 사건이 위에 표시됩니다");
        byId("event-pagination").hidden = events.length <= shown;
        setText("more-events", `이전 사건 더 보기 (${number(events.length - shown)}개 남음)`);
    }

    function renderCommunication() {
        const info = state.snapshot.communication || {};
        const letters = Array.isArray(info.letters) ? info.letters : [];
        const moving = state.snapshot.npcs.filter(npc => npc.travel_kind === 3).length;
        setText("communication-summary", `운반 중 ${letters.length}통 · 전달 완료 ${text(info.delivered, "0")}통 · 이사 중 ${moving}명 · 이사 완료 ${text(info.migrations_completed, "0")}명`);
        const fragment = document.createDocumentFragment();
        for (const letter of letters) {
            const row = element("p", "", `편지 #${text(letter.id)} · ${villageName(letter.source)} → ${villageName(letter.destination)} · 이동 ${decimal(letter.travel_seconds)}초 · `);
            const button = element("button", "text-button", `전령 #${text(letter.carrier)}`);
            button.type = "button";
            button.addEventListener("click", () => { selectNpc(String(letter.carrier)); byId("npcs-heading").scrollIntoView({ block: "start" }); });
            row.append(button);
            fragment.append(row);
        }
        if (!letters.length) fragment.append(element("p", "", "현재 운반 중인 편지가 없습니다."));
        byId("letter-list").replaceChildren(fragment);
        const news = document.createDocumentFragment();
        for (const village of state.snapshot.villages) {
            news.append(element("h4", "", villageName(village.id)));
            for (const report of village.known_news || []) {
                const description = !report.known ? "아직 소식 없음" : `${report.local ? "현지 기록" : "전해 들은 소식"} · ${decimal(report.age_seconds)}초 전 작성 · 인구 ${number(report.population)} · 치안 ${percentage(report.safety)} · 살인 ${number(report.murders)} / 약탈 ${number(report.raids)}`;
                news.append(element("p", "", `${villageName(report.village_id)}: ${description}`));
            }
        }
        byId("news-list").replaceChildren(news);
        setText("chapter-objective", state.snapshot.chapter?.objective || "");
    }

    function receive(snapshot) {
        if (!snapshot || snapshot.schema_version !== 1 || typeof snapshot.session_id !== "string" ||
            !Number.isFinite(snapshot.sequence) || !Array.isArray(snapshot.npcs) || !Array.isArray(snapshot.villages) || !Array.isArray(snapshot.events)) {
            throw new Error("지원하지 않는 월드 JSON 형식입니다.");
        }
        const now = Date.now();
        const sessionChanged = state.session !== null && state.session !== snapshot.session_id;
        if (sessionChanged) {
            state.selectedNpc = "";
            state.page = 0;
            state.eventDisplayLimit = EVENT_DISPLAY_LIMIT;
            state.seenEvents.clear();
            state.sessionNoticeUntil = now + 7000;
            byId("npc-details").replaceChildren(element("div", "detail-placeholder", "새 세션의 주민을 선택해 주세요."));
        }
        const changed = state.sequence !== snapshot.sequence || state.session !== snapshot.session_id;
        state.lastReceived = now;
        state.fetchFailed = false;
        if (!changed) {
            updateConnection();
            return;
        }
        const hadSnapshot = Boolean(state.snapshot) && !sessionChanged;
        state.newEvents = new Set(hadSnapshot ? snapshot.events.filter((event) => !state.seenEvents.has(String(event.id))).map((event) => String(event.id)) : []);
        state.seenEvents = new Set(snapshot.events.map((event) => String(event.id)));
        state.snapshot = snapshot;
        state.session = snapshot.session_id;
        state.sequence = snapshot.sequence;
        state.lastChanged = now;
        if (state.village && !snapshot.villages.some((village) => String(village.id) === state.village)) state.village = "";
        renderOverview();
        renderCommunication();
        renderFilterOptions();
        renderVillages();
        renderNpcs();
        renderEvents();
        byId("download-json").disabled = false;
        updateConnection();
    }

    async function poll() {
        const started = Date.now();
        const controller = new AbortController();
        const timeout = window.setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
        try {
            const response = await fetch("/api/state", {
                cache: "no-store",
                credentials: "omit",
                mode: "same-origin",
                headers: { Accept: "application/json" },
                signal: controller.signal
            });
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            receive(await response.json());
        } catch (error) {
            state.fetchFailed = true;
            updateConnection();
            if (!state.snapshot && error.message === "지원하지 않는 월드 JSON 형식입니다.") {
                setText("connection-description", "수신한 JSON 형식이 대시보드와 맞지 않습니다. 게임과 Monitor 파일 버전을 확인해 주세요.");
            }
        } finally {
            window.clearTimeout(timeout);
            window.setTimeout(poll, Math.max(250, POLL_INTERVAL_MS - (Date.now() - started)));
        }
    }

    function initializeControls() {
        byId("clear-village").addEventListener("click", () => selectVillage(""));
        for (const id of ["npc-search", "profession-filter", "npc-state-filter"]) {
            byId(id).addEventListener(id === "npc-search" ? "input" : "change", () => {
                if (!state.snapshot) return;
                state.page = 0;
                renderNpcs();
            });
        }
        for (const id of ["event-type-filter", "event-severity-filter", "event-village-filter", "event-search"]) {
            byId(id).addEventListener(id === "event-search" ? "input" : "change", () => {
                state.eventDisplayLimit = EVENT_DISPLAY_LIMIT;
                if (state.snapshot) renderEvents();
            });
        }
        for (const button of document.querySelectorAll("[data-sort]")) {
            button.addEventListener("click", () => {
                const field = button.dataset.sort;
                state.sortDirection = state.sort === field ? -state.sortDirection : field === "coins" || field === "raid_weight" ? -1 : 1;
                state.sort = field;
                state.page = 0;
                for (const headerButton of document.querySelectorAll("[data-sort]")) {
                    const selected = headerButton.dataset.sort === field;
                    headerButton.closest("th").setAttribute("aria-sort", selected ? state.sortDirection === 1 ? "ascending" : "descending" : "none");
                    headerButton.querySelector(".sort-arrow").textContent = selected ? state.sortDirection === 1 ? "↑" : "↓" : "";
                }
                if (state.snapshot) renderNpcs();
            });
        }
        byId("page-size").addEventListener("change", () => {
            state.pageSize = Number(byId("page-size").value);
            state.page = 0;
            if (state.snapshot) renderNpcs();
        });
        byId("previous-page").addEventListener("click", () => { state.page--; renderNpcs(); });
        byId("next-page").addEventListener("click", () => { state.page++; renderNpcs(); });
        byId("more-events").addEventListener("click", () => {
            state.eventDisplayLimit += EVENT_DISPLAY_LIMIT;
            if (state.snapshot) renderEvents();
        });
        byId("download-json").addEventListener("click", () => {
            if (!state.snapshot) return;
            const blob = new Blob([JSON.stringify(state.snapshot, null, 2)], { type: "application/json;charset=utf-8" });
            const url = URL.createObjectURL(blob);
            const anchor = element("a", "");
            anchor.href = url;
            anchor.download = `ashen-world-${new Date().toISOString().replace(/[:.]/g, "-")}.json`;
            document.body.append(anchor);
            anchor.click();
            anchor.remove();
            window.setTimeout(() => URL.revokeObjectURL(url), 1000);
        });
    }

    initializeControls();
    if (location.protocol !== "file:") setText("server-address", `LOCAL / ${location.host}`);
    if (location.protocol === "file:") {
        byId("connection-dot").classList.add("disconnected");
        setText("connection-label", "로컬 서버 주소로 열어 주세요");
        setText("connection-description", "게임 실행 후 http://127.0.0.1:8765 에 접속하면 JSON을 수신합니다.");
        return;
    }
    window.setInterval(updateConnection, 1000);
    poll();
})();
