//SPDX-License-Identifier: GPL-2.1-or-later
/*

   Copyright (C) 2007-2022 Cyril Hrubis <metan@ucw.cz>

 */

#include <widgets/gp_widgets.h>
#include <filters/gp_point.h>
#include <filters/gp_rotate.h>
#include <mupdf/fitz.h>

enum orientation {
	ROTATE_0,
	ROTATE_90,
	ROTATE_180,
	ROTATE_270,
};

struct document {
	int page_count;
	int cur_page;

	enum orientation orientation;

	fz_matrix page_transform;
	fz_context *fz_ctx;
	fz_document *fz_doc;
	fz_page *fz_pg;
};

static struct controls {
	gp_widget *page;
	unsigned int x_off;
	unsigned int y_off;
	gp_widget *pg_cnt;
	gp_widget *pg_nr;
	struct document *doc;
} controls;

static void draw_page(void)
{
	gp_widget *self = controls.page;
	gp_pixmap *pixmap = self->pixmap->pixmap;
	const gp_widget_render_ctx *ctx = gp_widgets_render_ctx();

	GP_DEBUG(1, "Redrawing canvas %ux%u", pixmap->w, pixmap->h);

	struct document *doc = controls.doc;

	if (!doc->fz_ctx) {
		gp_fill(pixmap, ctx->fg_color);
		return;
	}

	// Determines page size at 72 DPI
	fz_rect rect = fz_bound_page(doc->fz_ctx, doc->fz_pg);

	GP_DEBUG(1, "Page bounding box %fx%f - %fx%f",
	         rect.x0, rect.y0, rect.x1, rect.y1);

	gp_size w = gp_pixmap_w(pixmap);
	gp_size h = gp_pixmap_h(pixmap);

	switch (doc->orientation) {
	case ROTATE_0:
	case ROTATE_180:
	break;
	case ROTATE_90:
	case ROTATE_270:
		GP_SWAP(w, h);
	break;
	}

	// Find ZOOM fit size
	float x_rat = 1.00 * w / (rect.x1 - rect.x0);
	float y_rat = 1.00 * h / (rect.y1 - rect.y0);
	float rat = GP_MIN(x_rat, y_rat);

	doc->page_transform = fz_scale(rat, rat);

	fz_pixmap *pix;

	pix = fz_new_pixmap_from_page_number(doc->fz_ctx, doc->fz_doc, doc->cur_page,
	                                     doc->page_transform, fz_device_bgr(doc->fz_ctx), 0);

	// Blit the pixmap
	GP_DEBUG(1, "Blitting context");
	gp_pixmap page;

	//TODO: Fill only the corners
	gp_fill(pixmap, ctx->bg_color);

	gp_pixmap_init(&page, pix->w, pix->h, GP_PIXEL_RGB888, pix->samples, 0);

	switch (doc->orientation) {
	case ROTATE_0:
	break;
	case ROTATE_90:
		gp_pixmap_rotate_cw(&page);
	/* fallthrough */
	case ROTATE_180:
		gp_pixmap_rotate_cw(&page);
	/* fallthrough */
	case ROTATE_270:
		gp_pixmap_rotate_cw(&page);
	break;
	}

	controls.x_off = (pixmap->w - gp_pixmap_w(&page))/2;
	controls.y_off = (pixmap->h - gp_pixmap_h(&page))/2;

	if (ctx->color_scheme == GP_WIDGET_COLOR_SCHEME_DARK)
		gp_filter_invert(&page, &page, NULL);

	gp_blit_clipped(&page, 0, 0, gp_pixmap_w(&page), gp_pixmap_h(&page), pixmap, controls.x_off, controls.y_off);

	fz_drop_pixmap(doc->fz_ctx, pix);
}

static void load_page(struct document *doc, int page)
{
	if (page < 0 || page >= doc->page_count) {
		GP_WARN("Page %i out of max pages %i",
		         page, doc->page_count);
		return;
	}

	if (doc->cur_page != -1)
		fz_drop_page(doc->fz_ctx, doc->fz_pg);

	doc->cur_page = page;
	doc->fz_pg = fz_load_page(doc->fz_ctx, doc->fz_doc, doc->cur_page);

	if (controls.page)
		draw_page();

	gp_widget_tbox_printf(controls.pg_nr, "%i", doc->cur_page+1);
}

