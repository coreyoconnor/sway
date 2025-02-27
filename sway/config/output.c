#include <assert.h>
#include <drm_fourcc.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wlr/config.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_swapchain_manager.h>
#include "sway/config.h"
#include "sway/input/cursor.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/tree/root.h"
#include "log.h"
#include "util.h"

#if WLR_HAS_DRM_BACKEND
#include <wlr/backend/drm.h>
#endif

int output_name_cmp(const void *item, const void *data) {
	const struct output_config *output = item;
	const char *name = data;

	return strcmp(output->name, name);
}

void output_get_identifier(char *identifier, size_t len,
		struct sway_output *output) {
	struct wlr_output *wlr_output = output->wlr_output;
	snprintf(identifier, len, "%s %s %s",
		wlr_output->make ? wlr_output->make : "Unknown",
		wlr_output->model ? wlr_output->model : "Unknown",
		wlr_output->serial ? wlr_output->serial : "Unknown");
}

const char *sway_output_scale_filter_to_string(enum scale_filter_mode scale_filter) {
	switch (scale_filter) {
	case SCALE_FILTER_DEFAULT:
		return "smart";
	case SCALE_FILTER_LINEAR:
		return "linear";
	case SCALE_FILTER_NEAREST:
		return "nearest";
	case SCALE_FILTER_SMART:
		return "smart";
	}
	sway_assert(false, "Unknown value for scale_filter.");
	return NULL;
}

struct output_config *new_output_config(const char *name) {
	struct output_config *oc = calloc(1, sizeof(struct output_config));
	if (oc == NULL) {
		return NULL;
	}
	oc->name = strdup(name);
	if (oc->name == NULL) {
		free(oc);
		return NULL;
	}
	oc->enabled = -1;
	oc->width = oc->height = -1;
	oc->refresh_rate = -1;
	oc->custom_mode = -1;
	oc->drm_mode.type = -1;
	oc->x = oc->y = -1;
	oc->scale = -1;
	oc->scale_filter = SCALE_FILTER_DEFAULT;
	oc->transform = -1;
	oc->subpixel = WL_OUTPUT_SUBPIXEL_UNKNOWN;
	oc->max_render_time = -1;
	oc->adaptive_sync = -1;
	oc->render_bit_depth = RENDER_BIT_DEPTH_DEFAULT;
	oc->power = -1;
	return oc;
}

// supersede_output_config clears all fields in dst that were set in src
static void supersede_output_config(struct output_config *dst, struct output_config *src) {
	if (src->enabled != -1) {
		dst->enabled = -1;
	}
	if (src->width != -1) {
		dst->width = -1;
	}
	if (src->height != -1) {
		dst->height = -1;
	}
	if (src->x != -1) {
		dst->x = -1;
	}
	if (src->y != -1) {
		dst->y = -1;
	}
	if (src->scale != -1) {
		dst->scale = -1;
	}
	if (src->scale_filter != SCALE_FILTER_DEFAULT) {
		dst->scale_filter = SCALE_FILTER_DEFAULT;
	}
	if (src->subpixel != WL_OUTPUT_SUBPIXEL_UNKNOWN) {
		dst->subpixel = WL_OUTPUT_SUBPIXEL_UNKNOWN;
	}
	if (src->refresh_rate != -1) {
		dst->refresh_rate = -1;
	}
	if (src->custom_mode != -1) {
		dst->custom_mode = -1;
	}
	if (src->drm_mode.type != (uint32_t) -1) {
		dst->drm_mode.type = -1;
	}
	if (src->transform != -1) {
		dst->transform = -1;
	}
	if (src->max_render_time != -1) {
		dst->max_render_time = -1;
	}
	if (src->adaptive_sync != -1) {
		dst->adaptive_sync = -1;
	}
	if (src->render_bit_depth != RENDER_BIT_DEPTH_DEFAULT) {
		dst->render_bit_depth = RENDER_BIT_DEPTH_DEFAULT;
	}
	if (src->background) {
		free(dst->background);
		dst->background = NULL;
	}
	if (src->background_option) {
		free(dst->background_option);
		dst->background_option = NULL;
	}
	if (src->background_fallback) {
		free(dst->background_fallback);
		dst->background_fallback = NULL;
	}
	if (src->power != -1) {
		dst->power = -1;
	}
}

