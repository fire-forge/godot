/*************************************************************************/
/*  text_server_fb.h                                                     */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2022 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2022 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#ifndef TEXT_SERVER_FB_H
#define TEXT_SERVER_FB_H

/*************************************************************************/
/* Fallback Text Server provides simplified TS functionality, without    */
/* BiDi, shaping and advanced font features support.                     */
/*************************************************************************/

#ifdef GDEXTENSION
// Headers for building as GDExtension plug-in.

#include <godot_cpp/godot.hpp>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/mutex_lock.hpp>

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/text_server_extension.hpp>
#include <godot_cpp/classes/text_server_manager.hpp>

#include <godot_cpp/classes/caret_info.hpp>
#include <godot_cpp/classes/global_constants_binds.hpp>
#include <godot_cpp/classes/glyph.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/ref.hpp>

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/rid_owner.hpp>
#include <godot_cpp/templates/thread_work_pool.hpp>
#include <godot_cpp/templates/vector.hpp>

using namespace godot;

#else
// Headers for building as built-in module.

#include "servers/text/text_server_extension.h"

#include "core/object/worker_thread_pool.h"
#include "core/templates/hash_map.h"
#include "core/templates/rid_owner.h"
#include "scene/resources/texture.h"

#include "modules/modules_enabled.gen.h" // For freetype, msdfgen.

#endif

// Thirdparty headers.

#ifdef MODULE_FREETYPE_ENABLED
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include FT_STROKER_H
#include FT_ADVANCES_H
#include FT_MULTIPLE_MASTERS_H
#include FT_BBOX_H
#include FT_CONFIG_OPTIONS_H
#if !defined(FT_CONFIG_OPTION_USE_BROTLI) && !defined(_MSC_VER)
#warning FreeType is configured without Brotli support, built-in fonts will not be available.
#endif
#endif

/*************************************************************************/

class TextServerFallback : public TextServerExtension {
	GDCLASS(TextServerFallback, TextServerExtension);
	_THREAD_SAFE_CLASS_

	HashMap<StringName, int32_t> feature_sets;
	HashMap<int32_t, StringName> feature_sets_inv;

	void _insert_feature_sets();
	_FORCE_INLINE_ void _insert_feature(const StringName &p_name, int32_t p_tag);

	// Font cache data.

#ifdef MODULE_FREETYPE_ENABLED
	mutable FT_Library ft_library = nullptr;
#endif

	const int rect_range = 1;

	struct FontTexture {
		Image::Format format;
		PackedByteArray imgdata;
		int texture_w = 0;
		int texture_h = 0;
		PackedInt32Array offsets;
		Ref<ImageTexture> texture;
		bool dirty = true;
	};

	struct FontTexturePosition {
		int index = 0;
		int x = 0;
		int y = 0;
	};

	struct FontGlyph {
		bool found = false;
		int texture_idx = -1;
		Rect2 rect;
		Rect2 uv_rect;
		Vector2 advance;
	};

	struct FontForSizeFallback {
		double ascent = 0.0;
		double descent = 0.0;
		double underline_position = 0.0;
		double underline_thickness = 0.0;
		double scale = 1.0;
		double oversampling = 1.0;

		Vector2i size;

		Vector<FontTexture> textures;
		HashMap<int32_t, FontGlyph> glyph_map;
		HashMap<Vector2i, Vector2> kerning_map;

#ifdef MODULE_FREETYPE_ENABLED
		FT_Face face = nullptr;
		FT_StreamRec stream;
#endif

		~FontForSizeFallback() {
#ifdef MODULE_FREETYPE_ENABLED
			if (face != nullptr) {
				FT_Done_Face(face);
			}
#endif
		}
	};

	struct FontFallback {
		Mutex mutex;

