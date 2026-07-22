// All 2D UI: the hotbar HUD (with floating machine bars and belt-item icons),
// the crafting menu, the machine panel with drag-and-drop, the F1 help
// overlay, and the F3 perf readout. Layout structs are the single source of
// truth shared by each panel's hit-testing and drawing.

#include "game/VoxelGame.h"
#include "VoxelGameInternal.h"
#include "game/Atlas.h"
#include "game/MachineSystem.h"
#include "game/SaveSystem.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace vg;

namespace {

    // Machine-panel layout: computed once per frame from the window size and
    // content counts; the single source of truth for hit-testing AND drawing.
    struct PanelLayout {
        static constexpr float RowH = 26.0f;   // action row height
        static constexpr float Cell = 40.0f;   // item cell size
        static constexpr float Gap  = 6.0f;    // between cells
        static constexpr float StripH = Cell + 12.0f; // IN / OUT band height
        static constexpr int   InvCols = 10;

        float px = 0, py = 0, panelW = 640.0f, panelH = 0;
        int   rows = 0;      // action rows (AUTO + recipes + TAKE)
        float rowsY = 0;     // top of the action rows
        float inY = 0;       // top of the IN cell strip
        float outY = 0;      // top of the OUT cell strip
        float stripCellsX = 0; // first cell x in the IN/OUT strips
        float barY = 0;      // progress bar
        float invLabelY = 0;
        float invY = 0;      // top of the inventory grid
        int   invRows = 0;
        float tooltipY = 0;
        float footerY = 0;
    };

    PanelLayout panelLayout(int w, int h, int actionRows, int invCount) {
        PanelLayout L;
        L.rows = actionRows;
        L.invRows = std::max(1, (invCount + PanelLayout::InvCols - 1) / PanelLayout::InvCols);
        const float headerH = 44.0f, barH = 20.0f, invLabelH = 20.0f;
        const float tooltipH = 18.0f, footerH = 24.0f;
        const float invH = L.invRows * (PanelLayout::Cell + PanelLayout::Gap);
        L.panelH = headerH + actionRows * PanelLayout::RowH + 2.0f * PanelLayout::StripH +
                   barH + invLabelH + invH + tooltipH + footerH + 12.0f;
        L.px = (static_cast<float>(w) - L.panelW) * 0.5f;
        L.py = (static_cast<float>(h) - L.panelH) * 0.5f;
        L.rowsY = L.py + headerH;
        L.inY = L.rowsY + actionRows * PanelLayout::RowH + 6.0f;
        L.outY = L.inY + PanelLayout::StripH;
        L.stripCellsX = L.px + 64.0f;
        L.barY = L.outY + PanelLayout::StripH + 2.0f;
        L.invLabelY = L.barY + barH;
        L.invY = L.invLabelY + invLabelH;
        L.tooltipY = L.invY + invH + 2.0f;
        L.footerY = L.py + L.panelH - footerH + 2.0f;
        return L;
    }

    // The item types present in an inventory, with counts, in enum order.
    std::vector<std::pair<ItemId, int>> itemsOf(const Inventory& inv) {
        std::vector<std::pair<ItemId, int>> out;
        for (int i = 1; i < static_cast<int>(ItemId::Count); ++i) {
            const ItemId id = static_cast<ItemId>(i);
            if (inv.count(id) > 0) out.emplace_back(id, inv.count(id));
        }
        return out;
    }

    // Which cell of a grid at (originX, originY) is under the mouse? -1 = none.
    int hitCell(float mx, float my, float originX, float originY, int count, int cols) {
        for (int i = 0; i < count; ++i) {
            const float cx = originX + (i % cols) * (PanelLayout::Cell + PanelLayout::Gap);
            const float cy = originY + (i / cols) * (PanelLayout::Cell + PanelLayout::Gap);
            if (mx >= cx && mx < cx + PanelLayout::Cell &&
                my >= cy && my < cy + PanelLayout::Cell) {
                return i;
            }
        }
        return -1;
    }

    // One item cell: dark slab, icon, count (shared by machine panel + menu).
    void drawItemCell(engine::UiRenderer& ui, const engine::Texture& atlas,
                      float x, float y, ItemId id, int count) {
        ui.rect(x, y, PanelLayout::Cell, PanelLayout::Cell, glm::vec4(0.16f, 0.16f, 0.19f, 1.0f));
        glm::vec2 uv0, uv1;
        Atlas::uvForTile(iconTile(id), uv0, uv1);
        ui.icon(atlas, x + 4, y + 4, PanelLayout::Cell - 8, PanelLayout::Cell - 8, uv0, uv1);
        const std::string cnt = std::to_string(count);
        ui.text(x + PanelLayout::Cell - ui.textWidth(11.0f, cnt) - 3,
                y + PanelLayout::Cell - 13, 11.0f, cnt, glm::vec4(1.0f));
    }

    // ---- The shared panel palette (every overlay draws from these). ----
    constexpr glm::vec4 kPanelBg    {0.08f, 0.08f, 0.10f, 0.96f};
    constexpr glm::vec4 kRowSelBg   {0.9f, 0.75f, 0.15f, 0.85f};
    constexpr glm::vec4 kTextOnSel  {0.05f, 0.05f, 0.05f, 1.0f};
    constexpr glm::vec4 kTextMain   {0.90f, 0.90f, 0.92f, 1.0f};
    constexpr glm::vec4 kTextDim    {0.45f, 0.45f, 0.48f, 1.0f};
    constexpr glm::vec4 kTextHeader {1.0f, 1.0f, 0.7f, 1.0f};
    constexpr glm::vec4 kTextFooter {0.7f, 0.7f, 0.75f, 1.0f};
    constexpr glm::vec4 kTextTooltip{1.0f, 1.0f, 0.8f, 1.0f};

    // Row label color for the standard states: selected (dark on the gold
    // slab), normal, or dimmed (unaffordable / informational).
    glm::vec4 rowColor(bool selected, bool dim = false) {
        return selected ? kTextOnSel : dim ? kTextDim : kTextMain;
    }

    // Frame a panel the way every overlay does: begin, full-screen dim,
    // panel slab, header title (pass nullptr for none).
    void beginPanel(engine::UiRenderer& ui, int w, int h, float px, float py,
                    float panelW, float panelH, const char* title, float dimAlpha) {
        ui.begin(w, h);
        ui.rect(0, 0, static_cast<float>(w), static_cast<float>(h),
                glm::vec4(0, 0, 0, dimAlpha));
        ui.rect(px, py, panelW, panelH, kPanelBg);
        if (title) ui.text(px + 16, py + 12, 18.0f, title, kTextHeader);
    }

    // MenuList: one frame of the shared list idiom — W/S + arrows wrap-
    // navigate, optional wheel, the cursor picks the row it moves over,
    // Enter or LMB inside the rows activates. The caller owns the selection
    // int, what activation means, and the sounds (play "click" on .changed).
    struct MenuNav {
        bool changed = false;     // selection moved this frame
        bool enter = false;       // Enter / keypad Enter
        bool clickedRows = false; // LMB pressed inside the rows area
        int  hoverRow = -1;       // row under the cursor; -1 = outside
        bool activated() const { return enter || clickedRows; }
    };

    MenuNav menuNav(engine::Input& in, int& sel, int rows,
                    float px, float panelW, float rowsY, float rowH,
                    bool useWheel = false) {
        const int before = sel;
        if (in.wasKeyPressed(SDL_SCANCODE_W) || in.wasKeyPressed(SDL_SCANCODE_UP)) {
            sel = (sel - 1 + rows) % rows;
        }
        if (in.wasKeyPressed(SDL_SCANCODE_S) || in.wasKeyPressed(SDL_SCANCODE_DOWN)) {
            sel = (sel + 1) % rows;
        }
        if (useWheel) {
            const int wheel = in.wheelSteps();
            if (wheel != 0) sel = ((sel - wheel) % rows + rows) % rows;
        }
        const float mx = in.mouseX(), my = in.mouseY();
        const bool overRows = mx >= px && mx <= px + panelW &&
                              my >= rowsY && my < rowsY + rows * rowH;
        MenuNav r;
        if (overRows) r.hoverRow = static_cast<int>((my - rowsY) / rowH);
        if ((in.mouseRelX() != 0.0f || in.mouseRelY() != 0.0f) && overRows) {
            sel = r.hoverRow;
        }
        sel = std::min(sel, rows - 1);
        r.changed = (sel != before);
        r.enter = in.wasKeyPressed(SDL_SCANCODE_RETURN) ||
                  in.wasKeyPressed(SDL_SCANCODE_KP_ENTER);
        r.clickedRows = in.wasMousePressed(SDL_BUTTON_LEFT) && overRows;
        return r;
    }