// merge_output_config sets all fields in dst that were set in src
static void merge_output_config(struct output_config *dst, struct output_config *src) {
	if (src->enabled != -1) {
		dst->enabled = src->enabled;
	}
	if (src->width != -1) {
		dst->width = src->width;
	}
	if (src->height != -1) {
		dst->height = src->height;
	}
	if (src->x != -1) {
		dst->x = src->x;
	}
	if (src->y != -1) {
		dst->y = src->y;
	}
	if (src->scale != -1) {
		dst->scale = src->scale;
	}
	if (src->scale_filter != SCALE_FILTER_DEFAULT) {
		dst->scale_filter = src->scale_filter;
	}
	if (src->subpixel != WL_OUTPUT_SUBPIXEL_UNKNOWN) {
		dst->subpixel = src->subpixel;
	}
	if (src->refresh_rate != -1) {
		dst->refresh_rate = src->refresh_rate;
	}
	if (src->custom_mode != -1) {
		dst->custom_mode = src->custom_mode;
	}
	if (src->drm_mode.type != (uint32_t) -1) {
		memcpy(&dst->drm_mode, &src->drm_mode, sizeof(src->drm_mode));
	}
	if (src->transform != -1) {
		dst->transform = src->transform;
	}
	if (src->max_render_time != -1) {
		dst->max_render_time = src->max_render_time;
	}
	if (src->adaptive_sync != -1) {
		dst->adaptive_sync = src->adaptive_sync;
	}
	if (src->render_bit_depth != RENDER_BIT_DEPTH_DEFAULT) {
		dst->render_bit_depth = src->render_bit_depth;
	}
	if (src->background) {
		free(dst->background);
		dst->background = strdup(src->background);
	}
	if (src->background_option) {
		free(dst->background_option);
		dst->background_option = strdup(src->background_option);
	}
	if (src->background_fallback) {
		free(dst->background_fallback);
		dst->background_fallback = strdup(src->background_fallback);
	}
	if (src->power != -1) {
		dst->power = src->power;
	}
}

void store_output_config(struct output_config *oc) {
	bool merged = false;
	bool wildcard = strcmp(oc->name, "*") == 0;
	struct sway_output *output = wildcard ? NULL : all_output_by_name_or_id(oc->name);

	char id[128];
	if (output) {
		output_get_identifier(id, sizeof(id), output);
	}

	for (int i = 0; i < config->output_configs->length; i++) {
		struct output_config *old = config->output_configs->items[i];

		// If the old config matches the new config's name, regardless of
		// whether it was name or identifier, merge on top of the existing
		// config. If the new config is a wildcard, this also merges on top of
		// old wildcard configs.
		if (strcmp(old->name, oc->name) == 0) {
			merge_output_config(old, oc);
			merged = true;
			continue;
		}

		// If the new config is a wildcard config we supersede all non-wildcard
		// configs. Old wildcard configs have already been handled above.
		if (wildcard) {
			supersede_output_config(old, oc);
			continue;
		}

		// If the new config matches an output's name, and the old config
		// matches on that output's identifier, supersede it.
		if (output && strcmp(old->name, id) == 0 &&
				strcmp(oc->name, output->wlr_output->name) == 0) {
			supersede_output_config(old, oc);
		}
	}

	sway_log(SWAY_DEBUG, "Config stored for output %s (enabled: %d) (%dx%d@%fHz "
		"position %d,%d scale %f subpixel %s transform %d) (bg %s %s) (power %d) "
		"(max render time: %d)",
		oc->name, oc->enabled, oc->width, oc->height, oc->refresh_rate,
		oc->x, oc->y, oc->scale, sway_wl_output_subpixel_to_string(oc->subpixel),
		oc->transform, oc->background, oc->background_option, oc->power,
		oc->max_render_time);

	// If the configuration was not merged into an existing configuration, add
	// it to the list. Otherwise we're done with it and can free it.
	if (!merged) {
		list_add(config->output_configs, oc);
	} else {
		free_output_config(oc);
	}
}

static void set_mode(struct wlr_output *output, struct wlr_output_state *pending,
		int width, int height, float refresh_rate, bool custom) {
	// Not all floating point integers can be represented exactly
	// as (int)(1000 * mHz / 1000.f)
	// round() the result to avoid any error
	int mhz = (int)roundf(refresh_rate * 1000);
	// If no target refresh rate is given, match highest available
	mhz = mhz <= 0 ? INT_MAX : mhz;

	if (wl_list_empty(&output->modes) || custom) {
		sway_log(SWAY_DEBUG, "Assigning custom mode to %s", output->name);
		wlr_output_state_set_custom_mode(pending, width, height,
			refresh_rate > 0 ? mhz : 0);
		return;
	}

	struct wlr_output_mode *mode, *best = NULL;
	int best_diff_mhz = INT_MAX;
	wl_list_for_each(mode, &output->modes, link) {
		if (mode->width == width && mode->height == height) {
			int diff_mhz = abs(mode->refresh - mhz);
			if (diff_mhz < best_diff_mhz) {
				best_diff_mhz = diff_mhz;
				best = mode;
				if (best_diff_mhz == 0) {
					break;
				}
			}
		}
	}
	if (best) {
		sway_log(SWAY_INFO, "Assigning configured mode (%dx%d@%.3fHz) to %s",
			best->width, best->height, best->refresh / 1000.f, output->name);
	} else {
		best = wlr_output_preferred_mode(output);
		sway_log(SWAY_INFO, "Configured mode (%dx%d@%.3fHz) not available, "
			"applying preferred mode (%dx%d@%.3fHz)",
			width, height, refresh_rate,
			best->width, best->height, best->refresh / 1000.f);
	}
	wlr_output_state_set_mode(pending, best);
}

