//SPDX-License-Identifier: GPL-2.1-or-later
/*

   Copyright (C) 2007-2020 Cyril Hrubis <metan@ucw.cz>

 */

#include <widgets/gp_widgets.h>
#include <mupdf/fitz.h>

struct document {
	int page_count;
	int cur_page;
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

static void draw_page(gp_widget_event *ev)
{
	gp_widget *self = ev->self;
	gp_pixmap *pixmap = self->pixmap->pixmap;

	GP_DEBUG(1, "Redrawing canvas %ux%u", pixmap->w, pixmap->h);

	struct document *doc = controls.doc;

	if (!doc->fz_ctx) {
		gp_fill(pixmap, ev->ctx->fg_color);
		return;
	}

	// Determines page size at 72 DPI
	fz_rect rect = fz_bound_page(doc->fz_ctx, doc->fz_pg);

	GP_DEBUG(1, "Page bounding box %fx%f - %fx%f",
	         rect.x0, rect.y0, rect.x1, rect.y1);

	// Find ZOOM fit size
	float x_rat = 1.00 * pixmap->w / (rect.x1 - rect.x0);
	float y_rat = 1.00 * pixmap->h / (rect.y1 - rect.y0);
	float rat = GP_MIN(x_rat, y_rat);

	doc->page_transform = fz_scale(rat, rat);

	fz_pixmap *pix;

	pix = fz_new_pixmap_from_page_number(doc->fz_ctx, doc->fz_doc, doc->cur_page,
	                                     doc->page_transform, fz_device_bgr(doc->fz_ctx), 0);

	// Blit the pixmap
	GP_DEBUG(1, "Blitting context");
	gp_pixmap page;
	//TODO: Fill only the corners
	gp_fill(pixmap, ev->ctx->bg_color);

	gp_pixmap_init(&page, pix->w, pix->h, GP_PIXEL_RGB888, pix->samples);

	controls.x_off = (pixmap->w - page.w)/2;
	controls.y_off = (pixmap->h - page.h)/2;

	gp_blit(&page, 0, 0, page.w, page.h, pixmap, controls.x_off, controls.y_off);

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

	gp_widget_textbox_printf(controls.pg_nr, "%i", doc->cur_page+1);
}

static int load_document(struct document *doc, const char *filename)
{
	fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);

	if (!ctx)
		return 1;

	fz_register_document_handlers(ctx);

	doc->fz_ctx = ctx;
	doc->fz_doc = fz_open_document(ctx, (char*)filename);
	doc->page_count = fz_count_pages(ctx, doc->fz_doc);
	doc->cur_page = -1;

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
	gp_widget_pixmap_update(controls.page);
}

static void load_page_and_redraw(int page)
{
	load_page(controls.doc, page);
	gp_widget_redraw(controls.page);
	gp_widget_pixmap_update(controls.page);
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
		tbox->tbox->filter = GP_TEXT_BOX_FILTER_INT;
	break;
	case GP_WIDGET_EVENT_ACTION:
		load_page_and_redraw(atoi(tbox->tbox->buf) - 1);
	break;
	case GP_WIDGET_EVENT_FILTER:
		return page_number_check(ev);
	default:
		return 0;
	}

	return 1;
}

int button_prev_event(gp_widget_event *ev)
{
	if (ev->type == GP_WIDGET_EVENT_ACTION)
		load_and_redraw(controls.doc, -1);

	return 0;
}

int button_next_event(gp_widget_event *ev)
{
	if (ev->type == GP_WIDGET_EVENT_ACTION)
		load_and_redraw(controls.doc, 1);

	return 0;
}

int button_first_event(gp_widget_event *ev)
{
	if (ev->type == GP_WIDGET_EVENT_ACTION)
		load_page_and_redraw(0);

	return 0;
}

int button_last_event(gp_widget_event *ev)
{
	if (ev->type == GP_WIDGET_EVENT_ACTION)
		load_page_and_redraw(controls.doc->page_count - 1);

	return 0;
}

int textbox_search_event(gp_widget_event *ev)
{
	struct document *doc = controls.doc;
	gp_widget *tbox = ev->self;

	if (ev->type != GP_WIDGET_EVENT_ACTION)
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
	gp_widget_event_dump(ev);

	switch (ev->type) {
	case GP_WIDGET_EVENT_RESIZE:
		allocate_backing_pixmap(ev);
		draw_page(ev);
	break;
	case GP_WIDGET_EVENT_REDRAW:
		draw_page(ev);
	break;
	default:
	break;
	}

	return 0;
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
			load_and_redraw(doc, 1);
		break;
		case GP_KEY_LEFT:
		case GP_KEY_UP:
		case GP_KEY_PAGE_UP:
			load_and_redraw(doc, -1);
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

int main(int argc, char *argv[])
{
	void *uids;
	struct document doc = {};

	gp_widgets_register_callback(app_ev_callback);
	gp_widgets_getopt(&argc, &argv);

	if (argc && load_document(&doc, argv[0])) {
		GP_WARN("Can't load document '%s'", argv[0]);
		return 1;
	}

	gp_widget *layout = gp_app_layout_load("gppdf", &uids);

	controls.doc = &doc;
	controls.page = gp_widget_by_uid(uids, "page", GP_WIDGET_PIXMAP);
	controls.pg_cnt = gp_widget_by_uid(uids, "pg_cnt", GP_WIDGET_LABEL);
	controls.pg_nr = gp_widget_by_uid(uids, "pg_nr", GP_WIDGET_TEXTBOX);

	gp_widget_label_printf(controls.pg_cnt, "of %i", doc.page_count);

	gp_widget_event_unmask(controls.page, GP_WIDGET_EVENT_RESIZE);
	gp_widget_event_unmask(controls.page, GP_WIDGET_EVENT_REDRAW);

	controls.page->on_event = pixmap_on_event;

	gp_widgets_main_loop(layout, "gppdf", NULL, 0, NULL);

	return 0;
}
