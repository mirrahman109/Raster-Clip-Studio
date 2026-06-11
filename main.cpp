/*
 * ============================================================
 *  Raster Clip Studio
 *  CSE 4201 – Computer Graphics Assignment
 *  RUET – Department of Computer Science & Engineering
 * ============================================================
 *  Algorithms:
 *   - Slope-independent Midpoint Line Rasterization (all octants)
 *   - Cohen-Sutherland Line Clipping (pixel-accurate)
 *
 *  Build:
 *   Linux : g++ main.cpp -o RasterClipStudio -lGL -lGLU -lglut -std=c++17
 *   Windows: g++ main.cpp -o RasterClipStudio.exe -lopengl32 -lglu32 -lfreeglut -std=c++17
 * ============================================================
 */

#ifdef _WIN32
  #include <windows.h>
  #include <GL/freeglut.h>
#else
  #include <GL/glut.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

// ─────────────────────────── window / grid constants ─────────────
static const int WIN_W     = 1000;   // initial window width
static const int WIN_H     = 700;    // initial window height
static const int GRID_COLS = 40;
static const int GRID_ROWS = 28;
static const int CELL_SZ   = 18;

// HUD bar heights (fixed, independent of window size)
static const int HUD_TOP_H = 46;   // top title bar
static const int HUD_BOT_H = 76;   // bottom status bar

// Dynamic grid origin – recomputed in reshape so the grid stays
// centred in the viewport area between the two HUD bars.
// Margins around the grid (for axis labels etc.)
static const int GRID_MARGIN_L = 52;  // left  (room for Y labels)
static const int GRID_MARGIN_B = 28;  // bottom (room for X labels)

// Current viewport size (updated by reshape callback)
static int g_vw = WIN_W;
static int g_vh = WIN_H;

// Computed grid origin (updated by reshape)
static int g_gridOriginX = 60;
static int g_gridOriginY = 80;

// Recalculate grid origin so the grid is centred in the
// area between the two HUD bars.
static void recalcGrid() {
    // Available area between top and bottom HUD bars
    int areaW = g_vw;
    int areaH = g_vh - HUD_TOP_H - HUD_BOT_H;

    // Total grid pixel size
    int gridW = GRID_COLS * CELL_SZ;
    int gridH = GRID_ROWS * CELL_SZ;

    // Centre: if area is larger than grid, pad evenly; otherwise use min margin
    int padX = std::max(GRID_MARGIN_L, (areaW - gridW) / 2);
    int padY = std::max(GRID_MARGIN_B, (areaH - gridH) / 2);

    g_gridOriginX = padX;
    // Y origin is from bottom of viewport; HUD_BOT_H is the bottom bar
    g_gridOriginY = HUD_BOT_H + padY;
}

// Cohen-Sutherland region codes
static const int INSIDE = 0;
static const int LEFT   = 1;
static const int RIGHT  = 2;
static const int BOTTOM = 4;
static const int TOP    = 8;

// Animation phases
// PHASE_IDLE        : just loaded, show full line + clip window, no pixels yet
// PHASE_RASTER      : animating raster pixels one-by-one
// PHASE_DONE        : animation finished, all pixels + clipped result shown
enum AnimPhase { PHASE_IDLE, PHASE_RASTER, PHASE_DONE };

// ─────────────────────────── data types ──────────────────────────
struct Pt { int x, y; };

// ─────────────────────────── global state ────────────────────────
struct TestCase {
    int x1, y1, x2, y2;
    int xmin, ymin, xmax, ymax;
    const char* label;
};

// ── clip window – centred on the 40×28 grid ────────────────
// Grid centre: col 20, row 14.
// Window is 20 cols × 14 rows, placed symmetrically around the centre.
static const int CLIP_XMIN = 10;
static const int CLIP_YMIN =  7;
static const int CLIP_XMAX = 30;
static const int CLIP_YMAX = 21;


static const TestCase TESTS[] = {
    { 13,  9, 26, 18,  CLIP_XMIN, CLIP_YMIN, CLIP_XMAX, CLIP_YMAX,
      "Case 1: Line Completely Inside"    },
    {  2, 23,  8, 25,  CLIP_XMIN, CLIP_YMIN, CLIP_XMAX, CLIP_YMAX,
      "Case 2: Line Completely Outside"   },
    {  4, 11, 34, 16,  CLIP_XMIN, CLIP_YMIN, CLIP_XMAX, CLIP_YMAX,
      "Case 3: Line Crosses Boundaries"   },
    {  5, 22, 35,  6,  CLIP_XMIN, CLIP_YMIN, CLIP_XMAX, CLIP_YMAX,
      "Case 4: Negative Slope"            },
    {  2, 14, 38, 14,  CLIP_XMIN, CLIP_YMIN, CLIP_XMAX, CLIP_YMAX,
      "Case 5: Horizontal Line"           },
};
static const int NUM_TESTS = 5;

