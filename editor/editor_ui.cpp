/*
 * editor_ui.cpp  —  Dear ImGui level editor for Reckless Drivin'
 *
 * Entry point: EditorEnter(levelID) called from the main menu (press E).
 * Uses the existing SDL2 window and OpenGL context.
 *
 * Editable data:
 *   - Road geometry   (tRoadSeg[4] per segment)
 *   - Level metadata  (tLevelData: time, xStartPos)
 *   - Spawn groups    (tLevelData.objGrps[10].numObjs)
 *   - AI waypoints    (tTrackInfoSeg arrays for up/down lanes)
 *
 * Save format: saves/level_{id}_editor.bin  (see plan for layout)
 */

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl2.h"

#include <SDL.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef min
#undef max
#undef DrawText
#undef LoadImage
#endif
#include <GL/gl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <algorithm>
#include <vector>

extern "C" {
#include "roads.h"
#include "gameinitexit.h"
#include "packs.h"
#include "mac_compat.h"
#include "editor.h"

extern int gExit;
void EditorEnsureSavesDir(void);
}

/* mac_compat.h defines Button() as a zero-arg macro — kill it before ImGui */
#ifdef Button
#undef Button
#endif

/* =========================================================================
 * Editor state
 * ========================================================================= */

static int    sLevelID  = 0;

/* Working copies (all in native byte order) */
static SInt16 (*sRoad)[4]      = NULL;
static UInt32   sRoadLen       = 0;
static tLevelData sLevelMeta;
static tTrackInfoSeg *sTrackUp   = NULL;
static UInt32        sNumTrackUp = 0;
static tTrackInfoSeg *sTrackDown  = NULL;
static UInt32        sNumTrackDown= 0;

static bool   sDirty   = false;
static char   sSaveMsg[80] = "";
static float  sSaveMsgTimer = 0.0f;

/* Road canvas view
 * Aspect ratio: both axes use the same world-unit scale.
 *   pixelsPerUnit = sZoomY / 2    (segment = 2 world-Y units)
 *   MapX(wx)  = (wx - sScrollX) * pixelsPerUnit + canvasW/2
 *   SegToCanvasY(seg) = canvasH - (seg - sScrollY) * sZoomY
 */
static float  sScrollY      = 0.0f;  /* segment index at bottom of canvas */
static float  sScrollX      = 0.0f;  /* world X at horizontal canvas centre */
static float  sZoomY        = 2.0f;  /* pixels per segment (drives both axes) */
static float  sNeedFitWidth = 0.0f;  /* >0: auto-fit zoom to this half-width on next frame */

/* Selection */
static int    sSelSeg   = 0;      /* primary selected segment */
static int    sSel0     = -1;     /* range start (or -1 = no range) */
static int    sSel1     = -1;     /* range end */

/* Road tools */
enum RoadTool { kToolInspect = 0, kToolPaint, kToolShift };
static RoadTool sTool       = kToolInspect;
static float    sPaintWidth   = 320.0f;
static float    sPaintGapW    = 0.0f;    /* gap between strip A and strip B */
static float    sPaintCenterX = 0.0f;    /* used only for batch apply */
static float    sShiftX       = 0.0f;

/* =========================================================================
 * Track pieces
 * ========================================================================= */

enum PieceCurve { kPieceStraight = 0, kPieceCurveSmooth, kPieceCurveSineLR, kPieceCurveSineRL };

struct tTrackPieceDef {
    const char *name;
    const char *desc;
    int         numSegs;
    float       defWidth;
    float       defGap;
    float       param;      /* curveDX for smooth; amplitude for sine */
    PieceCurve  curve;
};

static const tTrackPieceDef kPieces[] = {
    { "Straight",    "Flat, no curve",          50,  300,   0,    0, kPieceStraight   },
    { "Long Str.",   "Flat, no curve",          150, 300,   0,    0, kPieceStraight   },
    { "Curve L",     "Gentle left",             80,  300,   0, -150, kPieceCurveSmooth },
    { "Curve R",     "Gentle right",            80,  300,   0, +150, kPieceCurveSmooth },
    { "Sharp L",     "Sharp left",              60,  300,   0, -300, kPieceCurveSmooth },
    { "Sharp R",     "Sharp right",             60,  300,   0, +300, kPieceCurveSmooth },
    { "Hairpin L",   "U-turn left",             100, 300,   0, -600, kPieceCurveSmooth },
    { "Hairpin R",   "U-turn right",            100, 300,   0, +600, kPieceCurveSmooth },
    { "Wide",        "Wide straight",           50,  500,   0,    0, kPieceStraight   },
    { "Narrow",      "Narrow straight",         50,  150,   0,    0, kPieceStraight   },
    { "Dual Lane",   "Two strips + gap",        50,  400, 120,    0, kPieceStraight   },
    { "Chicane L-R", "Swings left then right",  100, 300,   0,  200, kPieceCurveSineLR },
    { "Chicane R-L", "Swings right then left",  100, 300,   0,  200, kPieceCurveSineRL },
    { "Widen",       "Gradually widens",        80,    0,   0,  200, kPieceStraight   }, /* special */
    { "Narrow In",   "Gradually narrows",       80,    0,   0, -200, kPieceStraight   }, /* special */
};
#define kNumPieces ((int)(sizeof(kPieces) / sizeof(kPieces[0])))

static int   sAILineStep        = 8;    /* one waypoint every N segments */

/* =========================================================================
 * Fixed world objects (houses, gas stations, toll booths, etc.)
 * ========================================================================= */

static tObjectPos     *sObjs    = NULL;
static UInt32          sNumObjs = 0;
static int             sSelObj  = -1;   /* -1 = none selected */

enum ObjTool { kObjSelect = 0, kObjPlace };
static ObjTool  sObjTool      = kObjSelect;
static SInt16   sPlaceTypeRes = 1;
static float    sPlaceDir     = 0.0f;
static bool     sObjDragging  = false;
static int      sObjDragIdx   = -1;

/* Object type catalog — populated once from kPackObTy */
static bool              sObjTypesLoaded = false;
static std::vector<int>  sObjTypeIDs;

static void CollectObjTypeID(Ptr /*data*/, int /*size*/, int id, void *ctx)
{
    ((std::vector<int>*)ctx)->push_back(id);
}

static void EnsureObjTypesLoaded(void)
{
    if (sObjTypesLoaded) return;
    sObjTypesLoaded = true;
    ForEachPackEntry(kPackObTy, CollectObjTypeID, &sObjTypeIDs);
    std::sort(sObjTypeIDs.begin(), sObjTypeIDs.end());
    fprintf(stderr, "[Editor] Found %d object types in kPackObTy\n", (int)sObjTypeIDs.size());
}

/* Consistent color per typeRes for canvas markers */
static ImU32 ObjTypeColor(SInt16 typeRes, bool selected)
{
    if (selected) return IM_COL32(255, 230, 0, 255);
    int t = (int)(unsigned short)typeRes;
    int r = ((t * 37 + 80)  & 0xFF) | 60;
    int g = ((t * 71 + 150) & 0xFF) | 60;
    int b = ((t * 113+ 200) & 0xFF) | 60;
    return IM_COL32(r, g, b, 230);
}

static int   sSelectedPiece     = -1;
static int   sLastSelectedPiece = -2;   /* detect piece change → auto-init sliders */
static int   sPieceLength       = 50;
static float sPieceCurve        = 0.0f; /* curve param OR width delta for Widen/Narrow */
static float sPieceWidth        = 300.0f;
static float sPieceGap          = 0.0f;

/* =========================================================================
 * Save / Load helpers
 * ========================================================================= */

#define EDITOR_SAVE_MAGIC   0x52444C56u  /* 'RDLV' */
#define EDITOR_SAVE_VERSION 2u