static void set_modeline(struct wlr_output *output,
		struct wlr_output_state *pending, drmModeModeInfo *drm_mode) {
#if WLR_HAS_DRM_BACKEND
	if (!wlr_output_is_drm(output)) {
		sway_log(SWAY_ERROR, "Modeline can only be set to DRM output");
		return;
	}
	sway_log(SWAY_DEBUG, "Assigning custom modeline to %s", output->name);
	struct wlr_output_mode *mode = wlr_drm_connector_add_mode(output, drm_mode);
	if (mode) {
		wlr_output_state_set_mode(pending, mode);
	}
#else
	sway_log(SWAY_ERROR, "Modeline can only be set to DRM output");
#endif
}

/* Some manufacturers hardcode the aspect-ratio of the output in the physical
 * size field. */
static bool phys_size_is_aspect_ratio(struct wlr_output *output) {
	return (output->phys_width == 1600 && output->phys_height == 900) ||
		(output->phys_width == 1600 && output->phys_height == 1000) ||
		(output->phys_width == 160 && output->phys_height == 90) ||
		(output->phys_width == 160 && output->phys_height == 100) ||
		(output->phys_width == 16 && output->phys_height == 9) ||
		(output->phys_width == 16 && output->phys_height == 10);
}

// The minimum DPI at which we turn on a scale of 2
#define HIDPI_DPI_LIMIT (2 * 96)
// The minimum screen height at which we turn on a scale of 2
#define HIDPI_MIN_HEIGHT 1200
// 1 inch = 25.4 mm
#define MM_PER_INCH 25.4

static int compute_default_scale(struct wlr_output *output,
		struct wlr_output_state *pending) {
	struct wlr_box box = { .width = output->width, .height = output->height };
	if (pending->committed & WLR_OUTPUT_STATE_MODE) {
		switch (pending->mode_type) {
		case WLR_OUTPUT_STATE_MODE_FIXED:
			box.width = pending->mode->width;
			box.height = pending->mode->height;
			break;
		case WLR_OUTPUT_STATE_MODE_CUSTOM:
			box.width = pending->custom_mode.width;
			box.height = pending->custom_mode.height;
			break;
		}
	}
	enum wl_output_transform transform = output->transform;
	if (pending->committed & WLR_OUTPUT_STATE_TRANSFORM) {
		transform = pending->transform;
	}
	wlr_box_transform(&box, &box, transform, box.width, box.height);

	int width = box.width;
	int height = box.height;

	if (height < HIDPI_MIN_HEIGHT) {
		return 1;
	}

	if (output->phys_width == 0 || output->phys_height == 0) {
		return 1;
	}

	if (phys_size_is_aspect_ratio(output)) {
		return 1;
	}

	double dpi_x = (double) width / (output->phys_width / MM_PER_INCH);
	double dpi_y = (double) height / (output->phys_height / MM_PER_INCH);
	sway_log(SWAY_DEBUG, "Output DPI: %fx%f", dpi_x, dpi_y);
	if (dpi_x <= HIDPI_DPI_LIMIT || dpi_y <= HIDPI_DPI_LIMIT) {
		return 1;
	}

	return 2;
}

static bool render_format_is_10bit(uint32_t render_format) {
	return render_format == DRM_FORMAT_XRGB2101010 ||
		render_format == DRM_FORMAT_XBGR2101010;
}

static bool render_format_is_bgr(uint32_t fmt) {
	return fmt == DRM_FORMAT_XBGR2101010 || fmt == DRM_FORMAT_XBGR8888;
}

static bool output_config_is_disabling(struct output_config *oc) {
	return oc && (!oc->enabled || oc->power == 0);
}