		bool antialiased = true;
		bool mipmaps = false;
		bool msdf = false;
		int msdf_range = 14;
		int msdf_source_size = 48;
		int fixed_size = 0;
		bool force_autohinter = false;
		TextServer::Hinting hinting = TextServer::HINTING_LIGHT;
		TextServer::SubpixelPositioning subpixel_positioning = TextServer::SUBPIXEL_POSITIONING_AUTO;
		Dictionary variation_coordinates;
		double oversampling = 0.0;
		double embolden = 0.0;
		Transform2D transform;

		BitField<TextServer::FontStyle> style_flags = 0;
		String font_name;
		String style_name;

		HashMap<Vector2i, FontForSizeFallback *, VariantHasher, VariantComparator> cache;

		bool face_init = false;
		Dictionary supported_varaitions;
		Dictionary feature_overrides;

		// Language/script support override.
		HashMap<String, bool> language_support_overrides;
		HashMap<String, bool> script_support_overrides;

		PackedByteArray data;
		const uint8_t *data_ptr;
		size_t data_size;
		int face_index = 0;

		~FontFallback() {
			for (const KeyValue<Vector2i, FontForSizeFallback *> &E : cache) {
				memdelete(E.value);
			}
			cache.clear();
		}
	};

	_FORCE_INLINE_ FontTexturePosition find_texture_pos_for_glyph(FontForSizeFallback *p_data, int p_color_size, Image::Format p_image_format, int p_width, int p_height, bool p_msdf) const;
#ifdef MODULE_MSDFGEN_ENABLED
	_FORCE_INLINE_ FontGlyph rasterize_msdf(FontFallback *p_font_data, FontForSizeFallback *p_data, int p_pixel_range, int p_rect_margin, FT_Outline *outline, const Vector2 &advance) const;
#endif
#ifdef MODULE_FREETYPE_ENABLED
	_FORCE_INLINE_ FontGlyph rasterize_bitmap(FontForSizeFallback *p_data, int p_rect_margin, FT_Bitmap bitmap, int yofs, int xofs, const Vector2 &advance) const;
#endif
	_FORCE_INLINE_ bool _ensure_glyph(FontFallback *p_font_data, const Vector2i &p_size, int32_t p_glyph) const;
	_FORCE_INLINE_ bool _ensure_cache_for_size(FontFallback *p_font_data, const Vector2i &p_size) const;
	_FORCE_INLINE_ void _font_clear_cache(FontFallback *p_font_data);
	void _generateMTSDF_threaded(uint32_t y, void *p_td) const;

	_FORCE_INLINE_ Vector2i _get_size(const FontFallback *p_font_data, int p_size) const {
		if (p_font_data->msdf) {
			return Vector2i(p_font_data->msdf_source_size, 0);
		} else if (p_font_data->fixed_size > 0) {
			return Vector2i(p_font_data->fixed_size, 0);
		} else {
			return Vector2i(p_size, 0);
		}
	}

	_FORCE_INLINE_ Vector2i _get_size_outline(const FontFallback *p_font_data, const Vector2i &p_size) const {
		if (p_font_data->msdf) {
			return Vector2i(p_font_data->msdf_source_size, 0);
		} else if (p_font_data->fixed_size > 0) {
			return Vector2i(p_font_data->fixed_size, MIN(p_size.y, 1));
		} else {
			return p_size;
		}
	}

	// Shaped text cache data.
	struct TrimData {
		int trim_pos = -1;
		int ellipsis_pos = -1;
		Vector<Glyph> ellipsis_glyph_buf;
	};

	struct ShapedTextDataFallback {
		Mutex mutex;

		/* Source data */
		RID parent; // Substring parent ShapedTextData.

		int start = 0; // Substring start offset in the parent string.
		int end = 0; // Substring end offset in the parent string.

		String text;
		String custom_punct;
		TextServer::Direction direction = DIRECTION_LTR; // Desired text direction.
		TextServer::Orientation orientation = ORIENTATION_HORIZONTAL;

		struct Span {
			int start = -1;
			int end = -1;