    // ItemGrid: draw `items` as rows of `cols` cells at (x, y). Hit-testing
    // and tooltips share the geometry via hitCell / hoveredItemIn.
    void drawItemGrid(engine::UiRenderer& ui, const engine::Texture& atlas,
                      float x, float y,
                      const std::vector<std::pair<ItemId, int>>& items,
                      int cols = PanelLayout::InvCols) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            drawItemCell(ui, atlas,
                         x + (i % cols) * (PanelLayout::Cell + PanelLayout::Gap),
                         y + (i / cols) * (PanelLayout::Cell + PanelLayout::Gap),
                         items[i].first, items[i].second);
        }
    }

    ItemId hoveredItemIn(const std::vector<std::pair<ItemId, int>>& items,
                         float mx, float my, float x, float y,
                         int cols = PanelLayout::InvCols) {
        const int i = hitCell(mx, my, x, y, static_cast<int>(items.size()), cols);
        return i >= 0 ? items[i].first : ItemId::None;
    }

    // A plain text menu row (the pause + settings panels share this exact
    // geometry: gold slab inset 6, label at +20/+6, size 14).
    void drawSimpleRow(engine::UiRenderer& ui, float px, float panelW, float ry,
                       float rowH, const std::string& label, bool selected) {
        if (selected) {
            ui.rect(px + 6, ry, panelW - 12, rowH - 4, kRowSelBg);
        }
        ui.text(px + 20, ry + 6, 14.0f, label, rowColor(selected));
    }

    // Crafting-menu layout (rows + a materials grid); shared by update + draw.
    struct CraftLayout {
        static constexpr float RowH = 22.0f;
        float px = 0, py = 0, panelW = 700.0f, panelH = 0;
        float rowsY = 0;
        int   rows = 0;
        float invLabelY = 0, invY = 0;
        int   invRows = 0;
        float tooltipY = 0, footerY = 0;
    };

    CraftLayout craftLayout(int w, int h, int nRecipes, int invCount) {
        CraftLayout L;
        L.rows = nRecipes;
        L.invRows = std::max(1, (invCount + PanelLayout::InvCols - 1) / PanelLayout::InvCols);
        const float headerH = 40.0f, invLabelH = 20.0f, tooltipH = 16.0f, footerH = 22.0f;
        const float invH = L.invRows * (PanelLayout::Cell + PanelLayout::Gap);
        L.panelH = headerH + nRecipes * CraftLayout::RowH + invLabelH + invH +
                   tooltipH + footerH + 8.0f;
        L.px = (static_cast<float>(w) - L.panelW) * 0.5f;
        L.py = (static_cast<float>(h) - L.panelH) * 0.5f;
        L.rowsY = L.py + headerH;
        L.invLabelY = L.rowsY + nRecipes * CraftLayout::RowH + 2.0f;
        L.invY = L.invLabelY + invLabelH;
        L.tooltipY = L.invY + invH + 2.0f;
        L.footerY = L.py + L.panelH - footerH + 2.0f;
        return L;
    }

    // Inventory-overlay layout (owned-items grid + the hotbar assignment
    // strip); shared by update (hit-testing) and draw.
    struct InvLayout {
        float px = 0, py = 0, panelW = 560.0f, panelH = 0;
        float invY = 0; // owned-items grid, PanelLayout::InvCols wide
        int   invRows = 0;
        float tooltipY = 0;
        float hotbarLabelY = 0, hotbarY = 0; // the kHotbarSlots assignment cells
        float footerY = 0;
    };

    InvLayout invLayout(int w, int h, int invCount) {
        InvLayout L;
        L.invRows = std::max(1, (invCount + PanelLayout::InvCols - 1) / PanelLayout::InvCols);
        const float headerH = 40.0f, tooltipH = 18.0f, labelH = 20.0f, footerH = 24.0f;
        const float invH = L.invRows * (PanelLayout::Cell + PanelLayout::Gap);
        const float hotbarH = PanelLayout::Cell + PanelLayout::Gap;
        L.panelH = headerH + invH + tooltipH + labelH + hotbarH + footerH + 12.0f;
        L.px = (static_cast<float>(w) - L.panelW) * 0.5f;
        L.py = (static_cast<float>(h) - L.panelH) * 0.5f;
        L.invY = L.py + headerH;
        L.tooltipY = L.invY + invH + 2.0f;
        L.hotbarLabelY = L.tooltipY + tooltipH;
        L.hotbarY = L.hotbarLabelY + labelH;
        L.footerY = L.py + L.panelH - footerH + 2.0f;
        return L;
    }

    // Grey-out for hotbar cells whose item is assigned but out of stock.
    constexpr glm::vec4 kOutOfStockTint{0.45f, 0.45f, 0.5f, 0.8f};

} // namespace

void VoxelGame::drawDebugOverlay() {
    const int w = window().width();
    const int h = window().height();

    char line[96];
    m_ui.begin(w, h);
    m_ui.rect(8, 8, 360, 88, glm::vec4(0.05f, 0.05f, 0.08f, 0.82f));

    std::snprintf(line, sizeof(line), "FRAME AVG %5.1f MS  WORST %6.1f MS",
                  m_perf.avgMs, m_perf.worstMs);
    m_ui.text(16, 16, 12.0f, line, glm::vec4(0.9f, 0.9f, 0.92f, 1.0f));

    std::snprintf(line, sizeof(line), "REMESH %6.1f MS  %d CHUNKS  X%d PER S",
                  m_perf.lastRemeshMs, m_perf.chunksRemeshed, m_perf.remeshesPerSec);
    m_ui.text(16, 34, 12.0f, line, glm::vec4(0.9f, 0.9f, 0.92f, 1.0f));

    std::snprintf(line, sizeof(line), "POWER  %6.1f MS  X%d PER S",
                  m_perf.lastSolveMs, m_perf.solvesPerSec);
    m_ui.text(16, 52, 12.0f, line, glm::vec4(0.9f, 0.9f, 0.92f, 1.0f));

    m_ui.text(16, 70, 12.0f, "VOXEL FACTORY V" VOXEL_FACTORY_VERSION,
              glm::vec4(0.6f, 0.6f, 0.65f, 1.0f));

    m_ui.end();
}