static void queue_output_config(struct output_config *oc,
		struct sway_output *output, struct wlr_output_state *pending) {
	if (output == root->fallback_output) {
		return;
	}

	struct wlr_output *wlr_output = output->wlr_output;

	if (output_config_is_disabling(oc)) {
		sway_log(SWAY_DEBUG, "Turning off output %s", wlr_output->name);
		wlr_output_state_set_enabled(pending, false);
		return;
	}

	sway_log(SWAY_DEBUG, "Turning on output %s", wlr_output->name);
	wlr_output_state_set_enabled(pending, true);

	if (oc && oc->drm_mode.type != 0 && oc->drm_mode.type != (uint32_t) -1) {
		sway_log(SWAY_DEBUG, "Set %s modeline",
			wlr_output->name);
		set_modeline(wlr_output, pending, &oc->drm_mode);
	} else if (oc && oc->width > 0 && oc->height > 0) {
		sway_log(SWAY_DEBUG, "Set %s mode to %dx%d (%f Hz)",
			wlr_output->name, oc->width, oc->height, oc->refresh_rate);
		set_mode(wlr_output, pending, oc->width, oc->height,
			oc->refresh_rate, oc->custom_mode == 1);
	} else if (!wl_list_empty(&wlr_output->modes)) {
		sway_log(SWAY_DEBUG, "Set preferred mode");
		struct wlr_output_mode *preferred_mode =
			wlr_output_preferred_mode(wlr_output);
		wlr_output_state_set_mode(pending, preferred_mode);
	}

	if (oc && (oc->subpixel != WL_OUTPUT_SUBPIXEL_UNKNOWN || config->reloading)) {
		sway_log(SWAY_DEBUG, "Set %s subpixel to %s", oc->name,
			sway_wl_output_subpixel_to_string(oc->subpixel));
		wlr_output_state_set_subpixel(pending, oc->subpixel);
	}

	enum wl_output_transform tr = WL_OUTPUT_TRANSFORM_NORMAL;
	if (oc && oc->transform >= 0) {
		tr = oc->transform;
#if WLR_HAS_DRM_BACKEND
	} else if (wlr_output_is_drm(wlr_output)) {
		tr = wlr_drm_connector_get_panel_orientation(wlr_output);
		sway_log(SWAY_DEBUG, "Auto-detected output transform: %d", tr);
#endif
	}
	if (wlr_output->transform != tr) {
		sway_log(SWAY_DEBUG, "Set %s transform to %d", wlr_output->name, tr);
		wlr_output_state_set_transform(pending, tr);
	}

	// Apply the scale last before the commit, because the scale auto-detection
	// reads the pending output size
	float scale;
	if (oc && oc->scale > 0) {
		scale = oc->scale;

		// The factional-scale-v1 protocol uses increments of 120ths to send
		// the scale factor to the client. Adjust the scale so that we use the
		// same value as the clients'.
		float adjusted_scale = round(scale * 120) / 120;
		if (scale != adjusted_scale) {
			sway_log(SWAY_INFO, "Adjusting output scale from %f to %f",
				scale, adjusted_scale);
			scale = adjusted_scale;
		}
	} else {
		scale = compute_default_scale(wlr_output, pending);
		sway_log(SWAY_DEBUG, "Auto-detected output scale: %f", scale);
	}
	if (scale != wlr_output->scale) {
		sway_log(SWAY_DEBUG, "Set %s scale to %f", wlr_output->name, scale);
		wlr_output_state_set_scale(pending, scale);
	}

	if (oc && oc->adaptive_sync != -1) {
		sway_log(SWAY_DEBUG, "Set %s adaptive sync to %d", wlr_output->name,
			oc->adaptive_sync);
		wlr_output_state_set_adaptive_sync_enabled(pending, oc->adaptive_sync == 1);
	}

	if (oc && oc->render_bit_depth != RENDER_BIT_DEPTH_DEFAULT) {
		if (oc->render_bit_depth == RENDER_BIT_DEPTH_10 &&
			render_format_is_10bit(output->wlr_output->render_format)) {
			// 10-bit was set successfully before, try to save some tests by reusing the format
			wlr_output_state_set_render_format(pending, output->wlr_output->render_format);
		} else if (oc->render_bit_depth == RENDER_BIT_DEPTH_10) {
			wlr_output_state_set_render_format(pending, DRM_FORMAT_XRGB2101010);
		} else {
			wlr_output_state_set_render_format(pending, DRM_FORMAT_XRGB8888);
		}
	}
}

static bool finalize_output_config(struct output_config *oc, struct sway_output *output) {
	if (output == root->fallback_output) {
		return false;
	}

	struct wlr_output *wlr_output = output->wlr_output;
	if (oc && !oc->enabled) {
		sway_log(SWAY_DEBUG, "Disabling output %s", oc->name);
		if (output->enabled) {
			output_disable(output);
			wlr_output_layout_remove(root->output_layout, wlr_output);
		}
		return true;
	}

	if (oc) {
		enum scale_filter_mode scale_filter_old = output->scale_filter;
		switch (oc->scale_filter) {
			case SCALE_FILTER_DEFAULT:
			case SCALE_FILTER_SMART:
				output->scale_filter = ceilf(wlr_output->scale) == wlr_output->scale ?
					SCALE_FILTER_NEAREST : SCALE_FILTER_LINEAR;
				break;
			case SCALE_FILTER_LINEAR:
			case SCALE_FILTER_NEAREST:
				output->scale_filter = oc->scale_filter;
				break;
		}
		if (scale_filter_old != output->scale_filter) {
			sway_log(SWAY_DEBUG, "Set %s scale_filter to %s", oc->name,
				sway_output_scale_filter_to_string(output->scale_filter));
			wlr_damage_ring_add_whole(&output->scene_output->damage_ring);
		}
	}

	// Find position for it
	if (oc && (oc->x != -1 || oc->y != -1)) {
		sway_log(SWAY_DEBUG, "Set %s position to %d, %d", oc->name, oc->x, oc->y);
		wlr_output_layout_add(root->output_layout, wlr_output, oc->x, oc->y);
	} else {
		wlr_output_layout_add_auto(root->output_layout, wlr_output);
	}

	// Update output->{lx, ly, width, height}
	struct wlr_box output_box;
	wlr_output_layout_get_box(root->output_layout, wlr_output, &output_box);
	output->lx = output_box.x;
	output->ly = output_box.y;
	output->width = output_box.width;
	output->height = output_box.height;

	if (!output->enabled) {
		output_enable(output);
	}

	if (oc && oc->max_render_time >= 0) {
		sway_log(SWAY_DEBUG, "Set %s max render time to %d",
			oc->name, oc->max_render_time);
		output->max_render_time = oc->max_render_time;
	}

	return true;
}