static int g_x1, g_y1, g_x2, g_y2;
static int g_xmin, g_ymin, g_xmax, g_ymax;
static int g_currentTest = 0;

// All raster pixels for the full original line
static std::vector<Pt> g_rasterPixels;

// Clipping result
static bool  g_hasClipped = false;
static float g_cx1, g_cy1, g_cx2, g_cy2;   // clipped endpoints (float grid)

static std::vector<Pt> g_clippedPixels;

// Per-pixel: is this pixel inside the clip window?
static std::vector<bool> g_pixelInside;

// Animation state
static AnimPhase g_phase    = PHASE_IDLE;
static int       g_animStep = 0;           // how many pixels revealed so far

// Manual input
static bool g_inputMode  = false;
static int  g_inputField = 0;
static char g_inputBuf[32]  = "";
static int  g_inputCursor = 0;   // cursor position within g_inputBuf
static int  g_manualVals[8] = {13, 9, 26, 18, CLIP_XMIN, CLIP_YMIN, CLIP_XMAX, CLIP_YMAX};

// Pixel log
static bool g_showLog = false;

// Status messages
static std::string g_statusMsg;
static std::string g_outcodeMsg;

// ─────────────────────────── helpers ─────────────────────────────
// All grid-to-screen conversions use the dynamic origin so the grid
// stays centred whenever the window is resized or maximised.
static int   gx(int c)    { return g_gridOriginX + c * CELL_SZ; }
static int   gy(int r)    { return g_gridOriginY + r * CELL_SZ; }
static float gcx(float c) { return g_gridOriginX + c * CELL_SZ + CELL_SZ * 0.5f; }
static float gcy(float r) { return g_gridOriginY + r * CELL_SZ + CELL_SZ * 0.5f; }

static void drawText(int sx, int sy, const char* s,
                     void* font = GLUT_BITMAP_8_BY_13)
{
    glRasterPos2i(sx, sy);
    for (const char* c = s; *c; ++c)
        glutBitmapCharacter(font, *c);
}

static std::string intToStr(int v) {
    std::ostringstream o; o << v; return o.str();
}

// ─────────────────────────── Midpoint Line (all octants) ─────────
static void midpointLine(int x0, int y0, int x1, int y1, std::vector<Pt>& out) {
    int dx = x1 - x0, dy = y1 - y0;
    int sx = (dx >= 0) ? 1 : -1;
    int sy = (dy >= 0) ? 1 : -1;
    dx = std::abs(dx);
    dy = std::abs(dy);
    bool steep = dy > dx;
    if (steep) std::swap(dx, dy);

    int d = 2*dy - dx;
    int x = x0, y = y0;
    for (int i = 0; i <= dx; ++i) {
        out.push_back({x, y});
        if (d > 0) {
            if (steep) x += sx; else y += sy;
            d -= 2*dx;
        }
        if (steep) y += sy; else x += sx;
        d += 2*dy;
    }
}

// ─────────────────────────── Cohen-Sutherland (FIX ⑤) ──────────

static int computeCode(float x, float y,
                       float xmin, float ymin, float xmax, float ymax)
{
    int c = INSIDE;
    if      (x < xmin) c |= LEFT;
    else if (x > xmax) c |= RIGHT;
    if      (y < ymin) c |= BOTTOM;
    else if (y > ymax) c |= TOP;
    return c;
}

static bool cohenSutherland(float x0, float y0, float x1, float y1,
                             float xmin, float ymin, float xmax, float ymax,
                             float& ox0, float& oy0, float& ox1, float& oy1,
                             std::string& msg)
{
    int c0 = computeCode(x0,y0,xmin,ymin,xmax,ymax);
    int c1 = computeCode(x1,y1,xmin,ymin,xmax,ymax);
    char buf[256];
    snprintf(buf,sizeof(buf),"Cohen-Sutherland  Outcodes: P1=%d  P2=%d", c0, c1);
    msg = buf;

    while (true) {
        if (!(c0|c1)) {
            ox0=x0; oy0=y0; ox1=x1; oy1=y1;
            msg += "   => ACCEPTED";
            return true;
        }
        if (c0 & c1) {
            msg += "   => REJECTED (trivial)";
            return false;
        }
        int co = c0 ? c0 : c1;
        float x, y, dx = x1-x0, dy = y1-y0;
        if      (co & TOP)    { x = x0 + dx*(ymax-y0)/dy; y = ymax; }
        else if (co & BOTTOM) { x = x0 + dx*(ymin-y0)/dy; y = ymin; }
        else if (co & RIGHT)  { y = y0 + dy*(xmax-x0)/dx; x = xmax; }
        else                  { y = y0 + dy*(xmin-x0)/dx; x = xmin; }

        if (co == c0) { x0=x; y0=y; c0=computeCode(x0,y0,xmin,ymin,xmax,ymax); }
        else          { x1=x; y1=y; c1=computeCode(x1,y1,xmin,ymin,xmax,ymax); }
    }
}

