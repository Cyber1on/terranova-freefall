/* hdrcheck.c -- header check (added at the G2 merge).  Every shared header must be includable
 * together: one declaration per symbol, in its owner's header (the worker that decompiled it,
 * or game.h for widely shared ones); other headers include the owner's header.  A header may
 * keep a copy of a callee's declaration only when an include cycle prevents including the
 * owner, and the copy must be identical (this check catches any difference).  verify/build.sh
 * compiles this file with -fsyntax-only before building, so a conflicting declaration stops
 * the build.  When a batch is merged (or a worker adds a header), add its headers here.
 */
#include "game.h"
#include "2d.h"
#include "2d_prim.h"
#include "2d_tmap.h"
#include "3d.h"
#include "3d_a.h"
#include "3d_b.h"
#include "3d_c.h"
#include "edms.h"
#include "edms_a.h"
#include "edms_b.h"
#include "edms_c.h"
#include "fix.h"
#include "g1_a.h"
#include "g1_b.h"
#include "g1_c.h"
#include "g2_a.h"
#include "g2_b.h"
#include "g2_b2.h"
#include "g2_c.h"
#include "g3_a.h"
#include "g3_b.h"
#include "g3_c.h"
#include "g4_a.h"
#include "g4_b.h"
#include "g4_c1.h"
#include "g4_c2.h"
#include "g5_a.h"
#include "g5_b.h"
#include "g5_c.h"
#include "g6_a.h"
#include "g6_b.h"
#include "g6_c.h"
#include "g7_a.h"
#include "g7_c.h"
#include "lg.h"
#include "lg_types.h"
#include "lzw.h"
#include "res.h"
#include "resbuild.h"
#include "resmem.h"
#include "sup_a.h"
#include "sup_b.h"
#include "sup_c.h"
#include "support.h"
#include "t1_a.h"
#include "t1_b.h"
#include "t1_c.h"
#include "t2_a.h"
#include "t2_b.h"
#include "t2_c.h"
#include "g7_b.h"
#include "g8_a.h"
#include "g8_b.h"

#include "g8_c.h"
#include "g9_a.h"
#include "g9_b.h"
#include "g9_c.h"     /* (includes g9_b.h) */
#include "g10_a.h"
#include "g10_b.h"    /* (includes g10_a.h) */
#include "g10_c.h"    /* (includes g10_b.h) */

int hdrcheck_dummy;