static int load_document(struct document *doc, const char *filename)
{
	fz_context *f_ctx;

	if (doc->fz_doc) {
		fz_drop_document(doc->fz_ctx, doc->fz_doc);
		doc->fz_doc = NULL;
	}

	if (doc->fz_ctx) {
		fz_drop_context(doc->fz_ctx);
		doc->fz_ctx = NULL;
	}

	f_ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
	if (!f_ctx)
		return 1;

	fz_try(f_ctx) {
		fz_register_document_handlers(f_ctx);
	}
	fz_catch(f_ctx) {
		gp_dialog_msg_printf_run(GP_DIALOG_MSG_ERR,
		                         "Failed to register document handlers",
		                         "%s", fz_caught_message(f_ctx));
		fz_drop_context(f_ctx);
		return 1;
	}

	fz_try(f_ctx) {
		doc->fz_doc = fz_open_document(f_ctx, (char*)filename);
	}
	fz_catch(f_ctx) {
		gp_dialog_msg_printf_run(GP_DIALOG_MSG_ERR,
		                         "Failed to open document",
		                         "%s", fz_caught_message(f_ctx));
		fz_drop_context(f_ctx);
		return 1;
	}

	doc->fz_ctx = f_ctx;

	doc->page_count = fz_count_pages(doc->fz_ctx, doc->fz_doc);
	doc->cur_page = -1;

	gp_widget_label_printf(controls.pg_cnt, "of %i", controls.doc->page_count);

	load_page(doc, 0);

	return 0;
}

static void load_next_page(struct document *doc, int i)
{
	if (doc->cur_page + i < 0 || doc->cur_page + i >= doc->page_count) {
		GP_DEBUG(1, "No next/prev page.");
		return;
	}

	load_page(doc, doc->cur_page + i);
}

static void load_and_redraw(struct document *doc, int i)
{
	load_next_page(doc, i);
	gp_widget_redraw(controls.page);
}

static void load_page_and_redraw(int page)
{
	load_page(controls.doc, page);
	gp_widget_redraw(controls.page);
}

static int page_number_check(gp_widget_event *ev)
{
	gp_widget *tbox = ev->self;
	int val = atoi(tbox->tbox->buf) * 10 + ev->val - '0';

	if (val <= 0 || val > controls.doc->page_count)
		return 1;

	return 0;
}

int load_page_event(gp_widget_event *ev)
{
	gp_widget *tbox = ev->self;

	switch (ev->type) {
	case GP_WIDGET_EVENT_NEW:
		tbox->tbox->filter = GP_TBOX_FILTER_INT;
	break;
	case GP_WIDGET_EVENT_WIDGET:
		switch (ev->sub_type) {
		case GP_WIDGET_TBOX_TRIGGER:
			load_page_and_redraw(atoi(tbox->tbox->buf) - 1);
		break;
		case GP_WIDGET_TBOX_FILTER:
			return page_number_check(ev);
		break;
		}
	break;
	default:
		return 0;
	}

	return 1;
}

int button_prev_event(gp_widget_event *ev)
{
	if (ev->type == GP_WIDGET_EVENT_WIDGET)
		load_and_redraw(controls.doc, -1);

	return 0;
}

int button_next_event(gp_widget_event *ev)
{
	if (ev->type == GP_WIDGET_EVENT_WIDGET)
		load_and_redraw(controls.doc, 1);

	return 0;
}

int button_first_event(gp_widget_event *ev)
{
	if (ev->type == GP_WIDGET_EVENT_WIDGET)
		load_page_and_redraw(0);

	return 0;
}

int button_last_event(gp_widget_event *ev)
{
	if (ev->type == GP_WIDGET_EVENT_WIDGET)
		load_page_and_redraw(controls.doc->page_count - 1);

	return 0;
}

int button_open_file(gp_widget_event *ev)
{
	gp_dialog *dialog;

	if (ev->type != GP_WIDGET_EVENT_WIDGET)
		return 0;

	dialog = gp_dialog_file_open_new(NULL);
	if (gp_dialog_run(dialog) == GP_WIDGET_DIALOG_PATH)
		load_document(controls.doc, gp_dialog_file_path(dialog));

	gp_widget_redraw(controls.page);

	gp_dialog_free(dialog);

	return 0;
}