// ─────────────────────────── compute scene ───────────────────────
static void computeScene() {
    g_rasterPixels.clear();
    g_clippedPixels.clear();
    g_pixelInside.clear();
    g_hasClipped = false;
    g_statusMsg = "";
    g_outcodeMsg = "";

    // Full rasterisation of original line
    midpointLine(g_x1, g_y1, g_x2, g_y2, g_rasterPixels);

    // Cohen-Sutherland clipping
    g_hasClipped = cohenSutherland(
        (float)g_x1, (float)g_y1,
        (float)g_x2, (float)g_y2,
        (float)g_xmin, (float)g_ymin,
        (float)g_xmax, (float)g_ymax,
        g_cx1, g_cy1, g_cx2, g_cy2,
        g_outcodeMsg);


    if (g_hasClipped) {
        std::vector<Pt> raw;
        midpointLine((int)roundf(g_cx1),(int)roundf(g_cy1),
                     (int)roundf(g_cx2),(int)roundf(g_cy2), raw);
        for (const Pt& p : raw) {
            // A pixel at grid col p.x occupies [p.x .. p.x+1) in grid space.
            // It is inside if p.x >= xmin AND p.x < xmax AND same for y.
            if (p.x >= g_xmin && p.x < g_xmax &&
                p.y >= g_ymin && p.y < g_ymax)
            {
                g_clippedPixels.push_back(p);
            }
        }
    }

    // Mark each original pixel as inside or outside the clip window
    g_pixelInside.resize(g_rasterPixels.size());
    for (int i = 0; i < (int)g_rasterPixels.size(); ++i) {
        const Pt& p = g_rasterPixels[i];
        g_pixelInside[i] = (p.x >= g_xmin && p.x < g_xmax &&
                            p.y >= g_ymin && p.y < g_ymax);
    }
}

static void loadTest(int idx) {
    const TestCase& t = TESTS[idx];
    g_x1=t.x1; g_y1=t.y1; g_x2=t.x2; g_y2=t.y2;
    // Always restore the fixed centred clip window
    g_xmin=CLIP_XMIN; g_ymin=CLIP_YMIN;
    g_xmax=CLIP_XMAX; g_ymax=CLIP_YMAX;
    g_phase    = PHASE_IDLE;
    g_animStep = 0;
    computeScene();
    g_statusMsg = std::string("Loaded: ") + t.label +
                  "   |   Press [A] to animate rasterization";
}

// ─────────────────────────── fill cell ───────────────────────────
static void fillCell(int col, int row, float r, float g, float b, float a=1.f) {
    glColor4f(r,g,b,a);
    int px=gx(col), py=gy(row);
    glBegin(GL_QUADS);
    glVertex2i(px+1,       py+1);
    glVertex2i(px+CELL_SZ-1, py+1);
    glVertex2i(px+CELL_SZ-1, py+CELL_SZ-1);
    glVertex2i(px+1,       py+CELL_SZ-1);
    glEnd();
}

// Thin border around a cell
static void outlineCell(int col, int row, float r, float g, float b) {
    glColor3f(r,g,b);
    int px=gx(col), py=gy(row);
    glBegin(GL_LINE_LOOP);
    glVertex2i(px+1,       py+1);
    glVertex2i(px+CELL_SZ-1, py+1);
    glVertex2i(px+CELL_SZ-1, py+CELL_SZ-1);
    glVertex2i(px+1,       py+CELL_SZ-1);
    glEnd();
}

// ─────────────────────────── draw: grid ──────────────────────────
static void drawGrid() {
    glColor3f(0.20f,0.20f,0.25f);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (int c=0;c<=GRID_COLS;++c) { glVertex2i(gx(c),gy(0)); glVertex2i(gx(c),gy(GRID_ROWS)); }
    for (int r=0;r<=GRID_ROWS;++r) { glVertex2i(gx(0),gy(r)); glVertex2i(gx(GRID_COLS),gy(r)); }
    glEnd();

    glColor3f(0.38f,0.38f,0.50f);
    for (int c=0;c<=GRID_COLS;c+=5) {
        std::string s=intToStr(c);
        drawText(gx(c)-4, gy(0)-15, s.c_str(), GLUT_BITMAP_HELVETICA_10);
    }
    for (int r=0;r<=GRID_ROWS;r+=5) {
        std::string s=intToStr(r);
        drawText(gx(0)-24, gy(r)-4, s.c_str(), GLUT_BITMAP_HELVETICA_10);
    }
}