void VoxelGame::drawMachineUi() {
    const auto mit = m_machines.find(m_machineUiPos);
    if (mit == m_machines.end()) return;
    const Machine& mac = mit->second;

    const auto recipes = recipesForMachine(mac.type);
    const int rows = static_cast<int>(recipes.size()) + 2;
    const auto invItems = itemsOf(m_inventory);
    const auto inItems = itemsOf(mac.input);
    const auto outItems = itemsOf(mac.output);

    const int w = window().width();
    const int h = window().height();
    const PanelLayout L = panelLayout(w, h, rows, static_cast<int>(invItems.size()));
    const float mx = input().mouseX(), my = input().mouseY();

    beginPanel(m_ui, w, h, L.px, L.py, L.panelW, L.panelH, blockName(mac.type), 0.45f);

    // Header status (generators report their burn instead of network power --
    // their energized state is their own doing).
    const MachineTraits& traits = machineTraits(mac.type);
    switch (traits.kind) {
        case MachineKind::Generator: {
            const bool burning = mac.progress > 0.0f;
            const bool hungry = m_hungryGenerators.count(m_machineUiPos) > 0;
            const char* status = burning ? "BURNING" : hungry ? "OUT OF FUEL" : "IDLE";
            const glm::vec4 col = burning ? glm::vec4(0.4f, 0.95f, 0.45f, 1.0f)
                                : hungry  ? glm::vec4(0.95f, 0.4f, 0.35f, 1.0f)
                                          : glm::vec4(0.6f, 0.6f, 0.65f, 1.0f);
            m_ui.text(L.px + L.panelW - 150, L.py + 15, 13.0f, status, col);
            break;
        }
        case MachineKind::Collector: {
            const bool full = mac.output.count(traits.collects) >= traits.collectCap;
            const char* status = full ? "FULL" : mac.crafting ? "COLLECTING" : "WAITING FOR RAIN";
            const glm::vec4 col = full ? glm::vec4(1.0f, 0.85f, 0.3f, 1.0f)
                                : mac.crafting ? glm::vec4(0.45f, 0.7f, 0.95f, 1.0f)
                                               : glm::vec4(0.6f, 0.6f, 0.65f, 1.0f);
            m_ui.text(L.px + L.panelW - 175, L.py + 15, 13.0f, status, col);
            break;
        }
        default: {
            const bool powered = m_power.energized(m_machineUiPos.x, m_machineUiPos.y, m_machineUiPos.z);
            m_ui.text(L.px + L.panelW - 150, L.py + 15, 13.0f, powered ? "POWERED" : "NO POWER",
                      powered ? glm::vec4(0.4f, 0.95f, 0.45f, 1.0f) : glm::vec4(0.95f, 0.4f, 0.35f, 1.0f));
            break;
        }
    }

    // Action rows.
    for (int i = 0; i < rows; ++i) {
        const float ry = L.rowsY + i * PanelLayout::RowH;
        const bool selected = (i == m_machineUiSel);
        if (selected) {
            m_ui.rect(L.px + 6, ry, L.panelW - 12, PanelLayout::RowH - 2, kRowSelBg);
        }

        std::string label;
        bool actionable = false;
        if (i == 0) {
            // Row 0 is the machine's personality: an info line for the
            // bespoke kinds, the AUTO selector for recipe machines.
            actionable = true;
            switch (traits.kind) {
                case MachineKind::Generator:
                    label = std::string("  BURNS ") + itemName(traits.fuel) + " ( " +
                            std::to_string(static_cast<int>(traits.burnSeconds)) +
                            "S PER " + itemName(traits.fuel) +
                            ", ONLY WHILE A NETWORK NEEDS POWER )";
                    break;
                case MachineKind::Collector:
                    label = std::string("  COLLECTS ") + itemName(traits.collects) +
                            " ( NEEDS OPEN SKY ABOVE )";
                    break;
                case MachineKind::Miner: { // status row shows the filter
                    const ItemId filter = MachineSystem::minerFilter(mac);
                    label = (filter == ItemId::None)
                        ? "  MINES: ANY NEARBY NODE ( RADIUS " +
                          std::to_string(kMineRadius) + " )"
                        : std::string("  MINES: ") + itemName(filter) + " ONLY ( RADIUS " +
                          std::to_string(kMineRadius) + " )";
                    break;
                }
                case MachineKind::Processor:
                    label = std::string(mac.selectedRecipe < 0 ? "> " : "  ") +
                            "AUTO ( FIRST READY RECIPE )";
                    break;
            }
        } else if (i <= static_cast<int>(recipes.size())) {
            const MachineRecipe& r = *recipes[i - 1];
            label = std::string(mac.selectedRecipe == i - 1 ? "> " : "  ") +
                    "MAKE " + itemName(r.output.id) + "  (";
            actionable = false; // white if the player can contribute anything
            for (const ItemStack& in : r.inputs) {
                label += " " + std::string(itemName(in.id));
                if (in.count > 1) label += " x" + std::to_string(in.count);
                if (m_inventory.has(in.id, 1)) actionable = true;
            }
            label += " )";
        } else {
            label = "  TAKE OUTPUTS";
            for (int k = 1; k < static_cast<int>(ItemId::Count); ++k) {
                if (mac.output.count(static_cast<ItemId>(k)) > 0) { actionable = true; break; }
            }
        }

        m_ui.text(L.px + 16, ry + 5, 14.0f, label, rowColor(selected, !actionable));
    }

    // IN strip (highlighted as the drop target while dragging from inventory).
    if (m_drag.active() && m_drag.source == Drag::Source::PlayerInv) {
        const bool ok = MachineSystem::machineAccepts(mac, m_drag.id);
        m_ui.rect(L.px + 8, L.inY, L.panelW - 16, PanelLayout::StripH - 4,
                  ok ? glm::vec4(0.20f, 0.55f, 0.25f, 0.45f) : glm::vec4(0.55f, 0.20f, 0.20f, 0.45f));
    }
    m_ui.text(L.px + 16, L.inY + 16, 13.0f, "IN:", glm::vec4(0.85f, 0.85f, 0.9f, 1.0f));
    drawItemGrid(m_ui, m_atlas, L.stripCellsX, L.inY + 6.0f, inItems, /*cols=*/99);

    // OUT strip.
    m_ui.text(L.px + 16, L.outY + 16, 13.0f, "OUT:", glm::vec4(0.85f, 0.9f, 0.85f, 1.0f));
    drawItemGrid(m_ui, m_atlas, L.stripCellsX, L.outY + 6.0f, outItems, /*cols=*/99);

    // Progress bar.
    m_ui.rect(L.px + 16, L.barY, L.panelW - 32, 10, glm::vec4(0.0f, 0.0f, 0.0f, 0.8f));
    if (mac.crafting) {
        const float frac = glm::clamp(mac.craftTime > 0 ? mac.progress / mac.craftTime : 0.0f, 0.0f, 1.0f);
        m_ui.rect(L.px + 16, L.barY, (L.panelW - 32) * frac, 10, glm::vec4(0.30f, 0.90f, 0.40f, 0.95f));
    }

    // Inventory grid.
    m_ui.text(L.px + 16, L.invLabelY + 2, 13.0f, "INVENTORY", kTextHeader);
    drawItemGrid(m_ui, m_atlas, L.px + 16.0f, L.invY, invItems);

    // Tooltip: name of the hovered cell (any of the three regions).
    ItemId hovered = hoveredItemIn(invItems, mx, my, L.px + 16.0f, L.invY);
    if (hovered == ItemId::None) {
        hovered = hoveredItemIn(inItems, mx, my, L.stripCellsX, L.inY + 6.0f, 99);
    }
    if (hovered == ItemId::None) {
        hovered = hoveredItemIn(outItems, mx, my, L.stripCellsX, L.outY + 6.0f, 99);
    }
    if (hovered != ItemId::None) {
        m_ui.text(L.px + 16, L.tooltipY, 12.0f, itemName(hovered), kTextTooltip);
    }

    m_ui.text(L.px + 16, L.footerY, 12.0f,
              "DRAG ITEMS: LMB STACK / RMB ONE   ROWS: CLICK OR W/S + ENTER   ESC CLOSE",
              kTextFooter);

    // Drag payload rides the cursor, drawn last so it sits on top.
    if (m_drag.active()) {
        glm::vec2 uv0, uv1;
        Atlas::uvForTile(iconTile(m_drag.id), uv0, uv1);
        const float s = 34.0f;
        m_ui.icon(m_atlas, mx - s * 0.5f, my - s * 0.5f, s, s, uv0, uv1);
        m_ui.text(mx + s * 0.35f, my + s * 0.2f, 12.0f, std::to_string(m_drag.count),
                  glm::vec4(1.0f));
    }

    m_ui.end();
}

void VoxelGame::openMachineUi(const glm::ivec3& pos) {
    m_machineUiOpen = true;
    m_machineUiPos = pos;
    m_machineUiSel = 0;
    window().setRelativeMouse(false); // release the cursor for hover/click
    audio().play("open", kUiVolume);
}

void VoxelGame::closeMachineUi() {
    cancelDrag(); // never close with items in hand
    m_machineUiOpen = false;
    window().setRelativeMouse(true);
    audio().play("close", kUiVolume);
}

void VoxelGame::cancelDrag() {
    if (!m_drag.active()) {
        m_drag = Drag{};
        return;
    }
    if (m_drag.source == Drag::Source::PlayerInv) {
        m_inventory.add(m_drag.id, m_drag.count);
    } else {
        const auto mit = m_machines.find(m_machineUiPos);
        if (mit != m_machines.end()) {
            Inventory& buf = (m_drag.source == Drag::Source::MachineIn) ? mit->second.input
                                                                        : mit->second.output;
            buf.add(m_drag.id, m_drag.count);
        } else {
            m_inventory.add(m_drag.id, m_drag.count); // machine vanished
        }
    }
    m_drag = Drag{};
}

void VoxelGame::updateMachineUi() {
    const auto mit = m_machines.find(m_machineUiPos);
    if (mit == m_machines.end()) { // machine no longer exists
        cancelDrag();
        closeMachineUi();
        return;
    }
    Machine& mac = mit->second;

    // Action rows: AUTO, one MAKE row per recipe, then TAKE OUTPUTS.
    const auto recipes = recipesForMachine(mac.type);
    const int rows = static_cast<int>(recipes.size()) + 2;
    const auto invItems = itemsOf(m_inventory);
    const auto inItems = itemsOf(mac.input);
    const auto outItems = itemsOf(mac.output);
    const PanelLayout L = panelLayout(window().width(), window().height(), rows,
                                      static_cast<int>(invItems.size()));
    const float mx = input().mouseX(), my = input().mouseY();
    const bool inPanelX = mx >= L.px && mx <= L.px + L.panelW;

    const MenuNav nav = menuNav(input(), m_machineUiSel, rows, L.px, L.panelW,
                                L.rowsY, PanelLayout::RowH);
    if (nav.changed) audio().play("click", kUiVolume);

    const bool lmb = input().wasMousePressed(SDL_BUTTON_LEFT);
    const bool rmb = input().wasMousePressed(SDL_BUTTON_RIGHT);
    bool clickConsumed = false;

    // --- Drag pickup: LMB = whole stack, RMB = one item. ---
    if (!m_drag.active() && (lmb || rmb)) {
        int ci;
        if ((ci = hitCell(mx, my, L.px + 16.0f, L.invY,
                          static_cast<int>(invItems.size()), PanelLayout::InvCols)) >= 0) {
            const auto [id, cnt] = invItems[ci];
            const int take = lmb ? cnt : 1;
            m_inventory.remove(id, take);
            m_drag = {Drag::Source::PlayerInv, id, take};
            clickConsumed = true;
        } else if ((ci = hitCell(mx, my, L.stripCellsX, L.inY + 6.0f,
                                 static_cast<int>(inItems.size()), 99)) >= 0) {
            const auto [id, cnt] = inItems[ci];
            const int take = lmb ? cnt : 1;
            mac.input.remove(id, take);
            m_drag = {Drag::Source::MachineIn, id, take};
            clickConsumed = true;
        } else if ((ci = hitCell(mx, my, L.stripCellsX, L.outY + 6.0f,
                                 static_cast<int>(outItems.size()), 99)) >= 0) {
            const auto [id, cnt] = outItems[ci];
            const int take = lmb ? cnt : 1;
            mac.output.remove(id, take);
            m_drag = {Drag::Source::MachineOut, id, take};
            clickConsumed = true;
        }
        if (clickConsumed) audio().play("click", kUiVolume); // picked something up
    }

    // --- Drag drop. ---
    if (m_drag.active() && (input().wasMouseReleased(SDL_BUTTON_LEFT) ||
                            input().wasMouseReleased(SDL_BUTTON_RIGHT))) {
        const bool overIn = inPanelX && my >= L.inY && my < L.inY + PanelLayout::StripH;
        const bool overInv = inPanelX && my >= L.invY &&
                             my < L.invY + L.invRows * (PanelLayout::Cell + PanelLayout::Gap);
        if (overIn && m_drag.source == Drag::Source::PlayerInv &&
            MachineSystem::machineAccepts(mac, m_drag.id)) {
            mac.input.add(m_drag.id, m_drag.count);
            m_drag = Drag{};
        } else if (overInv && m_drag.source != Drag::Source::PlayerInv) {
            m_inventory.add(m_drag.id, m_drag.count);
            m_drag = Drag{};
        } else {
            cancelDrag(); // anywhere else: the payload goes back where it came from
        }
        audio().play("click", kUiVolume);
        updateTitle();
        clickConsumed = true;
    }

    // --- Row activation: Enter always; LMB only when over the rows area
    // and not already consumed by a drag pickup/drop. ---
    if (!m_drag.active() && (nav.enter || (nav.clickedRows && !clickConsumed))) {
        audio().play("click", kUiVolume);
        if (m_machineUiSel == 0) {
            // AUTO: run whichever recipe's inputs are ready first. (For
            // recipe-less machines like the Miner this row is informational.)
            mac.selectedRecipe = -1;
            if (!recipes.empty()) mac.progress = 0.0f;
        } else if (m_machineUiSel <= static_cast<int>(recipes.size())) {
            // MAKE row: lock the machine to this recipe and load the player's
            // matching ingredients (each input independently, so the player
            // can contribute just what they carry).
            const int idx = m_machineUiSel - 1;
            if (mac.selectedRecipe != idx) {
                mac.selectedRecipe = idx;
                mac.progress = 0.0f; // switching recipes restarts the craft
            }
            for (const ItemStack& in : recipes[idx]->inputs) {
                const int move = std::min(m_inventory.count(in.id), in.count * kLoadPerAction);
                if (move > 0) {
                    m_inventory.remove(in.id, move);
                    mac.input.add(in.id, move);
                }
            }
        } else {
            // Take all outputs.
            for (int i = 0; i < static_cast<int>(ItemId::Count); ++i) {
                const ItemId id = static_cast<ItemId>(i);
                const int c = mac.output.count(id);
                if (c > 0) {
                    mac.output.remove(id, c);
                    m_inventory.add(id, c);
                }
            }
        }
        updateTitle();
    }

    // E always closes; RMB closes only when it wasn't a pickup/drop.
    if (input().wasKeyPressed(SDL_SCANCODE_E) || (rmb && !clickConsumed && !m_drag.active())) {
        cancelDrag();
        closeMachineUi();
    }
}

