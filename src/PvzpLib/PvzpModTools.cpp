#include "PvzpModTools.h"

#include "../GameConstants.h"
#include "../Lawn/Board.h"
#include "../Lawn/CursorObject.h"
#include "../Lawn/Plant.h"
#include "../Lawn/Widget/GameButton.h"
#include "../LawnApp.h"
#include "../Resources.h"
#include "Attachment.h"
#include "EffectSystem.h"
#include "PvzpCommon.h"
#include "PvzpParticle.h"
#include "Reanimator.h"
#include "widget/WidgetManager.h"
#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace PvzpModTools
{
namespace
{
constexpr int ToolSize = 42;
struct State
{
    bool active = false;
    bool gesture = false;
    uint32_t plant = 0;
    int sourceCol = 0, sourceRow = 0, sourceX = 0, sourceY = 0;
    int x = 0, y = 0;
    const char* message = nullptr;
};
std::unordered_map<Board*, State> states;

bool Available(Board* b)
{
    return b && b->mApp && b->mApp->mGameScene == GameScenes::SCENE_PLAYING &&
        !b->mPaused && b->mTimeStopCounter == 0 && b->mBoardFadeOutCounter < 0 &&
        b->mApp->GetDialogCount() == 0 &&
        b->mApp->mGameMode != GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN &&
        b->mApp->mGameMode != GameMode::GAMEMODE_TREE_OF_WISDOM;
}

SeedType EffectiveType(Plant* p)
{
    return p->mSeedType == SeedType::SEED_IMITATER && p->mImitaterType != SeedType::SEED_NONE ?
        p->mImitaterType : p->mSeedType;
}

// PixelToGrid clamps right/bottom outside positions to the last cell. Tools
// must instead reject them, including roof pixels above the sloped first row.
bool CellAt(Board* b, int x, int y, int& col, int& row)
{
    if (x < LAWN_XMIN || x >= LAWN_XMIN + MAX_GRID_SIZE_X * 80)
        return false;
    col = (x - LAWN_XMIN) / 80;
    const int slope = b->StageHasRoof() ? std::max(4 - col, 0) * 20 : 0;
    const int height = b->StageHasRoof() || b->StageHasPool() ? 85 : 100;
    const int localY = y - LAWN_YMIN - slope;
    const int rows = b->StageHas6Rows() ? 6 : 5;
    if (localY < 0 || localY >= rows * height)
        return false;
    row = localY / height;
    return true;
}

bool Footprint(Board* b, Plant* p, int col, int row)
{
    const int width = EffectiveType(p) == SeedType::SEED_COBCANNON ? 2 : 1;
    return col >= 0 && col <= MAX_GRID_SIZE_X - width && row >= 0 &&
        row < (b->StageHas6Rows() ? 6 : 5);
}

bool Occupies(Plant* p, int col, int row)
{
    return !p->mDead && p->mRow == row && col >= p->mPlantCol &&
        col < p->mPlantCol + (EffectiveType(p) == SeedType::SEED_COBCANNON ? 2 : 1);
}

const char* Refusal(Board* b, Plant* p)
{
    if (!p || p->mBoard != b || !p->mIsOnBoard || p->mDead || p->mSquished || p->NotOnGround())
        return "PLANT NOT ON GROUND";
    if (p->mOnBungeeState != PlantOnBungeeState::NOT_ON_BUNGEE)
        return "BUNGEE: WAIT";
    if (b->mApp->IsWallnutBowlingLevel())
        return "BOWLING: UNAVAILABLE";
    if (p->mState == PlantState::STATE_DOINGSPECIAL || p->mState == PlantState::STATE_IMITATER_MORPHING)
        return "SPECIAL ANIMATION: WAIT";
    if (p->mSeedType == SeedType::SEED_GRAVEBUSTER)
        return "GRAVE-BOUND PLANT";
    if (p->mSeedType == SeedType::SEED_SQUASH && p->mState != PlantState::STATE_NOTREADY)
        return "SQUASH ATTACK: WAIT";
    if (p->mState >= PlantState::STATE_CHOMPER_BITING && p->mState <= PlantState::STATE_CHOMPER_SWALLOWING)
        return "CHOMPER EATING: WAIT";
    if (p->mState == PlantState::STATE_TANGLEKELP_GRABBING || p->mState == PlantState::STATE_COBCANNON_FIRING)
        return "ATTACK IN PROGRESS: WAIT";
    if (p->mState == PlantState::STATE_FLOWERPOT_INVULNERABLE || p->mState == PlantState::STATE_LILYPAD_INVULNERABLE)
        return "SUPPORT SETTLING: WAIT";
    const SeedType type = EffectiveType(p);
    if (type == SeedType::SEED_LILYPAD || type == SeedType::SEED_FLOWERPOT)
    {
        // Layers are independent plants, not attachments. Never pull a support
        // from under an occupant; normal plants leave their old support intact.
        for (Plant* other : b->mPlants)
            if (other != p && Occupies(other, p->mPlantCol, p->mRow) && !other->NotOnGround())
                return "SUPPORT OCCUPIED";
    }
    return nullptr;
}

Plant* Pick(Board* b, int x, int y)
{
    int col, row;
    if (!CellAt(b, x, y, col, row))
        return nullptr;
    Plant* best = nullptr;
    uint32_t bestID = 0;
    // Do not call GetTopPlantAt: legacy layer aggregation asserts on debug
    // stacks. A stable ID tie-break also makes equal render-order stacks usable.
    for (Plant* p : b->mPlants)
    {
        if (!Occupies(p, col, row) || p->NotOnGround())
            continue;
        const uint32_t id = b->mPlants.DataArrayGetID(p);
        if (!best || p->mRenderOrder > best->mRenderOrder ||
            (p->mRenderOrder == best->mRenderOrder && id > bestID))
        {
            best = p;
            bestID = id;
        }
    }
    return best;
}

bool OtherUI(Board* b, int x, int y)
{
    const auto contains = [x, y](GameButton* button)
    {
        return button && !button->mBtnNoDraw &&
            Sexy::Rect(button->mX, button->mY, button->mWidth, button->mHeight).Contains(x, y);
    };
    return GetFoodRect(b).Contains(x, y) || GetDebugRect(b).Contains(x, y) ||
        b->GetShovelButtonRect().Contains(x, y) || contains(b->mMenuButton.get()) ||
        contains(b->mStoreButton.get()) || y < LAWN_YMIN;
}

void SyncPotIdle(Board* b, int col, int row)
{
    bool occupied = false;
    for (Plant* other : b->mPlants)
        if (Occupies(other, col, row) && !other->NotOnGround() &&
            EffectiveType(other) != SeedType::SEED_FLOWERPOT)
            occupied = true;
    for (Plant* pot : b->mPlants)
        if (Occupies(pot, col, row) && EffectiveType(pot) == SeedType::SEED_FLOWERPOT)
            if (Reanimation* anim = b->mApp->ReanimationTryToGet(pot->mBodyReanimID))
            {
                if (occupied)
                    anim->mAnimRate = 0.0f;
                else if (anim->mAnimRate == 0.0f)
                    anim->mAnimRate = 12.0f; // resume empty pot, without RNG side effects
            }
}

// Recompute child transforms without advancing animation time or firing timed
// events. AttachmentUpdateAndSetMatrix explicitly calls Update, so avoid it.
void RefreshReanimChildren(Reanimation* anim, int depth);
void RefreshAttachmentChildren(Attachment* attachment, int depth)
{
    if (!attachment || depth > 16 || !gEffectSystem)
        return;
    for (int i = 0; i < attachment->mNumEffects; ++i)
    {
        const AttachEffect& effect = attachment->mEffectArray[i];
        if (effect.mEffectType == EffectType::EFFECT_REANIM)
            RefreshReanimChildren(gEffectSystem->mReanimationHolder->mReanimations.DataArrayTryToGet(effect.mEffectID), depth + 1);
        else if (effect.mEffectType == EffectType::EFFECT_ATTACHMENT)
            RefreshAttachmentChildren(gEffectSystem->mAttachmentHolder->mAttachments.DataArrayTryToGet(effect.mEffectID), depth + 1);
    }
}
void RefreshReanimChildren(Reanimation* anim, int depth)
{
    if (!anim || !anim->mDefinition || !anim->mTrackInstances || !gEffectSystem || depth > 16)
        return;
    for (int i = 0; i < anim->mDefinition->mTracks.count; ++i)
    {
        Attachment* attachment = gEffectSystem->mAttachmentHolder->mAttachments.DataArrayTryToGet(
            static_cast<unsigned int>(anim->mTrackInstances[i].mAttachmentID));
        if (!attachment)
            continue;
        SexyTransform2D matrix;
        anim->GetAttachmentOverlayMatrix(i, matrix);
        attachment->SetMatrix(matrix);
        RefreshAttachmentChildren(attachment, depth + 1);
    }
}
}

void Reset(Board* board) { states.erase(board); }
void Cancel(Board* board)
{
    auto it = states.find(board);
    if (it == states.end())
        return;
    it->second.active = false;
    it->second.plant = 0;
    it->second.message = nullptr;
    // Preserve gesture ownership until Up, including cancellation by pause/UI.
}
bool IsActive(Board* board)
{
    auto it = states.find(board);
    return it != states.end() && it->second.active;
}

Sexy::Rect GetGloveRect(Board* b)
{
    const Sexy::Rect shovel = b->GetShovelButtonRect();
    int x = shovel.mX + shovel.mWidth + 4;
    int y = shovel.mY + 4;
    if (x + ToolSize * 2 > 680)
    {
        x = std::clamp(shovel.mX, 0, 680 - ToolSize * 2);
        y = shovel.mY + shovel.mHeight + 4;
    }
    return Sexy::Rect(x, y, ToolSize, ToolSize);
}
Sexy::Rect GetFoodRect(Board* b)
{
    Sexy::Rect rect = GetGloveRect(b);
    rect.mX += ToolSize;
    return rect;
}
Sexy::Rect GetDebugRect(Board* b)
{
    const int bottom = b->mMenuButton ? b->mMenuButton->mY + b->mMenuButton->mHeight : 36;
    return Sexy::Rect(700, std::max(50, bottom + 8), 90, 30);
}

bool MovePlant(Board* b, Plant* p, int col, int row)
{
    if (!Available(b) || Refusal(b, p) || !Footprint(b, p, col, row))
        return false;
    if (col == p->mPlantCol && row == p->mRow)
        return true;
    const int oldCol = p->mPlantCol, oldRow = p->mRow;
    const float oldHeight = PlantDrawHeightOffset(b, p, p->mSeedType, oldCol, oldRow);
    const int oldOrder = p->mRenderOrder;
    const int x = b->GridToPixelX(col, row), y = b->GridToPixelY(col, row);
    const int dx = x - p->mX, dy = y - p->mY;
    p->mPlantCol = col;
    p->mRow = row;
    p->mX = x;
    p->mY = y;
    p->mStartRow = row; // stationary squash's next jump must start in its new row
    p->mRenderOrder = p->CalcRenderOrder();
    const int orderDelta = p->mRenderOrder - oldOrder;
    const float heightDelta = PlantDrawHeightOffset(b, p, p->mSeedType, col, row) - oldHeight;
    // These are plant-local transforms: adding world dx/dy to body/head would
    // translate twice (GameObject::BeginDraw already supplies the world origin).
    Reanimation* body = b->mApp->ReanimationTryToGet(p->mBodyReanimID);
    if (body)
    {
        body->SetPosition(body->mOverlayMatrix.m02, body->mOverlayMatrix.m12 + heightDelta);
        RefreshReanimChildren(body, 0);
    }
    const ReanimationID ids[] = {p->mBodyReanimID, p->mHeadReanimID, p->mHeadReanimID2,
        p->mHeadReanimID3, p->mBlinkReanimID, p->mLightReanimID, p->mSleepingReanimID};
    for (ReanimationID id : ids)
        if (Reanimation* anim = b->mApp->ReanimationTryToGet(id))
            anim->mRenderOrder += orderDelta;
    // Sleep Z is a separate world-space animation, unlike attached heads/glow.
    if (Reanimation* sleep = b->mApp->ReanimationTryToGet(p->mSleepingReanimID))
        sleep->SetPosition(sleep->mOverlayMatrix.m02 + dx, sleep->mOverlayMatrix.m12 + dy);
    if (PvzpParticleSystem* particle = b->mApp->ParticleTryToGet(p->mParticleID))
    {
        if (particle->mEffectType == ParticleEffect::PARTICLE_FUMECLOUD ||
            particle->mEffectType == ParticleEffect::PARTICLE_GLOOMCLOUD)
            particle->mRenderOrder = Board::MakeRenderOrder(RenderLayer::RENDER_LAYER_PARTICLE, row, 0);
        else if (particle->mEffectType != ParticleEffect::PARTICLE_LANTERN_SHINE)
            particle->mRenderOrder += orderDelta;
        // Lantern shine lives above fog, independent of the plant's row.
        // Move emitters individually: SystemMove on the whole system would
        // collapse distinct emitter centers onto the first emitter's center.
        for (auto* node = particle->mEmitterList.mHead; node; node = node->mNext)
        {
            PvzpParticleEmitter* emitter = particle->mParticleHolder->mEmitters.DataArrayTryToGet(
                static_cast<unsigned int>(node->mValue));
            if (emitter)
                emitter->SystemMove(emitter->mSystemCenter.x + dx, emitter->mSystemCenter.y + dy);
        }
    }
    for (MagnetItem& item : p->mMagnetItems)
        if (item.mItemType != MagnetItemType::MAGNET_ITEM_NONE)
        {
            item.mPosX += dx;
            item.mPosY += dy;
        }
    SyncPotIdle(b, oldCol, oldRow);
    SyncPotIdle(b, col, row);
    // Health, state/countdowns, targets, animation IDs/time, boost/fan ID maps
    // and existing projectiles are deliberately unchanged. No Die/AddPlant.
    b->MarkDirty();
    return true;
}

bool MouseDown(Board* b, int x, int y)
{
    if (!Available(b))
    {
        Cancel(b);
        return false;
    }
    State& s = states[b];
    if (GetGloveRect(b).Contains(x, y))
    {
        s.active = !s.active;
        s.gesture = true;
        s.plant = 0;
        s.message = nullptr;
        b->ClearCursor();
        return true;
    }
    if (!s.active)
        return false;
    if (OtherUI(b, x, y))
    {
        Cancel(b);
        return false;
    }
    s.gesture = true;
    s.x = x;
    s.y = y;
    s.plant = 0;
    Plant* p = Pick(b, x, y);
    s.message = Refusal(b, p);
    if (!s.message)
    {
        s.plant = b->mPlants.DataArrayGetID(p);
        s.sourceCol = p->mPlantCol;
        s.sourceRow = p->mRow;
        s.sourceX = p->mX;
        s.sourceY = p->mY;
    }
    return true;
}
bool MouseDrag(Board* b, int x, int y)
{
    auto it = states.find(b);
    if (it == states.end() || !it->second.gesture)
        return false;
    it->second.x = x;
    it->second.y = y;
    if (!Available(b))
        Cancel(b);
    return true;
}
bool MouseUp(Board* b, int x, int y)
{
    auto it = states.find(b);
    if (it == states.end() || !it->second.gesture)
        return false;
    State& s = it->second;
    s.gesture = false;
    Plant* p = s.plant ? b->mPlants.DataArrayTryToGet(s.plant) : nullptr;
    s.plant = 0;
    if (!Available(b))
    {
        Cancel(b);
        return true;
    }
    if (!p)
        return true;
    int col, row;
    if (OtherUI(b, x, y) || GetGloveRect(b).Contains(x, y) || !CellAt(b, x, y, col, row))
    {
        s.message = "OUTSIDE BOARD: CANCELLED";
        return true;
    }
    if (p->mPlantCol != s.sourceCol || p->mRow != s.sourceRow || p->mX != s.sourceX || p->mY != s.sourceY)
    {
        s.message = "PLANT MOVED: CANCELLED";
        return true;
    }
    s.message = Refusal(b, p);
    if (!s.message && !MovePlant(b, p, col, row))
        s.message = "INVALID DESTINATION: CANCELLED";
    return true;
}

void Draw(Board* b, Sexy::Graphics* graphics)
{
    if (!Available(b))
    {
        Cancel(b);
        return;
    }
    Sexy::Graphics g(*graphics);
    const Sexy::Rect glove = GetGloveRect(b);
    const bool active = IsActive(b);
    g.SetColor(active ? Sexy::Color(80, 165, 210, 235) : Sexy::Color(35, 40, 55, 225));
    g.FillRect(glove);
    g.SetColor(Sexy::Color(180, 220, 245));
    g.DrawRect(glove.mX, glove.mY, glove.mWidth - 1, glove.mHeight - 1);
    if (Sexy::IMAGE_ZEN_GARDENGLOVE)
        g.DrawImage(Sexy::IMAGE_ZEN_GARDENGLOVE, glove.mX + 3, glove.mY + 3, 36, 36);
    else
        PvzpDrawString(&g, "MOVE", glove.mX + ToolSize / 2, glove.mY + 27,
            Sexy::FONT_DWARVENTODCRAFT12, Sexy::Color(220, 240, 255), DrawStringJustification::DS_ALIGN_CENTER);
    auto it = states.find(b);
    if (it == states.end() || !active)
        return;
    State& s = it->second;
    const char* message = s.message ? s.message : "GLOVE: HOLD PLANT, DRAG, RELEASE";
    PvzpDrawString(&g, message, 400, 96, Sexy::FONT_DWARVENTODCRAFT12,
        Sexy::Color(180, 235, 255), DrawStringJustification::DS_ALIGN_CENTER);
    Plant* p = s.plant ? b->mPlants.DataArrayTryToGet(s.plant) : nullptr;
    if (!p || p->mDead)
        return;
    int col, row;
    if (!OtherUI(b, s.x, s.y) && !GetGloveRect(b).Contains(s.x, s.y) &&
        CellAt(b, s.x, s.y, col, row) && Footprint(b, p, col, row))
    {
        g.SetColor(Sexy::Color(100, 255, 150));
        g.DrawRect(b->GridToPixelX(col, row), b->GridToPixelY(col, row),
            EffectiveType(p) == SeedType::SEED_COBCANNON ? 159 : 79, 79);
    }
    else
    {
        g.SetColor(Sexy::Color(255, 90, 90));
        g.DrawRect(s.x - 18, s.y - 18, 36, 36);
    }
}
}