// ─────────────────────────── draw: clip window ───────────────────
static void drawClipWindow() {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.2f,0.5f,0.9f,0.08f);
    glBegin(GL_QUADS);
    glVertex2i(gx(g_xmin),gy(g_ymin)); glVertex2i(gx(g_xmax),gy(g_ymin));
    glVertex2i(gx(g_xmax),gy(g_ymax)); glVertex2i(gx(g_xmin),gy(g_ymax));
    glEnd();
    glDisable(GL_BLEND);

    glColor3f(0.3f,0.7f,1.0f);
    glLineWidth(2.5f);
    glBegin(GL_LINE_LOOP);
    glVertex2i(gx(g_xmin),gy(g_ymin)); glVertex2i(gx(g_xmax),gy(g_ymin));
    glVertex2i(gx(g_xmax),gy(g_ymax)); glVertex2i(gx(g_xmin),gy(g_ymax));
    glEnd();
    glLineWidth(1.0f);

    glColor3f(0.5f,0.85f,1.0f);
    char buf[32];
    snprintf(buf,sizeof(buf),"(%d,%d)",g_xmin,g_ymin);
    drawText(gx(g_xmin)+2, gy(g_ymin)+2, buf, GLUT_BITMAP_HELVETICA_10);
    snprintf(buf,sizeof(buf),"(%d,%d)",g_xmax,g_ymax);
    drawText(gx(g_xmax)-54, gy(g_ymax)-14, buf, GLUT_BITMAP_HELVETICA_10);
}


static void drawOriginalLine() {
    // Bright solid line through pixel centres
    glColor3f(0.75f, 0.75f, 0.85f);
    glLineWidth(1.8f);
    glBegin(GL_LINES);
    glVertex2f(gcx(g_x1), gcy(g_y1));
    glVertex2f(gcx(g_x2), gcy(g_y2));
    glEnd();
    glLineWidth(1.0f);

    // Endpoint dots
    glColor3f(1.0f, 0.8f, 0.15f);
    int rad = 5;
    auto dot = [&](float cx, float cy) {
        glBegin(GL_POLYGON);
        for (int a=0;a<16;++a) {
            float ang = a*3.14159f*2/16;
            glVertex2f(cx+rad*cosf(ang), cy+rad*sinf(ang));
        }
        glEnd();
    };
    dot(gcx(g_x1), gcy(g_y1));
    dot(gcx(g_x2), gcy(g_y2));

    // Labels
    glColor3f(1.0f,0.85f,0.3f);
    char buf[32];
    snprintf(buf,sizeof(buf),"P1(%d,%d)",g_x1,g_y1);
    drawText((int)gcx(g_x1)+8, (int)gcy(g_y1)+4, buf, GLUT_BITMAP_HELVETICA_10);
    snprintf(buf,sizeof(buf),"P2(%d,%d)",g_x2,g_y2);
    drawText((int)gcx(g_x2)+8, (int)gcy(g_y2)+4, buf, GLUT_BITMAP_HELVETICA_10);
}

// ─────────────────────────── draw: raster pixels ──────────────────

static void drawRasterPixels() {
    if (g_phase == PHASE_IDLE) return;   // nothing revealed yet

    int limit = (g_phase == PHASE_DONE)
                ? (int)g_rasterPixels.size()
                : g_animStep;
    limit = std::min(limit, (int)g_rasterPixels.size());

    for (int i = 0; i < limit; ++i) {
        const Pt& p = g_rasterPixels[i];
        bool inside = g_pixelInside[i];

        if (inside) {
            // Inside the clip window → always green, even during animation
            fillCell(p.x, p.y, 0.05f, 0.95f, 0.35f);
            outlineCell(p.x, p.y, 0.4f, 1.0f, 0.6f);
        } else {
            // Outside the clip window → dim red (clipped away)
            fillCell(p.x, p.y, 0.65f, 0.18f, 0.18f);
        }
    }

    // Highlight the frontier pixel during animation (white outline on top)
    if (g_phase == PHASE_RASTER && g_animStep > 0 &&
        g_animStep <= (int)g_rasterPixels.size())
    {
        const Pt& cur = g_rasterPixels[g_animStep-1];
        outlineCell(cur.x, cur.y, 1.0f, 1.0f, 1.0f);
    }
}