			Array fonts;
			int font_size = 0;

			Variant embedded_key;

			String language;
			Dictionary features;
			Variant meta;
		};
		Vector<Span> spans;

		struct EmbeddedObject {
			int pos = 0;
			InlineAlignment inline_align = INLINE_ALIGNMENT_CENTER;
			Rect2 rect;
		};
		HashMap<Variant, EmbeddedObject, VariantHasher, VariantComparator> objects;

		/* Shaped data */
		TextServer::Direction para_direction = DIRECTION_LTR; // Detected text direction.
		bool valid = false; // String is shaped.
		bool line_breaks_valid = false; // Line and word break flags are populated (and virtual zero width spaces inserted).
		bool justification_ops_valid = false; // Virtual elongation glyphs are added to the string.
		bool sort_valid = false;
		bool text_trimmed = false;

		bool preserve_invalid = true; // Draw hex code box instead of missing characters.
		bool preserve_control = false; // Draw control characters.

		double ascent = 0.0; // Ascent for horizontal layout, 1/2 of width for vertical.
		double descent = 0.0; // Descent for horizontal layout, 1/2 of width for vertical.
		double width = 0.0; // Width for horizontal layout, height for vertical.
		double width_trimmed = 0.0;
		int extra_spacing[4] = { 0, 0, 0, 0 };

		double upos = 0.0;
		double uthk = 0.0;

		TrimData overrun_trim_data;
		bool fit_width_minimum_reached = false;

		Vector<Glyph> glyphs;
		Vector<Glyph> glyphs_logical;
	};

	// Common data.

	double oversampling = 1.0;
	mutable RID_PtrOwner<FontFallback> font_owner;
	mutable RID_PtrOwner<ShapedTextDataFallback> shaped_owner;

	void _realign(ShapedTextDataFallback *p_sd) const;

protected:
	static void _bind_methods(){};

	void full_copy(ShapedTextDataFallback *p_shaped);
	void invalidate(ShapedTextDataFallback *p_shaped);

public:
	virtual bool has_feature(Feature p_feature) const override;
	virtual String get_name() const override;
	virtual int64_t get_features() const override;

	virtual void free_rid(const RID &p_rid) override;
	virtual bool has(const RID &p_rid) override;
	virtual bool load_support_data(const String &p_filename) override;

	virtual String get_support_data_filename() const override {
		return "";
	};
	virtual String get_support_data_info() const override {
		return "Not supported";
	};
	virtual bool save_support_data(const String &p_filename) const override;

	virtual bool is_locale_right_to_left(const String &p_locale) const override;

	virtual int64_t name_to_tag(const String &p_name) const override;
	virtual String tag_to_name(int64_t p_tag) const override;

	/* Font interface */
	virtual RID create_font() override;

	virtual void font_set_data(const RID &p_font_rid, const PackedByteArray &p_data) override;
	virtual void font_set_data_ptr(const RID &p_font_rid, const uint8_t *p_data_ptr, int64_t p_data_size) override;

	virtual void font_set_face_index(const RID &p_font_rid, int64_t p_index) override;
	virtual int64_t font_get_face_index(const RID &p_font_rid) const override;

	virtual int64_t font_get_face_count(const RID &p_font_rid) const override;

	virtual void font_set_style(const RID &p_font_rid, BitField<FontStyle> p_style) override;
	virtual BitField<FontStyle> font_get_style(const RID &p_font_rid) const override;

	virtual void font_set_style_name(const RID &p_font_rid, const String &p_name) override;
	virtual String font_get_style_name(const RID &p_font_rid) const override;

	virtual void font_set_name(const RID &p_font_rid, const String &p_name) override;
	virtual String font_get_name(const RID &p_font_rid) const override;

	virtual void font_set_antialiased(const RID &p_font_rid, bool p_antialiased) override;
	virtual bool font_is_antialiased(const RID &p_font_rid) const override;