static void default_output_config(struct output_config *oc,
		struct wlr_output *wlr_output) {
	oc->enabled = 1;
	oc->power = 1;
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		oc->width = mode->width;
		oc->height = mode->height;
		oc->refresh_rate = mode->refresh / 1000.f;
	}
	oc->x = oc->y = -1;
	oc->scale = 0; // auto
	oc->scale_filter = SCALE_FILTER_DEFAULT;
	struct sway_output *output = wlr_output->data;
	oc->subpixel = output->detected_subpixel;
	oc->transform = WL_OUTPUT_TRANSFORM_NORMAL;
	oc->max_render_time = 0;
}

// find_output_config returns a merged output_config containing all stored
// configuration that applies to the specified output.
struct output_config *find_output_config(struct sway_output *sway_output) {
	const char *name = sway_output->wlr_output->name;
	struct output_config *oc = NULL;

	struct output_config *result = new_output_config(name);
	if (config->reloading) {
		default_output_config(result, sway_output->wlr_output);
	}

	char id[128];
	output_get_identifier(id, sizeof(id), sway_output);

	int i;
	bool match = false;
	if ((i = list_seq_find(config->output_configs, output_name_cmp, "*")) >= 0) {
		match = true;
		oc = config->output_configs->items[i];
		merge_output_config(result, oc);
	}
	if ((i = list_seq_find(config->output_configs, output_name_cmp, name)) >= 0) {
		match = true;
		oc = config->output_configs->items[i];
		merge_output_config(result, oc);
	}
	if ((i = list_seq_find(config->output_configs, output_name_cmp, id)) >= 0) {
		match = true;
		oc = config->output_configs->items[i];
		merge_output_config(result, oc);
	}

	if (!match && !config->reloading) {
		// No name, identifier, or wildcard config. Since we are not
		// reloading with defaults, the output config will be empty, so
		// just return NULL
		free_output_config(result);
		return NULL;
	}

	return result;
}

static bool config_has_auto_mode(struct output_config *oc) {
	if (!oc) {
		return true;
	}
	if (oc->drm_mode.type != 0 && oc->drm_mode.type != (uint32_t)-1) {
		return true;
	} else if (oc->width > 0 && oc->height > 0) {
		return true;
	}
	return false;
}

struct search_context {
	struct wlr_output_swapchain_manager *swapchain_mgr;
	struct wlr_backend_output_state *states;
	struct matched_output_config *configs;
	size_t configs_len;
	bool degrade_to_off;
};

static void dump_output_state(struct wlr_output *wlr_output, struct wlr_output_state *state) {
	sway_log(SWAY_DEBUG, "Output state for %s", wlr_output->name);
	if (state->committed & WLR_OUTPUT_STATE_ENABLED) {
		sway_log(SWAY_DEBUG, "    enabled:       %s", state->enabled ? "yes" : "no");
	}
	if (state->committed & WLR_OUTPUT_STATE_RENDER_FORMAT) {
		sway_log(SWAY_DEBUG, "    render_format: %d", state->render_format);
	}
	if (state->committed & WLR_OUTPUT_STATE_MODE) {
		if (state->mode_type == WLR_OUTPUT_STATE_MODE_CUSTOM) {
			sway_log(SWAY_DEBUG, "    custom mode:   %dx%d@%dmHz",
				state->custom_mode.width, state->custom_mode.height, state->custom_mode.refresh);
		} else {
			sway_log(SWAY_DEBUG, "    mode:          %dx%d@%dmHz%s",
				state->mode->width, state->mode->height, state->mode->refresh,
				state->mode->preferred ? " (preferred)" : "");
		}
	}
	if (state->committed & WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED) {
		sway_log(SWAY_DEBUG, "    adaptive_sync: %s",
			state->adaptive_sync_enabled ? "enabled": "disabled");
	}
}

static bool search_valid_config(struct search_context *ctx, size_t output_idx);

static void reset_output_state(struct wlr_output_state *state) {
	wlr_output_state_finish(state);
	wlr_output_state_init(state);
	state->committed = 0;
}

static void clear_later_output_states(struct wlr_backend_output_state *states,
		size_t configs_len, size_t output_idx) {

	// Clear and disable all output states after this one to avoid conflict
	// with previous tests.
	for (size_t idx = output_idx+1; idx < configs_len; idx++) {
		struct wlr_backend_output_state *backend_state = &states[idx];
		struct wlr_output_state *state = &backend_state->base;

		reset_output_state(state);
		wlr_output_state_set_enabled(state, false);
	}
}

static bool search_finish(struct search_context *ctx, size_t output_idx) {
	struct wlr_backend_output_state *backend_state = &ctx->states[output_idx];
	struct wlr_output_state *state = &backend_state->base;
	struct wlr_output *wlr_output = backend_state->output;

	clear_later_output_states(ctx->states, ctx->configs_len, output_idx);
	dump_output_state(wlr_output, state);
	return wlr_output_swapchain_manager_prepare(ctx->swapchain_mgr, ctx->states, ctx->configs_len) &&
		search_valid_config(ctx, output_idx+1);
}

