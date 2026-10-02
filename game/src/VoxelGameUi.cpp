// All 2D UI: the hotbar HUD (with floating machine bars and belt-item icons),
// the crafting menu, the machine panel with drag-and-drop, the F1 help
// overlay, and the F3 perf readout. Layout structs are the single source of
// truth shared by each panel's hit-testing and drawing.

#include "game/VoxelGame.h"
#include "VoxelGameInternal.h"
#include "game/AlchemyCircle.h"
#include "game/Atlas.h"
#include "game/MachineSystem.h"
#include "game/SaveSystem.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
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
        static constexpr float CellPitch = Cell + Gap;
        static constexpr float StripH = Cell + 12.0f; // IN / OUT band height
        static constexpr int   InvCols = 10;
        // The fixed bands, named so the height math and the "does it fit"
        // math read from the same numbers instead of two copies drifting.
        static constexpr float HeaderH  = 44.0f;
        static constexpr float BarH     = 20.0f;
        static constexpr float LabelH   = 20.0f;
        static constexpr float TooltipH = 18.0f;
        static constexpr float FooterH  = 24.0f;
        static constexpr float Pad      = 12.0f;
        // No panel may come within this of the window edge.
        static constexpr float Margin   = 24.0f;

        float px = 0, py = 0, panelW = 640.0f, panelH = 0;
        int   rows = 0;      // action rows (AUTO + recipes + TAKE)
        float rowsY = 0;     // top of the action rows
        float inY = 0;       // top of the IN cell strip
        bool  hasFuel = false; // machine keeps fuel in its own buffer
        float fuelY = 0;     // top of the FUEL strip (only when hasFuel)
        float outY = 0;      // top of the OUT cell strip
        float stripCellsX = 0; // first cell x in the IN/OUT strips
        float barY = 0;      // progress bar
        float invLabelY = 0;
        float invY = 0;      // top of the inventory grid
        int   invRows = 0;
        float tooltipY = 0;
        float footerY = 0;
    };

    // ---- The windowed inventory grid ------------------------------------
    // The grid holds one cell per item TYPE the player owns, so it is the one
    // part of a panel that grows without bound and only ever upward -- adding
    // content to kItems makes every panel taller. Left alone it eventually
    // pushes the panel past the window and the header scrolls off the top,
    // which is what the recipe overhaul's ~30 new items made visible.
    //
    // `UiRenderer` still has no scissor primitive, so the fix is to draw the
    // rows that FIT and scroll the rest. Update and draw both take the same
    // slice, so hit-testing and drawing cannot disagree about which cell is
    // which.
    struct InvWindow {
        int rows = 0;      // grid rows actually drawn
        int totalRows = 0; // rows the full list would need
        int first = 0;     // topmost drawn row
        int hidden = 0;    // item cells scrolled out of view
        bool scrollable() const { return totalRows > rows; }
    };

    InvWindow invWindow(int itemCount, int rowsThatFit, int scroll) {
        InvWindow v;
        v.totalRows = std::max(1, (itemCount + PanelLayout::InvCols - 1) / PanelLayout::InvCols);
        v.rows = std::clamp(rowsThatFit, 1, v.totalRows);
        v.first = std::clamp(scroll, 0, v.totalRows - v.rows);
        v.hidden = itemCount - std::min(itemCount, v.rows * PanelLayout::InvCols);
        return v;
    }

    // The visible slice of an item list. Callers index THIS, so a cell index
    // means the same thing in the layout, the draw, and the drag pickup.
    std::vector<std::pair<ItemId, int>> invSlice(
            const std::vector<std::pair<ItemId, int>>& items, const InvWindow& v) {
        const int n = static_cast<int>(items.size());
        const int from = std::min(n, v.first * PanelLayout::InvCols);
        const int to   = std::min(n, from + v.rows * PanelLayout::InvCols);
        return {items.begin() + from, items.begin() + to};
    }

    // Suffix for an INVENTORY label when part of the grid is scrolled away.
    std::string invMoreLabel(const InvWindow& v) {
        if (!v.scrollable()) return {};
        return "   ( " + std::to_string(v.hidden) + " MORE - WHEEL )";
    }

    // Wheel over the grid scrolls it. Returns true when the wheel was spent
    // here, so a panel that also drives row selection with the wheel leaves
    // its rows alone while the cursor is over the grid. The new offset lands
    // next frame, which is what keeps the cells hit-tested this frame the same
    // ones the player was looking at when they clicked.
    bool scrollInvGrid(engine::Input& in, int& scroll, const InvWindow& v,
                       float gridX, float gridY, float mx, float my) {
        if (!v.scrollable()) return false;
        const float gw = PanelLayout::InvCols * PanelLayout::CellPitch;
        const float gh = v.rows * PanelLayout::CellPitch;
        if (mx < gridX || mx >= gridX + gw || my < gridY || my >= gridY + gh) return false;
        const int wheel = in.wheelSteps();
        if (wheel == 0) return false;
        scroll = std::clamp(v.first - wheel, 0, v.totalRows - v.rows);
        return true;
    }

    // Everything in the machine panel that is not the inventory grid. A machine
    // with its own fuel buffer shows a third strip, so it is one taller.
    float panelFixedHeight(int actionRows, bool fuelStrip) {
        return PanelLayout::HeaderH + actionRows * PanelLayout::RowH +
               (fuelStrip ? 3.0f : 2.0f) * PanelLayout::StripH + PanelLayout::BarH +
               PanelLayout::LabelH + PanelLayout::TooltipH + PanelLayout::FooterH +
               PanelLayout::Pad;
    }

    int panelInvRowsThatFit(int windowH, int actionRows, bool fuelStrip) {
        const float avail = static_cast<float>(windowH) - PanelLayout::Margin -
                            panelFixedHeight(actionRows, fuelStrip);
        return static_cast<int>(avail / PanelLayout::CellPitch);
    }

    PanelLayout panelLayout(int w, int h, int actionRows, int invRows, bool fuelStrip) {
        PanelLayout L;
        L.rows = actionRows;
        L.hasFuel = fuelStrip;
        L.invRows = std::max(1, invRows);
        const float invH = L.invRows * PanelLayout::CellPitch;
        L.panelH = panelFixedHeight(actionRows, fuelStrip) + invH;
        L.px = (static_cast<float>(w) - L.panelW) * 0.5f;
        L.py = (static_cast<float>(h) - L.panelH) * 0.5f;
        L.rowsY = L.py + PanelLayout::HeaderH;
        L.inY = L.rowsY + actionRows * PanelLayout::RowH + 6.0f;
        // FUEL sits with IN rather than next to OUT: both are things you feed
        // the machine, and the two feeds reading together is what makes the
        // split legible at a glance.
        L.fuelY = L.inY + PanelLayout::StripH;
        L.outY = (fuelStrip ? L.fuelY : L.inY) + PanelLayout::StripH;
        L.stripCellsX = L.px + 64.0f;
        L.barY = L.outY + PanelLayout::StripH + 2.0f;
        L.invLabelY = L.barY + PanelLayout::BarH;
        L.invY = L.invLabelY + PanelLayout::LabelH;
        L.tooltipY = L.invY + invH + 2.0f;
        L.footerY = L.py + L.panelH - PanelLayout::FooterH + 2.0f;
        return L;
    }

    // ---- Alchemy Circle panel -------------------------------------------
    // The ring is drawn as a RING: the eight pedestal cells sit at their true
    // compass bearings around the core's own catalyst cell, so what you see in
    // the panel is the thing you built on the ground. Rotation-invariant
    // matching is what makes that honest -- north on the panel is north in the
    // world, and the pattern would still match if it weren't.
    // There is deliberately NO blueprint list. The circle is laid BY HAND, one
    // drag per pedestal -- a recipe you PERFORM rather than a row you click --
    // so the panel shows the ring, what it currently spells, and nothing that
    // would arrange it for you. Two action rows: START/STOP -- a circle runs
    // only once started, see tickRuneCore for why -- and TAKE OUTPUTS, which
    // every machine panel has. Neither grows with the recipe table, so only
    // the inventory grid needs windowing.
    namespace circleMetrics {
        inline constexpr float RowH    = 22.0f;  // START/STOP, then TAKE OUTPUTS
        inline constexpr int   Rows    = 2;
        inline constexpr float HeaderH = 44.0f;
        inline constexpr float RingH   = 196.0f;
        inline constexpr float StatusH = 22.0f;
        inline constexpr float BarH    = 20.0f;
        inline constexpr float LabelH  = 20.0f;
        inline constexpr float TooltipH = 18.0f;
        inline constexpr float FooterH = 24.0f;
        inline constexpr float Pad     = 10.0f;

        // Everything except the inventory grid -- the one part that still
        // grows with content, and so the one part that scrolls.
        inline float fixedHeight() {
            return HeaderH + RingH + StatusH + Rows * RowH + BarH + LabelH +
                   PanelLayout::StripH + LabelH + TooltipH + FooterH + Pad;
        }

        // The ring widget alone is 196px, so this panel runs out of room before
        // the others do: a short window loses GRID rows, and nothing else.
        inline int invRowsThatFit(int windowH) {
            const float avail =
                static_cast<float>(windowH) - PanelLayout::Margin - fixedHeight();
            return static_cast<int>(avail / PanelLayout::CellPitch);
        }
    }

    struct CircleLayout {
        static constexpr float RowH = circleMetrics::RowH;
        float px = 0, py = 0, panelW = 660.0f, panelH = 0;
        float ringCx = 0, ringCy = 0;   // centre of the radial widget
        float ringR = 80.0f;            // orbit radius of the eight cells
        float statusY = 0;              // "READY: ..." / "MAKING ..." line
        float rowY = 0;                 // first action row (START/STOP)
        float barY = 0;
        float outLabelY = 0, outY = 0;  // the core's output strip
        float invLabelY = 0, invY = 0;
        int   invRows = 0;
        float tooltipY = 0, footerY = 0;
    };

    CircleLayout circleLayout(int w, int h, int invRows) {
        CircleLayout L;
        L.invRows = std::max(1, invRows);
        using namespace circleMetrics;
        const float invH = L.invRows * PanelLayout::CellPitch;
        L.panelH = fixedHeight() + invH;
        L.px = (static_cast<float>(w) - L.panelW) * 0.5f;
        L.py = (static_cast<float>(h) - L.panelH) * 0.5f;
        L.ringCx = L.px + L.panelW * 0.5f;
        L.ringCy = L.py + HeaderH + RingH * 0.5f;
        L.statusY = L.py + HeaderH + RingH;
        L.rowY = L.statusY + StatusH;
        L.barY = L.rowY + circleMetrics::Rows * CircleLayout::RowH + 2.0f;
        L.outLabelY = L.barY + BarH;
        L.outY = L.outLabelY + LabelH;
        L.invLabelY = L.outY + PanelLayout::StripH;
        L.invY = L.invLabelY + LabelH;
        L.tooltipY = L.invY + invH + 2.0f;
        L.footerY = L.py + L.panelH - FooterH + 2.0f;
        return L;
    }

    // Top-left corner of ring cell `slot` (0 = north, clockwise), and of the
    // centre catalyst cell (slot == -1).
    glm::vec2 circleCellPos(const CircleLayout& L, int slot) {
        const float half = PanelLayout::Cell * 0.5f;
        if (slot < 0) return {L.ringCx - half, L.ringCy - half};
        const float a = glm::radians(-90.0f + 45.0f * static_cast<float>(slot));
        return {L.ringCx + L.ringR * std::cos(a) - half,
                L.ringCy + L.ringR * std::sin(a) - half};
    }

    // Which circle cell is under the cursor? -1 = the centre, -2 = none.
    int circleHit(const CircleLayout& L, float mx, float my) {
        for (int s = -1; s < AlchemyCircle::kRingSlots; ++s) {
            const glm::vec2 p = circleCellPos(L, s);
            if (mx >= p.x && mx < p.x + PanelLayout::Cell &&
                my >= p.y && my < p.y + PanelLayout::Cell) {
                return s;
            }
        }
        return -2;
    }

    // The item types present in an inventory, with counts, in enum order.
    std::vector<std::pair<ItemId, int>> itemsOf(const Inventory& inv) {
        std::vector<std::pair<ItemId, int>> out;
        for (int i = 1; i < static_cast<int>(itemCount()); ++i) {
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
    // Craft bars. Green is work happening; amber is work READY with nowhere to
    // put it -- a different problem with a different fix (drain the output, or
    // belt it into a crate), so it must not read as slow progress.
    constexpr glm::vec4 kBarWorking {0.30f, 0.90f, 0.40f, 0.95f};
    constexpr glm::vec4 kBarJammed  {0.95f, 0.65f, 0.15f, 0.95f};
    // A belt's filter, drawn where its cargo would ride. Deliberately NOT
    // kOutOfStockTint: that means "you don't own this", and a filter is a
    // setting you chose. Ghosted enough to read as "waiting for", solid enough
    // to pick out down a line of belts.
    constexpr glm::vec4 kFilterGhost{0.72f, 0.76f, 0.85f, 0.85f};
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

    // `useArrows = false` leaves the arrow keys alone, for the one panel that
    // wants them for something else: a hand-cranked machine turns its handle
    // with the arrows, so W/S keep the rows and the two never fight.
    MenuNav menuNav(engine::Input& in, int& sel, int rows,
                    float px, float panelW, float rowsY, float rowH,
                    bool useWheel = false, bool useArrows = true) {
        const int before = sel;
        if (in.wasKeyPressed(SDL_SCANCODE_W) ||
            (useArrows && in.wasKeyPressed(SDL_SCANCODE_UP))) {
            sel = (sel - 1 + rows) % rows;
        }
        if (in.wasKeyPressed(SDL_SCANCODE_S) ||
            (useArrows && in.wasKeyPressed(SDL_SCANCODE_DOWN))) {
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

    // Which of a machine's two feed cells an item may be DROPPED into. The
    // machine itself never has to guess (that is the point of the fuel slot),
    // so these only decide whether a drop is legal and which strip lights up.
    // A machine without a fuel slot has one feed and takes whatever it accepts.
    bool acceptsAsIngredient(const Machine& mac, ItemId item) {
        if (!MachineSystem::machineAccepts(mac, item)) return false;
        if (!usesFuelSlot(mac.type)) return true;
        for (const MachineRecipe* r : recipesForMachine(mac.type)) {
            for (const ItemStack& in : r->inputs) {
                if (in.id == item) return true;
            }
        }
        return false; // pure fuel: it belongs in the FUEL cell, not this one
    }

    bool acceptsAsFuel(const Machine& mac, ItemId item) {
        return usesFuelSlot(mac.type) && fuelSeconds(item) > 0.0f;
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

    float craftFixedHeight(int nRecipes) {
        const float headerH = 40.0f, invLabelH = 20.0f, tooltipH = 16.0f, footerH = 22.0f;
        return headerH + nRecipes * CraftLayout::RowH + invLabelH +
               tooltipH + footerH + 8.0f;
    }

    int craftInvRowsThatFit(int windowH, int nRecipes) {
        const float avail = static_cast<float>(windowH) - PanelLayout::Margin -
                            craftFixedHeight(nRecipes);
        return static_cast<int>(avail / PanelLayout::CellPitch);
    }

    CraftLayout craftLayout(int w, int h, int nRecipes, int invRows) {
        CraftLayout L;
        L.rows = nRecipes;
        L.invRows = std::max(1, invRows);
        const float invLabelH = 20.0f, footerH = 22.0f, headerH = 40.0f;
        const float invH = L.invRows * PanelLayout::CellPitch;
        L.panelH = craftFixedHeight(nRecipes) + invH;
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
        float armorLabelY = 0, armorY = 0;   // the kArmorSlots equip cells
        float hotbarLabelY = 0, hotbarY = 0; // the kHotbarSlots assignment cells
        float footerY = 0;
    };

    // The overlay's own fixed bands (it has no action rows, but it does carry
    // the armor and hotbar strips below the grid).
    float overlayFixedHeight() {
        const float headerH = 40.0f, tooltipH = 18.0f, labelH = 20.0f, footerH = 24.0f;
        return headerH + tooltipH + 2 * (labelH + PanelLayout::CellPitch) + footerH + 12.0f;
    }

    int overlayInvRowsThatFit(int windowH) {
        const float avail = static_cast<float>(windowH) - PanelLayout::Margin -
                            overlayFixedHeight();
        return static_cast<int>(avail / PanelLayout::CellPitch);
    }

    InvLayout invLayout(int w, int h, int invRows) {
        InvLayout L;
        L.invRows = std::max(1, invRows);
        const float headerH = 40.0f, tooltipH = 18.0f, labelH = 20.0f, footerH = 24.0f;
        const float invH = L.invRows * PanelLayout::CellPitch;
        const float stripH = PanelLayout::CellPitch;
        L.panelH = overlayFixedHeight() + invH;
        L.px = (static_cast<float>(w) - L.panelW) * 0.5f;
        L.py = (static_cast<float>(h) - L.panelH) * 0.5f;
        L.invY = L.py + headerH;
        L.tooltipY = L.invY + invH + 2.0f;
        L.armorLabelY = L.tooltipY + tooltipH;
        L.armorY = L.armorLabelY + labelH;
        L.hotbarLabelY = L.armorY + stripH;
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
    m_ui.rect(8, 8, 360, 106, glm::vec4(0.05f, 0.05f, 0.08f, 0.82f));

    std::snprintf(line, sizeof(line), "FRAME AVG %5.1f MS  WORST %6.1f MS",
                  m_perf.avgMs, m_perf.worstMs);
    m_ui.text(16, 16, 12.0f, line, glm::vec4(0.9f, 0.9f, 0.92f, 1.0f));

    std::snprintf(line, sizeof(line), "REMESH %6.1f MS  %d CHUNKS  X%d PER S",
                  m_perf.lastRemeshMs, m_perf.chunksRemeshed, m_perf.remeshesPerSec);
    m_ui.text(16, 34, 12.0f, line, glm::vec4(0.9f, 0.9f, 0.92f, 1.0f));

    std::snprintf(line, sizeof(line), "POWER  %6.1f MS  X%d PER S",
                  m_perf.lastSolveMs, m_perf.solvesPerSec);
    m_ui.text(16, 52, 12.0f, line, glm::vec4(0.9f, 0.9f, 0.92f, 1.0f));

    // Where you are. The overlay has carried timings since it was born and
    // never a position, which is the one thing you actually want when a factory
    // is spread over an island and you are trying to describe where something
    // is (or find your way back to it).
    const glm::vec3 p = camera().position;
    std::snprintf(line, sizeof(line), "XYZ %7.1f %7.1f %7.1f",
                  static_cast<double>(p.x), static_cast<double>(p.y),
                  static_cast<double>(p.z));
    m_ui.text(16, 70, 12.0f, line, glm::vec4(0.9f, 0.9f, 0.92f, 1.0f));

    m_ui.text(16, 88, 12.0f, "VOXEL FACTORY V" VOXEL_FACTORY_VERSION,
              glm::vec4(0.6f, 0.6f, 0.65f, 1.0f));

    m_ui.end();
}

void VoxelGame::drawMachineUi() {
    const auto mit = m_machines.find(m_machineUiPos);
    if (mit == m_machines.end()) return;
    const Machine& mac = mit->second;

    // A Rune Core gets the radial panel instead of the generic strip panel.
    if (machineTraits(mac.type).kind == MachineKind::RuneCore) { drawCircleUi(); return; }

    const auto recipes = recipesForMachine(mac.type);
    // AUTO, one MAKE row per recipe, TAKE OUTPUTS, then the master switch LAST.
    // Last rather than first on purpose: putting it at the top would shift every
    // recipe row's index, and the row a player reaches for most is a recipe.
    // The header carries the OFF state instead, where status already lives.
    const bool switched = hasPowerSwitch(mac.type);
    const int rows = static_cast<int>(recipes.size()) + 2 + (switched ? 1 : 0);
    const int switchRow = switched ? rows - 1 : -1;
    const bool fuelStrip = usesFuelSlot(mac.type);
    const auto allItems = itemsOf(m_inventory);
    const auto inItems = itemsOf(mac.input);
    const auto fuelItems = itemsOf(mac.fuel);
    const auto outItems = itemsOf(mac.output);

    const int w = window().width();
    const int h = window().height();
    const InvWindow iv = invWindow(static_cast<int>(allItems.size()),
                                   panelInvRowsThatFit(h, rows, fuelStrip), m_invScroll);
    const auto invItems = invSlice(allItems, iv);
    const PanelLayout L = panelLayout(w, h, rows, iv.rows, fuelStrip);
    const float mx = input().mouseX(), my = input().mouseY();

    beginPanel(m_ui, w, h, L.px, L.py, L.panelW, L.panelH, blockName(mac.type), 0.45f);

    // Header status (generators report their burn instead of network power --
    // their energized state is their own doing).
    const MachineTraits& traits = machineTraits(mac.type);
    // OFF outranks even a jam, and a jam outranks everything else: a machine
    // reading BURNING or POWERED while producing nothing is the confusing case
    // this exists to end. One of the three takes the header slot outright
    // rather than any of them sharing it.
    if (!mac.enabled) {
        m_ui.text(L.px + L.panelW - 150, L.py + 15, 13.0f, "OFF",
                  glm::vec4(0.55f, 0.55f, 0.60f, 1.0f));
    }
    else if (mac.jammed) {
        m_ui.text(L.px + L.panelW - 150, L.py + 15, 13.0f, "OUTPUT FULL", kBarJammed);
    }
    else switch (traits.kind) {
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
        case MachineKind::Irrigator: {
            const bool wet = mac.progress > 0.0f;
            const char* status = wet ? "WATERING" : "DRY";
            m_ui.text(L.px + L.panelW - 150, L.py + 15, 13.0f, status,
                      wet ? glm::vec4(0.45f, 0.7f, 0.95f, 1.0f)
                          : glm::vec4(0.95f, 0.4f, 0.35f, 1.0f));
            break;
        }
        case MachineKind::Pedestal:
            break; // a pedestal draws no power, so "NO POWER" would be a lie
        case MachineKind::Storage: {
            // No recipe, no power, no fire -- the only thing worth reporting is
            // how much is in there. A total across item types, because the cap
            // itself is per type.
            int held = 0;
            for (int i = 1; i < static_cast<int>(itemCount()); ++i) {
                const ItemId id = static_cast<ItemId>(i);
                held += mac.input.count(id) + mac.output.count(id);
            }
            m_ui.text(L.px + L.panelW - 150, L.py + 15, 13.0f,
                      std::to_string(held) + " STORED",
                      glm::vec4(0.80f, 0.68f, 0.45f, 1.0f));
            break;
        }
        default: {
            // A machine that asks for no power must never be told it has none.
            // Fuel-fired ones report their fire instead; the manual tier is
            // simply working, slowly, by hand.
            if (traits.burnsFuel) {
                // The fire is this machine's power, so the header carries the
                // seconds left the way the network status carries POWERED.
                const bool burning = mac.burnLeft > 0.0f;
                const std::string status =
                    burning ? "BURNING " + std::to_string(static_cast<int>(mac.burnLeft)) + "S"
                            : std::string("OUT OF FUEL");
                m_ui.text(L.px + L.panelW - 175, L.py + 15, 13.0f, status,
                          burning ? glm::vec4(0.95f, 0.6f, 0.25f, 1.0f)
                                  : glm::vec4(0.95f, 0.4f, 0.35f, 1.0f));
                break;
            }
            if (traits.demand == 0) {
                m_ui.text(L.px + L.panelW - 150, L.py + 15, 13.0f, "MANUAL",
                          glm::vec4(0.75f, 0.7f, 0.55f, 1.0f));
                break;
            }
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
                case MachineKind::Generator: {
                    // Fuel is a shared registry now, so list it rather than
                    // naming one item the row would go stale on.
                    std::string fuels;
                    for (const FuelInfo& f : fuelRows()) {
                        fuels += fuels.empty() ? " " : " / ";
                        fuels += itemName(f.item);
                    }
                    label = "  BURNS" + fuels + " ( ONLY WHILE A NETWORK NEEDS POWER )";
                    break;
                }
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
                case MachineKind::Harvester:
                    label = "  REAPS RIPE CROPS ( RADIUS " +
                            std::to_string(kHarvestRadius) + " ) AND REPLANTS THEM";
                    break;
                case MachineKind::Irrigator:
                    label = std::string("  SPENDS ") + itemName(ItemId::SpringWater) +
                            " TO WATER RADIUS " + std::to_string(kIrrigateRadius);
                    break;
                case MachineKind::Pedestal:
                    // Only reachable for an ORPHAN pedestal -- one with no Rune
                    // Core behind it, so it is not part of any circle yet.
                    label = "  A RING SLOT ( NEEDS A RUNE CORE 2 CELLS AWAY )";
                    break;
                case MachineKind::Storage:
                    label = "  BULK STORAGE ( BELTS FILL IT AND DRAIN IT, " +
                            std::to_string(kChestCap) + " PER ITEM )";
                    break;
                case MachineKind::RuneCore: // dispatched to the circle panel
                case MachineKind::Processor:
                    label = std::string(mac.selectedRecipe < 0 ? "> " : "  ") +
                            "AUTO ( FIRST READY RECIPE )";
                    break;
            }
        } else if (i <= static_cast<int>(recipes.size())) {
            const MachineRecipe& r = *recipes[i - 1];
            // A weighted recipe names every product it can roll -- the row is
            // the only place a player learns a Sifter is a gamble.
            std::string product;
            for (const RecipeOutput& o : r.outputs) {
                if (o.stack.id == ItemId::None) continue;
                product += product.empty() ? "" : " / ";
                product += itemName(o.stack.id);
            }
            label = std::string(mac.selectedRecipe == i - 1 ? "> " : "  ") +
                    "MAKE " + product + "  (";
            actionable = false; // white if the player can contribute anything
            for (const ItemStack& in : r.inputs) {
                label += " " + std::string(itemName(in.id));
                if (in.count > 1) label += " x" + std::to_string(in.count);
                if (m_inventory.has(in.id, 1)) actionable = true;
            }
            label += " )";
        } else if (i == switchRow) {
            // Says what pressing it DOES, not what the state is -- the header
            // already says that, and a row labelled "ON" is ambiguous about
            // whether it reports or commands.
            label = mac.enabled ? "  TURN OFF" : "  TURN ON";
            actionable = true;
        } else {
            label = "  TAKE OUTPUTS";
            for (int k = 1; k < static_cast<int>(itemCount()); ++k) {
                if (mac.output.count(static_cast<ItemId>(k)) > 0) { actionable = true; break; }
            }
        }

        m_ui.text(L.px + 16, ry + 5, 14.0f, label, rowColor(selected, !actionable));
    }

    // IN / FUEL strips, highlighted as drop targets while dragging from the
    // inventory. With two feeds the highlight is per-strip, so the panel says
    // where the payload may land rather than just whether the machine wants it.
    if (m_drag.active() && m_drag.source == Drag::Source::PlayerInv) {
        const glm::vec4 yes(0.20f, 0.55f, 0.25f, 0.45f), no(0.55f, 0.20f, 0.20f, 0.45f);
        m_ui.rect(L.px + 8, L.inY, L.panelW - 16, PanelLayout::StripH - 4,
                  acceptsAsIngredient(mac, m_drag.id) ? yes : no);
        if (L.hasFuel) {
            m_ui.rect(L.px + 8, L.fuelY, L.panelW - 16, PanelLayout::StripH - 4,
                      acceptsAsFuel(mac, m_drag.id) ? yes : no);
        }
    }
    m_ui.text(L.px + 16, L.inY + 16, 13.0f, "IN:", glm::vec4(0.85f, 0.85f, 0.9f, 1.0f));
    drawItemGrid(m_ui, m_atlas, L.stripCellsX, L.inY + 6.0f, inItems, /*cols=*/99);

    if (L.hasFuel) {
        m_ui.text(L.px + 16, L.fuelY + 16, 13.0f, "FUEL:", glm::vec4(0.95f, 0.75f, 0.45f, 1.0f));
        drawItemGrid(m_ui, m_atlas, L.stripCellsX, L.fuelY + 6.0f, fuelItems, /*cols=*/99);
    }

    // OUT strip.
    m_ui.text(L.px + 16, L.outY + 16, 13.0f, "OUT:", glm::vec4(0.85f, 0.9f, 0.85f, 1.0f));
    drawItemGrid(m_ui, m_atlas, L.stripCellsX, L.outY + 6.0f, outItems, /*cols=*/99);

    // Progress bar. A cranked machine's bar is short by the width of the dial,
    // which sits on its right and IS the reason the bar moves at all.
    const float barW = (L.panelW - 32) - (traits.handCranked ? 78.0f : 0.0f);
    m_ui.rect(L.px + 16, L.barY, barW, 10, glm::vec4(0.0f, 0.0f, 0.0f, 0.8f));
    if (mac.crafting) {
        const float frac = mac.jammed
            ? 1.0f
            : glm::clamp(mac.craftTime > 0 ? mac.progress / mac.craftTime : 0.0f, 0.0f, 1.0f);
        m_ui.rect(L.px + 16, L.barY, barW * frac, 10, mac.jammed ? kBarJammed : kBarWorking);
    }

    // The crank dial: the handle, drawn as the four positions of one turn with
    // the next one lit. Built from rects rather than glyphs because the bitmap
    // font has no arrows -- and a turning handle reads better as a shape than
    // as the words "UP RIGHT DOWN LEFT" anyway.
    if (traits.handCranked) {
        const float cx = L.px + L.panelW - 46.0f, cy = L.barY + 5.0f;
        const float r = 13.0f, dot = 6.0f;
        for (int s = 0; s < 4; ++s) {
            // Screen bearings for UP / RIGHT / DOWN / LEFT, in crank order.
            const float ox = (s == 1 ? r : s == 3 ? -r : 0.0f);
            const float oy = (s == 2 ? r : s == 0 ? -r : 0.0f);
            const bool done = s < mac.crankStep;
            const bool next = s == mac.crankStep;
            const glm::vec4 col = next ? glm::vec4(1.00f, 0.85f, 0.30f, 1.00f)
                                : done ? glm::vec4(0.45f, 0.85f, 0.50f, 0.95f)
                                       : glm::vec4(0.30f, 0.30f, 0.35f, 0.85f);
            m_ui.rect(cx + ox - dot * 0.5f, cy + oy - dot * 0.5f, dot, dot, col);
        }
    }

    // Inventory grid (windowed; the label says so when it is scrolled).
    m_ui.text(L.px + 16, L.invLabelY + 2, 13.0f, "INVENTORY" + invMoreLabel(iv), kTextHeader);
    drawItemGrid(m_ui, m_atlas, L.px + 16.0f, L.invY, invItems);

    // Tooltip: name of the hovered cell (any of the item regions).
    ItemId hovered = hoveredItemIn(invItems, mx, my, L.px + 16.0f, L.invY);
    if (hovered == ItemId::None) {
        hovered = hoveredItemIn(inItems, mx, my, L.stripCellsX, L.inY + 6.0f, 99);
    }
    if (hovered == ItemId::None && L.hasFuel) {
        hovered = hoveredItemIn(fuelItems, mx, my, L.stripCellsX, L.fuelY + 6.0f, 99);
    }
    if (hovered == ItemId::None) {
        hovered = hoveredItemIn(outItems, mx, my, L.stripCellsX, L.outY + 6.0f, 99);
    }
    if (hovered != ItemId::None) {
        m_ui.text(L.px + 16, L.tooltipY, 12.0f, itemName(hovered), kTextTooltip);
    }

    m_ui.text(L.px + 16, L.footerY, 12.0f,
              traits.handCranked
                  ? "TURN THE HANDLE: UP RIGHT DOWN LEFT   DRAG: LMB STACK / RMB ONE   ESC CLOSE"
                  : "DRAG ITEMS: LMB STACK / RMB ONE   ROWS: CLICK OR W/S + ENTER   ESC CLOSE",
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
    m_invScroll = 0; // never open a panel already scrolled somewhere
    // Opening any PART of a circle opens the circle: a pedestal is a socket in
    // a 5x5 multiblock, not a machine you tune on its own, and walking to the
    // core just to look at the ring would be busywork. An orphan pedestal (no
    // core behind it) still falls through to the plain machine panel.
    if (m_world->getBlock(pos.x, pos.y, pos.z) == BlockId::Pedestal) {
        for (int s = 0; s < AlchemyCircle::kRingSlots; ++s) {
            const glm::ivec3 core = pos - AlchemyCircle::kRingOffsets[static_cast<std::size_t>(s)];
            if (m_world->getBlock(core.x, core.y, core.z) == BlockId::RuneCore &&
                m_machines.count(core) > 0) {
                m_machineUiPos = core;
                break;
            }
        }
    }
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
    } else if (m_drag.source == Drag::Source::PedestalIn) {
        const auto pit = m_machines.find(
            AlchemyCircle::slotPos(m_machineUiPos, m_drag.slot));
        if (pit != m_machines.end()) {
            pit->second.input.add(m_drag.id, m_drag.count);
        } else {
            m_inventory.add(m_drag.id, m_drag.count); // pedestal vanished
        }
    } else {
        const auto mit = m_machines.find(m_machineUiPos);
        if (mit != m_machines.end()) {
            Inventory& buf = (m_drag.source == Drag::Source::MachineIn)   ? mit->second.input
                           : (m_drag.source == Drag::Source::MachineFuel) ? mit->second.fuel
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

    if (machineTraits(mac.type).kind == MachineKind::RuneCore) { updateCircleUi(); return; }

    // Action rows: AUTO, one MAKE row per recipe, TAKE OUTPUTS, master switch.
    const MachineTraits& traits = machineTraits(mac.type);
    const auto recipes = recipesForMachine(mac.type);
    const bool switched = hasPowerSwitch(mac.type);
    const int rows = static_cast<int>(recipes.size()) + 2 + (switched ? 1 : 0);
    const int switchRow = switched ? rows - 1 : -1;
    const bool fuelStrip = usesFuelSlot(mac.type);
    const auto allItems = itemsOf(m_inventory);
    const auto inItems = itemsOf(mac.input);
    const auto fuelItems = itemsOf(mac.fuel);
    const auto outItems = itemsOf(mac.output);
    const InvWindow iv = invWindow(static_cast<int>(allItems.size()),
                                   panelInvRowsThatFit(window().height(), rows, fuelStrip),
                                   m_invScroll);
    const auto invItems = invSlice(allItems, iv);
    const PanelLayout L = panelLayout(window().width(), window().height(), rows,
                                      iv.rows, fuelStrip);
    const float mx = input().mouseX(), my = input().mouseY();
    const bool inPanelX = mx >= L.px && mx <= L.px + L.panelW;

    scrollInvGrid(input(), m_invScroll, iv, L.px + 16.0f, L.invY, mx, my);

    // A cranked machine wants the arrows for its handle, so W/S alone drive the
    // rows here (menuNav's useArrows).
    const MenuNav nav = menuNav(input(), m_machineUiSel, rows, L.px, L.panelW,
                                L.rowsY, PanelLayout::RowH, /*useWheel=*/false,
                                /*useArrows=*/!traits.handCranked);
    if (nav.changed) audio().play("click", kUiVolume);

    // --- The crank. This is the ONLY thing that moves a manual machine: one
    // full turn banks kCrankProgress seconds for the next tick to spend. A
    // wrong key is a slipped grip -- back to the top of the turn.
    if (traits.handCranked && !m_drag.active()) {
        for (int d = 0; d < 4; ++d) {
            if (!input().wasKeyPressed(kCrankOrder[d])) continue;
            if (d != mac.crankStep) {
                mac.crankStep = 0;
                audio().play("deny", kUiVolume);
                break;
            }
            // Every good step turns the model's handle a quarter; a slipped
            // grip does not, so the part moves exactly as far as the hand did.
            m_crankTarget += 0.25f;
            if (++mac.crankStep >= 4) {
                mac.crankStep = 0;
                mac.crankBanked += kCrankProgress;
                audio().play("craft", kCraftVolume);
            } else {
                audio().play("click", kUiVolume);
            }
            break; // one key per frame; two arrows at once is not half a turn
        }
    }

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
        } else if (L.hasFuel &&
                   (ci = hitCell(mx, my, L.stripCellsX, L.fuelY + 6.0f,
                                 static_cast<int>(fuelItems.size()), 99)) >= 0) {
            const auto [id, cnt] = fuelItems[ci];
            const int take = lmb ? cnt : 1;
            mac.fuel.remove(id, take);
            m_drag = {Drag::Source::MachineFuel, id, take};
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
        const bool overFuel = L.hasFuel && inPanelX && my >= L.fuelY &&
                              my < L.fuelY + PanelLayout::StripH;
        const bool overInv = inPanelX && my >= L.invY &&
                             my < L.invY + L.invRows * (PanelLayout::Cell + PanelLayout::Gap);
        if (overIn && m_drag.source == Drag::Source::PlayerInv &&
            acceptsAsIngredient(mac, m_drag.id)) {
            mac.input.add(m_drag.id, m_drag.count);
            m_drag = Drag{};
        } else if (overFuel && m_drag.source == Drag::Source::PlayerInv &&
                   acceptsAsFuel(mac, m_drag.id)) {
            mac.fuel.add(m_drag.id, m_drag.count);
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
        } else if (m_machineUiSel == switchRow) {
            // The master switch. Everything else about the machine is kept --
            // progress, buffers, the recipe lock -- so this is a pause, not a
            // reset. The re-solve is required rather than tidy: an off machine
            // stops drawing power (its network may now be satisfied), stops
            // producing it (its network may now be dark), and drops out of the
            // energized set, which is what makes it LOOK off.
            mac.enabled = !mac.enabled;
            audio().play(mac.enabled ? "click" : "deny", kUiVolume);
            solvePowerAndMarkDirty();
        } else {
            // Take all outputs.
            for (int i = 0; i < static_cast<int>(itemCount()); ++i) {
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

void VoxelGame::drawCircleUi() {
    const auto mit = m_machines.find(m_machineUiPos);
    if (mit == m_machines.end()) return;
    const Machine& core = mit->second;

    const auto allItems = itemsOf(m_inventory);
    const auto outItems = itemsOf(core.output);
    const int w = window().width(), h = window().height();
    const InvWindow iv = invWindow(static_cast<int>(allItems.size()),
                                   circleMetrics::invRowsThatFit(h), m_invScroll);
    const auto invItems = invSlice(allItems, iv);
    const CircleLayout L = circleLayout(w, h, iv.rows);
    const float mx = input().mouseX(), my = input().mouseY();

    const AlchemyCircle::Tier tier =
        AlchemyCircle::tierAt(*m_world, m_machines, m_machineUiPos);
    const bool energized =
        m_power.energized(m_machineUiPos.x, m_machineUiPos.y, m_machineUiPos.z);
    const auto ring = AlchemyCircle::ringContents(*m_world, m_machines, m_machineUiPos);
    // What the ring spells right now, and -- once started -- whether it still
    // spells the recipe it was started on. The two differ exactly when belts
    // are mid-refill, which is the case the lock exists for.
    const bool started = core.selectedRecipe >= 0 &&
                         core.selectedRecipe < static_cast<int>(circleRecipes().size());
    const AlchemyCircle::Match match =
        AlchemyCircle::findMatch(ring, core.input, tier, energized, core.selectedRecipe);

    beginPanel(m_ui, w, h, L.px, L.py, L.panelW, L.panelH, "ALCHEMY CIRCLE", 0.45f);

    // Header: the tier is the pedestal count, so say so plainly.
    const char* tierText = tier == AlchemyCircle::Tier::Greater ? "GREATER CIRCLE"
                         : tier == AlchemyCircle::Tier::Lesser  ? "LESSER CIRCLE"
                                                                : "INCOMPLETE";
    const glm::vec4 tierColor = tier == AlchemyCircle::Tier::Greater
                                    ? glm::vec4(0.75f, 0.65f, 1.0f, 1.0f)
                                : tier == AlchemyCircle::Tier::Lesser
                                    ? glm::vec4(0.65f, 0.85f, 0.95f, 1.0f)
                                    : glm::vec4(0.95f, 0.4f, 0.35f, 1.0f);
    m_ui.text(L.px + L.panelW - 260, L.py + 15, 13.0f, tierText, tierColor);
    m_ui.text(L.px + L.panelW - 110, L.py + 15, 13.0f,
              energized ? "POWERED" : "UNPOWERED",
              energized ? glm::vec4(0.4f, 0.95f, 0.45f, 1.0f) : kTextDim);

    // The ring, drawn at true compass bearings. An empty pedestal is an open
    // socket; a missing one is a dashed ghost, so what to build next is
    // obvious from the panel alone.
    for (int s = 0; s < AlchemyCircle::kRingSlots; ++s) {
        const glm::vec2 p = circleCellPos(L, s);
        const bool present =
            m_machines.count(AlchemyCircle::slotPos(m_machineUiPos, s)) > 0 &&
            m_world->getBlock(AlchemyCircle::slotPos(m_machineUiPos, s).x,
                              AlchemyCircle::slotPos(m_machineUiPos, s).y,
                              AlchemyCircle::slotPos(m_machineUiPos, s).z) == BlockId::Pedestal;
        if (!present) {
            m_ui.rect(p.x, p.y, PanelLayout::Cell, PanelLayout::Cell,
                      glm::vec4(0.14f, 0.12f, 0.16f, 0.75f));
            m_ui.text(p.x + 14, p.y + 13, 13.0f, "+", kTextDim);
            continue;
        }
        const ItemStack& held = ring[static_cast<std::size_t>(s)];
        if (held.id == ItemId::None) {
            m_ui.rect(p.x, p.y, PanelLayout::Cell, PanelLayout::Cell,
                      glm::vec4(0.20f, 0.20f, 0.26f, 1.0f));
        } else {
            drawItemCell(m_ui, m_atlas, p.x, p.y, held.id, held.count);
        }
    }

    // Centre: the catalyst socket.
    const glm::vec2 cp = circleCellPos(L, -1);
    const auto coreItems = itemsOf(core.input);
    if (coreItems.empty()) {
        m_ui.rect(cp.x, cp.y, PanelLayout::Cell, PanelLayout::Cell,
                  glm::vec4(0.26f, 0.20f, 0.34f, 1.0f));
    } else {
        drawItemCell(m_ui, m_atlas, cp.x, cp.y, coreItems.front().first,
                     coreItems.front().second);
    }

    // What the necklace currently spells, and whether it has been started.
    std::string status;
    glm::vec4 statusCol = kTextDim;
    const auto secondsFor = [&](const CircleRecipe& r) {
        return "  ( " + std::to_string(static_cast<int>(
                   AlchemyCircle::craftSeconds(r, tier, energized))) + "S )";
    };
    if (tier == AlchemyCircle::Tier::None) {
        status = "NEEDS THE FOUR CARDINAL PEDESTALS ( RADIUS 2 )";
        statusCol = glm::vec4(0.95f, 0.55f, 0.4f, 1.0f);
    } else if (started && match) {
        status = std::string("MAKING ") + itemName(match.recipe->output.id) +
                 secondsFor(*match.recipe);
        statusCol = glm::vec4(0.55f, 0.95f, 0.6f, 1.0f);
    } else if (started) {
        const CircleRecipe& r =
            circleRecipes()[static_cast<std::size_t>(core.selectedRecipe)];
        status = std::string("STARTED: ") + itemName(r.output.id) +
                 " -- WAITING FOR ITS PATTERN";
        statusCol = glm::vec4(0.95f, 0.8f, 0.45f, 1.0f);
    } else if (const AlchemyCircle::Match ready =
                   AlchemyCircle::findMatch(ring, core.input, tier, energized)) {
        status = std::string("READY: ") + itemName(ready.recipe->output.id) +
                 secondsFor(*ready.recipe) + " -- PRESS START";
        statusCol = glm::vec4(0.65f, 0.85f, 0.95f, 1.0f);
    } else {
        status = "NO PATTERN -- LAY INGREDIENTS ON THE PEDESTALS";
    }
    m_ui.text(L.px + 16, L.statusY, 13.0f, status, statusCol);

    // The action rows. Laying the pattern is the player's job; starting it and
    // collecting what it made are all that is left to click.
    const int sel = std::clamp(m_machineUiSel, 0, circleMetrics::Rows - 1);
    for (int i = 0; i < circleMetrics::Rows; ++i) {
        const float y = L.rowY + static_cast<float>(i) * CircleLayout::RowH;
        if (i == sel) {
            m_ui.rect(L.px + 6, y, L.panelW - 12, CircleLayout::RowH - 2, kRowSelBg);
        }
        const bool dim = i == 0 ? tier == AlchemyCircle::Tier::None : outItems.empty();
        const char* label = i == 1 ? "TAKE OUTPUTS" : started ? "STOP" : "START";
        m_ui.text(L.px + 16, y + 4, 13.0f, std::string("  ") + label,
                  rowColor(i == sel, dim));
    }

    // Progress + the core's output strip.
    m_ui.rect(L.px + 16, L.barY, L.panelW - 32, 10, glm::vec4(0.0f, 0.0f, 0.0f, 0.8f));
    if (core.crafting) {
        const float frac = glm::clamp(
            core.craftTime > 0 ? core.progress / core.craftTime : 0.0f, 0.0f, 1.0f);
        m_ui.rect(L.px + 16, L.barY, (L.panelW - 32) * frac, 10,
                  glm::vec4(0.65f, 0.45f, 0.95f, 0.95f));
    }
    m_ui.text(L.px + 16, L.outY + 16, 13.0f, "OUT:", glm::vec4(0.85f, 0.9f, 0.85f, 1.0f));
    drawItemGrid(m_ui, m_atlas, L.px + 64.0f, L.outY + 6.0f, outItems, /*cols=*/99);

    m_ui.text(L.px + 16, L.invLabelY + 2, 13.0f, "INVENTORY" + invMoreLabel(iv), kTextHeader);
    drawItemGrid(m_ui, m_atlas, L.px + 16.0f, L.invY, invItems);

    // Tooltip across all three regions (ring cells included).
    ItemId hovered = hoveredItemIn(invItems, mx, my, L.px + 16.0f, L.invY);
    if (hovered == ItemId::None) {
        hovered = hoveredItemIn(outItems, mx, my, L.px + 64.0f, L.outY + 6.0f, 99);
    }
    if (hovered == ItemId::None) {
        const int c = circleHit(L, mx, my);
        if (c >= 0) hovered = ring[static_cast<std::size_t>(c)].id;
        else if (c == -1 && !coreItems.empty()) hovered = coreItems.front().first;
    }
    if (hovered != ItemId::None) {
        m_ui.text(L.px + 16, L.tooltipY, 12.0f, itemName(hovered), kTextTooltip);
    }

    m_ui.text(L.px + 16, L.footerY, 12.0f,
              "DRAG ONTO THE RING: LMB STACK / RMB ONE   W/S + ENTER: ROWS   ESC CLOSE",
              kTextFooter);

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

void VoxelGame::updateCircleUi() {
    const auto mit = m_machines.find(m_machineUiPos);
    if (mit == m_machines.end()) { cancelDrag(); closeMachineUi(); return; }
    Machine& core = mit->second;

    const auto allItems = itemsOf(m_inventory);
    const InvWindow iv = invWindow(static_cast<int>(allItems.size()),
                                   circleMetrics::invRowsThatFit(window().height()),
                                   m_invScroll);
    const auto invItems = invSlice(allItems, iv);
    const CircleLayout L = circleLayout(window().width(), window().height(), iv.rows);
    const float mx = input().mouseX(), my = input().mouseY();

    // The grid is the only list left here, so the wheel always belongs to it.
    scrollInvGrid(input(), m_invScroll, iv, L.px + 16.0f, L.invY, mx, my);

    const MenuNav nav = menuNav(input(), m_machineUiSel, circleMetrics::Rows, L.px,
                                L.panelW, L.rowY, CircleLayout::RowH);
    if (nav.changed) audio().play("click", kUiVolume);

    const bool lmb = input().wasMousePressed(SDL_BUTTON_LEFT);
    const bool rmb = input().wasMousePressed(SDL_BUTTON_RIGHT);
    bool clickConsumed = false;

    const int cell = circleHit(L, mx, my);
    const auto outItems = itemsOf(core.output);

    // --- Drag pickup: from the inventory grid, a pedestal, the catalyst cell,
    // or the OUT strip. LMB takes the stack, RMB takes one. ---
    if (!m_drag.active() && (lmb || rmb)) {
        int ci;
        if (cell >= 0) {
            const auto pit = m_machines.find(AlchemyCircle::slotPos(m_machineUiPos, cell));
            if (pit != m_machines.end()) {
                const auto held = itemsOf(pit->second.input);
                if (!held.empty()) {
                    const auto [id, cnt] = held.front();
                    const int take = lmb ? cnt : 1;
                    pit->second.input.remove(id, take);
                    m_drag = {Drag::Source::PedestalIn, id, take, cell};
                    clickConsumed = true;
                    core.selectedRecipe = -1; // a hand edit to the ring stops it
                    core.progress = 0.0f;
                }
            }
        } else if (cell == -1) {
            const auto held = itemsOf(core.input);
            if (!held.empty()) {
                const auto [id, cnt] = held.front();
                const int take = lmb ? cnt : 1;
                core.input.remove(id, take);
                m_drag = {Drag::Source::MachineIn, id, take};
                clickConsumed = true;
                core.selectedRecipe = -1;
                core.progress = 0.0f;
            }
        } else if ((ci = hitCell(mx, my, L.px + 16.0f, L.invY,
                                 static_cast<int>(invItems.size()),
                                 PanelLayout::InvCols)) >= 0) {
            const auto [id, cnt] = invItems[ci];
            const int take = lmb ? cnt : 1;
            m_inventory.remove(id, take);
            m_drag = {Drag::Source::PlayerInv, id, take};
            clickConsumed = true;
        } else if ((ci = hitCell(mx, my, L.px + 64.0f, L.outY + 6.0f,
                                 static_cast<int>(outItems.size()), 99)) >= 0) {
            const auto [id, cnt] = outItems[ci];
            const int take = lmb ? cnt : 1;
            core.output.remove(id, take);
            m_drag = {Drag::Source::MachineOut, id, take};
            clickConsumed = true;
        }
        if (clickConsumed) audio().play("click", kUiVolume);
    }

    // --- Drag drop. A pedestal is a one-item-TYPE holder, so a drop that
    // would mix two items is refused and the payload goes home. ---
    if (m_drag.active() && (input().wasMouseReleased(SDL_BUTTON_LEFT) ||
                            input().wasMouseReleased(SDL_BUTTON_RIGHT))) {
        const bool overInv = mx >= L.px && mx <= L.px + L.panelW && my >= L.invY &&
                             my < L.invY + L.invRows * (PanelLayout::Cell + PanelLayout::Gap);
        bool placed = false;
        if (cell >= 0) {
            const auto pit = m_machines.find(AlchemyCircle::slotPos(m_machineUiPos, cell));
            if (pit != m_machines.end()) {
                Inventory& buf = pit->second.input;
                const bool empty = itemsOf(buf).empty();
                if ((empty || buf.count(m_drag.id) > 0) &&
                    buf.count(m_drag.id) + m_drag.count <= kPedestalCap) {
                    buf.add(m_drag.id, m_drag.count);
                    m_drag = Drag{};
                    placed = true;
                    // A hand edit to the ring stops a started circle: you are
                    // laying something new, and it waits for START again.
                    // Belts topping a pattern up never come through here,
                    // which is what lets a started circle run fed.
                    core.selectedRecipe = -1;
                    core.progress = 0.0f;
                }
            }
        } else if (cell == -1 && MachineSystem::machineAccepts(core, m_drag.id)) {
            core.input.add(m_drag.id, m_drag.count);
            m_drag = Drag{};
            placed = true;
            core.selectedRecipe = -1;
            core.progress = 0.0f;
        } else if (overInv && m_drag.source != Drag::Source::PlayerInv) {
            m_inventory.add(m_drag.id, m_drag.count);
            m_drag = Drag{};
            placed = true;
        }
        if (!placed) cancelDrag();
        audio().play("click", kUiVolume);
        updateTitle();
        clickConsumed = true;
    }

    // Which row, if any, was activated: a click acts on the row under the
    // cursor, Enter on the highlighted one.
    const int activated = m_drag.active()                  ? -1
                        : nav.clickedRows && !clickConsumed ? nav.hoverRow
                        : nav.enter                         ? m_machineUiSel
                                                            : -1;

    // --- Row 0: START / STOP. Starting locks the circle to what the ring
    // spells right now; that lock is the whole of "started" (tickRuneCore). ---
    if (activated == 0) {
        if (core.selectedRecipe >= 0) {
            core.selectedRecipe = -1;
            core.progress = 0.0f;
            audio().play("click", kUiVolume);
        } else {
            const AlchemyCircle::Tier tier =
                AlchemyCircle::tierAt(*m_world, m_machines, m_machineUiPos);
            const bool energized =
                m_power.energized(m_machineUiPos.x, m_machineUiPos.y, m_machineUiPos.z);
            const auto ring =
                AlchemyCircle::ringContents(*m_world, m_machines, m_machineUiPos);
            const AlchemyCircle::Match match =
                AlchemyCircle::findMatch(ring, core.input, tier, energized);
            if (tier == AlchemyCircle::Tier::None) {
                deny("THE CIRCLE NEEDS ITS FOUR CARDINAL PEDESTALS");
            } else if (match) {
                core.selectedRecipe =
                    static_cast<int>(match.recipe - circleRecipes().data());
                core.progress = 0.0f;
                audio().play("craft", kCraftVolume);
            } else if (AlchemyCircle::findMatch(ring, core.input, tier, true)) {
                // Laid right, but an eight-slot pattern only runs powered.
                deny("THAT PATTERN NEEDS A POWERED GREATER CIRCLE");
            } else {
                deny("NO PATTERN ON THE PEDESTALS TO START");
            }
        }
    }

    // --- Row 1: empty the core's output into the pack. ---
    if (activated == 1) {
        for (int i = 1; i < static_cast<int>(itemCount()); ++i) {
            const ItemId id = static_cast<ItemId>(i);
            const int c = core.output.count(id);
            if (c > 0) { core.output.remove(id, c); m_inventory.add(id, c); }
        }
        audio().play("click", kUiVolume);
        updateTitle();
    }

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
        // Name the FIRST thing you are short of and by how much. The row
        // already tints unaffordable, but "you cannot" and "you need four more
        // sticks" are different amounts of help.
        for (const ItemStack& in : r.inputs) {
            const int have = m_inventory.count(in.id);
            if (have >= in.count) continue;
            deny("NEED " + std::to_string(in.count - have) + " MORE " +
                 itemName(in.id));
            return;
        }
        deny("CANNOT CRAFT THAT YET");
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

    const auto allItems = itemsOf(m_inventory);
    const InvWindow iv = invWindow(static_cast<int>(allItems.size()),
                                   craftInvRowsThatFit(window().height(), n), m_invScroll);
    const CraftLayout L = craftLayout(window().width(), window().height(), n, iv.rows);
    const float mx = input().mouseX(), my = input().mouseY();

    // The wheel drives the recipe rows unless the cursor is over the grid.
    const bool scrolled = scrollInvGrid(input(), m_invScroll, iv,
                                        L.px + 16.0f, L.invY, mx, my);
    const MenuNav nav = menuNav(input(), m_menuSelection, n, L.px, L.panelW,
                                L.rowsY, CraftLayout::RowH, /*useWheel=*/!scrolled);
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
    m_invScroll = 0; // never open a panel already scrolled somewhere
    window().setRelativeMouse(false); // release the cursor for drag/hover
    audio().play("open", kUiVolume);
}

void VoxelGame::closeInventoryUi() {
    m_invDrag = ItemId::None; // an unfinished drag assigns nothing
    m_invOpen = false;
    window().setRelativeMouse(true);
    audio().play("close", kUiVolume);
}

void VoxelGame::recomputeArmor() {
    float sum = 0.0f;
    for (const ItemId id : m_armor) sum += itemArmor(id);
    m_armorMitigation = sum < kArmorMaxReduction ? sum : kArmorMaxReduction;
}

void VoxelGame::updateInventoryUi() {
    const auto allItems = itemsOf(m_inventory);
    const InvWindow iv = invWindow(static_cast<int>(allItems.size()),
                                   overlayInvRowsThatFit(window().height()), m_invScroll);
    const auto invItems = invSlice(allItems, iv);
    const InvLayout L = invLayout(window().width(), window().height(), iv.rows);
    const float mx = input().mouseX(), my = input().mouseY();

    scrollInvGrid(input(), m_invScroll, iv, L.px + 16.0f, L.invY, mx, my);

    const int gridHit = hitCell(mx, my, L.px + 16.0f, L.invY,
                                static_cast<int>(invItems.size()), PanelLayout::InvCols);
    const int slotHit = hitCell(mx, my, L.px + 16.0f, L.hotbarY, kHotbarSlots,
                                PanelLayout::InvCols);
    const int armorHit = hitCell(mx, my, L.px + 16.0f, L.armorY, kArmorSlots,
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
        const ArmorSlot as = itemArmorSlot(m_invDrag);
        const int armorIdx = static_cast<int>(as) - 1; // Head=0/Body=1/Feet=2
        if (as != ArmorSlot::None && armorHit == armorIdx && m_inventory.has(m_invDrag)) {
            // Equip into the piece's own slot: the worn piece leaves the pack;
            // a piece already there swaps back in.
            if (m_armor[armorIdx] != ItemId::None) m_inventory.add(m_armor[armorIdx], 1);
            m_inventory.remove(m_invDrag, 1);
            m_armor[armorIdx] = m_invDrag;
            recomputeArmor();
        } else if (slotHit >= 0) {
            for (ItemId& s : m_hotbar) {
                if (s == m_invDrag) s = ItemId::None;
            }
            m_hotbar[slotHit] = m_invDrag;
        }
        m_invDrag = ItemId::None;
        audio().play("click", kUiVolume);
        updateTitle();
    }

    // RMB unequips an armor slot / clears a hotbar slot directly; elsewhere it
    // closes (menu precedent).
    if (input().wasMousePressed(SDL_BUTTON_RIGHT)) {
        if (armorHit >= 0 && m_armor[armorHit] != ItemId::None) {
            m_inventory.add(m_armor[armorHit], 1);
            m_armor[armorHit] = ItemId::None;
            recomputeArmor();
            audio().play("click", kUiVolume);
            updateTitle();
        } else if (slotHit >= 0 && m_hotbar[slotHit] != ItemId::None) {
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
    const auto allItems = itemsOf(m_inventory);
    const InvWindow iv = invWindow(static_cast<int>(allItems.size()),
                                   overlayInvRowsThatFit(h), m_invScroll);
    const auto invItems = invSlice(allItems, iv);
    const InvLayout L = invLayout(w, h, iv.rows);
    const float mx = input().mouseX(), my = input().mouseY();

    beginPanel(m_ui, w, h, L.px, L.py, L.panelW, L.panelH,
               ("INVENTORY" + invMoreLabel(iv)).c_str(), 0.5f);

    // Everything the player owns, with counts (windowed; see InvWindow).
    drawItemGrid(m_ui, m_atlas, L.px + 16.0f, L.invY, invItems);

    // The armor strip: head/body/feet equip slots. Drag a matching piece here
    // to wear it (it leaves the pack); RMB unequips.
    m_ui.text(L.px + 16, L.armorLabelY + 2, 13.0f, "ARMOR", kTextHeader);
    const char* const kArmorNames[kArmorSlots] = {"HEAD", "BODY", "FEET"};
    const int armorHover = hitCell(mx, my, L.px + 16.0f, L.armorY, kArmorSlots,
                                   PanelLayout::InvCols);
    for (int i = 0; i < kArmorSlots; ++i) {
        const float cx = L.px + 16.0f + i * (PanelLayout::Cell + PanelLayout::Gap);
        const float cy = L.armorY;
        const bool dropHere = m_invDrag != ItemId::None &&
                              itemArmorSlot(m_invDrag) == static_cast<ArmorSlot>(i + 1);
        if (dropHere) {
            m_ui.rect(cx - 3, cy - 3, PanelLayout::Cell + 6, PanelLayout::Cell + 6,
                      glm::vec4(0.20f, 0.55f, 0.25f, 0.9f));
        }
        m_ui.rect(cx, cy, PanelLayout::Cell, PanelLayout::Cell,
                  glm::vec4(0.16f, 0.16f, 0.19f, 1.0f));
        const ItemId id = m_armor[i];
        if (id == ItemId::None) {
            m_ui.text(cx + 4, cy + PanelLayout::Cell * 0.5f - 5, 9.0f, kArmorNames[i],
                      glm::vec4(0.55f, 0.55f, 0.6f, 1.0f));
            continue;
        }
        glm::vec2 uv0, uv1;
        Atlas::uvForTile(iconTile(id), uv0, uv1);
        m_ui.icon(m_atlas, cx + 4, cy + 4, PanelLayout::Cell - 8, PanelLayout::Cell - 8,
                  uv0, uv1);
    }

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
    if (hovered == ItemId::None && armorHover >= 0) {
        hovered = m_armor[armorHover];
    }
    if (hovered != ItemId::None) {
        m_ui.text(L.px + 16, L.tooltipY, 12.0f, itemName(hovered), kTextTooltip);
    }

    m_ui.text(L.px + 16, L.footerY, 12.0f,
              "DRAG TO HOTBAR / ARMOR   RMB CLEAR OR UNEQUIP   TAB / ESC CLOSE",
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

    // Equipped armor: three small icons above the hotbar's RIGHT end, mirroring
    // the hearts on the left.
    {
        const float segW = 18.0f, segGap = 4.0f;
        const float ay = y - segW - 10.0f;
        for (int i = 0; i < kArmorSlots; ++i) {
            const float ax = x0 + totalW - (kArmorSlots - i) * (segW + segGap) + segGap;
            m_ui.rect(ax, ay, segW, segW, glm::vec4(0.10f, 0.10f, 0.12f, 0.85f));
            const ItemId id = m_armor[i];
            if (id == ItemId::None) continue;
            glm::vec2 uv0, uv1;
            Atlas::uvForTile(iconTile(id), uv0, uv1);
            m_ui.icon(m_atlas, ax + 1, ay + 1, segW - 2, segW - 2, uv0, uv1);
        }
    }

    // Elixir of Vigor: a thin timer bar above the hearts while the buff is live.
    if (m_vigorTimer > 0.0f) {
        const float bw = static_cast<float>(kMaxHealth) * 20.0f, bh = 4.0f;
        const float by = y - 42.0f;
        m_ui.text(x0, by - 14, 11.0f, "VIGOR", glm::vec4(0.55f, 0.9f, 0.5f, 1.0f));
        m_ui.rect(x0, by, bw, bh, glm::vec4(0.10f, 0.10f, 0.14f, 0.9f));
        m_ui.rect(x0, by, bw * (m_vigorTimer / kVigorSeconds), bh,
                  glm::vec4(0.45f, 0.85f, 0.35f, 0.95f));
    }

    // Why the last thing you tried was refused (see VoxelGame::deny). Centered
    // above the hotbar, over its own backing so it reads against any terrain,
    // and fading over the last third of its life so it leaves rather than
    // blinking out. Drawn before the bars below so a long reason cannot cover
    // the one bar that shares this band.
    if (m_denyTimer > 0.0f && !m_denyText.empty()) {
        const float fade = glm::clamp(m_denyTimer / (kDenySeconds / 3.0f), 0.0f, 1.0f);
        const float ts = 13.0f;
        const float tw = m_ui.textWidth(ts, m_denyText);
        const float tx = (static_cast<float>(w) - tw) * 0.5f;
        const float ty = y - 72.0f;
        m_ui.rect(tx - 8, ty - 5, tw + 16, ts + 10,
                  glm::vec4(0.06f, 0.04f, 0.05f, 0.78f * fade));
        m_ui.text(tx, ty, ts, m_denyText, glm::vec4(1.0f, 0.72f, 0.45f, fade));
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
        // A jam is FULL, not partial: show the whole bar amber rather than a
        // frozen sliver, so a stalled line is visible across the factory floor
        // instead of looking like a machine that happens to be slow.
        const float frac = m.jammed
            ? 1.0f
            : glm::clamp(m.craftTime > 0 ? m.progress / m.craftTime : 0.0f, 0.0f, 1.0f);
        m_ui.rect(bx, by, bw * frac, bh, m.jammed ? kBarJammed : kBarWorking);
    }

    // The FILTER ghost on an empty conduit: what this belt is waiting for.
    // Without it a filter would be invisible, and an invisible routing rule is
    // the exact problem filters were added to solve.
    //
    // Cargo itself left this loop -- it is world geometry now (buildCargoMesh),
    // so it slides between cells and a wall hides it. The ghost stays a
    // screen-space icon on purpose: it is an ANNOTATION, not a thing in the
    // world, it has to be legible through the tube it labels, and the world
    // pass does cutout rather than blending so it could not be drawn tinted.
    for (const auto& [pos, b] : m_belts) {
        if (b.item != ItemId::None || b.filter == ItemId::None) continue;
        glm::vec2 sp;
        if (!projectToScreen(glm::vec3(pos) + glm::vec3(0.5f, 0.85f, 0.5f), sp)) continue;
        const float dist = glm::length(camera().position - (glm::vec3(pos) + glm::vec3(0.5f)));
        if (dist > kDropRenderDist) continue; // match the cargo it stands in for
        const float s = glm::clamp(150.0f / dist, 10.0f, 40.0f) * 0.8f;
        glm::vec2 uv0, uv1;
        Atlas::uvForTile(iconTile(b.filter), uv0, uv1);
        m_ui.icon(m_atlas, sp.x - s * 0.5f, sp.y - s * 0.5f, s, s, uv0, uv1, kFilterGhost);
    }

    // Ground items: billboarded icons (same convention as belt cargo) with a
    // gentle bob and a count badge on stacks. Culled behind the camera and
    // beyond kDropRenderDist.
    {
        const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
        const glm::vec3 camPos = camera().position;
        const float maxD2 = kDropRenderDist * kDropRenderDist;
        for (const DroppedItem& d : m_drops) {
            if (d.dim != m_dimension || d.id == ItemId::None) continue;
            const float bob = 0.06f * std::sin(now * 2.0f + d.pos.x + d.pos.z);
            const glm::vec3 wc = d.pos + glm::vec3(0.0f, 0.12f + bob, 0.0f);
            const glm::vec3 rel = wc - camPos;
            const float dist2 = glm::dot(rel, rel);
            if (dist2 > maxD2) continue;
            glm::vec2 sp;
            if (!projectToScreen(wc, sp)) continue;
            const float s = glm::clamp(kDropIconScale / std::sqrt(std::max(0.25f, dist2)),
                                       kDropIconMin, kDropIconMax);
            glm::vec2 uv0, uv1;
            Atlas::uvForTile(iconTile(d.id), uv0, uv1);
            m_ui.icon(m_atlas, sp.x - s * 0.5f, sp.y - s * 0.5f, s, s, uv0, uv1);
            if (d.count > 1) {
                m_ui.text(sp.x + s * 0.15f, sp.y + s * 0.15f, 11.0f,
                          std::to_string(d.count), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            }
        }
    }

    // Timed-break progress: a thin bar over the block being mined.
    if (m_breaking && m_hasTarget) {
        glm::vec2 sp;
        if (projectToScreen(glm::vec3(m_breakTarget) + glm::vec3(0.5f, 1.1f, 0.5f), sp)) {
            const float bw = 40.0f, bh = 6.0f;
            const float bx = sp.x - bw * 0.5f, by = sp.y - bh * 0.5f;
            const float frac = glm::clamp(
                m_breakNeeded > 0.0f ? m_breakProgress / m_breakNeeded : 1.0f, 0.0f, 1.0f);
            m_ui.rect(bx - 1, by - 1, bw + 2, bh + 2, glm::vec4(0.0f, 0.0f, 0.0f, 0.7f));
            m_ui.rect(bx, by, bw * frac, bh, glm::vec4(0.92f, 0.82f, 0.25f, 0.95f));
        }
    }

    // Look-at machine panel (name, input/output buffers, controls).
    if (m_hasTarget) {
        const auto mit = m_machines.find(m_targetBlock);
        if (mit != m_machines.end()) {
            const Machine& m = mit->second;
            // A machine with its own fuel buffer gets a third line for it.
            const bool showFuel = usesFuelSlot(m.type);
            const float pw = 380.0f, ph = showFuel ? 118.0f : 98.0f;
            const float pxp = (static_cast<float>(w) - pw) * 0.5f;
            const float pyp = y - ph - 14.0f;
            m_ui.rect(pxp, pyp, pw, ph, glm::vec4(0.07f, 0.07f, 0.09f, 0.92f));
            m_ui.text(pxp + 12, pyp + 8, 16.0f, blockName(m.type), glm::vec4(1.0f, 1.0f, 0.7f, 1.0f));
            // An idled machine is otherwise indistinguishable from a starved
            // one at a glance -- both just sit there -- so say which.
            if (!m.enabled) {
                m_ui.text(pxp + pw - 60, pyp + 10, 14.0f, "OFF",
                          glm::vec4(0.55f, 0.55f, 0.60f, 1.0f));
            }

            std::string in = "IN:";
            std::string fuel = "FUEL:";
            std::string out = "OUT:";
            // Which pile is full, named. Standing in front of a stalled line
            // and reading the counts is exactly when a player needs to know
            // WHICH item stopped it, and the numbers alone don't say.
            const int inCap = MachineSystem::inputCap(m);
            const int outCap = MachineSystem::outputCap(m);
            for (int i = 0; i < static_cast<int>(itemCount()); ++i) {
                const ItemId id = static_cast<ItemId>(i);
                if (m.input.count(id) > 0) {
                    in += " " + std::string(itemName(id)) + " x" + std::to_string(m.input.count(id));
                    if (m.input.count(id) >= inCap) in += " ( FULL )";
                }
                if (m.fuel.count(id) > 0)
                    fuel += " " + std::string(itemName(id)) + " x" + std::to_string(m.fuel.count(id));
                if (m.output.count(id) > 0) {
                    out += " " + std::string(itemName(id)) + " x" + std::to_string(m.output.count(id));
                    if (m.output.count(id) >= outCap) out += " ( FULL )";
                }
            }
            float ly = pyp + 32;
            m_ui.text(pxp + 12, ly, 13.0f, in, glm::vec4(0.85f, 0.85f, 0.9f, 1.0f));
            ly += 20;
            if (showFuel) {
                m_ui.text(pxp + 12, ly, 13.0f, fuel, glm::vec4(0.95f, 0.75f, 0.45f, 1.0f));
                ly += 20;
            }
            m_ui.text(pxp + 12, ly, 13.0f, out,
                      m.jammed ? kBarJammed : glm::vec4(0.85f, 0.9f, 0.85f, 1.0f));
            m_ui.text(pxp + 12, pyp + ph - 22.0f, 12.0f,
                      !m.enabled ? "RMB OPEN - THEN TURN ON"
                      : m.jammed ? "JAMMED - DRAIN IT OR BELT IT TO A CRATE"
                      : machineTraits(m.type).handCranked ? "RMB OPEN - THEN CRANK IT"
                                                          : "RMB OPEN",
                      m.jammed ? kBarJammed : glm::vec4(0.7f, 0.7f, 0.75f, 1.0f));
        }

        // Look-at conduit. The tube reports its own flow and filter in words,
        // which is the other half of what the retired top-face arrow used to
        // do -- the glowing OUT arm shows the direction at a glance, and this
        // names it, along with the routing rule an arrow could never show.
        const auto bit = m_belts.find(m_targetBlock);
        if (bit != m_belts.end()) {
            const Belt& b = bit->second;
            const glm::ivec3& f = b.facing;
            const char* dir = f.x > 0 ? "EAST" : f.x < 0 ? "WEST"
                            : f.z > 0 ? "SOUTH" : f.z < 0 ? "NORTH"
                            : f.y > 0 ? "UP" : "DOWN";
            const float pw = 380.0f, ph = 78.0f;
            const float pxp = (static_cast<float>(w) - pw) * 0.5f;
            const float pyp = y - ph - 14.0f;
            m_ui.rect(pxp, pyp, pw, ph, glm::vec4(0.07f, 0.07f, 0.09f, 0.92f));
            m_ui.text(pxp + 12, pyp + 8, 16.0f, blockName(BlockId::Belt),
                      glm::vec4(1.0f, 1.0f, 0.7f, 1.0f));
            m_ui.text(pxp + 12, pyp + 32, 13.0f, std::string("FLOW: ") + dir,
                      glm::vec4(0.85f, 0.85f, 0.9f, 1.0f));
            // "ANY" rather than an empty line: an unfiltered belt is a
            // deliberate state, not a missing one.
            m_ui.text(pxp + 12, pyp + ph - 22.0f, 12.0f,
                      b.filter == ItemId::None
                          ? std::string("FILTER: ANY")
                          : std::string("FILTER: ") + itemName(b.filter),
                      b.filter == ItemId::None ? glm::vec4(0.7f, 0.7f, 0.75f, 1.0f)
                                               : glm::vec4(0.85f, 0.9f, 0.85f, 1.0f));
        }
    }

    m_ui.end();
}

void VoxelGame::drawCraftMenu() {
    const int w = window().width();
    const int h = window().height();
    const auto& recipes = handcraftRecipes();
    const int n = static_cast<int>(recipes.size());
    const auto allItems = itemsOf(m_inventory);
    const InvWindow iv = invWindow(static_cast<int>(allItems.size()),
                                   craftInvRowsThatFit(h, n), m_invScroll);
    const auto invItems = invSlice(allItems, iv);
    const CraftLayout L = craftLayout(w, h, n, iv.rows);
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
    m_ui.text(L.px + 16, L.invLabelY + 2, 13.0f, "INVENTORY" + invMoreLabel(iv), kTextHeader);
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
        {"1. START BARE: DIG TURF FOR FIBER, LEAVES FOR STICKS AND SAPLINGS,", 1},
        {"   DIRT FOR PEBBLES. 3 FIBER TWISTS INTO TWINE - EVERY TOOL NEEDS IT.", 1},
        {"2. CRAFT WITH " + k(Action::CraftMenu) +
             ": WOOD TOOLS, THEN STONE. A PICKAXE UNLOCKS STONE.", 1},
        {"3. OUT PAST THE HILLS: SOURCE PATCHES THAT REGROW, AND A DEAD FACTORY.", 1},
        {"4. ORE > INGOT > GENERATOR + PRESS. A POWERED PRESS MAKES THE PLATES.", 1},
        {"5. GENERATORS BURN WOOD OR CHARCOAL. KEEP THEM FED, REPLANT TREES.", 1},
        {"6. RAIN FILLS BARRELS AND HELD BUCKETS. IT IS THE ONLY WATER.", 1},
        {"7. CONDUITS CARRY ITEMS THE WAY THEIR ARROW POINTS. WRENCH + " +
             k(Action::WrenchRotate) + " RE-AIMS.", 1},
        {"8. BUFFERS FILL UP AND JAM ( AMBER BAR ). CRATES HOLD THE OVERFLOW,", 1},
        {"   AND " + k(Action::BeltFilter) +
             " SETS A CONDUIT TO CARRY ONLY WHAT YOU HOLD.", 1},
        {"9. GRINDER > CAULDRON > INFUSER > ALEMBIC > DISTILLER > TRANSMUTER", 1},
        {"10. NO FLYING: BUILD SCAFFOLD. FALL OFF THE EDGE AND YOUR PACK IS LOST.", 2},
        {"", 1},
        {"CONTROLS", 0},
        {move + " MOVE   " + k(Action::Jump) + " JUMP   " + k(Action::Sprint) + " SPRINT", 1},
        {"LMB MINE   RMB PLACE   1-0 OR WHEEL SELECT", 1},
        {k(Action::Inventory) + " INVENTORY: DRAG ITEMS ONTO THE HOTBAR TO ASSIGN THEM", 1},
        {"RMB WITH DRAUGHT > DRINK ( HEAL )   HARD FALLS HURT   " +
             k(Action::Screenshot) + " SCREENSHOT", 1},
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