	virtual void font_set_generate_mipmaps(const RID &p_font_rid, bool p_generate_mipmaps) override;
	virtual bool font_get_generate_mipmaps(const RID &p_font_rid) const override;

	virtual void font_set_multichannel_signed_distance_field(const RID &p_font_rid, bool p_msdf) override;
	virtual bool font_is_multichannel_signed_distance_field(const RID &p_font_rid) const override;

	virtual void font_set_msdf_pixel_range(const RID &p_font_rid, int64_t p_msdf_pixel_range) override;
	virtual int64_t font_get_msdf_pixel_range(const RID &p_font_rid) const override;

	virtual void font_set_msdf_size(const RID &p_font_rid, int64_t p_msdf_size) override;
	virtual int64_t font_get_msdf_size(const RID &p_font_rid) const override;

	virtual void font_set_fixed_size(const RID &p_font_rid, int64_t p_fixed_size) override;
	virtual int64_t font_get_fixed_size(const RID &p_font_rid) const override;

	virtual void font_set_force_autohinter(const RID &p_font_rid, bool p_force_autohinter) override;
	virtual bool font_is_force_autohinter(const RID &p_font_rid) const override;

	virtual void font_set_hinting(const RID &p_font_rid, TextServer::Hinting p_hinting) override;
	virtual TextServer::Hinting font_get_hinting(const RID &p_font_rid) const override;

	virtual void font_set_subpixel_positioning(const RID &p_font_rid, SubpixelPositioning p_subpixel) override;
	virtual SubpixelPositioning font_get_subpixel_positioning(const RID &p_font_rid) const override;

	virtual void font_set_embolden(const RID &p_font_rid, double p_strength) override;
	virtual double font_get_embolden(const RID &p_font_rid) const override;

	virtual void font_set_transform(const RID &p_font_rid, const Transform2D &p_transform) override;
	virtual Transform2D font_get_transform(const RID &p_font_rid) const override;

	virtual void font_set_variation_coordinates(const RID &p_font_rid, const Dictionary &p_variation_coordinates) override;
	virtual Dictionary font_get_variation_coordinates(const RID &p_font_rid) const override;

	virtual void font_set_oversampling(const RID &p_font_rid, double p_oversampling) override;
	virtual double font_get_oversampling(const RID &p_font_rid) const override;

	virtual Array font_get_size_cache_list(const RID &p_font_rid) const override;
	virtual void font_clear_size_cache(const RID &p_font_rid) override;
	virtual void font_remove_size_cache(const RID &p_font_rid, const Vector2i &p_size) override;

	virtual void font_set_ascent(const RID &p_font_rid, int64_t p_size, double p_ascent) override;
	virtual double font_get_ascent(const RID &p_font_rid, int64_t p_size) const override;

	virtual void font_set_descent(const RID &p_font_rid, int64_t p_size, double p_descent) override;
	virtual double font_get_descent(const RID &p_font_rid, int64_t p_size) const override;

	virtual void font_set_underline_position(const RID &p_font_rid, int64_t p_size, double p_underline_position) override;
	virtual double font_get_underline_position(const RID &p_font_rid, int64_t p_size) const override;

	virtual void font_set_underline_thickness(const RID &p_font_rid, int64_t p_size, double p_underline_thickness) override;
	virtual double font_get_underline_thickness(const RID &p_font_rid, int64_t p_size) const override;

	virtual void font_set_scale(const RID &p_font_rid, int64_t p_size, double p_scale) override;
	virtual double font_get_scale(const RID &p_font_rid, int64_t p_size) const override;

	virtual int64_t font_get_texture_count(const RID &p_font_rid, const Vector2i &p_size) const override;
	virtual void font_clear_textures(const RID &p_font_rid, const Vector2i &p_size) override;
	virtual void font_remove_texture(const RID &p_font_rid, const Vector2i &p_size, int64_t p_texture_index) override;