bool VoxelGame::canCraft(const Recipe& r) const {
    for (const ItemStack& in : r.inputs) {
        if (!m_inventory.has(in.id, in.count)) return false;
    }
    return true;
}

void VoxelGame::tryCraft(const Recipe& r) {
    if (!canCraft(r)) {
        audio().play("deny", kCraftVolume);
        return;
    }
    for (const ItemStack& in : r.inputs) {
        m_inventory.remove(in.id, in.count);
    }
    m_inventory.add(r.output.id, r.output.count);
    audio().play("craft", kCraftVolume);
    updateTitle();
}

void VoxelGame::updateMenu() {
    const auto& recipes = handcraftRecipes();
    const int n = static_cast<int>(recipes.size());
    if (n == 0) return;

    const auto invItems = itemsOf(m_inventory);
    const CraftLayout L = craftLayout(window().width(), window().height(), n,
                                      static_cast<int>(invItems.size()));

    const MenuNav nav = menuNav(input(), m_menuSelection, n, L.px, L.panelW,
                                L.rowsY, CraftLayout::RowH, /*useWheel=*/true);
    if (nav.changed) audio().play("click", kUiVolume);

    // Enter always crafts the selection; LMB crafts the row it lands on.
    if (nav.enter) {
        tryCraft(recipes[m_menuSelection]);
    } else if (nav.clickedRows) {
        m_menuSelection = nav.hoverRow;
        tryCraft(recipes[m_menuSelection]);
    }

    // RMB also closes (E and Esc are handled elsewhere).
    if (input().wasMousePressed(SDL_BUTTON_RIGHT)) {
        m_menuOpen = false;
        window().setRelativeMouse(true);
        audio().play("close", kUiVolume);
    }
}

void VoxelGame::openInventoryUi() {
    m_invOpen = true;
    window().setRelativeMouse(false); // release the cursor for drag/hover
    audio().play("open", kUiVolume);
}

void VoxelGame::closeInventoryUi() {
    m_invDrag = ItemId::None; // an unfinished drag assigns nothing
    m_invOpen = false;
    window().setRelativeMouse(true);
    audio().play("close", kUiVolume);
}

void VoxelGame::updateInventoryUi() {
    const auto invItems = itemsOf(m_inventory);
    const InvLayout L = invLayout(window().width(), window().height(),
                                  static_cast<int>(invItems.size()));
    const float mx = input().mouseX(), my = input().mouseY();

    const int gridHit = hitCell(mx, my, L.px + 16.0f, L.invY,
                                static_cast<int>(invItems.size()), PanelLayout::InvCols);
    const int slotHit = hitCell(mx, my, L.px + 16.0f, L.hotbarY, kHotbarSlots,
                                PanelLayout::InvCols);

    // LMB press picks up an assignment: from the grid it's a reference copy
    // (the item stays in the inventory); from a slot it clears the slot and
    // carries its assignment (rearrange).
    if (m_invDrag == ItemId::None && input().wasMousePressed(SDL_BUTTON_LEFT)) {
        if (gridHit >= 0) {
            m_invDrag = invItems[gridHit].first;
            audio().play("click", kUiVolume);
        } else if (slotHit >= 0 && m_hotbar[slotHit] != ItemId::None) {
            m_invDrag = m_hotbar[slotHit];
            m_hotbar[slotHit] = ItemId::None;
            audio().play("click", kUiVolume);
            updateTitle();
        }
    }

    // LMB release over a slot assigns; any other slot already holding the
    // item is cleared first (an item lives on at most one slot). A release
    // anywhere else just drops the drag -- which for a slot-sourced drag IS
    // the clear gesture.
    if (m_invDrag != ItemId::None && input().wasMouseReleased(SDL_BUTTON_LEFT)) {
        if (slotHit >= 0) {
            for (ItemId& s : m_hotbar) {
                if (s == m_invDrag) s = ItemId::None;
            }
            m_hotbar[slotHit] = m_invDrag;
        }
        m_invDrag = ItemId::None;
        audio().play("click", kUiVolume);
        updateTitle();
    }

    // RMB on a slot clears it directly; elsewhere it closes (menu precedent).
    if (input().wasMousePressed(SDL_BUTTON_RIGHT)) {
        if (slotHit >= 0 && m_hotbar[slotHit] != ItemId::None) {
            m_hotbar[slotHit] = ItemId::None;
            audio().play("click", kUiVolume);
            updateTitle();
        } else if (m_invDrag == ItemId::None) {
            closeInventoryUi();
        }
    }
}

void VoxelGame::drawInventoryUi() {
    const int w = window().width();
    const int h = window().height();
    const auto invItems = itemsOf(m_inventory);
    const InvLayout L = invLayout(w, h, static_cast<int>(invItems.size()));
    const float mx = input().mouseX(), my = input().mouseY();

    beginPanel(m_ui, w, h, L.px, L.py, L.panelW, L.panelH, "INVENTORY", 0.5f);

    // Everything the player owns, with counts.
    drawItemGrid(m_ui, m_atlas, L.px + 16.0f, L.invY, invItems);

    // The hotbar strip: assignment slots (empty slab / greyed at count 0),
    // with the selected slot outlined like the HUD.
    m_ui.text(L.px + 16, L.hotbarLabelY + 2, 13.0f, "HOTBAR", kTextHeader);
    const int slotHover = hitCell(mx, my, L.px + 16.0f, L.hotbarY, kHotbarSlots,
                                  PanelLayout::InvCols);
    for (int i = 0; i < kHotbarSlots; ++i) {
        const float cx = L.px + 16.0f + i * (PanelLayout::Cell + PanelLayout::Gap);
        const float cy = L.hotbarY;
        if (m_invDrag != ItemId::None && slotHover == i) {
            // Drop-target highlight (the IN-strip convention).
            m_ui.rect(cx - 3, cy - 3, PanelLayout::Cell + 6, PanelLayout::Cell + 6,
                      glm::vec4(0.20f, 0.55f, 0.25f, 0.9f));
        } else if (i == m_selectedSlot) {
            m_ui.rect(cx - 3, cy - 3, PanelLayout::Cell + 6, PanelLayout::Cell + 6,
                      glm::vec4(1.0f, 0.85f, 0.2f, 0.95f));
        }
        m_ui.rect(cx, cy, PanelLayout::Cell, PanelLayout::Cell,
                  glm::vec4(0.16f, 0.16f, 0.19f, 1.0f));
        const std::string key = (i < 9) ? std::to_string(i + 1) : "0";
        m_ui.text(cx + 3, cy + 3, 10.0f, key, glm::vec4(0.75f, 0.75f, 0.8f, 1.0f));
        const ItemId id = m_hotbar[i];
        if (id == ItemId::None) continue; // empty slab; iconTile(None) is -1
        const int count = m_inventory.count(id);
        glm::vec2 uv0, uv1;
        Atlas::uvForTile(iconTile(id), uv0, uv1);
        m_ui.icon(m_atlas, cx + 4, cy + 4, PanelLayout::Cell - 8, PanelLayout::Cell - 8,
                  uv0, uv1, count > 0 ? glm::vec4(1.0f) : kOutOfStockTint);
        const std::string cnt = std::to_string(count);
        m_ui.text(cx + PanelLayout::Cell - m_ui.textWidth(11.0f, cnt) - 3,
                  cy + PanelLayout::Cell - 13, 11.0f, cnt,
                  count > 0 ? glm::vec4(1.0f) : glm::vec4(0.55f, 0.55f, 0.6f, 1.0f));
    }

    // Tooltip: name of the hovered cell (grid or hotbar strip).
    ItemId hovered = hoveredItemIn(invItems, mx, my, L.px + 16.0f, L.invY);
    if (hovered == ItemId::None && slotHover >= 0) {
        hovered = m_hotbar[slotHover];
    }
    if (hovered != ItemId::None) {
        m_ui.text(L.px + 16, L.tooltipY, 12.0f, itemName(hovered), kTextTooltip);
    }

    m_ui.text(L.px + 16, L.footerY, 12.0f,
              "DRAG TO A SLOT TO ASSIGN   RMB SLOT CLEAR   TAB / ESC CLOSE",
              kTextFooter);

    // The dragged assignment rides the cursor, drawn last so it sits on top
    // (no count: it's a reference, not a stack).
    if (m_invDrag != ItemId::None) {
        glm::vec2 uv0, uv1;
        Atlas::uvForTile(iconTile(m_invDrag), uv0, uv1);
        const float s = 34.0f;
        m_ui.icon(m_atlas, mx - s * 0.5f, my - s * 0.5f, s, s, uv0, uv1);
    }

    m_ui.end();
}