static void SaveLevel(void)
{
    char path[256];
    FILE *f;
    UInt32 magic   = EDITOR_SAVE_MAGIC;
    UInt32 version = EDITOR_SAVE_VERSION;

    EditorEnsureSavesDir();
    snprintf(path, sizeof(path), "saves/level_%d_editor.bin", sLevelID);
    f = fopen(path, "wb");
    if (!f) {
        snprintf(sSaveMsg, sizeof(sSaveMsg), "ERROR: cannot write %s", path);
        sSaveMsgTimer = 4.0f;
        return;
    }

    fwrite(&magic,   4, 1, f);
    fwrite(&version, 4, 1, f);

    /* 1. Level metadata */
    fwrite(&sLevelMeta, sizeof(tLevelData), 1, f);

    /* 2. Road segments */
    fwrite(&sRoadLen, 4, 1, f);
    fwrite(sRoad, sizeof(SInt16) * 4, sRoadLen, f);

    /* 3. Track Up waypoints */
    fwrite(&sNumTrackUp, 4, 1, f);
    if (sNumTrackUp && sTrackUp)
        fwrite(sTrackUp, sizeof(tTrackInfoSeg), sNumTrackUp, f);

    /* 4. Track Down waypoints */
    fwrite(&sNumTrackDown, 4, 1, f);
    if (sNumTrackDown && sTrackDown)
        fwrite(sTrackDown, sizeof(tTrackInfoSeg), sNumTrackDown, f);

    /* 5. Fixed world objects (v2) */
    fwrite(&sNumObjs, 4, 1, f);
    if (sNumObjs && sObjs)
        fwrite(sObjs, sizeof(tObjectPos), sNumObjs, f);

    fclose(f);
    sDirty = false;
    snprintf(sSaveMsg, sizeof(sSaveMsg), "Saved to %s", path);
    sSaveMsgTimer = 3.0f;
    fprintf(stderr, "[Editor] Saved level %d to %s\n", sLevelID, path);
}

static void LoadEditorData(int levelID)
{
    UInt32 roadLen = 0;
    UInt32 i;

    /* Free previous working copies */
    if (sRoad)      { free(sRoad);      sRoad = NULL; }
    if (sTrackUp)   { free(sTrackUp);   sTrackUp = NULL; }
    if (sTrackDown) { free(sTrackDown); sTrackDown = NULL; }
    if (sObjs)      { free(sObjs);      sObjs = NULL; sNumObjs = 0; sSelObj = -1; }

    sLevelID = levelID;
    sRoad    = (SInt16(*)[4])EditorLoadRoadData(levelID, &roadLen, &sObjs, &sNumObjs);
    sRoadLen = roadLen;

    if (!sRoad) {
        fprintf(stderr, "[Editor] EditorLoadRoadData failed for level %d\n", levelID);
        return;
    }

    /* Copy level metadata */
    sLevelMeta = *gLevelData;

    /* Copy track waypoints */
    if (gTrackUp && gTrackUp->num > 0) {
        sNumTrackUp = gTrackUp->num;
        sTrackUp = (tTrackInfoSeg*)malloc(sNumTrackUp * sizeof(tTrackInfoSeg));
        if (sTrackUp)
            for (i = 0; i < sNumTrackUp; i++) sTrackUp[i] = gTrackUp->track[i];
    }
    if (gTrackDown && gTrackDown->num > 0) {
        sNumTrackDown = gTrackDown->num;
        sTrackDown = (tTrackInfoSeg*)malloc(sNumTrackDown * sizeof(tTrackInfoSeg));
        if (sTrackDown)
            for (i = 0; i < sNumTrackDown; i++) sTrackDown[i] = gTrackDown->track[i];
    }

    /* Auto-fit: compute road + object X extents so we can centre and zoom
     * to show all content.  We store the half-width so the canvas can pick
     * the right sZoomY on its first frame (when it knows its pixel width). */
    if (sRoad && sRoadLen > 0) {
        float xMinF = (float)sRoad[0][0], xMaxF = (float)sRoad[0][3];
        UInt32 k;
        for (k = 0; k < sRoadLen; k++) {
            if ((float)sRoad[k][0] < xMinF) xMinF = (float)sRoad[k][0];
            if ((float)sRoad[k][3] > xMaxF) xMaxF = (float)sRoad[k][3];
        }
        for (k = 0; k < sNumObjs; k++) {
            if ((float)sObjs[k].x < xMinF) xMinF = (float)sObjs[k].x;
            if ((float)sObjs[k].x > xMaxF) xMaxF = (float)sObjs[k].x;
        }
        sScrollX = (xMinF + xMaxF) * 0.5f;
        /* sNeedFitWidth stores the half-width of data; canvas will compute
         * the right sZoomY on the first frame to fit it with 10 % padding. */
        sNeedFitWidth = (xMaxF - xMinF) * 0.5f * 1.1f;
        if (sNeedFitWidth < 1.0f) sNeedFitWidth = 1.0f;
    }

    /* Scroll to player spawn point: game always sets pos.y=500 (world units).
     * Road segments map approximately as: world_y = seg_index * 2.
     * So spawn is near segment 250.
     *
     * With flipped Y, sScrollY = bottom segment of canvas.
     * At zoom=2.0 and ~400px canvas height, ~200 segments are visible.
     * Set sScrollY so that spawn appears near the top of the canvas:
     *   top_seg ≈ sScrollY + 200, want top_seg ≈ spawn + 15
     *   → sScrollY ≈ spawn - 185 */
    {
        float spawnSeg = 500.0f / 2.0f;
        sScrollY = spawnSeg - 185.0f;
        if (sScrollY < 0.0f) sScrollY = 0.0f;
        sSelSeg = (int)spawnSeg;
    }

    sZoomY   = 2.0f;
    sSel0 = sSel1 = -1;
    sDirty = false;
    sSaveMsgTimer = 0.0f;

    /* EditorLoadRoadData byte-swapped the pack data in-place.
     * Unload the pack now so that LoadLevel always gets a fresh big-endian
     * buffer and won't double-swap the data into garbage when the game starts. */
    EditorUnloadLevelPack(levelID);

    fprintf(stderr, "[Editor] Level %d loaded: %u road segs, %u/%u waypoints\n",
            levelID, sRoadLen, sNumTrackUp, sNumTrackDown);
}

/* =========================================================================
 * AI line regeneration
 * ========================================================================= */

/* Rebuild TrackUp and TrackDown from the current road geometry.
 *
 * One waypoint is placed every sAILineStep segments.
 * TrackDown (cars racing toward the finish, decreasing Y):
 *   → follows strip B centre  (right lane in driving convention)
 * TrackUp (oncoming / returning AI, increasing Y):
 *   → follows strip A centre  (left lane)
 *
 * When there is no gap (strip A and B share the same edge) the two
 * centre X values naturally land on the left-half and right-half of
 * the single road strip, which is still a sensible starting point.
 *
 * Velocity is inherited from the old arrays (average), or left at 0
 * so the user can fill it in via the Waypoints table.
 */
static void RegenerateAILines(void)
{
    if (!sRoad || sRoadLen == 0) return;

    /* Preserve average velocity from existing data */
    float avgUp = 0.0f, avgDown = 0.0f;
    if (sNumTrackUp > 0 && sTrackUp) {
        for (UInt32 i = 0; i < sNumTrackUp; i++) avgUp += sTrackUp[i].velo;
        avgUp /= (float)sNumTrackUp;
    }
    if (sNumTrackDown > 0 && sTrackDown) {
        for (UInt32 i = 0; i < sNumTrackDown; i++) avgDown += sTrackDown[i].velo;
        avgDown /= (float)sNumTrackDown;
    }

    /* Free old arrays */
    if (sTrackUp)   { free(sTrackUp);   sTrackUp   = NULL; sNumTrackUp   = 0; }
    if (sTrackDown) { free(sTrackDown); sTrackDown  = NULL; sNumTrackDown = 0; }

    int step = sAILineStep > 0 ? sAILineStep : 1;
    UInt32 numWpts = ((UInt32)sRoadLen + (UInt32)step - 1) / (UInt32)step;
    if (numWpts == 0) return;

    sTrackUp   = (tTrackInfoSeg*)calloc(numWpts, sizeof(tTrackInfoSeg));
    sTrackDown = (tTrackInfoSeg*)calloc(numWpts, sizeof(tTrackInfoSeg));
    if (!sTrackUp || !sTrackDown) {
        free(sTrackUp);   sTrackUp   = NULL;
        free(sTrackDown); sTrackDown = NULL;
        return;
    }
    sNumTrackUp = sNumTrackDown = numWpts;

    for (UInt32 wi = 0; wi < numWpts; wi++) {
        int seg = (int)wi * step;
        if (seg >= (int)sRoadLen) seg = (int)sRoadLen - 1;

        /* Strip A centre (left lane) and strip B centre (right lane) */
        float stripACx = ((float)sRoad[seg][0] + (float)sRoad[seg][1]) * 0.5f;
        float stripBCx = ((float)sRoad[seg][2] + (float)sRoad[seg][3]) * 0.5f;
        SInt32 worldY  = (SInt32)(seg * 2);

        sTrackUp[wi].flags = 0;
        sTrackUp[wi].x     = (SInt16)stripACx;
        sTrackUp[wi].y     = worldY;
        sTrackUp[wi].velo  = avgUp;

        sTrackDown[wi].flags = 0;
        sTrackDown[wi].x     = (SInt16)stripBCx;
        sTrackDown[wi].y     = worldY;
        sTrackDown[wi].velo  = avgDown;
    }

    sDirty = true;
    fprintf(stderr, "[Editor] Regenerated AI lines: %u waypoints, step=%d\n",
            numWpts, step);
}

