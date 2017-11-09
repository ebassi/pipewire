/* Spa
 * Copyright (C) 2016 Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) 2016 Axis Communications AB
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/timerfd.h>

#include <spa/type-map.h>
#include <spa/clock.h>
#include <spa/log.h>
#include <spa/loop.h>
#include <spa/node.h>
#include <spa/param-alloc.h>
#include <spa/list.h>
#include <spa/video/format-utils.h>

#include <lib/pod.h>

#define NAME "videotestsrc"

#define FRAMES_TO_TIME(this,f) ((this->current_format.info.raw.framerate.denom * (f) * SPA_NSEC_PER_SEC) / \
                                (this->current_format.info.raw.framerate.num))

struct type {
	uint32_t node;
	uint32_t clock;
	uint32_t format;
	uint32_t props;
	uint32_t prop_live;
	uint32_t prop_pattern;
	uint32_t pattern_smpte_snow;
	uint32_t pattern_snow;
	struct spa_type_param param;
	struct spa_type_meta meta;
	struct spa_type_data data;
	struct spa_type_media_type media_type;
	struct spa_type_media_subtype media_subtype;
	struct spa_type_format_video format_video;
	struct spa_type_video_format video_format;
	struct spa_type_event_node event_node;
	struct spa_type_command_node command_node;
	struct spa_type_param_alloc_buffers param_alloc_buffers;
	struct spa_type_param_alloc_meta_enable param_alloc_meta_enable;
};

static inline void init_type(struct type *type, struct spa_type_map *map)
{
	type->node = spa_type_map_get_id(map, SPA_TYPE__Node);
	type->clock = spa_type_map_get_id(map, SPA_TYPE__Clock);
	type->format = spa_type_map_get_id(map, SPA_TYPE__Format);
	type->props = spa_type_map_get_id(map, SPA_TYPE__Props);
	type->prop_live = spa_type_map_get_id(map, SPA_TYPE_PROPS__live);
	type->prop_pattern = spa_type_map_get_id(map, SPA_TYPE_PROPS__patternType);
	type->pattern_smpte_snow = spa_type_map_get_id(map, SPA_TYPE_PROPS__patternType ":smpte-snow");
	type->pattern_snow = spa_type_map_get_id(map, SPA_TYPE_PROPS__patternType ":snow");
	spa_type_param_map(map, &type->param);
	spa_type_meta_map(map, &type->meta);
	spa_type_data_map(map, &type->data);
	spa_type_media_type_map(map, &type->media_type);
	spa_type_media_subtype_map(map, &type->media_subtype);
	spa_type_format_video_map(map, &type->format_video);
	spa_type_video_format_map(map, &type->video_format);
	spa_type_event_node_map(map, &type->event_node);
	spa_type_command_node_map(map, &type->command_node);
	spa_type_param_alloc_buffers_map(map, &type->param_alloc_buffers);
	spa_type_param_alloc_meta_enable_map(map, &type->param_alloc_meta_enable);
}

struct props {
	bool live;
	uint32_t pattern;
};

#define MAX_BUFFERS 16
#define MAX_PORTS 1

struct buffer {
	struct spa_buffer *outbuf;
	bool outstanding;
	struct spa_meta_header *h;
	struct spa_list link;
};

struct impl {
	struct spa_handle handle;
	struct spa_node node;
	struct spa_clock clock;

	struct type type;
	struct spa_type_map *map;
	struct spa_log *log;
	struct spa_loop *data_loop;

	struct props props;

	const struct spa_node_callbacks *callbacks;
	void *callbacks_data;

	bool async;
	struct spa_source timer_source;
	struct itimerspec timerspec;

	struct spa_port_info info;
	struct spa_port_io *io;

	bool have_format;
	struct spa_video_info current_format;
	size_t bpp;
	int stride;

	struct buffer buffers[MAX_BUFFERS];
	uint32_t n_buffers;

	bool started;
	uint64_t start_time;
	uint64_t elapsed_time;

	uint64_t frame_count;
	struct spa_list empty;
};

#define CHECK_PORT(this,d,p)  ((d) == SPA_DIRECTION_OUTPUT && (p) < MAX_PORTS)

#define DEFAULT_LIVE false
#define DEFAULT_PATTERN pattern_smpte_snow

static void reset_props(struct impl *this, struct props *props)
{
	props->live = DEFAULT_LIVE;
	props->pattern = this->type.DEFAULT_PATTERN;
}

static int impl_node_enum_params(struct spa_node *node,
				 uint32_t id, uint32_t *index,
				 const struct spa_pod_object *filter,
				 struct spa_pod_builder *builder)
{
	struct impl *this;
	struct type *t;
	uint32_t offset;
	struct spa_pod *param;

	spa_return_val_if_fail(node != NULL, SPA_RESULT_INVALID_ARGUMENTS);
	spa_return_val_if_fail(builder != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	this = SPA_CONTAINER_OF(node, struct impl, node);
	t = &this->type;

	offset = builder->offset;

      next:
	if (id == t->param.idList) {
		if (*index > 0)
			return SPA_RESULT_ENUM_END;

		param = spa_pod_builder_object(builder,
			id, t->param.List,
			":", t->param.listId,   "I",  t->param.idProps);
	}
	else if (id == t->param.idProps) {
		struct props *p = &this->props;

		if (*index > 0)
			return SPA_RESULT_ENUM_END;

		param = spa_pod_builder_object(builder,
			id, t->props,
			":", t->prop_live,    "b",  p->live,
			":", t->prop_pattern, "Ie", p->pattern,
							2, t->pattern_smpte_snow,
							   t->pattern_snow);
	}
	else
		return SPA_RESULT_UNKNOWN_PARAM;

	(*index)++;

	spa_pod_builder_reset(builder, offset);
	if (spa_pod_filter(builder, param, (struct spa_pod*)filter) < 0)
		goto next;

	return SPA_RESULT_OK;
}

static int impl_node_set_param(struct spa_node *node, uint32_t id, uint32_t flags,
			       const struct spa_pod_object *param)
{
	struct impl *this;
	struct type *t;

	spa_return_val_if_fail(node != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	this = SPA_CONTAINER_OF(node, struct impl, node);
	t = &this->type;

	if (id == t->param.idProps) {
		struct props *p = &this->props;

		spa_pod_object_parse(param,
			":", t->prop_live,    "?b", &p->live,
			":", t->prop_pattern, "?I", &p->pattern,
			NULL);

		if (p->live)
			this->info.flags |= SPA_PORT_INFO_FLAG_LIVE;
		else
			this->info.flags &= ~SPA_PORT_INFO_FLAG_LIVE;
	}
	else
		return SPA_RESULT_UNKNOWN_PARAM;

	return SPA_RESULT_OK;
}

#include "draw.c"

static int fill_buffer(struct impl *this, struct buffer *b)
{
	return draw(this, b->outbuf->datas[0].data);
}

static void set_timer(struct impl *this, bool enabled)
{
	if (this->async || this->props.live) {
		if (enabled) {
			if (this->props.live) {
				uint64_t next_time = this->start_time + this->elapsed_time;
				this->timerspec.it_value.tv_sec = next_time / SPA_NSEC_PER_SEC;
				this->timerspec.it_value.tv_nsec = next_time % SPA_NSEC_PER_SEC;
			} else {
				this->timerspec.it_value.tv_sec = 0;
				this->timerspec.it_value.tv_nsec = 1;
			}
		} else {
			this->timerspec.it_value.tv_sec = 0;
			this->timerspec.it_value.tv_nsec = 0;
		}
		timerfd_settime(this->timer_source.fd, TFD_TIMER_ABSTIME, &this->timerspec, NULL);
	}
}

static void read_timer(struct impl *this)
{
	uint64_t expirations;

	if (this->async || this->props.live) {
		if (read(this->timer_source.fd, &expirations, sizeof(uint64_t)) != sizeof(uint64_t))
			perror("read timerfd");
	}
}

static int make_buffer(struct impl *this)
{
	struct buffer *b;
	struct spa_port_io *io = this->io;
	int n_bytes;

	read_timer(this);

	if (spa_list_is_empty(&this->empty)) {
		set_timer(this, false);
		spa_log_error(this->log, NAME " %p: out of buffers", this);
		return SPA_RESULT_OUT_OF_BUFFERS;
	}
	b = spa_list_first(&this->empty, struct buffer, link);
	spa_list_remove(&b->link);
	b->outstanding = true;

	n_bytes = b->outbuf->datas[0].maxsize;

	spa_log_trace(this->log, NAME " %p: dequeue buffer %d", this, b->outbuf->id);

	fill_buffer(this, b);

	b->outbuf->datas[0].chunk->offset = 0;
	b->outbuf->datas[0].chunk->size = n_bytes;
	b->outbuf->datas[0].chunk->stride = this->stride;

	if (b->h) {
		b->h->seq = this->frame_count;
		b->h->pts = this->start_time + this->elapsed_time;
		b->h->dts_offset = 0;
	}

	this->frame_count++;
	this->elapsed_time = FRAMES_TO_TIME(this, this->frame_count);
	set_timer(this, true);

	io->buffer_id = b->outbuf->id;
	io->status = SPA_RESULT_HAVE_BUFFER;

	return io->status;
}

static void on_output(struct spa_source *source)
{
	struct impl *this = source->data;
	int res;

	res = make_buffer(this);

	if (res == SPA_RESULT_HAVE_BUFFER)
		this->callbacks->have_output(this->callbacks_data);
}

static int impl_node_send_command(struct spa_node *node, const struct spa_command *command)
{
	struct impl *this;

	spa_return_val_if_fail(node != NULL, SPA_RESULT_INVALID_ARGUMENTS);
	spa_return_val_if_fail(command != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	this = SPA_CONTAINER_OF(node, struct impl, node);

	if (SPA_COMMAND_TYPE(command) == this->type.command_node.Start) {
		struct timespec now;

		if (!this->have_format)
			return SPA_RESULT_NO_FORMAT;

		if (this->n_buffers == 0)
			return SPA_RESULT_NO_BUFFERS;

		if (this->started)
			return SPA_RESULT_OK;

		clock_gettime(CLOCK_MONOTONIC, &now);
		if (this->props.live)
			this->start_time = SPA_TIMESPEC_TO_TIME(&now);
		else
			this->start_time = 0;
		this->frame_count = 0;
		this->elapsed_time = 0;

		this->started = true;
		set_timer(this, true);
	} else if (SPA_COMMAND_TYPE(command) == this->type.command_node.Pause) {
		if (!this->have_format)
			return SPA_RESULT_NO_FORMAT;

		if (this->n_buffers == 0)
			return SPA_RESULT_NO_BUFFERS;

		if (!this->started)
			return SPA_RESULT_OK;

		this->started = false;
		set_timer(this, false);
	} else
		return SPA_RESULT_NOT_IMPLEMENTED;

	return SPA_RESULT_OK;
}

static int
impl_node_set_callbacks(struct spa_node *node,
			const struct spa_node_callbacks *callbacks,
			void *data)
{
	struct impl *this;

	spa_return_val_if_fail(node != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	this = SPA_CONTAINER_OF(node, struct impl, node);

	this->callbacks = callbacks;
	this->callbacks_data = data;

	return SPA_RESULT_OK;
}

static int
impl_node_get_n_ports(struct spa_node *node,
		      uint32_t *n_input_ports,
		      uint32_t *max_input_ports,
		      uint32_t *n_output_ports,
		      uint32_t *max_output_ports)
{
	spa_return_val_if_fail(node != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	if (n_input_ports)
		*n_input_ports = 0;
	if (n_output_ports)
		*n_output_ports = 1;
	if (max_input_ports)
		*max_input_ports = 0;
	if (max_output_ports)
		*max_output_ports = 1;

	return SPA_RESULT_OK;
}

static int
impl_node_get_port_ids(struct spa_node *node,
		       uint32_t n_input_ports,
		       uint32_t *input_ids,
		       uint32_t n_output_ports,
		       uint32_t *output_ids)
{
	spa_return_val_if_fail(node != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	if (n_output_ports > 0 && output_ids != NULL)
		output_ids[0] = 0;

	return SPA_RESULT_OK;
}

static int impl_node_add_port(struct spa_node *node, enum spa_direction direction, uint32_t port_id)
{
	return SPA_RESULT_NOT_IMPLEMENTED;
}

static int
impl_node_remove_port(struct spa_node *node, enum spa_direction direction, uint32_t port_id)
{
	return SPA_RESULT_NOT_IMPLEMENTED;
}

static int
impl_node_port_get_info(struct spa_node *node,
			enum spa_direction direction,
			uint32_t port_id,
			const struct spa_port_info **info)
{
	struct impl *this;

	spa_return_val_if_fail(node != NULL, SPA_RESULT_INVALID_ARGUMENTS);
	spa_return_val_if_fail(info != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	this = SPA_CONTAINER_OF(node, struct impl, node);

	spa_return_val_if_fail(CHECK_PORT(this, direction, port_id), SPA_RESULT_INVALID_PORT);

	*info = &this->info;

	return SPA_RESULT_OK;
}

static int port_enum_formats(struct spa_node *node,
			     enum spa_direction direction, uint32_t port_id,
			     uint32_t *index,
			     const struct spa_pod_object *filter,
			     struct spa_pod_builder *builder,
			     struct spa_pod **param)
{
	struct impl *this = SPA_CONTAINER_OF(node, struct impl, node);
	struct type *t = &this->type;

	switch (*index) {
	case 0:
		*param = spa_pod_builder_object(builder,
			t->param.idEnumFormat, t->format,
			"I", t->media_type.video,
			"I", t->media_subtype.raw,
			":", t->format_video.format,    "Ieu", t->video_format.RGB,
								2, t->video_format.RGB,
								   t->video_format.UYVY,
			":", t->format_video.size,      "Rru", &SPA_RECTANGLE(320, 240),
								2, &SPA_RECTANGLE(1, 1),
								   &SPA_RECTANGLE(INT32_MAX, INT32_MAX),
			":", t->format_video.framerate, "Fru", &SPA_FRACTION(25,1),
								2, &SPA_FRACTION(0, 1),
								   &SPA_FRACTION(INT32_MAX, 1));
		break;
	default:
		return SPA_RESULT_ENUM_END;
	}
	return SPA_RESULT_OK;
}

static int port_get_format(struct spa_node *node,
			   enum spa_direction direction, uint32_t port_id,
			   uint32_t *index,
			   const struct spa_pod_object *filter,
			   struct spa_pod_builder *builder,
			   struct spa_pod **param)
{
	struct impl *this = SPA_CONTAINER_OF(node, struct impl, node);
	struct type *t = &this->type;

	if (!this->have_format)
		return SPA_RESULT_NO_FORMAT;
	if (*index > 0)
		return SPA_RESULT_ENUM_END;

	*param = spa_pod_builder_object(builder,
		t->param.idFormat, t->format,
		"I", t->media_type.video,
		"I", t->media_subtype.raw,
		":", t->format_video.format,    "I", this->current_format.info.raw.format,
		":", t->format_video.size,      "R", &this->current_format.info.raw.size,
		":", t->format_video.framerate, "F", &this->current_format.info.raw.framerate);

	return SPA_RESULT_OK;
}

static int
impl_node_port_enum_params(struct spa_node *node,
			   enum spa_direction direction, uint32_t port_id,
			   uint32_t id, uint32_t *index,
			   const struct spa_pod_object *filter,
			   struct spa_pod_builder *builder)
{
	struct impl *this;
	struct type *t;
	uint32_t offset;
	struct spa_pod *param;
	int res;

	spa_return_val_if_fail(node != NULL, SPA_RESULT_INVALID_ARGUMENTS);
	spa_return_val_if_fail(index != NULL, SPA_RESULT_INVALID_ARGUMENTS);
	spa_return_val_if_fail(builder != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	this = SPA_CONTAINER_OF(node, struct impl, node);
	t = &this->type;

	spa_return_val_if_fail(CHECK_PORT(this, direction, port_id), SPA_RESULT_INVALID_PORT);

	offset = builder->offset;

      next:
	if (id == t->param.idList) {
		uint32_t list[] = { t->param.idEnumFormat,
				    t->param.idFormat,
				    t->param.idBuffers,
				    t->param.idMeta };

		if (*index < SPA_N_ELEMENTS(list))
			param = spa_pod_builder_object(builder, id, t->param.List,
				":", t->param.listId, "I", list[*index]);
		else
			return SPA_RESULT_ENUM_END;
	}
	else if (id == t->param.idEnumFormat) {
		if ((res = port_enum_formats(node, direction, port_id, index, filter, builder, &param)) < 0)
			return res;
	}
	else if (id == t->param.idFormat) {
		if ((res = port_get_format(node, direction, port_id, index, filter, builder, &param)) < 0)
			return res;
	}
	else if (id == t->param.idBuffers) {
		struct spa_video_info_raw *raw_info = &this->current_format.info.raw;

		if (!this->have_format)
			return SPA_RESULT_NO_FORMAT;
		if (*index > 0)
			return SPA_RESULT_ENUM_END;

		param = spa_pod_builder_object(builder,
			id, t->param_alloc_buffers.Buffers,
			":", t->param_alloc_buffers.size,    "i", this->stride * raw_info->size.height,
			":", t->param_alloc_buffers.stride,  "i", this->stride,
			":", t->param_alloc_buffers.buffers, "ir", 2,
								2, 1, 32,
			":", t->param_alloc_buffers.align,   "i", 16);
	}
	else if (id == t->param.idMeta) {
		if (!this->have_format)
			return SPA_RESULT_NO_FORMAT;

		switch (*index) {
		case 0:
			param = spa_pod_builder_object(builder,
				id, t->param_alloc_meta_enable.MetaEnable,
				":", t->param_alloc_meta_enable.type, "I", t->meta.Header,
				":", t->param_alloc_meta_enable.size, "i", sizeof(struct spa_meta_header));
			break;

		default:
			return SPA_RESULT_ENUM_END;
		}
	}
	else
		return SPA_RESULT_UNKNOWN_PARAM;

	(*index)++;

	spa_pod_builder_reset(builder, offset);
	if (spa_pod_filter(builder, param, (struct spa_pod*)filter) < 0)
		goto next;

	return SPA_RESULT_OK;
}

static int clear_buffers(struct impl *this)
{
	if (this->n_buffers > 0) {
		spa_log_info(this->log, NAME " %p: clear buffers", this);
		this->n_buffers = 0;
		spa_list_init(&this->empty);
		this->started = false;
		set_timer(this, false);
	}
	return SPA_RESULT_OK;
}

static int port_set_format(struct spa_node *node,
			   enum spa_direction direction, uint32_t port_id,
			   uint32_t flags,
			   const struct spa_pod_object *format)
{
	struct impl *this = SPA_CONTAINER_OF(node, struct impl, node);

	if (format == NULL) {
		this->have_format = false;
		clear_buffers(this);
	} else {
		struct spa_video_info info = { 0 };

		spa_pod_object_parse(format,
			"I", &info.media_type,
			"I", &info.media_subtype);

		if (info.media_type != this->type.media_type.video &&
		    info.media_subtype != this->type.media_subtype.raw)
			return SPA_RESULT_INVALID_MEDIA_TYPE;

		if (spa_format_video_raw_parse(format, &info.info.raw, &this->type.format_video) < 0)
			return SPA_RESULT_INVALID_MEDIA_TYPE;

		if (info.info.raw.format == this->type.video_format.RGB)
			this->bpp = 3;
		else if (info.info.raw.format == this->type.video_format.UYVY)
			this->bpp = 2;
		else
			return SPA_RESULT_INVALID_MEDIA_TYPE;

		this->current_format = info;
		this->have_format = true;
	}

	if (this->have_format) {
		struct spa_video_info_raw *raw_info = &this->current_format.info.raw;
		this->stride = SPA_ROUND_UP_N(this->bpp * raw_info->size.width, 4);
	}

	return SPA_RESULT_OK;
}

static int
impl_node_port_set_param(struct spa_node *node,
			 enum spa_direction direction, uint32_t port_id,
			 uint32_t id, uint32_t flags,
			 const struct spa_pod_object *param)
{
	struct impl *this;
	struct type *t;

	spa_return_val_if_fail(node != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	this = SPA_CONTAINER_OF(node, struct impl, node);
	t = &this->type;

	spa_return_val_if_fail(CHECK_PORT(this, direction, port_id), SPA_RESULT_INVALID_PORT);

	if (id == t->param.idFormat) {
		return port_set_format(node, direction, port_id, flags, param);
	}
	else
		return SPA_RESULT_UNKNOWN_PARAM;
}

static int
impl_node_port_use_buffers(struct spa_node *node,
			   enum spa_direction direction,
			   uint32_t port_id,
			   struct spa_buffer **buffers,
			   uint32_t n_buffers)
{
	struct impl *this;
	uint32_t i;

	spa_return_val_if_fail(node != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	this = SPA_CONTAINER_OF(node, struct impl, node);

	spa_return_val_if_fail(CHECK_PORT(this, direction, port_id), SPA_RESULT_INVALID_PORT);

	if (!this->have_format)
		return SPA_RESULT_NO_FORMAT;

	clear_buffers(this);

	for (i = 0; i < n_buffers; i++) {
		struct buffer *b;
		struct spa_data *d = buffers[i]->datas;

		b = &this->buffers[i];
		b->outbuf = buffers[i];
		b->outstanding = false;
		b->h = spa_buffer_find_meta(buffers[i], this->type.meta.Header);

		if ((d[0].type == this->type.data.MemPtr ||
		     d[0].type == this->type.data.MemFd ||
		     d[0].type == this->type.data.DmaBuf) && d[0].data == NULL) {
			spa_log_error(this->log, NAME " %p: invalid memory on buffer %p", this,
				      buffers[i]);
			return SPA_RESULT_ERROR;
		}
		spa_list_append(&this->empty, &b->link);
	}
	this->n_buffers = n_buffers;

	return SPA_RESULT_OK;
}

static int
impl_node_port_alloc_buffers(struct spa_node *node,
			     enum spa_direction direction,
			     uint32_t port_id,
			     struct spa_pod_object **params,
			     uint32_t n_params,
			     struct spa_buffer **buffers,
			     uint32_t *n_buffers)
{
	struct impl *this;

	spa_return_val_if_fail(node != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	this = SPA_CONTAINER_OF(node, struct impl, node);

	spa_return_val_if_fail(CHECK_PORT(this, direction, port_id), SPA_RESULT_INVALID_PORT);

	if (!this->have_format)
		return SPA_RESULT_NO_FORMAT;

	return SPA_RESULT_NOT_IMPLEMENTED;
}

static int
impl_node_port_set_io(struct spa_node *node,
		      enum spa_direction direction,
		      uint32_t port_id,
		      struct spa_port_io *io)
{
	struct impl *this;

	spa_return_val_if_fail(node != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	this = SPA_CONTAINER_OF(node, struct impl, node);

	spa_return_val_if_fail(CHECK_PORT(this, direction, port_id), SPA_RESULT_INVALID_PORT);

	this->io = io;

	return SPA_RESULT_OK;
}

static inline void reuse_buffer(struct impl *this, uint32_t id)
{
	struct buffer *b = &this->buffers[id];
	spa_return_if_fail(b->outstanding);

	spa_log_trace(this->log, NAME " %p: reuse buffer %d", this, id);

	b->outstanding = false;
	spa_list_append(&this->empty, &b->link);

	if (!this->props.live)
		set_timer(this, true);
}

static int impl_node_port_reuse_buffer(struct spa_node *node, uint32_t port_id, uint32_t buffer_id)
{
	struct impl *this;

	spa_return_val_if_fail(node != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	this = SPA_CONTAINER_OF(node, struct impl, node);

	spa_return_val_if_fail(port_id == 0, SPA_RESULT_INVALID_PORT);
	spa_return_val_if_fail(this->n_buffers > 0, SPA_RESULT_NO_BUFFERS);
	spa_return_val_if_fail(buffer_id < this->n_buffers, SPA_RESULT_INVALID_BUFFER_ID);

	reuse_buffer(this, buffer_id);

	return SPA_RESULT_OK;
}

static int
impl_node_port_send_command(struct spa_node *node,
			    enum spa_direction direction,
			    uint32_t port_id,
			    const struct spa_command *command)
{
	return SPA_RESULT_NOT_IMPLEMENTED;
}

static int impl_node_process_input(struct spa_node *node)
{
	return SPA_RESULT_NOT_IMPLEMENTED;
}

static int impl_node_process_output(struct spa_node *node)
{
	struct impl *this;
	struct spa_port_io *io;

	spa_return_val_if_fail(node != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	this = SPA_CONTAINER_OF(node, struct impl, node);
	io = this->io;
	spa_return_val_if_fail(io != NULL, SPA_RESULT_WRONG_STATE);

	if (io->status == SPA_RESULT_HAVE_BUFFER)
		return SPA_RESULT_HAVE_BUFFER;

	if (io->buffer_id < this->n_buffers) {
		reuse_buffer(this, this->io->buffer_id);
		this->io->buffer_id = SPA_ID_INVALID;
	}

	if (!this->props.live && (io->status == SPA_RESULT_NEED_BUFFER))
		return make_buffer(this);
	else
		return SPA_RESULT_OK;
}

static const struct spa_dict_item node_info_items[] = {
	{ "media.class", "Video/Source" },
};

static const struct spa_dict node_info = {
	SPA_N_ELEMENTS(node_info_items),
	node_info_items
};

static const struct spa_node impl_node = {
	SPA_VERSION_NODE,
	&node_info,
	impl_node_enum_params,
	impl_node_set_param,
	impl_node_send_command,
	impl_node_set_callbacks,
	impl_node_get_n_ports,
	impl_node_get_port_ids,
	impl_node_add_port,
	impl_node_remove_port,
	impl_node_port_get_info,
	impl_node_port_enum_params,
	impl_node_port_set_param,
	impl_node_port_use_buffers,
	impl_node_port_alloc_buffers,
	impl_node_port_set_io,
	impl_node_port_reuse_buffer,
	impl_node_port_send_command,
	impl_node_process_input,
	impl_node_process_output,
};

static int impl_clock_enum_params(struct spa_clock *clock, uint32_t id, uint32_t *index,
				  struct spa_pod_builder *builder)
{
	return SPA_RESULT_NOT_IMPLEMENTED;
}

static int impl_clock_set_param(struct spa_clock *clock, uint32_t id, uint32_t flags,
				const struct spa_pod_object *param)
{
	return SPA_RESULT_NOT_IMPLEMENTED;
}

static int
impl_clock_get_time(struct spa_clock *clock,
		    int32_t *rate,
		    int64_t *ticks,
		    int64_t *monotonic_time)
{
	struct timespec now;
	uint64_t tnow;

	spa_return_val_if_fail(clock != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	if (rate)
		*rate = SPA_NSEC_PER_SEC;

	clock_gettime(CLOCK_MONOTONIC, &now);
	tnow = SPA_TIMESPEC_TO_TIME(&now);

	if (ticks)
		*ticks = tnow;
	if (monotonic_time)
		*monotonic_time = tnow;

	return SPA_RESULT_OK;
}

static const struct spa_clock impl_clock = {
	SPA_VERSION_CLOCK,
	NULL,
	SPA_CLOCK_STATE_STOPPED,
	impl_clock_enum_params,
	impl_clock_set_param,
	impl_clock_get_time,
};

static int impl_get_interface(struct spa_handle *handle, uint32_t interface_id, void **interface)
{
	struct impl *this;

	spa_return_val_if_fail(handle != NULL, SPA_RESULT_INVALID_ARGUMENTS);
	spa_return_val_if_fail(interface != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	this = (struct impl *) handle;

	if (interface_id == this->type.node)
		*interface = &this->node;
	else if (interface_id == this->type.clock)
		*interface = &this->clock;
	else
		return SPA_RESULT_UNKNOWN_INTERFACE;

	return SPA_RESULT_OK;
}

static int impl_clear(struct spa_handle *handle)
{
	struct impl *this;

	spa_return_val_if_fail(handle != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	this = (struct impl *) handle;

	if (this->data_loop)
		spa_loop_remove_source(this->data_loop, &this->timer_source);
	close(this->timer_source.fd);

	return SPA_RESULT_OK;
}

static int
impl_init(const struct spa_handle_factory *factory,
	  struct spa_handle *handle,
	  const struct spa_dict *info,
	  const struct spa_support *support,
	  uint32_t n_support)
{
	struct impl *this;
	uint32_t i;

	spa_return_val_if_fail(factory != NULL, SPA_RESULT_INVALID_ARGUMENTS);
	spa_return_val_if_fail(handle != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	handle->get_interface = impl_get_interface;
	handle->clear = impl_clear;

	this = (struct impl *) handle;

	for (i = 0; i < n_support; i++) {
		if (strcmp(support[i].type, SPA_TYPE__TypeMap) == 0)
			this->map = support[i].data;
		else if (strcmp(support[i].type, SPA_TYPE__Log) == 0)
			this->log = support[i].data;
		else if (strcmp(support[i].type, SPA_TYPE_LOOP__DataLoop) == 0)
			this->data_loop = support[i].data;
	}
	if (this->map == NULL) {
		spa_log_error(this->log, "a type-map is needed");
		return SPA_RESULT_ERROR;
	}
	init_type(&this->type, this->map);

	this->node = impl_node;
	this->clock = impl_clock;
	reset_props(this, &this->props);

	spa_list_init(&this->empty);

	this->timer_source.func = on_output;
	this->timer_source.data = this;
	this->timer_source.fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	this->timer_source.mask = SPA_IO_IN;
	this->timer_source.rmask = 0;
	this->timerspec.it_value.tv_sec = 0;
	this->timerspec.it_value.tv_nsec = 0;
	this->timerspec.it_interval.tv_sec = 0;
	this->timerspec.it_interval.tv_nsec = 0;

	if (this->data_loop)
		spa_loop_add_source(this->data_loop, &this->timer_source);

	this->info.flags = SPA_PORT_INFO_FLAG_CAN_USE_BUFFERS | SPA_PORT_INFO_FLAG_NO_REF;
	if (this->props.live)
		this->info.flags |= SPA_PORT_INFO_FLAG_LIVE;

	spa_log_info(this->log, NAME " %p: initialized", this);

	return SPA_RESULT_OK;
}

static const struct spa_interface_info impl_interfaces[] = {
	{SPA_TYPE__Node,},
	{SPA_TYPE__Clock,},
};

static int
impl_enum_interface_info(const struct spa_handle_factory *factory,
			 const struct spa_interface_info **info,
			 uint32_t index)
{
	spa_return_val_if_fail(factory != NULL, SPA_RESULT_INVALID_ARGUMENTS);
	spa_return_val_if_fail(info != NULL, SPA_RESULT_INVALID_ARGUMENTS);

	switch (index) {
	case 0:
		*info = &impl_interfaces[index];
		break;
	default:
		return SPA_RESULT_ENUM_END;
	}
	return SPA_RESULT_OK;
}

static const struct spa_dict_item info_items[] = {
	{ "factory.author", "Wim Taymans <wim.taymans@gmail.com>" },
	{ "factory.description", "Generate a video test pattern" },
};

static const struct spa_dict info = {
	SPA_N_ELEMENTS(info_items),
	info_items
};

const struct spa_handle_factory spa_videotestsrc_factory = {
	SPA_VERSION_HANDLE_FACTORY,
	NAME,
	&info,
	sizeof(struct impl),
	impl_init,
	impl_enum_interface_info,
};
