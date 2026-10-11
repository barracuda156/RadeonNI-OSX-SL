/*
 * Rewrites of ARB program text that Apple's compiler accepts and Mesa's
 * does not.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_ARBTEXT_H
#define RDN_ARBTEXT_H

/*
 * Apple's ARB programs may ALIAS a binding or a component of a constant:
 *
 *   ALIAS v0 = vertex.attrib[0];       -> ATTRIB v0 = ...;
 *   ALIAS v0 = fragment.color.primary; -> ATTRIB v0 = ...;
 *   ALIAS c4 = program.env[4];         -> PARAM c4 = ...;
 *   ALIAS o = result.color;            -> OUTPUT o = ...;
 *   PARAM k = { 1, 2, 0, 0 }; ALIAS one = k.x;
 *                                      -> PARAM one = { 1, 1, 1, 1 };
 *
 * Mesa's parser only takes an existing variable after ALIAS (Sid Meier's
 * Pirates!'s Direct3D layer writes all of these; without the rewrite the
 * program is refused and the meshes that use it are drawn with no program).
 *
 * Returns a malloc'd copy of text with the statements rewritten, and sets
 * *len, or NULL when nothing needed rewriting (text and *len are
 * unchanged).
 */
char *rdn_arb_alias_rewrite(const char *text, int *len);

#endif