/* =========================================================================
 * Road canvas drawing helpers
 * ========================================================================= */

/* Both axes share the same pixels-per-world-unit scale:
 *   ppu = sZoomY / 2  (one segment spans 2 world-Y units)
 * X is centred at sScrollX; Y origin is sScrollY (bottom segment). */
static float MapX(float worldX, float canvasW)
{
    return (worldX - sScrollX) * (sZoomY * 0.5f) + canvasW * 0.5f;
}

/* Y is flipped: segment 0 (the finish) is at the BOTTOM of the canvas.
 * Higher segment indices (the start / spawn area) are toward the TOP.
 * sScrollY = the segment index shown at the bottom of the canvas. */
static float CanvasXToWorldX(float px, float canvasW)
{
    return sScrollX + (px - canvasW * 0.5f) / (sZoomY * 0.5f);
}

static int CanvasYToSeg(float py, float canvasH)
{
    return (int)(sScrollY + (canvasH - py) / sZoomY);
}

static float SegToCanvasY(int seg, float canvasH)
{
    return canvasH - ((float)seg - sScrollY) * sZoomY;
}

static float GetSegCenterX(int seg)
{
    if (!sRoad || seg < 0 || (UInt32)seg >= sRoadLen) return 0.0f;
    return ((float)sRoad[seg][0] + (float)sRoad[seg][3]) * 0.5f;
}

static float GetSegWidth(int seg)
{
    if (!sRoad || seg < 0 || (UInt32)seg >= sRoadLen) return 300.0f;
    return (float)((int)sRoad[seg][3] - (int)sRoad[seg][0]);
}

/* Compute the road shape at step i inside a piece.
 * curveType     — lateral movement style (from kPieces[].curve)
 * isWidthXition — true for Widen/Narrow: width changes from startW by param, no lateral shift
 * numSegs       — total length of piece in segments (controls t interpolation)
 * startCX       — world X centre of the piece's first segment
 * startW        — road width at the start (only used when isWidthXition=true)
 * width         — constant road width (or final width target for width-transition pieces)
 * gap           — gap between strip A and strip B
 * param         — lateral displacement for curves, OR total width delta for Widen/Narrow */
static void ComputePieceSeg(PieceCurve curveType, bool isWidthXition,
                             int i, int numSegs, float startCX,
                             float startW, float width, float gap, float param,
                             SInt16 out[4])
{
    float t  = (numSegs > 1) ? (float)i / (float)(numSegs - 1) : 0.0f;
    float cx = startCX;
    float w  = width;

    if (isWidthXition) {
        /* Gradually widen or narrow — no lateral movement */
        w = startW + param * t;
        if (w < 20.0f) w = 20.0f;
    } else {
        switch (curveType) {
            case kPieceCurveSmooth: {
                /* Ease in/out: zero derivative at both ends → smooth joins */
                float eased = (1.0f - cosf(t * 3.14159265f)) * 0.5f;
                cx += param * eased;
                break;
            }
            case kPieceCurveSineLR:
                cx -= param * sinf(t * 3.14159265f);  /* left first */
                break;
            case kPieceCurveSineRL:
                cx += param * sinf(t * 3.14159265f);  /* right first */
                break;
            default:
                break;
        }
    }

    float hw = w * 0.5f;
    float gw = gap;
    out[0] = (SInt16)(cx - hw);
    out[1] = (SInt16)(cx - gw * 0.5f);
    out[2] = (SInt16)(cx + gw * 0.5f);
    out[3] = (SInt16)(cx + hw);
}