// ─────────────────────────── draw: legend ────────────────────────
static void drawLegend() {
    // Panel sits in the top-right corner, just below the title bar
    int lx = g_vw - 210, ly = g_vh - 58;
    int dy = 18;

    // Background panel
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.08f,0.08f,0.14f,0.82f);
    glBegin(GL_QUADS);
    glVertex2i(lx-6,    ly-4*dy-6);
    glVertex2i(g_vw-4,  ly-4*dy-6);
    glVertex2i(g_vw-4,  ly+14);
    glVertex2i(lx-6,    ly+14);
    glEnd();
    glDisable(GL_BLEND);

    glColor3f(0.7f,0.7f,0.85f);
    drawText(lx, ly, "LEGEND", GLUT_BITMAP_HELVETICA_12);
    ly -= dy;

    // Original line
    glColor3f(0.75f,0.75f,0.85f);
    glLineWidth(1.8f);
    glBegin(GL_LINES); glVertex2i(lx,ly+5); glVertex2i(lx+22,ly+5); glEnd();
    glLineWidth(1.f);
    glColor3f(0.85f,0.85f,0.9f);
    drawText(lx+26, ly, "Original Line", GLUT_BITMAP_HELVETICA_10);
    ly -= dy;

    // Inside pixel (green)
    glColor3f(0.05f,0.95f,0.35f);
    glBegin(GL_QUADS);
    glVertex2i(lx,ly); glVertex2i(lx+12,ly);
    glVertex2i(lx+12,ly+12); glVertex2i(lx,ly+12);
    glEnd();
    glColor3f(0.85f,0.85f,0.9f);
    drawText(lx+16, ly, "Raster (inside window)", GLUT_BITMAP_HELVETICA_10);
    ly -= dy;

    // Outside pixel (red)
    glColor3f(0.65f,0.18f,0.18f);
    glBegin(GL_QUADS);
    glVertex2i(lx,ly); glVertex2i(lx+12,ly);
    glVertex2i(lx+12,ly+12); glVertex2i(lx,ly+12);
    glEnd();
    glColor3f(0.85f,0.85f,0.9f);
    drawText(lx+16, ly, "Raster (clipped away)", GLUT_BITMAP_HELVETICA_10);
    ly -= dy;

    // Clipped result (green)
    glColor3f(0.05f,0.95f,0.35f);
    glBegin(GL_QUADS);
    glVertex2i(lx,ly); glVertex2i(lx+12,ly);
    glVertex2i(lx+12,ly+12); glVertex2i(lx,ly+12);
    glEnd();
    glColor3f(0.85f,0.85f,0.9f);
    drawText(lx+16, ly, "Clipped Result", GLUT_BITMAP_HELVETICA_10);
}

// ─────────────────────────── draw: pixel log ─────────────────────
static void drawPixelLog() {
    if (!g_showLog) return;

    int px = 10, py = 85, pw = 310, ph = 220;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.04f,0.04f,0.10f,0.90f);
    glBegin(GL_QUADS);
    glVertex2i(px,py); glVertex2i(px+pw,py);
    glVertex2i(px+pw,py+ph); glVertex2i(px,py+ph);
    glEnd();
    glDisable(GL_BLEND);

    glColor3f(0.3f,0.65f,1.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2i(px,py); glVertex2i(px+pw,py);
    glVertex2i(px+pw,py+ph); glVertex2i(px,py+ph);
    glEnd();

    glColor3f(0.45f,0.90f,1.0f);
    drawText(px+6, py+ph-15,
             "Pixel Log – Midpoint Algorithm",
             GLUT_BITMAP_HELVETICA_10);

    // Determine how many rows fit and which row to scroll to
    static const int LINE_H = 13;
    int maxLines = (ph - 32) / LINE_H;

    // Total pixels revealed so far
    int revealed = 0;
    if (g_phase == PHASE_RASTER) revealed = g_animStep;
    else if (g_phase == PHASE_DONE) revealed = (int)g_rasterPixels.size();

    // Scroll so the current step is always visible (bottom-anchored)
    int start = std::max(0, revealed - maxLines);

    for (int i = start; i < revealed && i < (int)g_rasterPixels.size(); ++i) {
        const Pt& p  = g_rasterPixels[i];
        bool inside  = g_pixelInside[i];
        int  row_y   = py + ph - 32 - (i - start) * LINE_H;

        bool isCurrent = (g_phase == PHASE_RASTER && i == g_animStep - 1);
        if (isCurrent) {
            glColor4f(1.0f, 1.0f, 0.25f, 0.22f);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBegin(GL_QUADS);
            glVertex2i(px+2, row_y-2);
            glVertex2i(px+pw-2, row_y-2);
            glVertex2i(px+pw-2, row_y+LINE_H);
            glVertex2i(px+2, row_y+LINE_H);
            glEnd();
            glDisable(GL_BLEND);
        }

        // Colour code by inside/outside
        if (isCurrent)       glColor3f(1.0f, 1.0f, 0.3f);
        else if (inside)     glColor3f(1.0f, 0.65f, 0.2f);
        else                 glColor3f(0.8f, 0.3f, 0.3f);

        char buf[48];
        snprintf(buf, sizeof(buf), " [%2d] (%3d,%3d)  %s",
                 i, p.x, p.y, inside ? "IN " : "OUT");
        drawText(px+6, row_y, buf, GLUT_BITMAP_HELVETICA_10);
    }
}

