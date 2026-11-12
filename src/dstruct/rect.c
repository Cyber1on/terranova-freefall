/* rect.c -- x:\prj\tech\libsrc\dstruct\rect.c (Terra Nova v1.09): rectangle utilities.
 * Decompiled from __FF.EXE.
 *
 *   0x2F2850 RectTestSect   0x2F2884 RectSect   0x2F290C RectUnion   0x2F2960 RectEncloses
 *   0x2F2994 RectTestPt     0x2F29C8 RectMove   0x2F2A10 RectOffsettedRect   0x2F2A4C RectClipCode
 *
 * As System Shock.  Rectangles are half-open: lr is outside.  LGPoint arguments are passed
 * by value in one register (x low word, y high word).
 */
#include "sup_a.h"

#define RECT_TEST_SECT(r1, r2) ((r1)->ul.y < (r2)->lr.y && (r1)->lr.y > (r2)->ul.y && \
                                (r1)->ul.x < (r2)->lr.x && (r1)->lr.x > (r2)->ul.x)
#define lg_max(a, b) ((a) > (b) ? (a) : (b))
#define lg_min(a, b) ((a) < (b) ? (a) : (b))

int RectTestSect(LGRect *pr1, LGRect *pr2)
{
    return RECT_TEST_SECT(pr1, pr2);
}

/* Intersection; returns 0 (prsect untouched) if the rectangles don't overlap. */
int RectSect(LGRect *pr1, LGRect *pr2, LGRect *prsect)
{
    if (!RECT_TEST_SECT(pr1, pr2))
        return 0;
    prsect->ul.x = lg_max(pr1->ul.x, pr2->ul.x);
    prsect->lr.x = lg_min(pr1->lr.x, pr2->lr.x);
    prsect->ul.y = lg_max(pr1->ul.y, pr2->ul.y);
    prsect->lr.y = lg_min(pr1->lr.y, pr2->lr.y);
    return 1;
}

void RectUnion(LGRect *pr1, LGRect *pr2, LGRect *prunion)
{
    prunion->ul.x = lg_min(pr1->ul.x, pr2->ul.x);
    prunion->lr.x = lg_max(pr1->lr.x, pr2->lr.x);
    prunion->ul.y = lg_min(pr1->ul.y, pr2->ul.y);
    prunion->lr.y = lg_max(pr1->lr.y, pr2->lr.y);
}

/* TRUE if pr1 contains pr2. */
int RectEncloses(LGRect *pr1, LGRect *pr2)
{
    return pr1->ul.y <= pr2->ul.y && pr1->lr.y >= pr2->lr.y &&
           pr1->ul.x <= pr2->ul.x && pr1->lr.x >= pr2->lr.x;
}

int RectTestPt(LGRect *prect, LGPoint pt)
{
    return pt.y >= prect->ul.y && pt.y < prect->lr.y &&
           pt.x >= prect->ul.x && pt.x < prect->lr.x;
}

void RectMove(LGRect *pr, LGPoint delta)
{
    pr->ul.x += delta.x;
    pr->ul.y += delta.y;
    pr->lr.x += delta.x;
    pr->lr.y += delta.y;
}

void RectOffsettedRect(LGRect *pr, LGPoint delta, LGRect *proff)
{
    proff->ul.x = pr->ul.x + delta.x;
    proff->ul.y = pr->ul.y + delta.y;
    proff->lr.x = pr->lr.x + delta.x;
    proff->lr.y = pr->lr.y + delta.y;
}

/* Outcode of pt: 1 left, 2 right, 4 above, 8 below. */
int RectClipCode(LGRect *pr, LGPoint pt)
{
    short code = 0;

    if (pt.x < pr->ul.x)
        code = 1;
    if (pt.x >= pr->lr.x)
        code |= 2;
    if (pt.y < pr->ul.y)
        code |= 4;
    if (pt.y >= pr->lr.y)
        code |= 8;
    return code;
}
