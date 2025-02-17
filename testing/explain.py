# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0.  If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# Copyright 2024 MonetDB Foundation;
# Copyright August 2008 - 2023 MonetDB B.V.;
# Copyright 1997 - July 2008 CWI.

import re

def filter(tab):
    res = []
    for row in tab:
        if row[0].find('usec') < 0:
            res.append(row)
    return res

def function_histogram(tab):
    histo = {}
    for row in tab:
        if row[0].find('usec') < 0:
            g = re.match(r'^[^#].*\s([a-zA-Z_][a-zA-Z_0-9]*\.[a-zA-Z_][a-zA-Z_0-9]*)\(.*;', row[0])
            if g:
                f = g.group(1)
                if f in histo:
                    histo[f]+=1
                else:
                    histo[f]=1
    nhisto = []
    for key,val in histo.items():
        nhisto.append((key, str(val)))
    return sorted(nhisto)

# Returns functions returning more than one parameter in the MAL plan, at the moment, all returning parameters must be bats
def function_with_more_than_one_result_bat(tab):
    histo = {}
    for row in tab:
        if row[0].find('usec') < 0:
            g = re.match(r'^[^#].*\(([A-Z]\_[0-9]+:bat\[:[a-z]+\],?\ ?)+\)\ :=\ .*;', row[0])
            if g:
                g2 = re.match(r'^[^#].*\s([a-zA-Z_][a-zA-Z_0-9]*\.[a-zA-Z_][a-zA-Z_0-9]*)\(.*;', row[0])
                if g2:
                    f = g2.group(1)
                    if f in histo:
                        histo[f]+=1
                    else:
                        histo[f]=1
    nhisto = []
    for key,val in histo.items():
        nhisto.append((key, str(val)))
    return sorted(nhisto)