static void DrawRoadCanvas(ImDrawList *dl, ImVec2 pos, ImVec2 size)
{
    int fromSeg, toSeg, step, i;
    float segH;

    if (!sRoad || sRoadLen == 0) return;

    /* LOD: at low zoom show one band per pixel to cap draw calls */
    step = (sZoomY < 1.0f) ? (int)ceilf(1.0f / sZoomY) : 1;
    segH = sZoomY * (float)step;
    if (segH < 1.0f) segH = 1.0f;

    fromSeg = (int)sScrollY;
    if (fromSeg < 0) fromSeg = 0;
    fromSeg = (fromSeg / (step > 0 ? step : 1)) * (step > 0 ? step : 1);

    toSeg = fromSeg + (int)(size.y / segH) * step + step * 2;
    if (toSeg > (int)sRoadLen) toSeg = (int)sRoadLen;

    /* Grass background */
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                      IM_COL32(35, 65, 25, 255));

    for (i = fromSeg; i < toSeg; i += step) {
        /* With flipped Y: py is the BOTTOM edge of this band, py2 is the TOP */
        float py  = pos.y + SegToCanvasY(i,      size.y);
        float py2 = pos.y + SegToCanvasY(i + step, size.y);

        float le = pos.x + MapX((float)sRoad[i][0], size.x);
        float ll = pos.x + MapX((float)sRoad[i][1], size.x);
        float rl = pos.x + MapX((float)sRoad[i][2], size.x);
        float re = pos.x + MapX((float)sRoad[i][3], size.x);

        /* Strip A: [0]→[1] — road texture in-game */
        dl->AddRectFilled(ImVec2(le, py2), ImVec2(ll, py),
                          IM_COL32(100, 100, 100, 255));
        /* Gap: [1]→[2] — background texture in-game (grass/water).
         * The green canvas background already shows through here. */
        /* Strip B: [2]→[3] — road texture in-game */
        dl->AddRectFilled(ImVec2(rl, py2), ImVec2(re, py),
                          IM_COL32(100, 100, 100, 255));
        /* Centre marker in each strip at high zoom */
        if (segH >= 3.0f) {
            if (ll > le + 2.0f) {
                float cx = (le + ll) * 0.5f;
                dl->AddRectFilled(ImVec2(cx - 1.0f, py2), ImVec2(cx + 1.0f, py),
                                  IM_COL32(220, 200, 20, 255));
            }
            if (re > rl + 2.0f) {
                float cx = (rl + re) * 0.5f;
                dl->AddRectFilled(ImVec2(cx - 1.0f, py2), ImVec2(cx + 1.0f, py),
                                  IM_COL32(220, 200, 20, 255));
            }
        }
    }

    /* Selection range overlay */
    if (sSel0 >= 0 && sSel1 >= 0) {
        int s0 = sSel0 < sSel1 ? sSel0 : sSel1;  /* lower seg  → bottom of canvas */
        int s1 = sSel0 < sSel1 ? sSel1 : sSel0;  /* higher seg → top of canvas */
        float hy0 = pos.y + SegToCanvasY(s1, size.y);  /* top edge of selection */
        float hy1 = pos.y + SegToCanvasY(s0, size.y);  /* bottom edge */
        hy0 = hy0 < pos.y ? pos.y : hy0;
        hy1 = hy1 > pos.y + size.y ? pos.y + size.y : hy1;
        if (hy1 > hy0) {
            dl->AddRectFilled(ImVec2(pos.x, hy0), ImVec2(pos.x + size.x, hy1),
                              IM_COL32(255, 255, 0, 35));
            dl->AddRect(ImVec2(pos.x, hy0), ImVec2(pos.x + size.x, hy1),
                        IM_COL32(255, 255, 0, 180));
        }
    }

    /* Cursor line at selected segment */
    if (sSelSeg >= fromSeg && sSelSeg < toSeg) {
        float cy = pos.y + SegToCanvasY(sSelSeg, size.y);
        dl->AddLine(ImVec2(pos.x, cy), ImVec2(pos.x + size.x, cy),
                    IM_COL32(255, 80, 80, 230), 2.0f);
    }

    /* Track piece preview — drawn in cyan before committing */
    if (sSelectedPiece >= 0 && sRoad && sSelSeg >= 0 && (UInt32)sSelSeg < sRoadLen) {
        const tTrackPieceDef &p = kPieces[sSelectedPiece];
        bool  isWX    = (p.defWidth == 0.0f);
        float startCX = GetSegCenterX(sSelSeg);
        float startW  = GetSegWidth(sSelSeg);
        int endSeg = std::min(sSelSeg + sPieceLength, (int)sRoadLen);
        for (int pi = sSelSeg; pi < endSeg; pi += (step > 0 ? step : 1)) {
            SInt16 pv[4];
            ComputePieceSeg(p.curve, isWX, pi - sSelSeg, sPieceLength,
                            startCX, startW, sPieceWidth, sPieceGap, sPieceCurve, pv);
            float py  = pos.y + SegToCanvasY(pi, size.y);
            float py2 = pos.y + SegToCanvasY(pi + step, size.y);
            float ple  = pos.x + MapX((float)pv[0], size.x);
            float pll  = pos.x + MapX((float)pv[1], size.x);
            float prl  = pos.x + MapX((float)pv[2], size.x);
            float pre  = pos.x + MapX((float)pv[3], size.x);
            dl->AddRectFilled(ImVec2(ple, py2), ImVec2(pll, py), IM_COL32(0, 200, 220, 120));
            dl->AddRectFilled(ImVec2(prl, py2), ImVec2(pre, py), IM_COL32(0, 200, 220, 120));
        }
        /* Mark the end of the piece with a line */
        float ey = pos.y + SegToCanvasY(endSeg - 1, size.y);
        if (ey >= pos.y && ey <= pos.y + size.y)
            dl->AddLine(ImVec2(pos.x, ey), ImVec2(pos.x + size.x, ey),
                        IM_COL32(0, 220, 220, 220), 1.5f);
    }

    /* Spawn point indicator — player always starts at world y=500, x=xStartPos */
    {
        float spawnSeg = 500.0f / 2.0f;
        float sy = pos.y + SegToCanvasY((int)spawnSeg, size.y);
        if (sy >= pos.y - 1.0f && sy <= pos.y + size.y + 1.0f) {
            float sx = pos.x + MapX((float)sLevelMeta.xStartPos, size.x);
            /* Full-width spawn line */
            dl->AddLine(ImVec2(pos.x, sy), ImVec2(pos.x + size.x, sy),
                        IM_COL32(0, 220, 60, 160), 1.5f);
            /* Spawn marker at xStartPos */
            dl->AddCircleFilled(ImVec2(sx, sy), 6.0f, IM_COL32(0, 220, 60, 255));
            dl->AddCircle(ImVec2(sx, sy), 6.0f, IM_COL32(0, 0, 0, 200), 12, 1.5f);
            dl->AddText(ImVec2(sx + 9.0f, sy - 13.0f),
                        IM_COL32(0, 255, 80, 230), "SPAWN");
        }
    }

    /* Fixed world objects — colored circles with typeRes label */
    if (sObjs && sNumObjs > 0) {
        for (UInt32 oi = 0; oi < sNumObjs; oi++) {
            float ox = pos.x + MapX((float)sObjs[oi].x, size.x);
            float oy = pos.y + (size.y - ((float)sObjs[oi].y / 2.0f - sScrollY) * sZoomY);
            /* Cull objects well outside canvas */
            if (ox < pos.x - 24.0f || ox > pos.x + size.x + 24.0f) continue;
            if (oy < pos.y - 24.0f || oy > pos.y + size.y + 24.0f) continue;

            bool sel = ((int)oi == sSelObj);
            ImU32 col = ObjTypeColor(sObjs[oi].typeRes, sel);

            if (sel) {
                dl->AddCircle(ImVec2(ox, oy), 11.0f, IM_COL32(255, 230, 0, 255), 16, 2.5f);
            }
            dl->AddCircleFilled(ImVec2(ox, oy), sel ? 7.0f : 5.0f, col);
            dl->AddCircle(ImVec2(ox, oy), sel ? 7.0f : 5.0f, IM_COL32(0, 0, 0, 180), 8, 1.0f);

            /* Direction indicator */
            float ddx = cosf(sObjs[oi].dir) * 10.0f;
            float ddy = -sinf(sObjs[oi].dir) * 10.0f;
            dl->AddLine(ImVec2(ox, oy), ImVec2(ox + ddx, oy + ddy),
                        IM_COL32(255, 255, 255, 180), 1.5f);

            /* Type label (always for selected, only at higher zoom for others) */
            if (sel || sZoomY >= 1.5f) {
                char lbl[16];
                snprintf(lbl, sizeof(lbl), "t%d", (int)sObjs[oi].typeRes);
                dl->AddText(ImVec2(ox + 8.0f, oy - 14.0f),
                            sel ? IM_COL32(255, 230, 0, 255) : IM_COL32(220, 220, 220, 200),
                            lbl);
            }
        }
    }

    /* Canvas border */
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                IM_COL32(80, 80, 80, 255));
}

/* =========================================================================
 * Object panel (left sidebar)
 * ========================================================================= */

static void DrawObjPanel(void)
{
    EnsureObjTypesLoaded();

    ImGui::Text("Objects  (%u)", sNumObjs);
    ImGui::Separator();

    /* Tool selector */
    if (ImGui::RadioButton("Select##ot", sObjTool == kObjSelect)) sObjTool = kObjSelect;
    ImGui::SameLine();
    if (ImGui::RadioButton("Place##ot",  sObjTool == kObjPlace))  sObjTool = kObjPlace;

    ImGui::Spacing();

    if (sObjTool == kObjPlace) {
        /* ── Place mode: type picker ──────────────────────── */
        ImGui::Text("Place type (typeRes):");
        ImGui::SetNextItemWidth(-1.0f);
        {
            int tv = (int)sPlaceTypeRes;
            if (ImGui::InputInt("##placeType", &tv)) {
                if (tv < 1) tv = 1; if (tv > 32767) tv = 32767;
                sPlaceTypeRes = (SInt16)tv;
            }
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("Dir (rad)##pd", &sPlaceDir, 0.0f, 6.2832f, "%.2f");
        ImGui::TextDisabled("Click road view to place.");
        ImGui::Separator();
        ImGui::TextDisabled("Available types:");

        /* Two-column type grid */
        float bw = (ImGui::GetContentRegionAvail().x
                    - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (bw < 28.0f) bw = 28.0f;
        int col = 0;
        for (int id : sObjTypeIDs) {
            bool picked = (id == (int)sPlaceTypeRes);
            if (picked) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.55f, 0.15f, 1.0f));
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%d##t%d", id, id);
            if (col == 1) ImGui::SameLine();
            if (ImGui::Button(lbl, ImVec2(bw, 0)))
                sPlaceTypeRes = (SInt16)id;
            if (picked) ImGui::PopStyleColor();
            col = (col + 1) % 2;
        }
    } else {
        /* ── Select mode: selected object properties ─────── */
        if (sSelObj >= 0 && (UInt32)sSelObj < sNumObjs) {
            tObjectPos &o = sObjs[sSelObj];
            ImGui::Text("Object %d", sSelObj);
            ImGui::Separator();

            {
                int tv = (int)o.typeRes;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputInt("TypeRes##st", &tv)) {
                    if (tv < 1) tv = 1; if (tv > 32767) tv = 32767;
                    o.typeRes = (SInt16)tv; sDirty = true;
                }
            }
            {
                int xv = (int)o.x, yv = (int)o.y;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputInt("X##ox", &xv)) { o.x = (SInt32)xv; sDirty = true; }
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputInt("Y##oy", &yv)) { o.y = (SInt32)yv; sDirty = true; }
            }
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderFloat("Dir##odir", &o.dir, 0.0f, 6.2832f, "%.2f")) sDirty = true;

            ImGui::Spacing();
            if (ImGui::Button("Jump to##ojump", ImVec2(-1.0f, 0))) {
                float objSeg = (float)sObjs[sSelObj].y / 2.0f;
                sScrollY = objSeg - 80.0f;
                if (sScrollY < 0.0f) sScrollY = 0.0f;
            }
            if (ImGui::Button("Duplicate##odup", ImVec2(-1.0f, 0))) {
                tObjectPos *newObjs = (tObjectPos*)realloc(
                    sObjs, (sNumObjs + 1) * sizeof(tObjectPos));
                if (newObjs) {
                    sObjs = newObjs;
                    sObjs[sNumObjs] = o;
                    sObjs[sNumObjs].x += 30;   /* slight offset so it's visible */
                    sSelObj = (int)sNumObjs;
                    sNumObjs++;
                    sDirty = true;
                }
            }
            if (ImGui::Button("Delete##odel", ImVec2(-1.0f, 0))) {
                for (UInt32 i = (UInt32)sSelObj + 1; i < sNumObjs; i++)
                    sObjs[i - 1] = sObjs[i];
                sNumObjs--;
                if (sSelObj >= (int)sNumObjs) sSelObj = (int)sNumObjs - 1;
                sDirty = true;
            }
        } else {
            ImGui::TextDisabled("Click an object in road view\nto select it.");
        }

        /* Object list */
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("All objects:");
        ImGui::BeginChild("##objlist", ImVec2(-1.0f, -1.0f), false);
        for (UInt32 i = 0; i < sNumObjs; i++) {
            char lbl[64];
            snprintf(lbl, sizeof(lbl), "#%u  t%d  (%d,%d)",
                     i, (int)sObjs[i].typeRes,
                     (int)sObjs[i].x, (int)sObjs[i].y);
            bool sel = ((int)i == sSelObj);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.15f, 0.45f, 0.15f, 1.0f));
            if (ImGui::Selectable(lbl, sel)) {
                sSelObj = (int)i;
                /* Scroll canvas to object */
                float objSeg = (float)sObjs[i].y / 2.0f;
                sScrollY = objSeg - 80.0f;
                if (sScrollY < 0.0f) sScrollY = 0.0f;
            }
            if (sel) ImGui::PopStyleColor();
        }
        ImGui::EndChild();
    }
}

