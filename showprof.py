#!/usr/bin/env python
###############################################################################
#
# showprof.py -- Show profiling results for pvsim.py.
#
###############################################################################

import pstats

p = pstats.Stats("pvsim.profile")
p.strip_dirs()
p.sort_stats("time").print_callers(100)

##p.print_callees("Capture")