	virtual void font_set_texture_image(const RID &p_font_rid, const Vector2i &p_size, int64_t p_texture_index, const Ref<Image> &p_image) override;
	virtual Ref<Image> font_get_texture_image(const RID &p_font_rid, const Vector2i &p_size, int64_t p_texture_index) const override;

	virtual void font_set_texture_offsets(const RID &p_font_rid, const Vector2i &p_size, int64_t p_texture_index, const PackedInt32Array &p_offset) override;
	virtual PackedInt32Array font_get_texture_offsets(const RID &p_font_rid, const Vector2i &p_size, int64_t p_texture_index) const override;

	virtual Array font_get_glyph_list(const RID &p_font_rid, const Vector2i &p_size) const override;
	virtual void font_clear_glyphs(const RID &p_font_rid, const Vector2i &p_size) override;
	virtual void font_remove_glyph(const RID &p_font_rid, const Vector2i &p_size, int64_t p_glyph) override;

	virtual Vector2 font_get_glyph_advance(const RID &p_font_rid, int64_t p_size, int64_t p_glyph) const override;
	virtual void font_set_glyph_advance(const RID &p_font_rid, int64_t p_size, int64_t p_glyph, const Vector2 &p_advance) override;

	virtual Vector2 font_get_glyph_offset(const RID &p_font_rid, const Vector2i &p_size, int64_t p_glyph) const override;
	virtual void font_set_glyph_offset(const RID &p_font_rid, const Vector2i &p_size, int64_t p_glyph, const Vector2 &p_offset) override;

	virtual Vector2 font_get_glyph_size(const RID &p_font_rid, const Vector2i &p_size, int64_t p_glyph) const override;
	virtual void font_set_glyph_size(const RID &p_font_rid, const Vector2i &p_size, int64_t p_glyph, const Vector2 &p_gl_size) override;

	virtual Rect2 font_get_glyph_uv_rect(const RID &p_font_rid, const Vector2i &p_size, int64_t p_glyph) const override;
	virtual void font_set_glyph_uv_rect(const RID &p_font_rid, const Vector2i &p_size, int64_t p_glyph, const Rect2 &p_uv_rect) override;

	virtual int64_t font_get_glyph_texture_idx(const RID &p_font_rid, const Vector2i &p_size, int64_t p_glyph) const override;
	virtual void font_set_glyph_texture_idx(const RID &p_font_rid, const Vector2i &p_size, int64_t p_glyph, int64_t p_texture_idx) override;
	virtual RID font_get_glyph_texture_rid(const RID &p_font_rid, const Vector2i &p_size, int64_t p_glyph) const override;
	virtual Size2 font_get_glyph_texture_size(const RID &p_font_rid, const Vector2i &p_size, int64_t p_glyph) const override;

	virtual Dictionary font_get_glyph_contours(const RID &p_font, int64_t p_size, int64_t p_index) const override;

	virtual Array font_get_kerning_list(const RID &p_font_rid, int64_t p_size) const override;
	virtual void font_clear_kerning_map(const RID &p_font_rid, int64_t p_size) override;
	virtual void font_remove_kerning(const RID &p_font_rid, int64_t p_size, const Vector2i &p_glyph_pair) override;

	virtual void font_set_kerning(const RID &p_font_rid, int64_t p_size, const Vector2i &p_glyph_pair, const Vector2 &p_kerning) override;
	virtual Vector2 font_get_kerning(const RID &p_font_rid, int64_t p_size, const Vector2i &p_glyph_pair) const override;

	virtual int64_t font_get_glyph_index(const RID &p_font_rid, int64_t p_size, int64_t p_char, int64_t p_variation_selector = 0) const override;

	virtual bool font_has_char(const RID &p_font_rid, int64_t p_char) const override;
	virtual String font_get_supported_chars(const RID &p_font_rid) const override;

