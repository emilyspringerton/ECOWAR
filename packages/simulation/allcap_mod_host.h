/* allcap_mod_host.h -- real extern declaration for allcap_mod.c's own one exported entry point
 * (EMILY/BACKLOG.md SECTION 381, "the wincon mod ... the ALLCAP mod"). Same "-include this header
 * before compiling the generated C" pattern every mod host header in this repo already
 * establishes.
 *
 * on_allcap_check is pure PARENA logic (no #target/inline-c) -- no REFLUX/runtime include needed
 * here, unlike every trigger-only mod's own host header.
 */
#ifndef ALLCAP_MOD_HOST_H
#define ALLCAP_MOD_HOST_H

extern int on_allcap_check(int owned_count, int total_count);

#endif /* ALLCAP_MOD_HOST_H */
