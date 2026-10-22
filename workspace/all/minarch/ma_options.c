#include "ma_internal.h"
#include "utils.h"
#include "ma_options.h"

static void Option_setValue(Option* item, const char* value) {
	// TODO: store previous value?
	item->value = Option_getValueIndex(item, value);
}

// TODO: does this also need to be applied to OptionList_vars()?
static const char* option_key_name[] = {
	"pcsx_rearmed_analog_combo", "DualShock Toggle Combo",
	NULL};
static const char* getOptionNameFromKey(const char* key, const char* name) {
	char* _key = NULL;
	for (int i = 0; (_key = (char*)option_key_name[i]); i += 2) {
		if (exactMatch((char*)key, _key))
			return option_key_name[i + 1];
	}
	return name;
}

// the following 3 functions always touch config.core, the rest can operate on arbitrary OptionLists
void OptionList_init(const struct retro_core_option_definition* defs) {
	int count;
	for (count = 0; defs[count].key; count++)
		;

	// TODO: add frontend options to this? so the can use the same override method? eg. minarch_*

	config.core.count = count;
	config.core.categories = NULL; // There is no categories in v1 definition
	if (count) {
		config.core.options = calloc(count + 1, sizeof(Option));

		for (int i = 0; i < config.core.count; i++) {
			int len;
			const struct retro_core_option_definition* def = &defs[i];
			Option* item = &config.core.options[i];
			len = strlen(def->key) + 1;

			item->key = calloc(len, sizeof(char));
			strcpy(item->key, def->key);

			// getOptionNameFromKey may return a fixed string longer than desc,
			// so size the allocation from what is actually copied
			const char* opt_name = getOptionNameFromKey(def->key, def->desc ? def->desc : def->key);
			len = strlen(opt_name) + 1;
			item->name = calloc(len, sizeof(char));
			strcpy(item->name, opt_name);

			if (def->info) {
				len = strlen(def->info) + 1;
				item->desc = calloc(len, sizeof(char));
				strncpy(item->desc, def->info, len);

				item->full = calloc(len, sizeof(char));
				strncpy(item->full, item->desc, len);

				GFX_wrapText(font.tiny, item->desc, DEVICE_WIDTH - SCALE1(2 * PADDING), 2);
				GFX_wrapText(font.medium, item->full, DEVICE_WIDTH - SCALE1(2 * PADDING), 16);
			}

			for (count = 0; def->values[count].value; count++)
				;

			item->count = count;
			item->values = calloc(count + 1, sizeof(char*));
			item->labels = calloc(count + 1, sizeof(char*));

			for (int j = 0; j < count; j++) {
				const char* value = def->values[j].value;
				const char* label = def->values[j].label;

				len = strlen(value) + 1;
				item->values[j] = calloc(len, sizeof(char));
				strcpy(item->values[j], value);

				if (label) {
					len = strlen(label) + 1;
					item->labels[j] = calloc(len, sizeof(char));
					strcpy(item->labels[j], label);
				} else {
					item->labels[j] = item->values[j];
				}
			}

			item->value = Option_getValueIndex(item, def->default_value);
			item->default_value = item->value;
		}
	}
}