static bool search_adaptive_sync(struct search_context *ctx, size_t output_idx) {
	struct matched_output_config *cfg = &ctx->configs[output_idx];
	struct wlr_backend_output_state *backend_state = &ctx->states[output_idx];
	struct wlr_output_state *state = &backend_state->base;

	if (cfg->config && cfg->config->adaptive_sync == 1) {
		wlr_output_state_set_adaptive_sync_enabled(state, true);
		if (search_finish(ctx, output_idx)) {
			return true;
		}
	}
	if (!cfg->config || cfg->config->adaptive_sync != -1) {
		wlr_output_state_set_adaptive_sync_enabled(state, false);
		if (search_finish(ctx, output_idx)) {
			return true;
		}
	}
	// If adaptive sync has not been set, or fallback in case we are on a
	// backend that cannot disable adaptive sync such as the wayland backend.
	state->committed &= ~WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED;
	return search_finish(ctx, output_idx);
}

static bool search_mode(struct search_context *ctx, size_t output_idx) {
	struct matched_output_config *cfg = &ctx->configs[output_idx];
	struct wlr_backend_output_state *backend_state = &ctx->states[output_idx];
	struct wlr_output_state *state = &backend_state->base;
	struct wlr_output *wlr_output = backend_state->output;

	if (!config_has_auto_mode(cfg->config)) {
		return search_adaptive_sync(ctx, output_idx);
	}

	struct wlr_output_mode *preferred_mode = wlr_output_preferred_mode(wlr_output);
	if (preferred_mode) {
		wlr_output_state_set_mode(state, preferred_mode);
		if (search_adaptive_sync(ctx, output_idx)) {
			return true;
		}
	}

	if (wl_list_empty(&wlr_output->modes)) {
		state->committed &= ~WLR_OUTPUT_STATE_MODE;
		return search_adaptive_sync(ctx, output_idx);
	}

	struct wlr_output_mode *mode;
	wl_list_for_each(mode, &backend_state->output->modes, link) {
		if (mode == preferred_mode) {
			continue;
		}
		wlr_output_state_set_mode(state, mode);
		if (search_adaptive_sync(ctx, output_idx)) {
			return true;
		}
	}

	return false;
}

static bool search_render_format(struct search_context *ctx, size_t output_idx) {
	struct matched_output_config *cfg = &ctx->configs[output_idx];
	struct wlr_backend_output_state *backend_state = &ctx->states[output_idx];
	struct wlr_output_state *state = &backend_state->base;
	struct wlr_output *wlr_output = backend_state->output;

	uint32_t fmts[] = {
		DRM_FORMAT_XRGB2101010,
		DRM_FORMAT_XBGR2101010,
		DRM_FORMAT_XRGB8888,
		DRM_FORMAT_INVALID,
	};
	if (render_format_is_bgr(wlr_output->render_format)) {
		// Start with BGR in the unlikely event that we previously required it.
		fmts[0] = DRM_FORMAT_XBGR2101010;
		fmts[1] = DRM_FORMAT_XRGB2101010;
	}

	const struct wlr_drm_format_set *primary_formats =
		wlr_output_get_primary_formats(wlr_output, WLR_BUFFER_CAP_DMABUF);
	bool need_10bit = cfg->config && cfg->config->render_bit_depth == RENDER_BIT_DEPTH_10;
	for (size_t idx = 0; fmts[idx] != DRM_FORMAT_INVALID; idx++) {
		if (!need_10bit && render_format_is_10bit(fmts[idx])) {
			continue;
		}
		if (!wlr_drm_format_set_get(primary_formats, fmts[idx])) {
			// This is not a supported format for this output
			continue;
		}
		wlr_output_state_set_render_format(state, fmts[idx]);
		if (search_mode(ctx, output_idx)) {
			return true;
		}
	}
	return false;
}

static bool search_valid_config(struct search_context *ctx, size_t output_idx) {
	if (output_idx >= ctx->configs_len) {
		// We reached the end of the search, all good!
		return true;
	}

	struct matched_output_config *cfg = &ctx->configs[output_idx];
	struct wlr_backend_output_state *backend_state = &ctx->states[output_idx];
	struct wlr_output_state *state = &backend_state->base;
	struct wlr_output *wlr_output = backend_state->output;

	if (!output_config_is_disabling(cfg->config)) {
		// Search through our possible configurations, doing a depth-first
		// through render_format, modes, adaptive_sync and the next output's
		// config.
		queue_output_config(cfg->config, cfg->output, &backend_state->base);
		if (search_render_format(ctx, output_idx)) {
			return true;
		} else if (!ctx->degrade_to_off) {
			return false;
		}
		// We could not get anything to work, try to disable this output to see
		// if we can at least make the outputs before us work.
		sway_log(SWAY_DEBUG, "Unable to find valid config with output %s, disabling",
			wlr_output->name);
		reset_output_state(state);
	}

	wlr_output_state_set_enabled(state, false);
	return search_finish(ctx, output_idx);
}

