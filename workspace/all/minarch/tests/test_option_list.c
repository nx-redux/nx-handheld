// Host test for ma_option_list.c: legacy retro_variable parsing and option lookups.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ma_option_list.h"

static int fails = 0;
#define CHECK(cond, msg)               \
	do {                               \
		if (cond) {                    \
			printf("PASS: %s\n", msg); \
		} else {                       \
			printf("FAIL: %s\n", msg); \
			fails++;                   \
		}                              \
	} while (0)

static int streq(const char* a, const char* b) {
	return a && b && !strcmp(a, b);
}

int main(void) {
	Option o;

	// "Name; a|b|c"
	memset(&o, 0, sizeof(o));
	Option_initVar(&o, "core_speed", "Speed; slow|normal|fast");
	CHECK(streq(o.key, "core_speed") && streq(o.name, "Speed"), "name before \"; \"");
	CHECK(o.count == 3 && streq(o.values[0], "slow") && streq(o.values[1], "normal") && streq(o.values[2], "fast") &&
			  !o.values[3],
		  "three values, NULL-terminated");
	CHECK(o.labels[0] == o.values[0] && o.labels[2] == o.values[2] && !o.labels[3], "labels are the values");
	CHECK(o.value == 0 && o.default_value == 0, "first value is the default");
	CHECK(Option_getValueIndex(&o, "fast") == 2 && Option_getValueIndex(&o, "slow") == 0, "value index");
	CHECK(Option_getValueIndex(&o, "warp") == 0, "unknown value: index 0");
	CHECK(Option_getValueIndex(&o, NULL) == 0 && Option_getValueIndex(NULL, "fast") == 0, "NULL value or option: 0");
	CHECK(Option_valueCount(&o) == 3, "value count");
	Option_free(&o); // ASan: name aliases var, freed once

	// a static frontend list: counted up to its NULL, none without values
	{
		char* labels[] = {"None", "Line", "Grid", NULL};
		Option fe = {.values = labels, .labels = labels};
		CHECK(Option_valueCount(&fe) == 3, "NULL-terminated values counted");
		fe.values = NULL;
		CHECK(Option_valueCount(&fe) == 0, "no values: 0");
	}

	// a single value has no '|'
	memset(&o, 0, sizeof(o));
	Option_initVar(&o, "k", "Only; one");
	CHECK(o.count == 1 && streq(o.values[0], "one") && !o.values[1], "single value");
	Option_free(&o);

	// missing "; ": the key names it, the whole string is the values
	memset(&o, 0, sizeof(o));
	Option_initVar(&o, "core_k", "a|b");
	CHECK(streq(o.name, "core_k") && o.name != o.var, "no separator: named after the key");
	CHECK(o.count == 2 && streq(o.values[0], "a") && streq(o.values[1], "b"), "no separator: values from the start");
	Option_free(&o); // ASan: the strdup'd name is freed too

	memset(&o, 0, sizeof(o));
	Option_initVar(&o, "core_k", "Name;a|b");
	CHECK(streq(o.name, "core_k") && o.count == 2 && streq(o.values[0], "Name;a"), "';' without a space is no separator");
	Option_free(&o);

	// empty values
	memset(&o, 0, sizeof(o));
	Option_initVar(&o, "k", "N; a||b|");
	CHECK(o.count == 4 && streq(o.values[1], "") && streq(o.values[2], "b") && streq(o.values[3], ""),
		  "empty values kept");
	Option_free(&o);

	// Option with no values
	memset(&o, 0, sizeof(o));
	CHECK(Option_getValueIndex(&o, "x") == 0, "no values: 0");

	// OptionList lookups
	{
		OptionList list;
		Option opts[2];
		memset(&list, 0, sizeof(list));
		memset(opts, 0, sizeof(opts));
		Option_initVar(&opts[0], "a_key", "A; x|y|z");
		Option_initVar(&opts[1], "b_key", "B; on|off");
		list.options = opts;
		list.count = 2;

		CHECK(OptionList_getOption(&list, "b_key") == &opts[1], "getOption by key");
		CHECK(!OptionList_getOption(&list, "c_key") && !OptionList_getOption(&list, "a_ke"), "getOption: unknown key");
		CHECK(streq(OptionList_getOptionValue(&list, "a_key"), "x"), "getOptionValue: default");
		opts[0].value = 2;
		CHECK(streq(OptionList_getOptionValue(&list, "a_key"), "z"), "getOptionValue: last");
		opts[0].value = 3;
		CHECK(!OptionList_getOptionValue(&list, "a_key"), "getOptionValue: past the end is NULL");
		opts[0].value = -1;
		CHECK(!OptionList_getOptionValue(&list, "a_key"), "getOptionValue: negative is NULL");
		CHECK(!OptionList_getOptionValue(&list, "nope"), "getOptionValue: unknown key is NULL");
		char** saved = opts[1].values;
		opts[1].values = NULL;
		CHECK(!OptionList_getOptionValue(&list, "b_key"), "getOptionValue: no values is NULL");
		opts[1].values = saved;

		Option_free(&opts[0]);
		Option_free(&opts[1]);
	}

	// Option_free on a v1/v2-style option (own strings, labels distinct or shared)
	{
		Option v;
		memset(&v, 0, sizeof(v));
		v.key = strdup("k");
		v.name = strdup("Name");
		v.desc = strdup("d");
		v.full = strdup("f");
		v.category = strdup("c");
		v.count = 2;
		v.values = calloc(3, sizeof(char*));
		v.labels = calloc(3, sizeof(char*));
		v.values[0] = strdup("1");
		v.labels[0] = strdup("One");
		v.values[1] = strdup("2");
		v.labels[1] = v.values[1];
		Option_free(&v); // ASan: everything freed once
		CHECK(1, "v2 option freed (shared and own labels)");
	}

	printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", fails);
	return fails ? 1 : 0;
}
