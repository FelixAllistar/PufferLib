# conifers hill-climb log

Track: real, dense, tall conifers (dawn redwood, araucaria, podocarp, cheirolep cypress).
Each line: pass, the worst fault seen, the change, kept or reverted, and why.

Triangles at the start (full / reduced, from the gallery labels): redwood v2 15762 / 1976, v3 14729 / 1928, v7 15050 / 1964;
araucaria v0 12902 / 3546, v4 12684 / 3430; podocarp v1 37610 / 3748, v5 54966 / 4536; cheirolep v6 8442 / 992.

1. Fault: every conifer is half its real height (redwood 22-30 m, araucaria 15-23, podocarp 12-18, cheirolep 6-10). Change: mesh heights redwood 32-40 m, araucaria 26-34, podocarp 17-22, cheirolep 15-22 (instances scale 0.7-1.25 round that), trunk radius per metre of height lowered so girth stays about 1/25-1/35 of height. Kept: the brief's sizes; crowns grow with the height, the cypress and podocarp triangle counts with them (fixed in later passes).
2. Fault: the redwood sprays carry flat brown-green bars. Cause: the fir twig set's first cut is a strip of trunk bark (aspect 0.21), stamped into L_SPRAY and L_CL_SPRAY like a twig. Change: drop spray sprites narrower than 0.45 (render/veg.c photos_load). Kept: sprays read finer and feathery, the bars are gone, nothing else uses the set.