void VoxelGame::drawHud() {
    const int w = window().width();
    const int h = window().height();
    m_ui.begin(w, h);

    const int n = kHotbarSlots;
    const float slot = 52.0f, gap = 6.0f, pad = 6.0f;
    const float totalW = n * slot + (n - 1) * gap;
    const float x0 = (static_cast<float>(w) - totalW) * 0.5f;
    const float y = static_cast<float>(h) - slot - 22.0f;

    for (int i = 0; i < n; ++i) {
        const ItemId item = m_hotbar[i];
        const float sx = x0 + i * (slot + gap);

        if (i == m_selectedSlot) {
            m_ui.rect(sx - 3, y - 3, slot + 6, slot + 6, glm::vec4(1.0f, 0.85f, 0.2f, 0.95f));
        }
        m_ui.rect(sx, y, slot, slot, glm::vec4(0.10f, 0.10f, 0.12f, 0.85f));

        // Key label (top-left). Unassigned slots stay an empty slab.
        const std::string key = (i < 9) ? std::to_string(i + 1) : "0";
        m_ui.text(sx + 4, y + 4, 11.0f, key, glm::vec4(0.75f, 0.75f, 0.8f, 1.0f));
        if (item == ItemId::None) continue;

        // An assigned-but-out-of-stock slot greys out and re-enables on
        // restock (the count is read live from the inventory).
        const int count = m_inventory.count(item);
        glm::vec2 uv0, uv1;
        Atlas::uvForTile(iconTile(item), uv0, uv1);
        m_ui.icon(m_atlas, sx + pad, y + pad, slot - 2 * pad, slot - 2 * pad, uv0, uv1,
                  count > 0 ? glm::vec4(1.0f) : kOutOfStockTint);
        const std::string cnt = std::to_string(count);
        const float th = 14.0f;
        m_ui.text(sx + slot - m_ui.textWidth(th, cnt) - 4, y + slot - th - 4, th, cnt,
                  count > 0 ? glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)
                            : glm::vec4(0.55f, 0.55f, 0.6f, 1.0f));
    }

    // Boss HP bar: top-center while a boss lives in the active dimension.
    if (m_creatures.bossAlive(m_dimension)) {
        const float bw = 420.0f, bh = 14.0f;
        const float bx = (static_cast<float>(w) - bw) * 0.5f;
        const float by = 26.0f;
        const char* name = m_creatures.bossName(m_dimension);
        m_ui.text(bx + (bw - m_ui.textWidth(15.0f, name)) * 0.5f, by - 20, 15.0f,
                  name, glm::vec4(0.85f, 0.6f, 1.0f, 1.0f));
        m_ui.rect(bx - 2, by - 2, bw + 4, bh + 4, glm::vec4(0.0f, 0.0f, 0.0f, 0.75f));
        m_ui.rect(bx, by, bw * m_creatures.bossHpFrac(m_dimension), bh,
                  glm::vec4(0.62f, 0.25f, 0.9f, 0.95f));
    }

    // Health: heart segments above the hotbar's left end (partial segments
    // show fractional damage, e.g. from a hard landing).
    {
        const float segW = 16.0f, segH = 12.0f, segGap = 4.0f;
        const float hy = y - segH - 10.0f;
        for (int i = 0; i < static_cast<int>(kMaxHealth); ++i) {
            const float hx = x0 + i * (segW + segGap);
            m_ui.rect(hx, hy, segW, segH, glm::vec4(0.10f, 0.10f, 0.12f, 0.85f));
            const float fill = glm::clamp(m_player.health - static_cast<float>(i), 0.0f, 1.0f);
            if (fill > 0.0f) {
                m_ui.rect(hx, hy, segW * fill, segH, glm::vec4(0.85f, 0.20f, 0.25f, 0.95f));
            }
        }
    }

    // Held-bucket rain collection: a small fill bar above the hotbar.
    if (m_bucketFill > 0.0f) {
        const float bw = 180.0f;
        const float bx = (static_cast<float>(w) - bw) * 0.5f;
        const float by = y - 30.0f;
        m_ui.text(bx, by - 16, 12.0f, "COLLECTING RAIN", glm::vec4(0.7f, 0.85f, 1.0f, 1.0f));
        m_ui.rect(bx, by, bw, 8, glm::vec4(0.10f, 0.10f, 0.14f, 0.9f));
        m_ui.rect(bx, by, bw * (m_bucketFill / kBucketFillSeconds), 8,
                  glm::vec4(0.35f, 0.6f, 0.95f, 1.0f));
    }

    // Floating progress bars over actively-crafting machines.
    for (const auto& [pos, m] : m_machines) {
        if (!m.crafting) continue;
        glm::vec2 sp;
        if (!projectToScreen(glm::vec3(pos) + glm::vec3(0.5f, 1.25f, 0.5f), sp)) continue;
        const float bw = 46.0f, bh = 7.0f;
        const float bx = sp.x - bw * 0.5f, by = sp.y - bh * 0.5f;
        m_ui.rect(bx - 1, by - 1, bw + 2, bh + 2, glm::vec4(0.0f, 0.0f, 0.0f, 0.7f));
        const float frac = glm::clamp(m.craftTime > 0 ? m.progress / m.craftTime : 0.0f, 0.0f, 1.0f);
        m_ui.rect(bx, by, bw * frac, bh, glm::vec4(0.30f, 0.90f, 0.40f, 0.95f));
    }

    // Items currently riding on conduits, drawn as floating icons.
    for (const auto& [pos, b] : m_belts) {
        if (b.item == ItemId::None) continue;
        glm::vec2 sp;
        if (!projectToScreen(glm::vec3(pos) + glm::vec3(0.5f, 0.85f, 0.5f), sp)) continue;
        const float dist = glm::length(camera().position - (glm::vec3(pos) + glm::vec3(0.5f)));
        const float s = glm::clamp(150.0f / dist, 10.0f, 40.0f);
        glm::vec2 uv0, uv1;
        Atlas::uvForTile(iconTile(b.item), uv0, uv1);
        m_ui.icon(m_atlas, sp.x - s * 0.5f, sp.y - s * 0.5f, s, s, uv0, uv1);
    }

    // Look-at machine panel (name, input/output buffers, controls).
    if (m_hasTarget) {
        const auto mit = m_machines.find(m_targetBlock);
        if (mit != m_machines.end()) {
            const Machine& m = mit->second;
            const float pw = 380.0f, ph = 98.0f;
            const float pxp = (static_cast<float>(w) - pw) * 0.5f;
            const float pyp = y - ph - 14.0f;
            m_ui.rect(pxp, pyp, pw, ph, glm::vec4(0.07f, 0.07f, 0.09f, 0.92f));
            m_ui.text(pxp + 12, pyp + 8, 16.0f, blockName(m.type), glm::vec4(1.0f, 1.0f, 0.7f, 1.0f));

            std::string in = "IN:";
            std::string out = "OUT:";
            for (int i = 0; i < static_cast<int>(ItemId::Count); ++i) {
                const ItemId id = static_cast<ItemId>(i);
                if (m.input.count(id) > 0)
                    in += " " + std::string(itemName(id)) + " x" + std::to_string(m.input.count(id));
                if (m.output.count(id) > 0)
                    out += " " + std::string(itemName(id)) + " x" + std::to_string(m.output.count(id));
            }
            m_ui.text(pxp + 12, pyp + 32, 13.0f, in, glm::vec4(0.85f, 0.85f, 0.9f, 1.0f));
            m_ui.text(pxp + 12, pyp + 52, 13.0f, out, glm::vec4(0.85f, 0.9f, 0.85f, 1.0f));
            m_ui.text(pxp + 12, pyp + 76, 12.0f, "RMB OPEN", glm::vec4(0.7f, 0.7f, 0.75f, 1.0f));
        }
    }

    m_ui.end();
}

void VoxelGame::drawCraftMenu() {
    const int w = window().width();
    const int h = window().height();
    const auto& recipes = handcraftRecipes();
    const int n = static_cast<int>(recipes.size());
    const auto invItems = itemsOf(m_inventory);
    const CraftLayout L = craftLayout(w, h, n, static_cast<int>(invItems.size()));
    const float mx = input().mouseX(), my = input().mouseY();

    beginPanel(m_ui, w, h, L.px, L.py, L.panelW, L.panelH, "CRAFTING", 0.5f);

    for (int i = 0; i < n; ++i) {
        const Recipe& r = recipes[i];
        const float ry = L.rowsY + i * CraftLayout::RowH;
        const bool affordable = canCraft(r);
        const bool selected = (i == m_menuSelection);

        if (selected) {
            m_ui.rect(L.px + 6, ry - 1, L.panelW - 12, CraftLayout::RowH - 2, kRowSelBg);
        }
        const glm::vec4 col = rowColor(selected, !affordable);

        std::string s = itemName(r.output.id);
        if (r.output.count > 1) s += " x" + std::to_string(r.output.count);
        s += "  (";
        for (const ItemStack& in : r.inputs) {
            s += " " + std::string(itemName(in.id));
            if (in.count > 1) s += " x" + std::to_string(in.count);
        }
        s += " )   HAVE " + std::to_string(m_inventory.count(r.output.id));
        m_ui.text(L.px + 14, ry + 3, 14.0f, s, col);
    }

    // Materials on hand, with a hover tooltip.
    m_ui.text(L.px + 16, L.invLabelY + 2, 13.0f, "INVENTORY", kTextHeader);
    drawItemGrid(m_ui, m_atlas, L.px + 16.0f, L.invY, invItems);
    const ItemId hovered = hoveredItemIn(invItems, mx, my, L.px + 16.0f, L.invY);
    if (hovered != ItemId::None) {
        m_ui.text(L.px + 16, L.tooltipY, 12.0f, itemName(hovered), kTextTooltip);
    }

    m_ui.text(L.px + 16, L.footerY, 12.0f,
              "CLICK / WHEEL / W/S + ENTER CRAFT   E CLOSE", kTextFooter);

    m_ui.end();
}