static int compare_matched_output_config_priority(const void *a, const void *b) {

	const struct matched_output_config *amc = a;
	const struct matched_output_config *bmc = b;
	bool a_disabling = output_config_is_disabling(amc->config);
	bool b_disabling = output_config_is_disabling(bmc->config);
	bool a_enabled = amc->output->enabled;
	bool b_enabled = bmc->output->enabled;

	// We want to give priority to existing enabled outputs. To do so, we want
	// the configuration order to be:
	// 1. Existing, enabled outputs
	// 2. Outputs that need to be enabled
	// 3. Disabled or disabling outputs
	if (a_enabled && !a_disabling) {
		return -1;
	} else if (b_enabled && !b_disabling) {
		return 1;
	} else if (b_disabling && !a_disabling) {
		return -1;
	} else if (a_disabling && !b_disabling) {
		return 1;
	}
	return 0;
}

void sort_output_configs_by_priority(struct matched_output_config *configs,
		size_t configs_len) {
	qsort(configs, configs_len, sizeof(*configs), compare_matched_output_config_priority);
}

bool apply_output_configs(struct matched_output_config *configs,
		size_t configs_len, bool test_only, bool degrade_to_off) {
	struct wlr_backend_output_state *states = calloc(configs_len, sizeof(*states));
	if (!states) {
		return false;
	}

	sway_log(SWAY_DEBUG, "Committing %zd outputs", configs_len);
	for (size_t idx = 0; idx < configs_len; idx++) {
		struct matched_output_config *cfg = &configs[idx];
		struct wlr_backend_output_state *backend_state = &states[idx];

		backend_state->output = cfg->output->wlr_output;
		wlr_output_state_init(&backend_state->base);

		sway_log(SWAY_DEBUG, "Preparing config for %s",
			cfg->output->wlr_output->name);
		queue_output_config(cfg->config, cfg->output, &backend_state->base);
	}

	struct wlr_output_swapchain_manager swapchain_mgr;
	wlr_output_swapchain_manager_init(&swapchain_mgr, server.backend);

	bool ok = wlr_output_swapchain_manager_prepare(&swapchain_mgr, states, configs_len);
	if (!ok) {
		sway_log(SWAY_ERROR, "Requested backend configuration failed, searching for valid fallbacks");
		struct search_context ctx = {
			.swapchain_mgr = &swapchain_mgr,
			.states = states,
			.configs = configs,
			.configs_len = configs_len,
			.degrade_to_off = degrade_to_off,
		};
		if (!search_valid_config(&ctx, 0)) {
			sway_log(SWAY_ERROR, "Search for valid config failed");
			goto out;
		}
	}

	if (test_only) {
		// The swapchain manager already did a test for us
		goto out;
	}

	for (size_t idx = 0; idx < configs_len; idx++) {
		struct matched_output_config *cfg = &configs[idx];
		struct wlr_backend_output_state *backend_state = &states[idx];

		struct wlr_scene_output_state_options opts = {
			.swapchain = wlr_output_swapchain_manager_get_swapchain(
				&swapchain_mgr, backend_state->output),
		};
		struct wlr_scene_output *scene_output = cfg->output->scene_output;
		struct wlr_output_state *state = &backend_state->base;
		if (!wlr_scene_output_build_state(scene_output, state, &opts)) {
			sway_log(SWAY_ERROR, "Building output state for '%s' failed",
				backend_state->output->name);
			goto out;
		}
	}

	ok = wlr_backend_commit(server.backend, states, configs_len);
	if (!ok) {
		sway_log(SWAY_ERROR, "Backend commit failed");
		goto out;
	}

	sway_log(SWAY_DEBUG, "Commit of %zd outputs succeeded", configs_len);

	wlr_output_swapchain_manager_apply(&swapchain_mgr);

	for (size_t idx = 0; idx < configs_len; idx++) {
		struct matched_output_config *cfg = &configs[idx];
		sway_log(SWAY_DEBUG, "Finalizing config for %s",
			cfg->output->wlr_output->name);
		finalize_output_config(cfg->config, cfg->output);
	}

out:
	wlr_output_swapchain_manager_finish(&swapchain_mgr);
	for (size_t idx = 0; idx < configs_len; idx++) {
		struct wlr_backend_output_state *backend_state = &states[idx];
		wlr_output_state_finish(&backend_state->base);
	}
	free(states);

	// Reconfigure all devices, since input config may have been applied before
	// this output came online, and some config items (like map_to_output) are
	// dependent on an output being present.
	input_manager_configure_all_input_mappings();
	// Reconfigure the cursor images, since the scale may have changed.
	input_manager_configure_xcursor();

	struct sway_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
		cursor_rebase(seat->cursor);
	}

	return ok;
}

