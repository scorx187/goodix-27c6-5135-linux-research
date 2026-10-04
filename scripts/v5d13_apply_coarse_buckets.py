#!/usr/bin/env python3
from pathlib import Path

p = Path('/home/sam/libfprint/libfprint/fpi-print.c')
s = p.read_text()

anchor = '''  return FPI_MATCH_FAIL;\n}\n\n/**\n * fpi_print_sigfm_match:\n'''
insert = '''  return FPI_MATCH_FAIL;\n}\n\nstatic const gchar *\nsigfm_coarse_score_bucket (gint score)\n{\n  if (score <= 0)\n    return "ZERO";\n\n  if (score <= 4)\n    return "ONE_TO_FOUR";\n\n  if (score <= 9)\n    return "FIVE_TO_NINE";\n\n  if (score <= 19)\n    return "TEN_TO_NINETEEN";\n\n  return "TWENTY_PLUS";\n}\n\n/**\n * fpi_print_sigfm_match:\n'''
if s.count(anchor) != 1:
    raise SystemExit('PATCH_FAIL=anchor')
s = s.replace(anchor, insert, 1)

old_decl = '''{\n  SigfmImgInfo *probe;\n\n  if (template == NULL ||\n'''
new_decl = '''{\n  SigfmImgInfo *probe;\n  gboolean coarse_diagnostics;\n  gboolean matched = FALSE;\n  gint best_score = 0;\n\n  coarse_diagnostics =\n    g_strcmp0 (g_getenv ("LIBFPRINT_SIGFM_COARSE_DIAGNOSTICS"),\n               "COARSE_ONLY") == 0;\n\n  if (template == NULL ||\n'''
if s.count(old_decl) != 1:
    raise SystemExit('PATCH_FAIL=decl')
s = s.replace(old_decl, new_decl, 1)

old_tail = '''      if (score >= threshold)\n        return FPI_MATCH_SUCCESS;\n    }\n\n  return FPI_MATCH_FAIL;\n}\n'''
new_tail = '''      if (score > best_score)\n        best_score = score;\n\n      if (score >= threshold)\n        {\n          if (!coarse_diagnostics)\n            return FPI_MATCH_SUCCESS;\n\n          matched = TRUE;\n        }\n    }\n\n  if (coarse_diagnostics)\n    g_print ("SIGFM_MATCH_BEST_BUCKET=%s\\n",\n             sigfm_coarse_score_bucket (best_score));\n\n  return matched ? FPI_MATCH_SUCCESS : FPI_MATCH_FAIL;\n}\n'''
if s.count(old_tail) != 1:
    raise SystemExit('PATCH_FAIL=tail')
s = s.replace(old_tail, new_tail, 1)

p.write_text(s)
print('V5D13_PATCH=PASS')