namespace {

    // Pause menu rows + layout, shared by update (hit-testing) and draw.
    constexpr const char* kPauseRows[] = {"RESUME", "SETTINGS", "SAVE GAME", "SAVE AND QUIT"};
    constexpr int kPauseRowCount = static_cast<int>(sizeof(kPauseRows) / sizeof(kPauseRows[0]));

    struct PauseLayout {
        static constexpr float RowH = 30.0f;
        float px = 0, py = 0, panelW = 340.0f, panelH = 0;
        float rowsY = 0;
        float footerY = 0;
    };

    PauseLayout pauseLayout(int w, int h) {
        PauseLayout L;
        const float headerH = 46.0f, footerH = 30.0f;
        L.panelH = headerH + kPauseRowCount * PauseLayout::RowH + footerH + 10.0f;
        L.px = (static_cast<float>(w) - L.panelW) * 0.5f;
        L.py = (static_cast<float>(h) - L.panelH) * 0.5f;
        L.rowsY = L.py + headerH;
        L.footerY = L.py + L.panelH - footerH + 6.0f;
        return L;
    }

    // Settings panel rows (the keybinds subpanel's rows come from the Action
    // enum plus RESET DEFAULTS + BACK). One layout serves both panels.
    constexpr const char* kSettingsRows[] = {
        "FULLSCREEN", "VSYNC", "SENSITIVITY", "VOLUME", "KEYBINDS...", "BACK"};
    constexpr int kSettingsRowCount =
        static_cast<int>(sizeof(kSettingsRows) / sizeof(kSettingsRows[0]));
    constexpr int kBindsRowCount = kActionCount + 2; // + RESET DEFAULTS + BACK

    struct SettingsLayout {
        static constexpr float RowH = 28.0f;
        float px = 0, py = 0, panelW = 460.0f, panelH = 0;
        float rowsY = 0;
        float footerY = 0;
    };

    SettingsLayout settingsLayout(int w, int h, int rows) {
        SettingsLayout L;
        const float headerH = 46.0f, footerH = 30.0f;
        L.panelH = headerH + rows * SettingsLayout::RowH + footerH + 10.0f;
        L.px = (static_cast<float>(w) - L.panelW) * 0.5f;
        L.py = (static_cast<float>(h) - L.panelH) * 0.5f;
        L.rowsY = L.py + headerH;
        L.footerY = L.py + L.panelH - footerH + 6.0f;
        return L;
    }

    // Main-menu shell rows (same count/geometry as the pause menu, so
    // pauseLayout() serves both).
    constexpr const char* kShellRows[] = {"NEW GAME", "CONTINUE", "SETTINGS", "QUIT"};
    constexpr int kShellRowCount = static_cast<int>(sizeof(kShellRows) / sizeof(kShellRows[0]));
    static_assert(kShellRowCount == kPauseRowCount, "shell reuses the pause layout");

    // Save-slot picker: kSaveSlots cards plus a trailing BACK row, all one card
    // tall so the shared menuNav() maps them uniformly.
    struct SlotLayout {
        static constexpr float CardH = 46.0f;
        float px = 0, py = 0, panelW = 480.0f, panelH = 0;
        float rowsY = 0;   // top of the first card
        float footerY = 0;
    };

    SlotLayout slotLayout(int w, int h) {
        SlotLayout L;
        const float headerH = 46.0f, footerH = 30.0f;
        const int rows = kSaveSlots + 1; // cards + BACK
        L.panelH = headerH + rows * SlotLayout::CardH + footerH + 10.0f;
        L.px = (static_cast<float>(w) - L.panelW) * 0.5f;
        L.py = (static_cast<float>(h) - L.panelH) * 0.5f;
        L.rowsY = L.py + headerH;
        L.footerY = L.py + L.panelH - footerH + 6.0f;
        return L;
    }

    // "1H 02M" for a long session, else "12M 34S" — compact for a slot card.
    std::string formatPlaytime(std::uint32_t sec) {
        char buf[24];
        if (sec >= 3600) {
            std::snprintf(buf, sizeof(buf), "%uH %02uM", sec / 3600, (sec / 60) % 60);
        } else {
            std::snprintf(buf, sizeof(buf), "%uM %02uS", sec / 60, sec % 60);
        }
        return buf;
    }

    // Local calendar time of a save, "YYYY-MM-DD HH:MM".
    std::string formatDate(std::uint64_t unixTime) {
        std::time_t t = static_cast<std::time_t>(unixTime);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[24];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
        return buf;
    }

} // namespace

void VoxelGame::openPauseMenu() {
    m_pauseOpen = true;
    m_pauseSel = 0;
    setPaused(true); // the engine stops accruing simulation time
    window().setRelativeMouse(false); // release the cursor for hover/click
    // Machine hums pause with the simulation; rain keeps playing (ambience).
    for (const auto& [pos, h] : m_humLoops) audio().setLoopPaused(h, true);
    audio().play("open", kUiVolume);
}

void VoxelGame::closePauseMenu() {
    m_pauseOpen = false;
    setPaused(false);
    window().setRelativeMouse(true);
    // Hums resume only at home: in the arena they stay paused (dimension rule).
    for (const auto& [pos, h] : m_humLoops) {
        audio().setLoopPaused(h, m_dimension != DimensionId::Overworld);
    }
    audio().play("close", kUiVolume);
}

void VoxelGame::updatePauseMenu() {
    const PauseLayout L = pauseLayout(window().width(), window().height());

    const MenuNav nav = menuNav(input(), m_pauseSel, kPauseRowCount,
                                L.px, L.panelW, L.rowsY, PauseLayout::RowH);
    if (nav.changed) audio().play("click", kUiVolume);

    if (nav.activated()) {
        switch (m_pauseSel) {
            case 0:
                closePauseMenu();
                break;
            case 1:
                openSettingsUi(); // plays "open"; pause stays underneath
                break;
            case 2:
                audio().play("click", kUiVolume);
                if (saveGame()) {
                    window().setTitle("Voxel Factory  —  SAVED");
                }
                break;
            default:
                audio().play("click", kUiVolume);
                quit(); // onExit() saves on every quit path
                break;
        }
    }
}

void VoxelGame::drawPauseMenu() {
    const int w = window().width();
    const int h = window().height();
    const PauseLayout L = pauseLayout(w, h);

    beginPanel(m_ui, w, h, L.px, L.py, L.panelW, L.panelH, "PAUSED", 0.6f);
    m_ui.text(L.px + L.panelW - 16 - m_ui.textWidth(12.0f, "V" VOXEL_FACTORY_VERSION),
              L.py + 16, 12.0f, "V" VOXEL_FACTORY_VERSION, glm::vec4(0.6f, 0.6f, 0.65f, 1.0f));

    for (int i = 0; i < kPauseRowCount; ++i) {
        drawSimpleRow(m_ui, L.px, L.panelW, L.rowsY + i * PauseLayout::RowH,
                      PauseLayout::RowH, kPauseRows[i], i == m_pauseSel);
    }

    m_ui.text(L.px + 16, L.footerY, 12.0f, "W/S + ENTER   ESC RESUME", kTextFooter);

    m_ui.end();
}

// ---- Main-menu shell (launch) ------------------------------------------------

void VoxelGame::openMainMenu() {
    m_shellOpen = true;
    m_shellSel = 0;
    m_slotPickerOpen = false;
    m_slotConfirm = -1;
    setPaused(true);                  // nothing simulates behind the menu
    window().setRelativeMouse(false); // release the cursor for hover/click
}

bool VoxelGame::anySaveExists() const {
    std::error_code ec;
    for (int i = 0; i < kSaveSlots; ++i) {
        if (std::filesystem::exists(saveSlotPath(i), ec)) return true;
    }
    return false;
}

void VoxelGame::updateMainMenu() {
    const PauseLayout L = pauseLayout(window().width(), window().height());
    const MenuNav nav = menuNav(input(), m_shellSel, kShellRowCount,
                                L.px, L.panelW, L.rowsY, PauseLayout::RowH);
    if (nav.changed) audio().play("click", kUiVolume);
    if (!nav.activated()) return;
    switch (m_shellSel) {
        case 0: openSlotPicker(/*newGame=*/true); break;
        case 1:
            if (anySaveExists()) openSlotPicker(/*newGame=*/false);
            else audio().play("deny", kCraftVolume); // nothing to continue
            break;
        case 2: openSettingsUi(); break;
        default:
            audio().play("click", kUiVolume);
            quit(); // no world built yet -> onExit's save is a no-op
            break;
    }
}