// ─────────────────────────── draw: HUD ──────────────────────────
static void drawHUD() {
    // ── Top bar ──────────────────────────────────────────────────
    glColor3f(0.10f,0.10f,0.17f);
    glBegin(GL_QUADS);
    glVertex2i(0,      g_vh-HUD_TOP_H);
    glVertex2i(g_vw,   g_vh-HUD_TOP_H);
    glVertex2i(g_vw,   g_vh);
    glVertex2i(0,      g_vh);
    glEnd();

    glColor3f(0.35f,0.82f,1.0f);
    drawText(10, g_vh-21, "Raster Clip Studio", GLUT_BITMAP_HELVETICA_18);
    glColor3f(0.50f,0.50f,0.68f);
    drawText(10, g_vh-39,
             "CSE 4201 | Midpoint Line + Cohen-Sutherland Clipping | RUET",
             GLUT_BITMAP_HELVETICA_10);

    // Active test label (centre of top bar)
    glColor3f(0.80f,0.80f,0.92f);
    std::string tl = "Active: " + std::string(TESTS[g_currentTest].label);
    drawText(g_vw/2 - 150, g_vh-22, tl.c_str(), GLUT_BITMAP_HELVETICA_12);

    // ── Bottom bar ───────────────────────────────────────────────
    glColor3f(0.09f,0.09f,0.14f);
    glBegin(GL_QUADS);
    glVertex2i(0,    0);
    glVertex2i(g_vw, 0);
    glVertex2i(g_vw, HUD_BOT_H);
    glVertex2i(0,    HUD_BOT_H);
    glEnd();

    glColor3f(0.3f,0.7f,1.0f);
    drawText(10, 60, "KEYS:", GLUT_BITMAP_HELVETICA_12);
    glColor3f(0.68f,0.68f,0.80f);
    drawText(10, 46,
             "[1-5] Load Test   [A] Animate   [L] Pixel Log   [R] Reset   [I] Input Mode   [ESC] Quit",
             GLUT_BITMAP_HELVETICA_10);

    // Outcode result
    glColor3f(0.25f,1.0f,0.55f);
    drawText(10, 30, g_outcodeMsg.c_str(), GLUT_BITMAP_HELVETICA_10);

    // Status
    glColor3f(1.0f, 0.85f, 0.28f);
    drawText(10, 14, g_statusMsg.c_str(), GLUT_BITMAP_HELVETICA_10);

    // Pixel / step counter (right side of bottom bar)
    glColor3f(0.55f,0.55f,0.70f);
    if (g_phase == PHASE_RASTER) {
        std::string s = "Step " + intToStr(g_animStep) +
                        " / " + intToStr((int)g_rasterPixels.size());
        drawText(g_vw - 160, 60, s.c_str(), GLUT_BITMAP_HELVETICA_10);
        glColor3f(1.0f,0.4f,0.4f);
        drawText(g_vw - 160, 46, "ANIMATING...", GLUT_BITMAP_HELVETICA_10);
    } else {
        std::string s = "Total pixels: " + intToStr((int)g_rasterPixels.size());
        drawText(g_vw - 160, 60, s.c_str(), GLUT_BITMAP_HELVETICA_10);
        if (g_phase == PHASE_DONE) {
            glColor3f(0.3f,1.0f,0.55f);
            drawText(g_vw - 160, 46, "DONE  [A] replay", GLUT_BITMAP_HELVETICA_10);
        }
    }
}