	virtual void font_render_range(const RID &p_font, const Vector2i &p_size, int64_t p_start, int64_t p_end) override;
	virtual void font_render_glyph(const RID &p_font_rid, const Vector2i &p_size, int64_t p_index) override;

	virtual void font_draw_glyph(const RID &p_font, const RID &p_canvas, int64_t p_size, const Vector2 &p_pos, int64_t p_index, const Color &p_color = Color(1, 1, 1)) const override;
	virtual void font_draw_glyph_outline(const RID &p_font, const RID &p_canvas, int64_t p_size, int64_t p_outline_size, const Vector2 &p_pos, int64_t p_index, const Color &p_color = Color(1, 1, 1)) const override;

	virtual bool font_is_language_supported(const RID &p_font_rid, const String &p_language) const override;
	virtual void font_set_language_support_override(const RID &p_font_rid, const String &p_language, bool p_supported) override;
	virtual bool font_get_language_support_override(const RID &p_font_rid, const String &p_language) override;
	virtual void font_remove_language_support_override(const RID &p_font_rid, const String &p_language) override;
	virtual PackedStringArray font_get_language_support_overrides(const RID &p_font_rid) override;

	virtual bool font_is_script_supported(const RID &p_font_rid, const String &p_script) const override;
	virtual void font_set_script_support_override(const RID &p_font_rid, const String &p_script, bool p_supported) override;
	virtual bool font_get_script_support_override(const RID &p_font_rid, const String &p_script) override;
	virtual void font_remove_script_support_override(const RID &p_font_rid, const String &p_script) override;
	virtual PackedStringArray font_get_script_support_overrides(const RID &p_font_rid) override;

	virtual void font_set_opentype_feature_overrides(const RID &p_font_rid, const Dictionary &p_overrides) override;
	virtual Dictionary font_get_opentype_feature_overrides(const RID &p_font_rid) const override;

	virtual Dictionary font_supported_feature_list(const RID &p_font_rid) const override;
	virtual Dictionary font_supported_variation_list(const RID &p_font_rid) const override;

	virtual double font_get_global_oversampling() const override;
	virtual void font_set_global_oversampling(double p_oversampling) override;

	/* Shaped text buffer interface */

	virtual RID create_shaped_text(Direction p_direction = DIRECTION_AUTO, Orientation p_orientation = ORIENTATION_HORIZONTAL) override;

	virtual void shaped_text_clear(const RID &p_shaped) override;

	virtual void shaped_text_set_direction(const RID &p_shaped, Direction p_direction = DIRECTION_AUTO) override;
	virtual Direction shaped_text_get_direction(const RID &p_shaped) const override;
	virtual Direction shaped_text_get_inferred_direction(const RID &p_shaped) const override;

	virtual void shaped_text_set_bidi_override(const RID &p_shaped, const Array &p_override) override;

	virtual void shaped_text_set_custom_punctuation(const RID &p_shaped, const String &p_punct) override;
	virtual String shaped_text_get_custom_punctuation(const RID &p_shaped) const override;

	virtual void shaped_text_set_orientation(const RID &p_shaped, Orientation p_orientation = ORIENTATION_HORIZONTAL) override;
	virtual Orientation shaped_text_get_orientation(const RID &p_shaped) const override;

	virtual void shaped_text_set_preserve_invalid(const RID &p_shaped, bool p_enabled) override;
	virtual bool shaped_text_get_preserve_invalid(const RID &p_shaped) const override;

	virtual void shaped_text_set_preserve_control(const RID &p_shaped, bool p_enabled) override;
	virtual bool shaped_text_get_preserve_control(const RID &p_shaped) const override;

	virtual void shaped_text_set_spacing(const RID &p_shaped, SpacingType p_spacing, int64_t p_value) override;
	virtual int64_t shaped_text_get_spacing(const RID &p_shaped, SpacingType p_spacing) const override;

