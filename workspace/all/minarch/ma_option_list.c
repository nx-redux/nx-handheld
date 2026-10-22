#include "ma_option_list.h"
#include <stdlib.h>
#include <string.h>

int Option_getValueIndex(Option* item, const char* value) {
	if (!value || !item || !item->values)
		return 0;
	int i = 0;
	while (item->values[i]) {
		if (!strcmp(item->values[i], value))
			return i;
		i++;
	}
	return 0;
}

int Option_valueCount(const Option* item) {
	int count = 0;
	while (item->values && item->values[count])
		count++;
	return count;
}

void Option_initVar(Option* item, const char* key, const char* value) {
	int len, count;

	len = strlen(key) + 1;
	item->key = calloc(len, sizeof(char));
	strcpy(item->key, key);

	len = strlen(value) + 1;
	item->var = calloc(len, sizeof(char));
	strcpy(item->var, value);

	// spec format is "Name; val1|val2" — tolerate a missing "; "
	// separator: fall back to the key for the name and treat the whole
	// string as the value list (was a NULL deref below)
	char* tmp = strchr(item->var, ';');
	if (tmp && *(tmp + 1) == ' ') {
		*tmp = '\0';
		item->name = item->var;
		tmp += 2;
	} else {
		item->name = strdup(item->key);
		tmp = item->var;
	}

	char* opt = tmp;
	for (count = 0; (tmp = strchr(tmp, '|')); tmp++, count++)
		;
	count += 1; // last entry after final '|'

	item->count = count;
	item->values = calloc(count + 1, sizeof(char*));
	item->labels = calloc(count + 1, sizeof(char*));

	tmp = opt;
	int j;
	for (j = 0; (tmp = strchr(tmp, '|')); j++) {
		item->values[j] = opt;
		item->labels[j] = opt;
		*tmp = '\0';
		tmp += 1;
		opt = tmp;
	}
	item->values[j] = opt;
	item->labels[j] = opt;

	// no native default_value support for retro vars
	item->value = 0;
	item->default_value = item->value;
}

void Option_free(Option* item) {
	if (item->var) {
		// values/labels are all points to var
		// so no need to free individually
		free(item->var);
	} else {
		if (item->desc)
			free(item->desc);
		if (item->full)
			free(item->full);
		for (int j = 0; j < item->count; j++) {
			char* value = item->values[j];
			char* label = item->labels[j];
			if (label != value)
				free(label);
			free(value);
		}
	}
	free(item->values);
	free(item->labels);
	free(item->key);
	// legacy vars alias name to the same allocation as var (freed above)
	if (item->name != item->var)
		free(item->name);
	free(item->category);
}

Option* OptionList_getOption(OptionList* list, const char* key) {
	for (int i = 0; i < list->count; i++) {
		Option* item = &list->options[i];
		if (!strcmp(item->key, key))
			return item;
	}
	return NULL;
}

char* OptionList_getOptionValue(OptionList* list, const char* key) {
	Option* item = OptionList_getOption(list, key);
	if (item) {
		if (item->value >= 0 && item->value < Option_valueCount(item)) {
			return item->values[item->value];
		}
	}
	return NULL;
}