void apply_all_output_configs(void) {
	size_t configs_len = wl_list_length(&root->all_outputs);
	struct matched_output_config *configs = calloc(configs_len, sizeof(*configs));
	if (!configs) {
		return;
	}

	int config_idx = 0;
	struct sway_output *sway_output;
	wl_list_for_each(sway_output, &root->all_outputs, link) {
		if (sway_output == root->fallback_output) {
			configs_len--;
			continue;
		}

		struct matched_output_config *config = &configs[config_idx++];
		config->output = sway_output;
		config->config = find_output_config(sway_output);
	}

	sort_output_configs_by_priority(configs, configs_len);
	apply_output_configs(configs, configs_len, false, true);
	for (size_t idx = 0; idx < configs_len; idx++) {
		struct matched_output_config *cfg = &configs[idx];
		free_output_config(cfg->config);
	}
	free(configs);
}

void free_output_config(struct output_config *oc) {
	if (!oc) {
		return;
	}
	free(oc->name);
	free(oc->background);
	free(oc->background_option);
	free(oc);
}

static void handle_swaybg_client_destroy(struct wl_listener *listener,
		void *data) {
	struct sway_config *sway_config =
		wl_container_of(listener, sway_config, swaybg_client_destroy);
	wl_list_remove(&sway_config->swaybg_client_destroy.link);
	wl_list_init(&sway_config->swaybg_client_destroy.link);
	sway_config->swaybg_client = NULL;
}

static bool _spawn_swaybg(char **command) {
	if (config->swaybg_client != NULL) {
		wl_client_destroy(config->swaybg_client);
	}
	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
		sway_log_errno(SWAY_ERROR, "socketpair failed");
		return false;
	}
	if (!sway_set_cloexec(sockets[0], true) || !sway_set_cloexec(sockets[1], true)) {
		return false;
	}

	config->swaybg_client = wl_client_create(server.wl_display, sockets[0]);
	if (config->swaybg_client == NULL) {
		sway_log_errno(SWAY_ERROR, "wl_client_create failed");
		return false;
	}

	config->swaybg_client_destroy.notify = handle_swaybg_client_destroy;
	wl_client_add_destroy_listener(config->swaybg_client,
		&config->swaybg_client_destroy);

	pid_t pid = fork();
	if (pid < 0) {
		sway_log_errno(SWAY_ERROR, "fork failed");
		return false;
	} else if (pid == 0) {
		restore_nofile_limit();

		pid = fork();
		if (pid < 0) {
			sway_log_errno(SWAY_ERROR, "fork failed");
			_exit(EXIT_FAILURE);
		} else if (pid == 0) {
			if (!sway_set_cloexec(sockets[1], false)) {
				_exit(EXIT_FAILURE);
			}

			char wayland_socket_str[16];
			snprintf(wayland_socket_str, sizeof(wayland_socket_str),
				"%d", sockets[1]);
			setenv("WAYLAND_SOCKET", wayland_socket_str, true);

			execvp(command[0], command);
			sway_log_errno(SWAY_ERROR, "failed to execute '%s' "
				"(background configuration probably not applied)",
				command[0]);
			_exit(EXIT_FAILURE);
		}
		_exit(EXIT_SUCCESS);
	}

	if (close(sockets[1]) != 0) {
		sway_log_errno(SWAY_ERROR, "close failed");
		return false;
	}
	int fork_status = 0;
	if (waitpid(pid, &fork_status, 0) < 0) {
		sway_log_errno(SWAY_ERROR, "waitpid failed");
		return false;
	}

	return WIFEXITED(fork_status) && WEXITSTATUS(fork_status) == EXIT_SUCCESS;
}

bool spawn_swaybg(void) {
	if (!config->swaybg_command) {
		return true;
	}

	size_t length = 2;
	for (int i = 0; i < config->output_configs->length; i++) {
		struct output_config *oc = config->output_configs->items[i];
		if (!oc->background) {
			continue;
		}
		if (strcmp(oc->background_option, "solid_color") == 0) {
			length += 4;
		} else if (oc->background_fallback) {
			length += 8;
		} else {
			length += 6;
		}
	}

	char **cmd = calloc(length, sizeof(char *));
	if (!cmd) {
		sway_log(SWAY_ERROR, "Failed to allocate spawn_swaybg command");
		return false;
	}

	size_t i = 0;
	cmd[i++] = config->swaybg_command;
	for (int j = 0; j < config->output_configs->length; j++) {
		struct output_config *oc = config->output_configs->items[j];
		if (!oc->background) {
			continue;
		}
		if (strcmp(oc->background_option, "solid_color") == 0) {
			cmd[i++] = "-o";
			cmd[i++] = oc->name;
			cmd[i++] = "-c";
			cmd[i++] = oc->background;
		} else {
			cmd[i++] = "-o";
			cmd[i++] = oc->name;
			cmd[i++] = "-i";
			cmd[i++] = oc->background;
			cmd[i++] = "-m";
			cmd[i++] = oc->background_option;
			if (oc->background_fallback) {
				cmd[i++] = "-c";
				cmd[i++] = oc->background_fallback;
			}
		}
		assert(i <= length);
	}

	for (size_t k = 0; k < i; k++) {
		sway_log(SWAY_DEBUG, "spawn_swaybg cmd[%zd] = %s", k, cmd[k]);
	}

	bool result = _spawn_swaybg(cmd);
	free(cmd);
	return result;
}