// ─────────────────────────── draw: input panel ───────────────────
static void drawInputPanel() {
    if (!g_inputMode) return;
    const char* labels[] = {"x1","y1","x2","y2","xmin","ymin","xmax","ymax"};
    const char* groups[]  = {"-- Line endpoints --", "", "", "",
                              "-- Clip window --", "", "", ""};

    int px=10, py=270, pw=260, ph=275;  // taller panel for header spacing
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.04f,0.07f,0.04f,0.93f);
    glBegin(GL_QUADS);
    glVertex2i(px,py); glVertex2i(px+pw,py);
    glVertex2i(px+pw,py+ph); glVertex2i(px,py+ph);
    glEnd();
    glDisable(GL_BLEND);

    glColor3f(0.3f,1.0f,0.5f);
    glBegin(GL_LINE_LOOP);
    glVertex2i(px,py); glVertex2i(px+pw,py);
    glVertex2i(px+pw,py+ph); glVertex2i(px,py+ph);
    glEnd();

    glColor3f(0.4f,1.0f,0.6f);
    drawText(px+6, py+ph-15,
             "Input  (ENTER=next  UP/DN=field  LT/RT=cursor  ESC=cancel)",
             GLUT_BITMAP_HELVETICA_10);


    const int FIELD_H  = 25;   // normal row height
    const int HEADER_PAD = 10; // extra space above a group header row
    int fieldY[8];
    {
        int cursor = py + ph - 34;
        for (int i = 0; i < 8; ++i) {
            if (groups[i][0]) cursor -= HEADER_PAD;  // extra gap above header
            fieldY[i] = cursor;
            cursor -= FIELD_H;
        }
    }

    for (int i=0;i<8;++i) {
        int fy = fieldY[i];
        // Group header (drawn 14px above the field baseline, with bottom gap built in)
        if (groups[i][0]) {
            glColor3f(0.4f,0.75f,1.0f);
            drawText(px+6, fy+14, groups[i], GLUT_BITMAP_HELVETICA_10);
        }
        // Active field highlight
        if (i == g_inputField) {
            glColor3f(1.0f,1.0f,0.3f);
            glBegin(GL_QUADS);
            glVertex2i(px+4,fy-3); glVertex2i(px+pw-4,fy-3);
            glVertex2i(px+pw-4,fy+13); glVertex2i(px+4,fy+13);
            glEnd();
            glColor3f(0.0f,0.0f,0.0f);
        } else {
            glColor3f(0.70f,0.70f,0.82f);
        }
        char buf[64];
        if (i == g_inputField) {
            // Build display string with cursor bar inserted at g_inputCursor
            char left[32] = {}, right[32] = {};
            int l = (int)strlen(g_inputBuf);
            int cur = std::min(g_inputCursor, l);
            strncpy(left,  g_inputBuf,       cur);
            strncpy(right, g_inputBuf + cur, l - cur);
            snprintf(buf, sizeof(buf), " %s = %s|%s", labels[i], left, right);
        } else {
            snprintf(buf, sizeof(buf), " %s = %d", labels[i], g_manualVals[i]);
        }
        drawText(px+6, fy, buf, GLUT_BITMAP_HELVETICA_10);
    }
}

// ─────────────────────────── display callback ────────────────────
static void display() {
    glClearColor(0.07f,0.07f,0.11f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, g_vw, 0, g_vh);   // always matches current viewport
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    drawGrid();
    drawClipWindow();
    drawOriginalLine();   // always visible (FIX ③)
    drawRasterPixels();   // reveals step by step
    drawLegend();
    drawHUD();
    drawPixelLog();
    drawInputPanel();

    glutSwapBuffers();
}

// ─────────────────────────── animation timer ─────────────────────
static void timerCB(int) {
    if (g_phase != PHASE_RASTER) return;
    ++g_animStep;
    if (g_animStep >= (int)g_rasterPixels.size()) {
        g_animStep = (int)g_rasterPixels.size();
        g_phase    = PHASE_DONE;
        g_statusMsg = "Animation complete. Press [A] to replay.";
    }
    glutPostRedisplay();
    if (g_phase == PHASE_RASTER)
        glutTimerFunc(80, timerCB, 0);
}

// ─────────────────────────── special key callback (arrow keys) ───
static void specialKeys(int key, int, int) {
    if (!g_inputMode) return;

    if (key == GLUT_KEY_UP || key == GLUT_KEY_DOWN) {
        // Save current field before jumping
        g_manualVals[g_inputField] = atoi(g_inputBuf);

        if (key == GLUT_KEY_UP)
            g_inputField = (g_inputField > 0) ? g_inputField - 1 : 0;
        else
            g_inputField = (g_inputField < 7) ? g_inputField + 1 : 7;

        // Load the newly selected field's value into the edit buffer
        snprintf(g_inputBuf, sizeof(g_inputBuf), "%d", g_manualVals[g_inputField]);
        g_inputCursor = (int)strlen(g_inputBuf);
    } else if (key == GLUT_KEY_LEFT) {
        if (g_inputCursor > 0) --g_inputCursor;
    } else if (key == GLUT_KEY_RIGHT) {
        int l = (int)strlen(g_inputBuf);
        if (g_inputCursor < l) ++g_inputCursor;
    }

    glutPostRedisplay();
}

