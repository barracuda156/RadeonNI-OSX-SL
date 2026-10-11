/*
 * Test of the ARB program text rewrite (gld/rdn_arbtext.c): the ALIAS forms
 * Sid Meier's Pirates! writes, which Mesa's parser refuses.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../gld/rdn_arbtext.c"

static int failures;

static void check(const char *name, const char *in, const char *want)
{
	int len = (int)strlen(in);
	char *got = rdn_arb_alias_rewrite(in, &len);
	const char *have = got ? got : in;

	if (strcmp(have, want) || (got && len != (int)strlen(want))) {
		printf("FAIL %s:\n--- got\n%s\n--- want\n%s\n", name, have, want);
		failures++;
	} else {
		printf("ok   %s\n", name);
	}
	free(got);
}

int main(void)
{
	check("vertex attrib",
	      "!!ARBvp1.0\n ALIAS v0  =  vertex.attrib[0];\n#register_usage: v0\nEND\n",
	      "!!ARBvp1.0\n ATTRIB v0 = vertex.attrib[0];\n#register_usage: v0\nEND\n");
	check("fragment color",
	      "ALIAS v0_Temp =  fragment.color.primary;\n TEMP v0;\n",
	      "ATTRIB v0_Temp = fragment.color.primary;\n TEMP v0;\n");
	check("program env",
	      "ALIAS c4 = program.env[4] ;\nMUL r0, r1, c4;\n",
	      "PARAM c4 = program.env[4] ;\nMUL r0, r1, c4;\n");
	check("constant component",
	      "PARAM constant = { 1.0, 2.0, 0.0, 0.0 };\nALIAS one = constant.x;\nALIAS two = constant.y;\n",
	      "PARAM constant = { 1.0, 2.0, 0.0, 0.0 };\nPARAM one = { 1, 1, 1, 1 };\nPARAM two = { 2, 2, 2, 2 };\n");
	check("already fine: alias of a variable",
	      "TEMP r0;\nALIAS q = r0;\nMOV q, r0;\n", "TEMP r0;\nALIAS q = r0;\nMOV q, r0;\n");
	check("commented out is left",
	      "#ALIAS c1 = program.env[1] ;\nEND\n", "#ALIAS c1 = program.env[1] ;\nEND\n");
	check("an alias of a temp component is left",
	      "TEMP t;\nALIAS u = t.x;\n", "TEMP t;\nALIAS u = t.x;\n");
	check("statement then comment on the line",
	      "ALIAS v1 = vertex.attrib[1]; # normal\nEND\n",
	      "ATTRIB v1 = vertex.attrib[1]; # normal\nEND\n");
	check("no trailing newline",
	      "ALIAS v1 = vertex.attrib[1];", "ATTRIB v1 = vertex.attrib[1];");
	if (failures) {
		printf("FAIL arb_alias: %d\n", failures);
		return 1;
	}
	printf("PASS arb_alias\n");
	return 0;
}