/* =========================================================================
 * Inspector (right panel tabs)
 * ========================================================================= */

static void DrawRoadTab(void)
{
    int s0 = (sSel0 >= 0 && sSel1 >= 0) ? (sSel0 < sSel1 ? sSel0 : sSel1) : sSelSeg;
    int s1 = (sSel0 >= 0 && sSel1 >= 0) ? (sSel0 < sSel1 ? sSel1 : sSel0) : sSelSeg;

    /* Tool selector */
    ImGui::Text("Tool:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Inspect", sTool == kToolInspect)) sTool = kToolInspect;
    ImGui::SameLine();
    if (ImGui::RadioButton("Paint",   sTool == kToolPaint))   sTool = kToolPaint;
    ImGui::SameLine();
    if (ImGui::RadioButton("Shift X", sTool == kToolShift))   sTool = kToolShift;

    ImGui::Separator();

    /* Selection info */
    ImGui::Text("Selection: seg %d", s0);
    if (sSel0 >= 0 && sSel1 >= 0 && sSel0 != sSel1)
        ImGui::Text("           to  %d  (%d segs)", s1, s1 - s0 + 1);
    ImGui::SameLine();
    if (ImGui::SmallButton("All")) { sSel0 = 0; sSel1 = (int)sRoadLen - 1; }

    ImGui::Separator();

    /* ── Inspect ─────────────────────────────────────────────── */
    if (sTool == kToolInspect) {
        if (sSelSeg >= 0 && (UInt32)sSelSeg < sRoadLen) {
            int v[4] = { sRoad[sSelSeg][0], sRoad[sSelSeg][1],
                         sRoad[sSelSeg][2], sRoad[sSelSeg][3] };
            bool ch = false;
            ImGui::Text("Segment %d  (Y ~ %d px)", sSelSeg, sSelSeg * 2);
            ImGui::PushItemWidth(-1.0f);
            ch |= ImGui::DragInt("Strip A left##le",  &v[0], 1.0f, -32000, 32000);
            ch |= ImGui::DragInt("Strip A right##ll", &v[1], 1.0f, -32000, 32000);
            ch |= ImGui::DragInt("Strip B left##rl",  &v[2], 1.0f, -32000, 32000);
            ch |= ImGui::DragInt("Strip B right##re", &v[3], 1.0f, -32000, 32000);
            ImGui::PopItemWidth();
            if (ch) {
                for (int k = 0; k < 4; k++)
                    sRoad[sSelSeg][k] = (SInt16)v[k];
                sDirty = true;
            }
            ImGui::Spacing();
            ImGui::TextDisabled("Strip A: %d wide", v[1] - v[0]);
            ImGui::TextDisabled("Gap:     %d wide", v[2] - v[1]);
            ImGui::TextDisabled("Strip B: %d wide", v[3] - v[2]);
            ImGui::TextDisabled("Total:   %d wide", v[3] - v[0]);
        } else {
            ImGui::TextDisabled("Click the road view to select a segment.");
        }
    }

    /* ── Paint ───────────────────────────────────────────────── */
    else if (sTool == kToolPaint) {
        ImGui::TextDisabled("Drag on the road view to paint.");
        ImGui::TextDisabled("Road center follows your mouse X.");
        ImGui::Spacing();

        /* Clamp gap so it never exceeds total width */
        if (sPaintGapW > sPaintWidth) sPaintGapW = sPaintWidth;
        if (sPaintGapW < 0.0f)        sPaintGapW = 0.0f;

        ImGui::PushItemWidth(-1.0f);
        if (ImGui::SliderFloat("Width##pw", &sPaintWidth, 10.0f, 2000.0f)) {
            if (sPaintGapW > sPaintWidth) sPaintGapW = sPaintWidth;
        }
        ImGui::SliderFloat("Gap##gw", &sPaintGapW, 0.0f, sPaintWidth);
        ImGui::PopItemWidth();

        float hw = sPaintWidth * 0.5f;
        float gw = sPaintGapW;
        ImGui::TextDisabled("strip A: %.0f wide | gap: %.0f | strip B: %.0f wide",
            hw - gw * 0.5f, gw, hw - gw * 0.5f);
        ImGui::TextDisabled("(gap = 0 gives one solid road strip)");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Batch apply to selection:");
        ImGui::PushItemWidth(-1.0f);
        ImGui::SliderFloat("Centre X##pc", &sPaintCenterX, -2000.0f, 2000.0f);
        ImGui::PopItemWidth();
        ImGui::TextDisabled("[%.0f, %.0f, %.0f, %.0f]",
            sPaintCenterX - hw, sPaintCenterX - gw * 0.5f,
            sPaintCenterX + gw * 0.5f, sPaintCenterX + hw);
        bool hasRange = (s0 >= 0 && s1 >= 0 && s0 <= s1);
        if (!hasRange)
            ImGui::TextDisabled("Select a range in Inspect mode first.");
        if (hasRange && ImGui::Button("Apply to Selection##paint", ImVec2(-1.0f, 0))) {
            for (int i = s0; i <= s1; i++) {
                sRoad[i][0] = (SInt16)(sPaintCenterX - hw);
                sRoad[i][1] = (SInt16)(sPaintCenterX - gw * 0.5f);
                sRoad[i][2] = (SInt16)(sPaintCenterX + gw * 0.5f);
                sRoad[i][3] = (SInt16)(sPaintCenterX + hw);
            }
            sDirty = true;
        }
    }

    /* ── Shift X ─────────────────────────────────────────────── */
    else if (sTool == kToolShift) {
        ImGui::Text("Shift all X values (add a curve).");
        ImGui::Spacing();
        ImGui::PushItemWidth(-1.0f);
        ImGui::SliderFloat("Shift amount##sx", &sShiftX, -400.0f, 400.0f);
        ImGui::PopItemWidth();
        bool hasRange = (s0 >= 0 && s1 >= 0 && s0 <= s1);
        if (!hasRange)
            ImGui::TextDisabled("Select a range first.");
        if (hasRange && ImGui::Button("Apply to Selection##shift", ImVec2(-1.0f, 0))) {
            int dX = (int)sShiftX;
            for (int i = s0; i <= s1; i++) {
                sRoad[i][0] = (SInt16)(sRoad[i][0] + dX);
                sRoad[i][1] = (SInt16)(sRoad[i][1] + dX);
                sRoad[i][2] = (SInt16)(sRoad[i][2] + dX);
                sRoad[i][3] = (SInt16)(sRoad[i][3] + dX);
            }
            sDirty = true;
        }
        ImGui::Spacing();
        if (hasRange && ImGui::Button("Smooth Transitions##smooth", ImVec2(-1.0f, 0))) {
            /* 3 passes of weighted average to smooth sharp jumps */
            int iter, i, c;
            for (iter = 0; iter < 3; iter++) {
                for (i = s0 + 1; i < s1; i++) {
                    for (c = 0; c < 4; c++) {
                        sRoad[i][c] = (SInt16)(
                            ((int)sRoad[i-1][c] + (int)sRoad[i][c] * 2 + (int)sRoad[i+1][c]) / 4
                        );
                    }
                }
            }
            sDirty = true;
        }
    }

    ImGui::Separator();
    ImGui::Text("View");
    ImGui::PushItemWidth(-1.0f);
    ImGui::SliderFloat("Zoom##zy", &sZoomY, 0.05f, 20.0f, "%.2fx");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Both axes scale together.\n1 px = 2/zoom world units.");
    ImGui::SliderFloat("Centre X##xc", &sScrollX, -3000.0f, 3000.0f, "%.0f");
    ImGui::PopItemWidth();
    if (ImGui::Button("Fit all##fa")) { sNeedFitWidth = -1.0f; /* trigger recalc next frame */ }
    ImGui::SameLine();
    if (ImGui::Button("Jump to sel##js")) {
        if (sSelSeg >= 0) { sScrollY = (float)sSelSeg - 10.0f; if (sScrollY < 0) sScrollY = 0; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Top##gt"))    sScrollY = 0.0f;
    ImGui::SameLine();
    if (ImGui::Button("Bottom##gb")) sScrollY = (float)sRoadLen;
}

static void DrawPiecesTab(void)
{
    /* Auto-init sliders when a different piece is selected */
    if (sSelectedPiece != sLastSelectedPiece) {
        sLastSelectedPiece = sSelectedPiece;
        if (sSelectedPiece >= 0) {
            const tTrackPieceDef &pd = kPieces[sSelectedPiece];
            sPieceLength = pd.numSegs;
            sPieceCurve  = pd.param;
            /* Widen/Narrow: start width comes from the current road at the cursor */
            sPieceWidth  = (pd.defWidth > 0.0f) ? pd.defWidth : GetSegWidth(sSelSeg);
            sPieceGap    = pd.defGap;
        }
    }

    ImGui::TextDisabled("Pick a piece, tweak params, then Stamp.");
    ImGui::TextDisabled("Cursor auto-advances for easy chaining.");
    ImGui::Spacing();

    /* Piece grid — two columns */
    float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    for (int i = 0; i < kNumPieces; i++) {
        if (i % 2 != 0) ImGui::SameLine();
        bool sel = (sSelectedPiece == i);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.55f, 0.7f, 1.0f));
        if (ImGui::Button(kPieces[i].name, ImVec2(bw, 0)))
            sSelectedPiece = (sSelectedPiece == i) ? -1 : i;
        if (sel) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\nDefault: %d segs | w %.0f | gap %.0f | param %.0f",
                kPieces[i].desc, kPieces[i].numSegs,
                kPieces[i].defWidth, kPieces[i].defGap, kPieces[i].param);
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (sSelectedPiece < 0) {
        ImGui::TextDisabled("No piece selected.");
        return;
    }

    const tTrackPieceDef &p = kPieces[sSelectedPiece];
    bool isWidthTransition = (p.defWidth == 0.0f);

    ImGui::Text("%s", p.name);
    ImGui::TextDisabled("%s", p.desc);
    ImGui::Spacing();

    /* ── Parameter sliders ─────────────────────────────── */
    ImGui::PushItemWidth(-1.0f);

    /* Length */
    ImGui::SliderInt("Length (segs)##pl", &sPieceLength, 1, 500);
    if (sPieceLength < 1) sPieceLength = 1;

    /* Curve / amplitude / width-change */
    if (!isWidthTransition) {
        const char *paramLabel =
            (p.curve == kPieceCurveSmooth)  ? "Curve amount##pc"  :
            (p.curve == kPieceCurveSineLR ||
             p.curve == kPieceCurveSineRL)  ? "Amplitude##pc"     :
                                              "Lateral shift##pc";
        ImGui::SliderFloat(paramLabel, &sPieceCurve, -1200.0f, 1200.0f, "%.0f");
    } else {
        ImGui::SliderFloat("Width change##pc", &sPieceCurve, -1500.0f, 1500.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Total width added over the piece.\nNegative = narrows, Positive = widens.");
    }

    /* Width */
    const char *widthLabel = isWidthTransition ? "Start width##pw" : "Width##pw";
    ImGui::SliderFloat(widthLabel, &sPieceWidth, 20.0f, 2000.0f, "%.0f");

    /* Gap — clamped to width */
    if (sPieceGap > sPieceWidth) sPieceGap = sPieceWidth;
    if (sPieceGap < 0.0f)        sPieceGap = 0.0f;
    ImGui::SliderFloat("Gap##pg", &sPieceGap, 0.0f, sPieceWidth, "%.0f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("0 = one solid strip.\n>0 = two separate road strips with gap between them.");

    ImGui::PopItemWidth();

    if (ImGui::SmallButton("Reset to defaults##pr")) {
        sPieceLength = p.numSegs;
        sPieceCurve  = p.param;
        sPieceWidth  = (p.defWidth > 0.0f) ? p.defWidth : GetSegWidth(sSelSeg);
        sPieceGap    = p.defGap;
    }

    ImGui::Spacing();
    ImGui::Separator();

    int endSeg = sSelSeg + sPieceLength;
    ImGui::Text("Cursor: seg %d   End: seg %d", sSelSeg, endSeg - 1);
    if (endSeg > (int)sRoadLen)
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f),
                           "Warning: extends past road end!");

    ImGui::Spacing();
    if (ImGui::Button("Stamp##doStamp", ImVec2(-1.0f, 0))) {
        if (sRoad && sSelSeg >= 0 && (UInt32)sSelSeg < sRoadLen) {
            float startCX = GetSegCenterX(sSelSeg);
            float startW  = GetSegWidth(sSelSeg);
            int limit = std::min(endSeg, (int)sRoadLen);
            for (int i = sSelSeg; i < limit; i++) {
                ComputePieceSeg(p.curve, isWidthTransition,
                                i - sSelSeg, sPieceLength,
                                startCX, startW, sPieceWidth, sPieceGap, sPieceCurve,
                                sRoad[i]);
            }
            sSelSeg  = limit;   /* advance cursor — ready to chain next piece */
            sSel0 = sSel1 = -1;
            sDirty = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Deselect##dePiece", ImVec2(-1.0f, 0)))
        sSelectedPiece = -1;
}

static void DrawLevelTab(void)
{
    int v;
    ImGui::Spacing();
    ImGui::Text("Level %d metadata", sLevelID + 1);
    ImGui::Separator();

    v = (int)sLevelMeta.time;
    if (ImGui::InputInt("Time limit (s)##tl", &v)) {
        if (v < 0) v = 0; if (v > 65535) v = 65535;
        sLevelMeta.time = (UInt16)v;
        sDirty = true;
    }

    v = (int)sLevelMeta.xStartPos;
    if (ImGui::InputInt("Start X##sx", &v)) {
        if (v < -32768) v = -32768; if (v > 32767) v = 32767;
        sLevelMeta.xStartPos = (SInt16)v;
        sDirty = true;
    }

    ImGui::Spacing();
    ImGui::TextDisabled("roadInfo: %d  levelEnd: %d",
                        (int)sLevelMeta.roadInfo, (int)sLevelMeta.levelEnd);
}

static void DrawSpawnsTab(void)
{
    int g;
    ImGui::Spacing();
    ImGui::Text("Object spawn groups (10 slots)");
    ImGui::Separator();
    if (ImGui::BeginTable("##spawns", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Slot",    ImGuiTableColumnFlags_WidthFixed, 35.0f);
        ImGui::TableSetupColumn("Group ID",ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Count",   ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (g = 0; g < 10; g++) {
            int cnt = (int)sLevelMeta.objGrps[g].numObjs;
            char id_str[16], cnt_id[32];
            snprintf(id_str, sizeof(id_str), "%d",  (int)sLevelMeta.objGrps[g].resID);
            snprintf(cnt_id, sizeof(cnt_id), "##cnt%d", g);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%d", g);
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(id_str);
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputInt(cnt_id, &cnt)) {
                if (cnt < 0) cnt = 0; if (cnt > 32767) cnt = 32767;
                sLevelMeta.objGrps[g].numObjs = (SInt16)cnt;
                sDirty = true;
            }
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Set Count to 0 to disable a group.");
}

static void DrawWaypointsTab(void)
{
    ImGui::Spacing();
    ImGui::Text("AI racing-line waypoints");
    ImGui::Separator();

    /* ── Regenerate controls ─────────────────────────────────── */
    ImGui::PushItemWidth(120.0f);
    ImGui::SliderInt("Step (segs)##aistep", &sAILineStep, 1, 64);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("One waypoint per N road segments.\n"
                          "Lower = denser / more accurate.\n"
                          "Higher = fewer waypoints / faster AI.");
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Regenerate from road##airegen")) {
        RegenerateAILines();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Rebuild TrackUp and TrackDown from current road geometry.\n"
                          "TrackUp  → strip A centre (left / oncoming lane).\n"
                          "TrackDown → strip B centre (right / racing lane).");
    ImGui::SameLine();
    ImGui::TextDisabled("Up: %u pts  Down: %u pts", sNumTrackUp, sNumTrackDown);

    ImGui::Spacing();
    ImGui::TextDisabled("Manual edits below; regenerate overwrites them.");
    ImGui::Separator();
    ImGui::Spacing();

    auto drawTrackTable = [](const char *label, tTrackInfoSeg *track, UInt32 num) {
        if (!track || num == 0) { ImGui::TextDisabled("No data — press Regenerate."); return; }
        ImGui::Text("%s (%u pts)", label, num);
        if (ImGui::BeginTable(label, 4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
                ImVec2(0, 180.0f))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("X",    ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Y",    ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Velo", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Flags",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin((int)num);
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                    int  vx = (int)track[i].x;
                    int  vy = (int)track[i].y;
                    float vv = track[i].velo;
                    int  vf = (int)track[i].flags;
                    char xid[24], yid[24], vid[24];
                    snprintf(xid, 24, "##x%s%d", label, i);
                    snprintf(yid, 24, "##y%s%d", label, i);
                    snprintf(vid, 24, "##v%s%d", label, i);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetNextItemWidth(55.0f);
                    if (ImGui::InputInt(xid, &vx)) track[i].x = (SInt16)vx;
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(75.0f);
                    if (ImGui::InputInt(yid, &vy)) track[i].y = (SInt32)vy;
                    ImGui::TableSetColumnIndex(2);
                    ImGui::SetNextItemWidth(55.0f);
                    ImGui::InputFloat(vid, &vv, 0, 0, "%.1f");
                    track[i].velo = vv;
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("0x%04X", vf);
                }
            }
            ImGui::EndTable();
        }
    };

    drawTrackTable("TrackUp",   sTrackUp,   sNumTrackUp);
    ImGui::Spacing();
    drawTrackTable("TrackDown", sTrackDown, sNumTrackDown);
}

/* =========================================================================
 * Main editor window
 * ========================================================================= */

static void DrawEditorUI(bool *done)
{
    ImGuiIO &io = ImGui::GetIO();

    /* Full-screen window */
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGuiWindowFlags wflags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar;
    ImGui::Begin("##editor", NULL, wflags);

    /* ── Menu bar ──────────────────────────────────────────────────────── */
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save", "Ctrl+S"))    SaveLevel();
            if (ImGui::MenuItem("Revert"))            LoadEditorData(sLevelID);
            ImGui::Separator();
            if (ImGui::MenuItem("Exit Editor","Esc")) *done = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::TextUnformatted("Road view: left-drag = select range");
            ImGui::TextUnformatted("           scroll    = zoom in/out");
            ImGui::TextUnformatted("           mid-drag  = pan (X and Y)");
            ImGui::TextUnformatted("Save file:  saves/level_{n}_editor.bin");
            ImGui::TextUnformatted("Applied automatically when you play.");
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    /* Ctrl+S shortcut */
    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S, false))
        SaveLevel();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        *done = true;

    /* ── Toolbar ───────────────────────────────────────────────────────── */
    {
        ImVec4 saveCol = sDirty ? ImVec4(0.7f, 0.35f, 0.1f, 1.0f)
                                : ImVec4(0.2f, 0.5f,  0.2f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, saveCol);
        if (ImGui::Button("Save  Ctrl+S")) SaveLevel();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Revert"))  LoadEditorData(sLevelID);
        ImGui::SameLine();
        if (ImGui::Button("Exit  Esc")) *done = true;
        ImGui::SameLine();
        ImGui::TextDisabled(" | Level %d | %u segs", sLevelID + 1, sRoadLen);
        if (sDirty) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "[unsaved]");
        }
        if (sSaveMsgTimer > 0.0f) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "%s", sSaveMsg);
            sSaveMsgTimer -= io.DeltaTime;
        }
    }

    /* ── Three-panel layout: [Objects | Road canvas | Inspector] ─────── */
    float avW     = ImGui::GetContentRegionAvail().x;
    float avH     = ImGui::GetContentRegionAvail().y;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float objPanW = 175.0f;
    float inspW   = avW * 0.28f;
    float canvW   = avW - objPanW - inspW - spacing * 2.0f;
    if (canvW < 80.0f) canvW = 80.0f;

    /* ─── Left: object panel ──────────────────────────────────────────── */
    ImGui::BeginChild("##objpanel", ImVec2(objPanW, avH), true,
                      ImGuiWindowFlags_NoScrollbar);
    DrawObjPanel();
    ImGui::EndChild();

    ImGui::SameLine();

    /* ─── Center: road canvas ────────────────────────────────────────── */
    ImGui::BeginChild("##roadpanel", ImVec2(canvW, avH), false,
                      ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::TextDisabled("Road view   scroll=zoom   mid-drag=pan XY   left-drag=select");

        float infoH = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
        ImVec2 cpos = ImGui::GetCursorScreenPos();
        float  cw   = ImGui::GetContentRegionAvail().x;
        float  ch   = ImGui::GetContentRegionAvail().y - infoH;
        if (ch < 10.0f) ch = 10.0f;
        ImVec2 csz(cw, ch);

        ImGui::InvisibleButton("##canvas", csz,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);

        bool hovered = ImGui::IsItemHovered();
        bool lActive = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool mActive = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Middle);

        ImVec2 mp = ImGui::GetMousePos();
        float  mry = mp.y - cpos.y;   /* mouse Y relative to canvas */

        /* Auto-fit: recompute from data when sNeedFitWidth is set */
        if (sNeedFitWidth != 0.0f && cw > 0.0f && sRoad && sRoadLen > 0) {
            if (sNeedFitWidth < 0.0f) {
                /* Recompute extents (triggered by "Fit all" button) */
                float xMinF = (float)sRoad[0][0], xMaxF = (float)sRoad[0][3];
                for (UInt32 fi = 0; fi < sRoadLen; fi++) {
                    if ((float)sRoad[fi][0] < xMinF) xMinF = (float)sRoad[fi][0];
                    if ((float)sRoad[fi][3] > xMaxF) xMaxF = (float)sRoad[fi][3];
                }
                for (UInt32 fi = 0; fi < sNumObjs; fi++) {
                    if ((float)sObjs[fi].x < xMinF) xMinF = (float)sObjs[fi].x;
                    if ((float)sObjs[fi].x > xMaxF) xMaxF = (float)sObjs[fi].x;
                }
                sScrollX      = (xMinF + xMaxF) * 0.5f;
                sNeedFitWidth = (xMaxF - xMinF) * 0.5f * 1.1f;
                if (sNeedFitWidth < 1.0f) sNeedFitWidth = 1.0f;
            }
            /* sZoomY = ppu*2 where ppu = (cw/2) / halfWidth */
            sZoomY = cw / sNeedFitWidth;
            sZoomY = std::max(0.05f, std::min(20.0f, sZoomY));
            sNeedFitWidth = 0.0f;
        }

        /* Zoom around mouse — both axes scale together to preserve aspect ratio.
         * Y anchor: keep the segment under the mouse fixed.
         * X anchor: keep the world X under the mouse fixed. */
        if (hovered && io.MouseWheel != 0.0f) {
            float fromBottom  = ch - mry;
            float segAtMouse  = sScrollY + fromBottom / sZoomY;
            float worldXAtMouse = CanvasXToWorldX(mp.x - cpos.x, cw);

            float factor = io.MouseWheel > 0.0f ? 1.2f : (1.0f / 1.2f);
            sZoomY *= factor;
            sZoomY = std::max(0.05f, std::min(20.0f, sZoomY));

            /* Re-anchor Y */
            sScrollY = segAtMouse - fromBottom / sZoomY;
            /* Re-anchor X: after zoom, world X under mouse should still map to same px */
            sScrollX = worldXAtMouse - ((mp.x - cpos.x) - cw * 0.5f) / (sZoomY * 0.5f);
        }

        /* Pan with middle-drag (both axes, touchscreen-style: drag follows finger) */
        if (mActive) {
            sScrollY += io.MouseDelta.y / sZoomY;
            sScrollX -= io.MouseDelta.x / (sZoomY * 0.5f);
        }

        /* ── Object interaction (click to select/place, drag to move) ── */
        {
            const float kHitR = 9.0f;  /* px */

            /* Detect object hit on initial click */
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                int hit = -1;
                float bestD2 = kHitR * kHitR;
                for (UInt32 oi = 0; oi < sNumObjs; oi++) {
                    float ox = MapX((float)sObjs[oi].x, cw);
                    float oy = ch - ((float)sObjs[oi].y / 2.0f - sScrollY) * sZoomY;
                    float dx = (mp.x - cpos.x) - ox;
                    float dy = mry - oy;
                    float d2 = dx * dx + dy * dy;
                    if (d2 < bestD2) { bestD2 = d2; hit = (int)oi; }
                }

                if (sObjTool == kObjPlace) {
                    if (hit < 0) {
                        /* Place new object at click position */
                        float worldX = CanvasXToWorldX(mp.x - cpos.x, cw);
                        float worldY = (sScrollY + (ch - mry) / sZoomY) * 2.0f;
                        tObjectPos *nArr = (tObjectPos*)realloc(
                            sObjs, (sNumObjs + 1) * sizeof(tObjectPos));
                        if (nArr) {
                            sObjs = nArr;
                            memset(&sObjs[sNumObjs], 0, sizeof(tObjectPos));
                            sObjs[sNumObjs].typeRes = sPlaceTypeRes;
                            sObjs[sNumObjs].dir     = sPlaceDir;
                            sObjs[sNumObjs].x = (SInt32)worldX;
                            sObjs[sNumObjs].y = (SInt32)worldY;
                            sSelObj = (int)sNumObjs;
                            sNumObjs++;
                            sDirty = true;
                        }
                    } else {
                        sSelObj = hit;  /* clicking existing object selects it */
                    }
                } else {
                    /* Select mode */
                    sSelObj      = hit;
                    sObjDragIdx  = hit;
                    sObjDragging = (hit >= 0);
                }
            }

            /* Drag selected object */
            if (sObjDragging && sObjDragIdx >= 0 && lActive
                    && sObjTool == kObjSelect
                    && (UInt32)sObjDragIdx < sNumObjs) {
                float worldX = CanvasXToWorldX(mp.x - cpos.x, cw);
                float worldY = (sScrollY + (ch - mry) / sZoomY) * 2.0f;
                sObjs[sObjDragIdx].x = (SInt32)worldX;
                sObjs[sObjDragIdx].y = (SInt32)worldY;
                sDirty = true;
            }
            if (!lActive) { sObjDragging = false; sObjDragIdx = -1; }
        }

        /* Left-drag: Paint mode = brush, other modes = range select
         * (suppressed when an object is being dragged) */
        static bool  sDragging     = false;
        static int   sDragStart    = 0;
        if (sObjDragging) {
            /* object drag takes priority — suppress road tools */
        } else if (sTool == kToolPaint && sRoad) {
            /* Brush: each segment dragged over gets the current paint shape,
             * centred on the mouse X position in world space. */
            if (lActive) {
                int seg = CanvasYToSeg(mry, ch);
                float worldX = CanvasXToWorldX(mp.x - cpos.x, cw);
                if (seg >= 0 && (UInt32)seg < sRoadLen) {
                    float hw = sPaintWidth * 0.5f;
                    float gw = std::min(sPaintGapW, sPaintWidth);
                    sRoad[seg][0] = (SInt16)(worldX - hw);
                    sRoad[seg][1] = (SInt16)(worldX - gw * 0.5f);
                    sRoad[seg][2] = (SInt16)(worldX + gw * 0.5f);
                    sRoad[seg][3] = (SInt16)(worldX + hw);
                    sSelSeg = seg;
                    sDirty = true;
                }
            }
        } else {
            /* Range select */
            if (lActive && !sDragging) {
                sDragStart = CanvasYToSeg(mry, ch);
                sDragging  = true;
            }
            if (!lActive) sDragging = false;
            if (lActive) {
                int cur = CanvasYToSeg(mry, ch);
                sSelSeg = cur;
                sSel0 = std::max(0, std::min(sDragStart, cur));
                sSel1 = std::min((int)sRoadLen - 1, std::max(sDragStart, cur));
            }
        }

        /* Clamp scroll */
        float maxScroll = (float)sRoadLen - ch / sZoomY;
        if (sScrollY < 0.0f)          sScrollY = 0.0f;
        if (maxScroll > 0.0f && sScrollY > maxScroll) sScrollY = maxScroll;

        /* Draw road */
        ImDrawList *dl = ImGui::GetWindowDrawList();
        DrawRoadCanvas(dl, cpos, csz);

        /* Hover label */
        if (hovered && sRoad) {
            int seg = CanvasYToSeg(mry, ch);
            if (seg >= 0 && (UInt32)seg < sRoadLen) {
                char lbl[48];
                snprintf(lbl, sizeof(lbl), "seg %d  Y~%d px", seg, seg * 2);
                dl->AddText(ImVec2(mp.x + 8.0f, mp.y - 16.0f),
                            IM_COL32(255, 255, 255, 200), lbl);
            }
        }

        /* Info bar */
        char info[80];
        snprintf(info, sizeof(info), "Scroll: %d/%u  |  Zoom: %.2fx  |  Sel: %d",
                 (int)sScrollY, sRoadLen, sZoomY, sSelSeg);
        ImGui::SetCursorScreenPos(ImVec2(cpos.x, cpos.y + ch + 2.0f));
        ImGui::TextDisabled("%s", info);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    /* ─── Right: tabbed inspector ─────────────────────────────────────── */
    ImGui::BeginChild("##inspector", ImVec2(inspW, avH), true,
                      ImGuiWindowFlags_NoScrollbar);
    {
        if (ImGui::BeginTabBar("##tabs")) {
            if (ImGui::BeginTabItem("Road"))       { DrawRoadTab();      ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Pieces"))     { DrawPiecesTab();    ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Level"))      { DrawLevelTab();     ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Spawns"))     { DrawSpawnsTab();    ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Waypoints"))  { DrawWaypointsTab(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

/* =========================================================================
 * Entry point — called from platform_interface.c
 * ========================================================================= */

extern "C" void EditorEnter(int levelID)
{
    SDL_Window    *win = SDL_GL_GetCurrentWindow();
    SDL_GLContext  ctx = SDL_GL_GetCurrentContext();

    if (!win || !ctx) {
        fprintf(stderr, "[Editor] No SDL window/context available.\n");
        return;
    }

    /* Load working data */
    LoadEditorData(levelID);
    if (!sRoad) return;

    /* Init ImGui */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(win, ctx);
    ImGui_ImplOpenGL2_Init();

    /* Editor loop */
    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) { done = true; gExit = 1; }
            /* Pass window resize on through */
        }

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        DrawEditorUI(&done);

        ImGui::Render();
        {
            int dw, dh;
            SDL_GL_GetDrawableSize(win, &dw, &dh);
            glViewport(0, 0, dw, dh);
        }
        glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(win);
        SDL_Delay(16);
    }

    /* Cleanup */
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    /* Free working copies */
    if (sRoad)      { free(sRoad);      sRoad      = NULL; }
    if (sTrackUp)   { free(sTrackUp);   sTrackUp   = NULL; }
    if (sTrackDown) { free(sTrackDown); sTrackDown = NULL; }
    if (sObjs)      { free(sObjs);      sObjs      = NULL; sNumObjs = 0; }
}