// ─────────────────────────── keyboard callback ───────────────────
static void keyboard(unsigned char key, int, int) {
    // ── input mode ───────────────────────────────────────────────
    if (g_inputMode) {
        if (key == 27) { g_inputMode=false; g_inputCursor=0; memset(g_inputBuf,0,sizeof(g_inputBuf)); glutPostRedisplay(); return; }
        if (key == 13) {
            // Commit current field value
            g_manualVals[g_inputField] = atoi(g_inputBuf);
            memset(g_inputBuf,0,sizeof(g_inputBuf));
            g_inputCursor = 0;
            ++g_inputField;
            if (g_inputField >= 8) {
                g_x1=g_manualVals[0]; g_y1=g_manualVals[1];
                g_x2=g_manualVals[2]; g_y2=g_manualVals[3];
                g_xmin=g_manualVals[4]; g_ymin=g_manualVals[5];
                g_xmax=g_manualVals[6]; g_ymax=g_manualVals[7];
                computeScene();
                g_inputMode=false; g_inputField=0;
                g_phase=PHASE_IDLE; g_animStep=0;
                g_statusMsg="Manual values applied. Press [A] to animate.";
            } else {
                // Pre-load next field's existing value into the buffer
                snprintf(g_inputBuf, sizeof(g_inputBuf), "%d", g_manualVals[g_inputField]);
                g_inputCursor = (int)strlen(g_inputBuf);
            }
        } else if (key==8||key==127) {
            // Backspace: delete character left of cursor
            int l=(int)strlen(g_inputBuf);
            if (g_inputCursor > 0 && l > 0) {
                memmove(g_inputBuf + g_inputCursor - 1,
                        g_inputBuf + g_inputCursor,
                        l - g_inputCursor + 1);
                --g_inputCursor;
            }
        } else if ((key>='0'&&key<='9')||key=='-') {
            int l=(int)strlen(g_inputBuf);
            if (l < 10) {
                // Insert character at cursor position
                memmove(g_inputBuf + g_inputCursor + 1,
                        g_inputBuf + g_inputCursor,
                        l - g_inputCursor + 1);
                g_inputBuf[g_inputCursor] = key;
                ++g_inputCursor;
            }
        }
        glutPostRedisplay();
        return;
    }

    // ── normal mode ──────────────────────────────────────────────
    switch (key) {
    case '1': case '2': case '3': case '4': case '5':
        g_currentTest = key-'1';
        loadTest(g_currentTest);
        break;

    case 'a': case 'A':
        // FIX ③: [A] starts / replays step-by-step rasterization
        g_phase    = PHASE_RASTER;
        g_animStep = 0;
        g_statusMsg= "Animating Midpoint rasterization...  (orange=inside, red=clipped away)";
        glutTimerFunc(80, timerCB, 0);
        break;

    case 'l': case 'L':
        g_showLog = !g_showLog;
        break;

    case 'r': case 'R':
        loadTest(g_currentTest);
        g_showLog=false;
        g_statusMsg="Reset.";
        break;

    case 'i': case 'I':
        g_inputMode=true; g_inputField=0;
        g_manualVals[0]=g_x1; g_manualVals[1]=g_y1;
        g_manualVals[2]=g_x2; g_manualVals[3]=g_y2;
        g_manualVals[4]=g_xmin; g_manualVals[5]=g_ymin;
        g_manualVals[6]=g_xmax; g_manualVals[7]=g_ymax;
        // Pre-load first field's current value so user can edit immediately
        snprintf(g_inputBuf, sizeof(g_inputBuf), "%d", g_manualVals[0]);
        g_inputCursor = (int)strlen(g_inputBuf);
        break;

    case 27:
        exit(0);
    }
    glutPostRedisplay();
}

// ─────────────────────────── main ────────────────────────────────
int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(WIN_W, WIN_H);

    int sw = glutGet(GLUT_SCREEN_WIDTH);
    int sh = glutGet(GLUT_SCREEN_HEIGHT);
    if (sw > 0 && sh > 0)
        glutInitWindowPosition((sw - WIN_W) / 2, (sh - WIN_H) / 2);
    else
        glutInitWindowPosition(160, 100);

    glutCreateWindow("Raster Clip Studio – CSE 4201 | RUET");

    loadTest(0);
    recalcGrid();   // set initial grid origin before first frame

    glutDisplayFunc(display);
    // reshape: save new size, recentre the grid, repaint
    glutReshapeFunc([](int w, int h){
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        g_vw = w;
        g_vh = h;
        glViewport(0, 0, w, h);
        recalcGrid();
        glutPostRedisplay();
    });
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(specialKeys);

    printf("=========================================\n");
    printf("  Raster Clip Studio  –  CSE 4201 RUET  \n");
    printf("=========================================\n");
    printf("  [1-5]  Load test case\n");
    printf("  [A]    Animate rasterization step-by-step\n");
    printf("  [L]    Toggle pixel coordinate log\n");
    printf("  [I]    Manual input (line + clip window)\n");
    printf("  [R]    Reset current test\n");
    printf("  [ESC]  Quit\n");
    printf("=========================================\n");
    printf("  Colour key:\n");
    printf("   Grey line  = original line (always visible)\n");
    printf("   Orange px  = inside clip window\n");
    printf("   Red   px   = outside (clipped away)\n");
    printf("   Green px   = accepted clipped segment\n");
    printf("=========================================\n");

    glutMainLoop();
    return 0;
}