void VoxelGame::drawMainMenu() {
    const int w = window().width();
    const int h = window().height();
    const PauseLayout L = pauseLayout(w, h);

    beginPanel(m_ui, w, h, L.px, L.py, L.panelW, L.panelH, "VOXEL FACTORY", 0.72f);
    m_ui.text(L.px + L.panelW - 16 - m_ui.textWidth(12.0f, "V" VOXEL_FACTORY_VERSION),
              L.py + 16, 12.0f, "V" VOXEL_FACTORY_VERSION, glm::vec4(0.6f, 0.6f, 0.65f, 1.0f));

    const bool canContinue = anySaveExists();
    for (int i = 0; i < kShellRowCount; ++i) {
        const bool sel = (i == m_shellSel);
        const bool disabled = (i == 1 && !canContinue); // CONTINUE inert with no saves
        const float ry = L.rowsY + i * PauseLayout::RowH;
        if (sel) m_ui.rect(L.px + 6, ry, L.panelW - 12, PauseLayout::RowH - 4, kRowSelBg);
        m_ui.text(L.px + 20, ry + 6, 14.0f, kShellRows[i],
                  disabled ? kTextDim : rowColor(sel));
    }

    m_ui.text(L.px + 16, L.footerY, 12.0f, "W/S + ENTER", kTextFooter);
    m_ui.end();
}

// ---- Save-slot picker --------------------------------------------------------

void VoxelGame::openSlotPicker(bool newGame) {
    m_slotPickerOpen = true;
    m_slotPickerNew = newGame;
    m_slotSel = 0;
    m_slotConfirm = -1;
    audio().play("open", kUiVolume);
}

void VoxelGame::closeSlotPicker() {
    m_slotPickerOpen = false;
    m_slotConfirm = -1;
    audio().play("close", kUiVolume);
}

void VoxelGame::updateSlotPicker() {
    const SlotLayout L = slotLayout(window().width(), window().height());
    const int rows = kSaveSlots + 1; // cards + BACK
    const MenuNav nav = menuNav(input(), m_slotSel, rows,
                                L.px, L.panelW, L.rowsY, SlotLayout::CardH);
    if (nav.changed) {
        audio().play("click", kUiVolume);
        m_slotConfirm = -1; // moving off a card disarms its confirm
    }

    // RMB deletes a used slot (arm on the first click, confirm on the second).
    if (input().wasMousePressed(SDL_BUTTON_RIGHT) &&
        nav.hoverRow >= 0 && nav.hoverRow < kSaveSlots) {
        const int slot = nav.hoverRow;
        std::error_code ec;
        if (std::filesystem::exists(saveSlotPath(slot), ec)) {
            m_slotSel = slot;
            if (m_slotConfirm == slot && m_slotConfirmDelete) {
                const std::string p = saveSlotPath(slot);
                std::filesystem::remove(p, ec);
                std::filesystem::remove(p + ".bak", ec);
                std::filesystem::remove(SaveSystem::metaPath(p), ec);
                m_slotConfirm = -1;
                audio().play("click", kUiVolume);
            } else {
                m_slotConfirm = slot;
                m_slotConfirmDelete = true;
                audio().play("click", kUiVolume);
            }
        }
        return;
    }

    if (!nav.activated()) return;

    if (m_slotSel == kSaveSlots) { closeSlotPicker(); return; } // BACK row

    const int slot = m_slotSel;
    std::error_code ec;
    const bool used = std::filesystem::exists(saveSlotPath(slot), ec);

    if (m_slotPickerNew) {
        if (!used) {
            startNewGame(slot);
        } else if (m_slotConfirm == slot && !m_slotConfirmDelete) {
            m_slotConfirm = -1;
            startNewGame(slot); // confirmed overwrite (the file is rewritten on save)
        } else {
            m_slotConfirm = slot; // arm the overwrite confirm
            m_slotConfirmDelete = false;
            audio().play("click", kUiVolume);
        }
    } else { // CONTINUE: load a used slot
        if (used) {
            if (!continueGame(slot)) audio().play("deny", kCraftVolume);
        } else {
            audio().play("deny", kCraftVolume); // empty slot
        }
    }
}

void VoxelGame::drawSlotPicker() {
    const int w = window().width();
    const int h = window().height();
    const SlotLayout L = slotLayout(w, h);

    const char* title = m_slotPickerNew ? "NEW GAME - SELECT SLOT"
                                        : "CONTINUE - SELECT SLOT";
    beginPanel(m_ui, w, h, L.px, L.py, L.panelW, L.panelH, title, 0.72f);

    const glm::vec4 warn(0.96f, 0.55f, 0.2f, 1.0f);

    for (int i = 0; i < kSaveSlots; ++i) {
        const float ry = L.rowsY + i * SlotLayout::CardH;
        const bool sel = (i == m_slotSel);
        if (sel) m_ui.rect(L.px + 6, ry, L.panelW - 12, SlotLayout::CardH - 4, kRowSelBg);

        char label[16];
        std::snprintf(label, sizeof(label), "SLOT %d", i + 1);
        m_ui.text(L.px + 20, ry + 6, 15.0f, label, rowColor(sel));

        std::error_code ec;
        const std::string p = saveSlotPath(i);
        const bool used = std::filesystem::exists(p, ec);
        if (m_slotConfirm == i) {
            m_ui.text(L.px + 20, ry + 26, 12.0f,
                      m_slotConfirmDelete ? "DELETE? RIGHT-CLICK AGAIN"
                                          : "OVERWRITE? ENTER AGAIN",
                      warn);
        } else if (used) {
            SlotMeta meta;
            std::string detail;
            if (SaveSystem::readMeta(p, meta)) {
                detail = formatPlaytime(meta.playtimeSeconds) + "   " +
                         formatDate(meta.unixTime);
                if (meta.bossProgress) {
                    const int bosses = (meta.bossProgress & 1 ? 1 : 0) +
                                       (meta.bossProgress & 2 ? 1 : 0);
                    char bc[16];
                    std::snprintf(bc, sizeof(bc), "   BOSS %d/2", bosses);
                    detail += bc;
                }
            } else {
                detail = "SAVED"; // a migrated legacy save has no metadata yet
            }
            m_ui.text(L.px + 20, ry + 26, 12.0f, detail, sel ? kTextOnSel : kTextDim);
        } else {
            m_ui.text(L.px + 20, ry + 26, 12.0f, "EMPTY", kTextDim);
        }
    }

    // BACK row (one card tall so navigation stays uniform).
    const float by = L.rowsY + kSaveSlots * SlotLayout::CardH;
    drawSimpleRow(m_ui, L.px, L.panelW, by, SlotLayout::CardH, "BACK",
                  m_slotSel == kSaveSlots);

    m_ui.text(L.px + 16, L.footerY, 12.0f,
              "W/S SELECT   ENTER CHOOSE   RMB DELETE   ESC BACK", kTextFooter);
    m_ui.end();
}

void VoxelGame::openSettingsUi() {
    m_settingsOpen = true;
    m_settingsSel = 0;
    m_bindsOpen = false;
    m_bindCapture = -1;
    // The pause menu underneath already froze the sim + released the cursor.
    audio().play("open", kUiVolume);
}

void VoxelGame::closeSettingsUi() {
    m_bindCapture = -1;
    m_bindsOpen = false;
    m_settingsOpen = false;
    if (!m_settingsPath.empty()) SettingsIO::save(m_settingsPath, m_settings);
    audio().play("close", kUiVolume);
}

void VoxelGame::applySettings() {
    // All three are guarded engine calls — safe headless and at any time.
    window().setFullscreen(m_settings.fullscreen);
    window().setVsync(m_settings.vsync);
    audio().setMasterVolume(m_settings.volume);
    // Sensitivity needs no push: its one use site reads m_settings live.
}

