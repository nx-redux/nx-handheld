// Minimal stand-in for libretro-common's libretro.h, used by run_tests.sh only when
// minarch/libretro-common (cloned by minarch's build, not tracked) is absent. Carries just the
// core-option structs ma_opts_schema.c reads, copied from libretro.h (MIT, libretro team).
#ifndef LIBRETRO_H__
#define LIBRETRO_H__

#define RETRO_NUM_CORE_OPTION_VALUES_MAX 128

struct retro_variable {
	const char* key;
	const char* value;
};

struct retro_core_option_value {
	const char* value;
	const char* label;
};

struct retro_core_option_definition {
	const char* key;
	const char* desc;
	const char* info;
	struct retro_core_option_value values[RETRO_NUM_CORE_OPTION_VALUES_MAX];
	const char* default_value;
};

struct retro_core_option_v2_category {
	const char* key;
	const char* desc;
	const char* info;
};

struct retro_core_option_v2_definition {
	const char* key;
	const char* desc;
	const char* desc_categorized;
	const char* info;
	const char* info_categorized;
	const char* category_key;
	struct retro_core_option_value values[RETRO_NUM_CORE_OPTION_VALUES_MAX];
	const char* default_value;
};

struct retro_core_options_v2 {
	struct retro_core_option_v2_category* categories;
	struct retro_core_option_v2_definition* definitions;
};

#endif
