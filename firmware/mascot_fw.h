// ============================================================================
// mascot_fw.h - On-device pixel mascot ("Charm") drawing.
//   Draws the same character family as the phone app, directly with gfx
//   primitives at device scale. Used by the Tamagotchi module.
//   Faces: HAPPY, NEUTRAL, SLEEPY, SAD, HUNGRY, EXCITED.
// ============================================================================
#pragma once

#include "platform.h"

enum MascotFace { F_HAPPY, F_NEUTRAL, F_SLEEPY, F_SAD, F_HUNGRY, F_EXCITED, F_DIZZY };

// Body palette
#define M_BODY     C_GOLD
#define M_BODY_TOP rgb(255, 224, 130)
#define M_BODY_DK  rgb(216, 138, 40)
#define M_BELLY    rgb(255, 242, 195)
#define M_GLOSS    rgb(255, 252, 230)
#define M_OUTLINE  rgb(40, 26, 12)
#define M_CHEEK    rgb(255, 122, 146)
#define M_EYE_W    rgb(255, 255, 255)
#define M_IRIS     rgb(70, 142, 240)
#define M_IRIS_DK  rgb(40, 96, 200)
#define M_PUPIL    rgb(16, 14, 24)

// Draw the mascot centered at (cx, cy) with overall scale s (pixels per unit).
// bob shifts vertically. pupilDX/DY move gaze. leanX shifts the UPPER body
// (head/arms/face/antenna) sideways while the feet stay planted -> a "leaning"
// illusion driven by device tilt (no true rotation needed).
static void mascotDraw(int cx, int cy, float s, MascotFace face,
                       int bob, int pupilDX, int pupilDY, int leanX = 0) {
  cy += bob;
  int cx2 = cx + leanX;           // leaning upper-body center
  int R = (int)(70 * s);          // body radius
  int armR = (int)(14 * s);
  // feet (stay planted at cx)
  gfx->fillRoundRect(cx - (int)(40*s), cy + (int)(58*s), (int)(28*s), (int)(16*s), (int)(6*s), M_BODY_DK);
  gfx->fillRoundRect(cx + (int)(12*s), cy + (int)(58*s), (int)(28*s), (int)(16*s), (int)(6*s), M_BODY_DK);
  // rounded ears (behind body, lean with the head)
  int earY = cy - R + (int)(6*s), earX = (int)(44*s), earR = (int)(20*s);
  gfx->fillCircle(cx2 - earX, earY, earR, M_OUTLINE);
  gfx->fillCircle(cx2 + earX, earY, earR, M_OUTLINE);
  gfx->fillCircle(cx2 - earX, earY, earR - (int)(3*s), M_BODY);
  gfx->fillCircle(cx2 + earX, earY, earR - (int)(3*s), M_BODY);
  gfx->fillCircle(cx2 - earX, earY, (int)(8*s), M_CHEEK);   // inner ear blush
  gfx->fillCircle(cx2 + earX, earY, (int)(8*s), M_CHEEK);
  // body (leans)
  gfx->fillCircle(cx2, cy, R + (int)(4*s), M_OUTLINE);
  gfx->fillCircle(cx2, cy, R, M_BODY);
  gfx->fillCircle(cx2, cy - (int)(14*s), R - (int)(8*s), M_BODY_TOP);
  // soft glossy highlight (top-left sheen)
  gfx->fillCircle(cx2 - (int)(30*s), cy - (int)(34*s), (int)(16*s), M_BODY_TOP);
  gfx->fillCircle(cx2 - (int)(34*s), cy - (int)(38*s), (int)(7*s), M_GLOSS);
  // belly
  gfx->fillCircle(cx2, cy + (int)(22*s), (int)(34*s), M_BELLY);
  // arms (with outline)
  gfx->fillCircle(cx2 - R - (int)(2*s), cy + (int)(6*s), armR + (int)(2*s), M_OUTLINE);
  gfx->fillCircle(cx2 + R + (int)(2*s), cy + (int)(6*s), armR + (int)(2*s), M_OUTLINE);
  gfx->fillCircle(cx2 - R - (int)(2*s), cy + (int)(6*s), armR, M_BODY);
  gfx->fillCircle(cx2 + R + (int)(2*s), cy + (int)(6*s), armR, M_BODY);
  // antenna (leans a bit more than the body for a top-heavy feel)
  int antx = cx2 + leanX / 2;
  gfx->drawLine(cx2, cy - R, antx, cy - R - (int)(22*s), M_OUTLINE);
  gfx->drawLine(cx2 + 1, cy - R, antx + 1, cy - R - (int)(22*s), M_OUTLINE);
  gfx->fillCircle(antx, cy - R - (int)(26*s), (int)(8*s), M_OUTLINE);
  gfx->fillCircle(antx, cy - R - (int)(26*s), (int)(6*s), M_CHEEK);
  gfx->fillCircle(antx - (int)(2*s), cy - R - (int)(28*s), (int)(2*s), M_GLOSS);

  cx = cx2;                       // face elements below all lean with the body
  int ex = (int)(28 * s);     // eye spacing from center
  int ey = cy - (int)(10 * s);
  int eR = (int)(18 * s);     // eye radius (bigger = cuter)

  // big glossy kawaii eye: dark iris, large soft pupil, two sparkles
  auto eyeOpen = [&](int x) {
    gfx->fillCircle(x, ey, eR + (int)(1*s), M_OUTLINE);
    gfx->fillCircle(x, ey, eR, M_EYE_W);
    gfx->fillCircle(x + pupilDX, ey + pupilDY, (int)(11*s), M_IRIS);
    gfx->fillCircle(x + pupilDX, ey + pupilDY + (int)(2*s), (int)(8*s), M_IRIS_DK);
    gfx->fillCircle(x + pupilDX, ey + pupilDY + (int)(1*s), (int)(5*s), M_PUPIL);
    gfx->fillCircle(x + pupilDX - (int)(3*s), ey + pupilDY - (int)(3*s), (int)(3*s), M_EYE_W);
    gfx->fillCircle(x + pupilDX + (int)(3*s), ey + pupilDY + (int)(4*s), (int)(2*s), M_EYE_W);
  };
  auto eyeClosed = [&](int x) {     // happy upward curve  ^
    int t = max(1, (int)(3*s));
    gfx->fillRect(x - eR, ey + (int)(2*s), (int)(8*s), t, M_OUTLINE);
    gfx->fillRect(x - (int)(4*s), ey - (int)(2*s), (int)(8*s), t, M_OUTLINE);
    gfx->fillRect(x + (int)(4*s), ey + (int)(2*s), (int)(8*s), t, M_OUTLINE);
  };
  auto eyeBig = [&](int x) {        // sparkling star-struck eyes
    gfx->fillCircle(x, ey, eR + (int)(4*s), M_OUTLINE);
    gfx->fillCircle(x, ey, eR + (int)(3*s), M_EYE_W);
    gfx->fillCircle(x, ey, (int)(9*s), M_IRIS);
    gfx->fillCircle(x, ey + (int)(1*s), (int)(6*s), M_PUPIL);
    gfx->fillCircle(x - (int)(4*s), ey - (int)(4*s), (int)(4*s), M_EYE_W);
    gfx->fillCircle(x + (int)(4*s), ey + (int)(5*s), (int)(2*s), M_EYE_W);
  };
  auto eyeDizzy = [&](int x) {        // swirly @_@ eye
    gfx->fillCircle(x, ey, eR, M_EYE_W);
    gfx->drawCircle(x, ey, eR, M_OUTLINE);
    for (int r = (int)(3*s); r < eR; r += max(2, (int)(3*s)))
      gfx->drawCircle(x, ey, r, M_OUTLINE);
  };

  // big round rosy cheeks (always glowing) with a soft highlight
  int chkR = (int)(13*s), chkY = cy + (int)(8*s), chkX = (int)(50*s);
  gfx->fillCircle(cx - chkX, chkY, chkR, M_CHEEK);
  gfx->fillCircle(cx + chkX, chkY, chkR, M_CHEEK);
  gfx->fillCircle(cx - chkX - (int)(3*s), chkY - (int)(3*s), (int)(3*s), M_GLOSS);
  gfx->fillCircle(cx + chkX - (int)(3*s), chkY - (int)(3*s), (int)(3*s), M_GLOSS);

  int mx = cx, my = cy + (int)(20 * s);

  // --- expression helpers: eyebrows + curved mouth carry most human emotion ---
  int browY = ey - eR - (int)(7*s);
  // Angled eyebrow above eye `x`. innerDY/outerDY (in units) shift the inner
  // (toward face center) and outer ends: inner-up = worried/sad, inner-down =
  // angry, both-up = surprised, flat = calm. Negative = up.
  auto brow = [&](int x, int innerDY, int outerDY) {
    int dir = (x < cx) ? 1 : -1;            // inner end points toward center
    int xi = x + dir * (int)(7*s);
    int xo = x - dir * (int)(8*s);
    int yi = browY + (int)(innerDY*s);
    int yo = browY + (int)(outerDY*s);
    int th = max(2, (int)(4*s));
    for (int t = 0; t < th; t++) gfx->drawLine(xi, yi + t, xo, yo + t, M_OUTLINE);
  };
  // Smooth mouth curve centered at mx. depth>0 = smile (U), depth<0 = frown.
  auto mouthCurve = [&](int y, int w, int depth) {
    int th = max(2, (int)(3*s));
    int px = mx - w, py = y;
    for (int i = 1; i <= 12; i++) {
      float t = i / 12.0f;
      int qx = mx - w + (int)(2 * w * t);
      int qy = y + (int)(depth * s * sinf(t * 3.14159f));
      for (int k = 0; k < th; k++) gfx->drawLine(px, py + k, qx, qy + k, M_OUTLINE);
      px = qx; py = qy;
    }
  };

  switch (face) {
    case F_HAPPY:
      brow(cx - ex, -2, -3); brow(cx + ex, -2, -3);   // softly raised, content
      eyeOpen(cx - ex); eyeOpen(cx + ex);
      // wide open smile with a peek of tongue
      mouthCurve(my + (int)(1*s), (int)(18*s), 9);
      gfx->fillCircle(mx, my + (int)(9*s), (int)(5*s), M_CHEEK);
      break;
    case F_NEUTRAL:
      brow(cx - ex, 0, 0); brow(cx + ex, 0, 0);       // calm, level
      eyeOpen(cx - ex); eyeOpen(cx + ex);
      mouthCurve(my + (int)(4*s), (int)(11*s), 2);     // faint pleasant smile
      break;
    case F_SLEEPY:
      brow(cx - ex, 3, 4); brow(cx + ex, 3, 4);        // heavy, drooping
      eyeClosed(cx - ex); eyeClosed(cx + ex);
      // small relaxed open mouth (breathing)
      gfx->drawCircle(mx, my + (int)(6*s), (int)(5*s), M_OUTLINE);
      // Z's
      textCenter("z", cx + (int)(60*s), cy - (int)(60*s), max(1,(int)(2*s)), C_TEXT);
      textCenter("Z", cx + (int)(74*s), cy - (int)(78*s), max(1,(int)(3*s)), C_TEXT);
      break;
    case F_SAD:
      brow(cx - ex, -6, 2); brow(cx + ex, -6, 2);      // inner raised = sorrow
      eyeOpen(cx - ex); eyeOpen(cx + ex);
      mouthCurve(my + (int)(9*s), (int)(13*s), -7);     // downturned frown
      // welling tear
      gfx->fillCircle(cx - ex, ey + (int)(20*s), (int)(4*s), M_IRIS);
      gfx->fillCircle(cx - ex - (int)(1*s), ey + (int)(18*s), (int)(2*s), M_GLOSS);
      break;
    case F_HUNGRY:
      brow(cx - ex, -4, 1); brow(cx + ex, -4, 1);      // pleading
      eyeOpen(cx - ex); eyeOpen(cx + ex);
      mouthCurve(my + (int)(6*s), (int)(12*s), -3);     // slight worried droop
      // little drool drop
      gfx->fillCircle(mx + (int)(8*s), my + (int)(12*s), (int)(2*s), M_IRIS);
      break;
    case F_EXCITED:
      brow(cx - ex, -8, -7); brow(cx + ex, -8, -7);    // brows shot up, thrilled
      eyeBig(cx - ex); eyeBig(cx + ex);
      // big open beaming mouth
      gfx->fillCircle(mx, my + (int)(7*s), (int)(11*s), M_OUTLINE);
      gfx->fillCircle(mx, my + (int)(5*s), (int)(7*s), M_CHEEK);
      gfx->fillRect(mx - (int)(8*s), my, (int)(16*s), (int)(3*s), M_EYE_W); // teeth
      break;
    case F_DIZZY:
      brow(cx - ex, -2, 3); brow(cx + ex, 3, -2);      // crooked, disoriented
      eyeDizzy(cx - ex); eyeDizzy(cx + ex);
      // wavy woozy mouth
      gfx->fillRect(mx - (int)(14*s), my + (int)(6*s), (int)(10*s), (int)(4*s), M_OUTLINE);
      gfx->fillRect(mx + (int)(4*s),  my + (int)(2*s), (int)(10*s), (int)(4*s), M_OUTLINE);
      break;
  }
}