void VoxelGame::updateSettingsUi() {
    // Key capture swallows EVERYTHING first (including W/S/Enter), so panel
    // navigation can never race an armed capture. Esc cancels via onEscape,
    // which runs before this and clears m_bindCapture.
    if (m_bindCapture >= 0) {
        for (int sc = 1; sc < SDL_SCANCODE_COUNT; ++sc) {
            const SDL_Scancode s = static_cast<SDL_Scancode>(sc);
            if (!input().wasKeyPressed(s)) continue;
            if (!bindableScancode(s)) {
                audio().play("deny", kCraftVolume); // reserved key; keep waiting
            } else {
                // A key lives on at most one action: steal it if needed (the
                // robbed row shows "---"; RESET DEFAULTS recovers).
                for (SDL_Scancode& b : m_settings.binds) {
                    if (b == s) b = SDL_SCANCODE_UNKNOWN;
                }
                m_settings.binds[static_cast<std::size_t>(m_bindCapture)] = s;
                m_bindCapture = -1;
                audio().play("click", kUiVolume);
            }
            break;
        }
        return; // mouse input is ignored while capturing
    }

    const int rows = m_bindsOpen ? kBindsRowCount : kSettingsRowCount;
    int& sel = m_bindsOpen ? m_bindsSel : m_settingsSel;
    const SettingsLayout L = settingsLayout(window().width(), window().height(), rows);

    const MenuNav nav = menuNav(input(), sel, rows, L.px, L.panelW,
                                L.rowsY, SettingsLayout::RowH);
    if (nav.changed) audio().play("click", kUiVolume);
    const bool activate = nav.activated();

    if (m_bindsOpen) {
        if (!activate) return;
        if (sel < kActionCount) {
            m_bindCapture = sel; // row shows PRESS A KEY until the next key
            audio().play("click", kUiVolume);
        } else if (sel == kActionCount) {
            m_settings.binds = kDefaultBinds; // RESET DEFAULTS
            audio().play("click", kUiVolume);
        } else {
            m_bindsOpen = false; // BACK
            audio().play("close", kUiVolume);
        }
        return;
    }

    // Settings panel: LEFT/RIGHT (or A/D) step the selected value; Enter or a
    // click flips toggles / steps a value up.
    int step = 0;
    if (input().wasKeyPressed(SDL_SCANCODE_LEFT) || input().wasKeyPressed(SDL_SCANCODE_A)) step -= 1;
    if (input().wasKeyPressed(SDL_SCANCODE_RIGHT) || input().wasKeyPressed(SDL_SCANCODE_D)) step += 1;

    auto adjust = [&](float& v, float lo, float hi, float delta) {
        const float next = glm::clamp(v + delta, lo, hi);
        if (next == v) {
            audio().play("deny", kCraftVolume); // already at the limit
            return false;
        }
        v = next;
        audio().play("click", kUiVolume);
        return true;
    };

    switch (sel) {
        case 0: // FULLSCREEN
            if (step != 0 || activate) {
                m_settings.fullscreen = !m_settings.fullscreen;
                applySettings();
                audio().play("click", kUiVolume);
            }
            break;
        case 1: // VSYNC
            if (step != 0 || activate) {
                m_settings.vsync = !m_settings.vsync;
                applySettings();
                audio().play("click", kUiVolume);
            }
            break;
        case 2: // SENSITIVITY (read live at the look site; no apply needed)
            if (step != 0 || activate) {
                adjust(m_settings.sensitivity, kSensitivityMin, kSensitivityMax,
                       (step != 0 ? static_cast<float>(step) : 1.0f) * kSensitivityStep);
            }
            break;
        case 3: // VOLUME
            if (step != 0 || activate) {
                if (adjust(m_settings.volume, kVolumeMin, kVolumeMax,
                           (step != 0 ? static_cast<float>(step) : 1.0f) * kVolumeStep)) {
                    applySettings();
                }
            }
            break;
        case 4: // KEYBINDS...
            if (activate) {
                m_bindsOpen = true;
                m_bindsSel = 0;
                audio().play("open", kUiVolume);
            }
            break;
        default: // BACK
            if (activate) closeSettingsUi();
            break;
    }
}

void VoxelGame::drawSettingsUi() {
    const int w = window().width();
    const int h = window().height();
    const int rows = m_bindsOpen ? kBindsRowCount : kSettingsRowCount;
    const int sel = m_bindsOpen ? m_bindsSel : m_settingsSel;
    const SettingsLayout L = settingsLayout(w, h, rows);

    beginPanel(m_ui, w, h, L.px, L.py, L.panelW, L.panelH,
               m_bindsOpen ? "KEYBINDS" : "SETTINGS", 0.6f);

    char buf[32];
    for (int i = 0; i < rows; ++i) {
        const float ry = L.rowsY + i * SettingsLayout::RowH;
        const bool selected = (i == sel);

        std::string label, value;
        bool capturing = false;
        if (m_bindsOpen) {
            if (i < kActionCount) {
                label = actionName(static_cast<Action>(i));
                capturing = (m_bindCapture == i);
                value = capturing ? "PRESS A KEY"
                                  : scancodeLabel(m_settings.binds[static_cast<std::size_t>(i)]);
            } else if (i == kActionCount) {
                label = "RESET DEFAULTS";
            } else {
                label = "BACK";
            }
        } else {
            label = kSettingsRows[i];
            switch (i) {
                case 0: value = m_settings.fullscreen ? "< ON >" : "< OFF >"; break;
                case 1: value = m_settings.vsync ? "< ON >" : "< OFF >"; break;
                case 2:
                    std::snprintf(buf, sizeof(buf), "< %.2f >", m_settings.sensitivity);
                    value = buf;
                    break;
                case 3:
                    std::snprintf(buf, sizeof(buf), "< %d%% >",
                                  static_cast<int>(std::lround(m_settings.volume * 100.0f)));
                    value = buf;
                    break;
                default: break;
            }
        }

        drawSimpleRow(m_ui, L.px, L.panelW, ry, SettingsLayout::RowH, label, selected);
        if (!value.empty()) {
            const glm::vec4 vcol = capturing ? glm::vec4(0.95f, 0.4f, 0.35f, 1.0f)
                                             : rowColor(selected);
            m_ui.text(L.px + L.panelW - 20 - m_ui.textWidth(14.0f, value), ry + 6, 14.0f,
                      value, vcol);
        }
    }

    const char* footer = m_bindCapture >= 0 ? "PRESS A KEY   ESC CANCEL"
                       : m_bindsOpen        ? "W/S + ENTER REBIND   ESC BACK"
                                            : "A/D ADJUST   ENTER SELECT   ESC BACK";
    m_ui.text(L.px + 16, L.footerY, 12.0f, footer, kTextFooter);

    m_ui.end();
}

void VoxelGame::drawHelp() {
    const int w = window().width();
    const int h = window().height();

    // Each line: text + a style (0 heading, 1 body, 2 dim). Built per call so
    // rebound keys show their current names (help only draws while open).
    struct Line { std::string text; int style; };
    const auto k = [this](Action a) { return scancodeLabel(m_settings.key(a)); };
    // "WASD" when all four movement binds are single characters, else spelled
    // out with slashes (covers LEFT CTRL-style names and unbound "---").
    std::string move = k(Action::MoveForward) + k(Action::MoveLeft) +
                       k(Action::MoveBack) + k(Action::MoveRight);
    if (move.size() != 4) {
        move = k(Action::MoveForward) + "/" + k(Action::MoveLeft) + "/" +
               k(Action::MoveBack) + "/" + k(Action::MoveRight);
    }
    const std::vector<Line> kLines = {
        {"HOW TO PLAY", 0},
        {"GOAL: BREW YOUR WAY UP TO THE PHILOSOPHERS STONE, THEN", 1},
        {"TRANSMUTE NEW RESOURCE SOURCES TO EXPAND YOUR ISLAND.", 1},
        {"", 1},
        {"1. MINE NODES (LMB) AT THE GLOWING SOURCE PATCHES. THEY REGROW.", 1},
        {"2. CRAFT WITH " + k(Action::CraftMenu) +
             ": ORE > INGOT > GENERATOR + GRINDER ( NO PLATES ).", 1},
        {"3. GENERATORS BURN WOOD. KEEP THEM FED, KEEP TREES PLANTED.", 1},
        {"4. A POWERED GRINDER PRESSES THE COPPER PLATES FOR ALL OTHER GEAR.", 1},
        {"5. RAIN FILLS BARRELS AND HELD BUCKETS. IT IS THE ONLY WATER.", 1},
        {"6. RIGHT-CLICK A MACHINE TO OPEN IT: LOAD INPUTS, TAKE OUTPUTS.", 1},
        {"7. CONDUITS CARRY ITEMS THE WAY THEIR ARROW POINTS. WRENCH + " +
             k(Action::WrenchRotate) + " RE-AIMS.", 1},
        {"8. GRINDER > CAULDRON > INFUSER > ALEMBIC > DISTILLER > TRANSMUTER", 1},
        {"9. CHOP TREES: LOGS GIVE WOOD, LEAVES DROP SAPLINGS. REPLANT ON GRASS.", 1},
        {"10. NO FLYING: BUILD SCAFFOLD. FALL OFF THE EDGE AND YOUR PACK IS LOST.", 2},
        {"", 1},
        {"CONTROLS", 0},
        {move + " MOVE   " + k(Action::Jump) + " JUMP   " + k(Action::Sprint) + " SPRINT", 1},
        {"LMB MINE   RMB PLACE   1-0 OR WHEEL SELECT", 1},
        {k(Action::Inventory) + " INVENTORY: DRAG ITEMS ONTO THE HOTBAR TO ASSIGN THEM", 1},
        {"RMB WITH DRAUGHT > DRINK ( HEAL )   HARD FALLS HURT", 1},
        {k(Action::CraftMenu) + " CRAFT MENU   RMB OPEN MACHINE   " +
             k(Action::QuickSave) + " SAVE   ESC QUIT ( AUTO SAVES )", 1},
        {"", 1},
        {k(Action::Help) + " OR ESC TO CLOSE", 2},
    };
    const int n = static_cast<int>(kLines.size());

    m_ui.begin(w, h);
    m_ui.rect(0, 0, static_cast<float>(w), static_cast<float>(h), glm::vec4(0, 0, 0, 0.55f));

    const float lineH = 24.0f, padY = 20.0f, panelW = 760.0f;
    const float panelH = padY * 2.0f + n * lineH;
    const float px = (static_cast<float>(w) - panelW) * 0.5f;
    const float py = (static_cast<float>(h) - panelH) * 0.5f;

    m_ui.rect(px, py, panelW, panelH, glm::vec4(0.08f, 0.08f, 0.10f, 0.96f));

    for (int i = 0; i < n; ++i) {
        const Line& line = kLines[static_cast<std::size_t>(i)];
        if (line.text.empty()) continue;
        const float ly = py + padY + i * lineH;
        const float size = line.style == 0 ? 18.0f : 14.0f;
        const glm::vec4 col = line.style == 0 ? glm::vec4(1.0f, 1.0f, 0.7f, 1.0f)
                            : line.style == 2 ? glm::vec4(0.65f, 0.65f, 0.7f, 1.0f)
                                              : glm::vec4(0.9f, 0.9f, 0.92f, 1.0f);
        m_ui.text(px + 22, ly, size, line.text, col);
    }

    m_ui.end();
}