	virtual bool shaped_text_add_string(const RID &p_shaped, const String &p_text, const Array &p_fonts, int64_t p_size, const Dictionary &p_opentype_features = Dictionary(), const String &p_language = "", const Variant &p_meta = Variant()) override;
	virtual bool shaped_text_add_object(const RID &p_shaped, const Variant &p_key, const Size2 &p_size, InlineAlignment p_inline_align = INLINE_ALIGNMENT_CENTER, int64_t p_length = 1) override;
	virtual bool shaped_text_resize_object(const RID &p_shaped, const Variant &p_key, const Size2 &p_size, InlineAlignment p_inline_align = INLINE_ALIGNMENT_CENTER) override;

	virtual int64_t shaped_get_span_count(const RID &p_shaped) const override;
	virtual Variant shaped_get_span_meta(const RID &p_shaped, int64_t p_index) const override;
	virtual void shaped_set_span_update_font(const RID &p_shaped, int64_t p_index, const Array &p_fonts, int64_t p_size, const Dictionary &p_opentype_features = Dictionary()) override;

	virtual RID shaped_text_substr(const RID &p_shaped, int64_t p_start, int64_t p_length) const override;
	virtual RID shaped_text_get_parent(const RID &p_shaped) const override;

	virtual double shaped_text_fit_to_width(const RID &p_shaped, double p_width, BitField<TextServer::JustificationFlag> p_jst_flags = JUSTIFICATION_WORD_BOUND | JUSTIFICATION_KASHIDA) override;
	virtual double shaped_text_tab_align(const RID &p_shaped, const PackedFloat32Array &p_tab_stops) override;

	virtual bool shaped_text_shape(const RID &p_shaped) override;
	virtual bool shaped_text_update_breaks(const RID &p_shaped) override;
	virtual bool shaped_text_update_justification_ops(const RID &p_shaped) override;

	virtual int64_t shaped_text_get_trim_pos(const RID &p_shaped) const override;
	virtual int64_t shaped_text_get_ellipsis_pos(const RID &p_shaped) const override;
	virtual const Glyph *shaped_text_get_ellipsis_glyphs(const RID &p_shaped) const override;
	virtual int64_t shaped_text_get_ellipsis_glyph_count(const RID &p_shaped) const override;

	virtual void shaped_text_overrun_trim_to_width(const RID &p_shaped, double p_width, BitField<TextServer::TextOverrunFlag> p_trim_flags) override;

	virtual bool shaped_text_is_ready(const RID &p_shaped) const override;

	virtual const Glyph *shaped_text_get_glyphs(const RID &p_shaped) const override;
	virtual const Glyph *shaped_text_sort_logical(const RID &p_shaped) override;
	virtual int64_t shaped_text_get_glyph_count(const RID &p_shaped) const override;

	virtual Vector2i shaped_text_get_range(const RID &p_shaped) const override;

	virtual Array shaped_text_get_objects(const RID &p_shaped) const override;
	virtual Rect2 shaped_text_get_object_rect(const RID &p_shaped, const Variant &p_key) const override;

	virtual Size2 shaped_text_get_size(const RID &p_shaped) const override;
	virtual double shaped_text_get_ascent(const RID &p_shaped) const override;
	virtual double shaped_text_get_descent(const RID &p_shaped) const override;
	virtual double shaped_text_get_width(const RID &p_shaped) const override;
	virtual double shaped_text_get_underline_position(const RID &p_shaped) const override;
	virtual double shaped_text_get_underline_thickness(const RID &p_shaped) const override;

	virtual PackedInt32Array string_get_word_breaks(const String &p_string, const String &p_language = "") const override;

	virtual String string_to_upper(const String &p_string, const String &p_language = "") const override;
	virtual String string_to_lower(const String &p_string, const String &p_language = "") const override;

	TextServerFallback();
	~TextServerFallback();
};

#endif // TEXT_SERVER_FB_H
