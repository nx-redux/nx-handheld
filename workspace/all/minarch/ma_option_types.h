#pragma once

// Core and frontend option types (plain C, no SDL).

typedef struct Option {
	char* key;
	char* name; // desc
	char* desc; // info, truncated
	char* full; // info, longer but possibly still truncated
	char* category;
	char* var;
	int default_value;
	int value;
	int count; // values, set for core options only (Option_free); others use Option_valueCount
	int lock;
	int hidden;
	char** values;
	char** labels;
} Option;
typedef struct OptionCategory {
	char* key;
	char* desc;
	char* info;
} OptionCategory;
typedef struct OptionList {
	int count;
	int changed;
	Option* options;

	int enabled_count;
	Option** enabled_options;

	OptionCategory* categories;
} OptionList;