void OptionList_v2_init(const struct retro_core_options_v2* opt_defs) {
	struct retro_core_option_v2_category* cats = opt_defs->categories;
	struct retro_core_option_v2_definition* defs = opt_defs->definitions;

	// categories may legally be NULL (all options uncategorized)
	int cat_count = 0;
	while (cats && cats[cat_count].key)
		cat_count++;

	int count = 0;
	while (defs[count].key)
		count++;

	// TODO: add frontend options to this? so the can use the same override method? eg. minarch_*

	if (cat_count) {
		config.core.categories = calloc(cat_count + 1, sizeof(OptionCategory));

		for (int i = 0; i < cat_count; i++) {
			const struct retro_core_option_v2_category* cat = &cats[i];
			OptionCategory* item = &config.core.categories[i];

			item->key = strdup(cat->key);
			item->desc = strdup(cat->desc ? cat->desc : cat->key);
			item->info = cat->info ? strdup(cat->info) : NULL;
			printf("CATEGORY %s\n", item->key);
		}
	} else {
		config.core.categories = NULL;
	}

	config.core.count = count;
	if (count) {
		config.core.options = calloc(count + 1, sizeof(Option));

		for (int i = 0; i < config.core.count; i++) {
			const struct retro_core_option_v2_definition* def = &defs[i];
			Option* item = &config.core.options[i];

			item->key = strdup(def->key);
			item->name = strdup(getOptionNameFromKey(def->key, def->desc_categorized ? def->desc_categorized : (def->desc ? def->desc : def->key)));
			item->category = def->category_key ? strdup(def->category_key) : NULL;

			if (def->info) {
				item->desc = strdup(def->info);
				item->full = strdup(item->desc);

				GFX_wrapText(font.tiny, item->desc, DEVICE_WIDTH - SCALE1(2 * PADDING), 2);
				GFX_wrapText(font.medium, item->full, DEVICE_WIDTH - SCALE1(2 * PADDING), 16);
			}

			for (count = 0; def->values[count].value; count++)
				;

			item->count = count;
			item->values = calloc(count + 1, sizeof(char*));
			item->labels = calloc(count + 1, sizeof(char*));

			for (int j = 0; j < count; j++) {
				const char* value = def->values[j].value;
				const char* label = def->values[j].label;

				item->values[j] = strdup(value);

				if (label) {
					item->labels[j] = strdup(label);
				} else {
					item->labels[j] = item->values[j];
				}
			}

			item->value = Option_getValueIndex(item, def->default_value);
			item->default_value = item->value;
		}
	}
}

void OptionList_vars(const struct retro_variable* vars) {
	int count;
	for (count = 0; vars[count].key; count++)
		;

	config.core.count = count;
	if (count) {
		config.core.options = calloc(count + 1, sizeof(Option));

		for (int i = 0; i < config.core.count; i++) {
			Option_initVar(&config.core.options[i], vars[i].key, vars[i].value);
		}
	}
}
void OptionList_reset(void) {
	if (!config.core.count)
		return;

	for (int i = 0; i < config.core.count; i++) {
		Option_free(&config.core.options[i]);
	}
	free(config.core.options);
	config.core.options = NULL;
	config.core.count = 0;

	if (config.core.categories) {
		for (int i = 0; config.core.categories[i].key; i++) {
			free(config.core.categories[i].key);
			free(config.core.categories[i].desc);
			free(config.core.categories[i].info);
		}
		free(config.core.categories);
		config.core.categories = NULL;
	}
}

void OptionList_setOptionRawValue(OptionList* list, const char* key, int value) {
	Option* item = OptionList_getOption(list, key);
	if (item) {
		item->value = value;
		list->changed = 1;
		if (exactMatch((char*)core.tag, "GB") && containsString(item->key, "palette"))
			Special_updatedDMGPalette(3); // from options
	}
}
// Core option batching (toggled via minarch_beginOptionsBatch/endOptionsBatch).
// While batching, OptionList_setOptionValue records that something changed but
// defers config.core.changed until the batch ends, so a burst of related option
// writes triggers a single core variable re-read instead of one per write.
int option_batch_mode = 0;
int option_batch_changed = 0;

void OptionList_setOptionValue(OptionList* list, const char* key, const char* value) {
	Option* item = OptionList_getOption(list, key);
	if (item) {
		Option_setValue(item, value);
		if (option_batch_mode)
			option_batch_changed = 1;
		else
			list->changed = 1;
		if (exactMatch((char*)core.tag, "GB") && containsString(item->key, "palette"))
			Special_updatedDMGPalette(2); // from core
	}
}
void OptionList_setOptionVisibility(OptionList* list, const char* key, int visible) {
	Option* item = OptionList_getOption(list, key);
	if (item)
		item->hidden = !visible;
	else
		printf("unknown option %s \n", key);
	fflush(stdout);
}