int tbox_search_event(gp_widget_event *ev)
{
	struct document *doc = controls.doc;
	gp_widget *tbox = ev->self;

	if (ev->type != GP_WIDGET_EVENT_WIDGET)
		return 0;

	if (ev->sub_type != GP_WIDGET_TBOX_TRIGGER)
		return 0;

	fz_rect page_bbox = fz_bound_page(doc->fz_ctx, doc->fz_pg);
	fz_stext_page *text = fz_new_stext_page(doc->fz_ctx, page_bbox);
	fz_stext_options opts = {0};
	fz_device *text_dev = fz_new_stext_device(doc->fz_ctx, text, &opts);

	fz_run_page_contents(doc->fz_ctx, doc->fz_pg, text_dev, doc->page_transform, NULL);

	int i, ret;
	fz_quad hitbox[128];

	ret = fz_search_stext_page(doc->fz_ctx, text, tbox->tbox->buf, hitbox, 128);

	gp_pixmap *p = controls.page->pixmap->pixmap;

	for (i = 0; i < ret; i++) {
		unsigned int x0 = controls.x_off + hitbox[i].ul.x;
		unsigned int y0 = controls.y_off + hitbox[i].ul.y;
		unsigned int x1 = controls.x_off + hitbox[i].lr.x;
		unsigned int y1 = controls.y_off + hitbox[i].lr.y;

		gp_rect_xyxy(p, x0, y0, x1, y1, 0xff0000);
	}

	if (ret)
		gp_widget_redraw(controls.page);

	return 1;
}

static void allocate_backing_pixmap(gp_widget_event *ev)
{
	gp_widget *w = ev->self;

	gp_pixmap_free(w->pixmap->pixmap);

	w->pixmap->pixmap = gp_pixmap_alloc(w->w, w->h, ev->ctx->pixel_type);
}

int pixmap_on_event(gp_widget_event *ev)
{
	switch (ev->type) {
	case GP_WIDGET_EVENT_RESIZE:
		allocate_backing_pixmap(ev);
		draw_page();
	break;
	case GP_WIDGET_EVENT_COLOR_SCHEME:
		draw_page();
	break;
	default:
	break;
	}

	return 0;
}

static void do_rotate_cw(void)
{
	struct document *doc = controls.doc;

	if (doc->orientation == ROTATE_270)
		doc->orientation = ROTATE_0;
	else
		doc->orientation++;

	draw_page();
	gp_widget_redraw(controls.page);
}

static void do_rotate_ccw(void)
{
	struct document *doc = controls.doc;

	if (doc->orientation == ROTATE_0)
		doc->orientation = ROTATE_270;
	else
		doc->orientation--;

	draw_page();
	gp_widget_redraw(controls.page);
}

int rotate_cw(gp_widget_event *ev)
{
	if (ev->type != GP_WIDGET_EVENT_WIDGET)
		return 0;

	do_rotate_cw();

	return 1;
}

int rotate_ccw(gp_widget_event *ev)
{
	if (ev->type != GP_WIDGET_EVENT_WIDGET)
		return 0;

	do_rotate_ccw();

	return 1;
}

static int app_ev_callback(gp_event *ev)
{
	struct document *doc = controls.doc;

	switch (ev->type) {
	case GP_EV_KEY:
		if (ev->code == GP_EV_KEY_UP)
			return 0;

		switch (ev->key.key) {
		case GP_KEY_RIGHT:
		case GP_KEY_PAGE_DOWN:
		case GP_KEY_DOWN:
		case GP_KEY_SPACE:
			load_and_redraw(doc, 1);
		break;
		case GP_KEY_LEFT:
		case GP_KEY_UP:
		case GP_KEY_PAGE_UP:
		case GP_KEY_BACKSPACE:
			load_and_redraw(doc, -1);
		break;
		case GP_KEY_R:
			do_rotate_cw();
		break;
		//case GP_KEY_F:
		//	toggle_fullscreen();
		//break;
		}
	break;
	default:
		return 0;
	}

	return 1;
}

static void app_init(int argc, char *argv[])
{
	if (!argc)
		return;

	load_document(controls.doc, argv[0]);
}

int main(int argc, char *argv[])
{
	gp_htable *uids;
	struct document doc = {};

	gp_widgets_register_callback(app_ev_callback);

	gp_widget *layout = gp_app_layout_load("gppdf", &uids);

	controls.doc = &doc;
	controls.page = gp_widget_by_uid(uids, "page", GP_WIDGET_PIXMAP);
	controls.pg_cnt = gp_widget_by_uid(uids, "pg_cnt", GP_WIDGET_LABEL);
	controls.pg_nr = gp_widget_by_uid(uids, "pg_nr", GP_WIDGET_TBOX);

	gp_widget_event_unmask(controls.page, GP_WIDGET_EVENT_COLOR_SCHEME);
	gp_widget_event_unmask(controls.page, GP_WIDGET_EVENT_RESIZE);

	controls.page->on_event = pixmap_on_event;

	gp_widgets_main_loop(layout, "gppdf", app_init, argc, argv);

	return 0;
}
