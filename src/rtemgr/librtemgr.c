// Copyright (c) 2021 Ryosuke Saito All rights reserved.
// MIT licensed

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <yaml.h>
#include <b64/cencode.h>
#include <b64/cdecode.h>
#include "rtemgr-common.h"


/**
 * Allocate new interface and return a pointer to it.
 */
rtemgr_intf *rtemgr_data_alloc_interface(rtemgr_data *d)
{
	rtemgr_intf *intf;
	int newsize = d->nr_intf + 1;
	int i = d->nr_intf;

	intf = realloc(d->interfaces, sizeof(*intf) * newsize);
	if (!intf)
		return NULL;

	intf[i].id = -1;
	memset(intf[i].name, 0, sizeof(intf[i].name));
	intf[i].domain = 0;
	intf[i].bus_type = -1;
	memset(intf[i].path, 0, sizeof(intf[i].path));
	intf[i].managed = 0;
	memset(intf[i].partner, 0, sizeof(intf[i].partner));
	d->nr_intf = newsize;
	d->interfaces = intf;
	return &intf[i];
}

/* Remove interface and return remaining number of interfaces */
int rtemgr_data_remove_interface(rtemgr_data *d)
{
	if (!d)
		return 0;

	if (d->nr_intf) {
		d->nr_intf--;
		if (!d->nr_intf) {
			free(d->interfaces);
			d->interfaces = NULL;
		}
	}
	return d->nr_intf;
}

void rtemgr_data_cleanup_interfaces(rtemgr_data *d) {
	while (rtemgr_data_remove_interface(d));
}

/**
 * Allocate memory for rtemgr_data.
 * rtemgr_data_free must be called after use of it.
 */
rtemgr_data *rtemgr_data_alloc(void)
{
	return calloc(1, sizeof(rtemgr_data));
}

/**
 * Free rtemgr_data memory allocated by rtemgr_data_alloc.
 *
 * NOTE:
 *   Do not set the pointer to a buffer that is later released in eventloop of
 *   rteipc core into cmd.val.v. Otherwise, a double free error will happen.
 */
void rtemgr_data_free(rtemgr_data *d)
{
	if (d) {
		free(d->cmd.val.v);
		rtemgr_data_cleanup_interfaces(d);
		free(d);
	}
}

static inline void encode_base64(char *out, void *data, size_t size)
{
	char *p = out;
	int cnt;

	base64_encodestate s;
	base64_init_encodestate(&s);
	cnt = base64_encode_block(data, size, p, &s);
	p += cnt;
	cnt = base64_encode_blockend(p, &s);
	p += cnt;
	*p = '\0';
}

static inline int decode_base64(void *out, char *in)
{
	char *p = out;

	base64_decodestate s;
	base64_init_decodestate(&s);
	return base64_decode_block(in, strlen(in), p, &s);
}

/**
 * Convert rtemgr_data to yaml formatted string.
 *
 * Return 0 on success, -1 on failure.
 */
int rtemgr_data_emit(const rtemgr_data *d, unsigned char *output, size_t size,
			size_t *written)
{
	yaml_emitter_t emitter;
	yaml_event_t event;
	char *buf, *tmp;
	size_t allocated = 0, block = 4096, padding = 256, estimated;
	int i, j;

	/* First allocate 4kb buffer */
	allocated = block;
	if (!(buf = malloc(allocated)))
		return -1;

	yaml_emitter_initialize(&emitter);

	yaml_emitter_set_output_string(&emitter, output, size, written);

	//- type: STREAM-START
	yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: DOCUMENT-START
	yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 1);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: MAPPING-START
	yaml_mapping_start_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_MAP_TAG, 1,
			YAML_BLOCK_MAPPING_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR "cmd"
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)"cmd", -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SEQUENCE-START
	yaml_sequence_start_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_SEQ_TAG, 1,
			YAML_BLOCK_SEQUENCE_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: MAPPING-START
	yaml_mapping_start_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_MAP_TAG, 1,
			YAML_BLOCK_MAPPING_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR "action"
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)"action", -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR
	snprintf(buf, allocated, "%d", d->cmd.action);
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)buf, -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR "error"
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)"error", -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR
	snprintf(buf, allocated, "%d", d->cmd.error);
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)buf, -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR "val"
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)"val", -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SEQUENCE-START
	yaml_sequence_start_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_SEQ_TAG, 1,
			YAML_BLOCK_SEQUENCE_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: MAPPING-START
	yaml_mapping_start_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_MAP_TAG, 1,
			YAML_BLOCK_MAPPING_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR "v"
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)"v", -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR
	estimated = (((4 * d->cmd.val.s / 3) + 3) & ~3) + padding;
	if (d->cmd.val.v && estimated > allocated) {
		allocated = block * ((estimated + block - 1) / block);
		if (!(tmp = realloc(buf, allocated)))
				goto error;
		buf = tmp;
	}
	encode_base64(buf, d->cmd.val.v, d->cmd.val.s);
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)"tag:yaml.org,2002:binary",
			(yaml_char_t *)buf, -1,
			0, 0, YAML_LITERAL_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR "s"
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)"s", -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR
	snprintf(buf, allocated, "%u", d->cmd.val.s);
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)buf, -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR "extra"
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)"extra", -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SEQUENCE-START
	yaml_sequence_start_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_SEQ_TAG, 1,
			YAML_BLOCK_SEQUENCE_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: MAPPING-START
	yaml_mapping_start_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_MAP_TAG, 1,
			YAML_BLOCK_MAPPING_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR "addr"
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)"addr", -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR
	snprintf(buf, allocated, "%u", d->cmd.val.extra.addr);
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)buf, -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR "rsize"
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)"rsize", -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR
	snprintf(buf, allocated, "%u", d->cmd.val.extra.rsize);
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)buf, -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: MAPPING-END
	yaml_mapping_end_event_initialize(&event);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SEQUENCE-END "extra"
	yaml_sequence_end_event_initialize(&event);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: MAPPING-END
	yaml_mapping_end_event_initialize(&event);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SEQUENCE-END "val"
	yaml_sequence_end_event_initialize(&event);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: MAPPING-END
	yaml_mapping_end_event_initialize(&event);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SEQUENCE-END "cmd"
	yaml_sequence_end_event_initialize(&event);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR "nr_intf"
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)"nr_intf", -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR
	snprintf(buf, allocated, "%d", d->nr_intf);
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)buf, -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SCALAR "interfaces"
	yaml_scalar_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_STR_TAG,
			(yaml_char_t *)"interfaces", -1,
			1, 0, YAML_PLAIN_SCALAR_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: SEQUENCE-START
	yaml_sequence_start_event_initialize(&event, NULL,
			(yaml_char_t *)YAML_SEQ_TAG, 1,
			YAML_BLOCK_SEQUENCE_STYLE);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	for (i = 0; i < d->nr_intf; i++) {
		//- type: MAPPING-START
		yaml_mapping_start_event_initialize(&event, NULL,
				(yaml_char_t *)YAML_MAP_TAG, 1,
				YAML_BLOCK_MAPPING_STYLE);
		if (!yaml_emitter_emit(&emitter, &event))
			goto error;

		//- type: SCALAR "id"
		yaml_scalar_event_initialize(&event, NULL,
				(yaml_char_t *)YAML_STR_TAG,
				(yaml_char_t *)"id", -1,
				1, 0, YAML_PLAIN_SCALAR_STYLE);
		if (!yaml_emitter_emit(&emitter, &event))
			goto error;

		//- type: SCALAR
		snprintf(buf, allocated, "%d", d->interfaces[i].id);
		yaml_scalar_event_initialize(&event, NULL,
				(yaml_char_t *)YAML_STR_TAG,
				(yaml_char_t *)buf, -1,
				1, 0, YAML_PLAIN_SCALAR_STYLE);
		if (!yaml_emitter_emit(&emitter, &event))
			goto error;

		//- type: SCALAR "bus_type"
		yaml_scalar_event_initialize(&event, NULL,
				(yaml_char_t *)YAML_STR_TAG,
				(yaml_char_t *)"bus_type", -1,
				1, 0, YAML_PLAIN_SCALAR_STYLE);
		if (!yaml_emitter_emit(&emitter, &event))
			goto error;

		//- type: SCALAR
		snprintf(buf, allocated, "%d", d->interfaces[i].bus_type);
		yaml_scalar_event_initialize(&event, NULL,
				(yaml_char_t *)YAML_STR_TAG,
				(yaml_char_t *)buf, -1,
				1, 0, YAML_PLAIN_SCALAR_STYLE);
		if (!yaml_emitter_emit(&emitter, &event))
			goto error;

		//- type: SCALAR "name"
		yaml_scalar_event_initialize(&event, NULL,
				(yaml_char_t *)YAML_STR_TAG,
				(yaml_char_t *)"name", -1,
				1, 0, YAML_PLAIN_SCALAR_STYLE);
		if (!yaml_emitter_emit(&emitter, &event))
			goto error;

		//- type: SCALAR
		snprintf(buf, allocated, "%s", d->interfaces[i].name);
		yaml_scalar_event_initialize(&event, NULL,
				(yaml_char_t *)YAML_STR_TAG,
				(yaml_char_t *)buf, -1,
				0, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
		if (!yaml_emitter_emit(&emitter, &event))
			goto error;

		//- type: SCALAR "path"
		yaml_scalar_event_initialize(&event, NULL,
				(yaml_char_t *)YAML_STR_TAG,
				(yaml_char_t *)"path", -1,
				1, 0, YAML_PLAIN_SCALAR_STYLE);
		if (!yaml_emitter_emit(&emitter, &event))
			goto error;

		//- type: SCALAR
		snprintf(buf, allocated, "%s", d->interfaces[i].path);
		yaml_scalar_event_initialize(&event, NULL,
				(yaml_char_t *)YAML_STR_TAG,
				(yaml_char_t *)buf, -1,
				0, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
		if (!yaml_emitter_emit(&emitter, &event))
			goto error;

		//- type: SCALAR "domain"
		yaml_scalar_event_initialize(&event, NULL,
				(yaml_char_t *)YAML_STR_TAG,
				(yaml_char_t *)"domain", -1,
				1, 0, YAML_PLAIN_SCALAR_STYLE);
		if (!yaml_emitter_emit(&emitter, &event))
			goto error;

		//- type: SCALAR
		snprintf(buf, allocated, "%d", d->interfaces[i].domain);
		yaml_scalar_event_initialize(&event, NULL,
				(yaml_char_t *)YAML_STR_TAG,
				(yaml_char_t *)buf, -1,
				1, 0, YAML_PLAIN_SCALAR_STYLE);
		if (!yaml_emitter_emit(&emitter, &event))
			goto error;

		//- type: SCALAR "managed"
		yaml_scalar_event_initialize(&event, NULL,
				(yaml_char_t *)YAML_STR_TAG,
				(yaml_char_t *)"managed", -1,
				1, 0, YAML_PLAIN_SCALAR_STYLE);
		if (!yaml_emitter_emit(&emitter, &event))
			goto error;

		//- type: SCALAR
		snprintf(buf, allocated, "%d", d->interfaces[i].managed);
		yaml_scalar_event_initialize(&event, NULL,
				(yaml_char_t *)YAML_STR_TAG,
				(yaml_char_t *)buf, -1,
				1, 0, YAML_PLAIN_SCALAR_STYLE);
		if (!yaml_emitter_emit(&emitter, &event))
			goto error;

		//- type: SCALAR "partner"
		yaml_scalar_event_initialize(&event, NULL,
				(yaml_char_t *)YAML_STR_TAG,
				(yaml_char_t *)"partner", -1,
				1, 0, YAML_PLAIN_SCALAR_STYLE);
		if (!yaml_emitter_emit(&emitter, &event))
			goto error;

		//- type: SCALAR
		snprintf(buf, allocated, "%s", d->interfaces[i].partner);
		yaml_scalar_event_initialize(&event, NULL,
				(yaml_char_t *)YAML_STR_TAG,
				(yaml_char_t *)buf, -1,
				0, 1, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
		if (!yaml_emitter_emit(&emitter, &event))
			goto error;

		//- type: MAPPING-END
		yaml_mapping_end_event_initialize(&event);
		if (!yaml_emitter_emit(&emitter, &event))
			goto error;
	}

	//- type: SEQUENCE-END
	yaml_sequence_end_event_initialize(&event);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: MAPPING-END
	yaml_mapping_end_event_initialize(&event);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: DOCUMENT-END
	yaml_document_end_event_initialize(&event, 1);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	//- type: STREAM-END
	yaml_stream_end_event_initialize(&event);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	yaml_emitter_delete(&emitter);
	return 0;

error:
	yaml_emitter_delete(&emitter);
	free(buf);
	return -1;
}

/*
 * Parse yaml formatted data and convert it to rtemgr_data.
 *
 * Return a pointer to rtemgr_data on success, otherwise NULL.
 * Caller must invoke rtemgr_data_free() to properly free it.
 */
rtemgr_data *rtemgr_data_parse(const unsigned char *input, size_t size)
{
	yaml_parser_t parser;
	yaml_event_t event;
	yaml_token_t token;
	yaml_char_t *value;
	rtemgr_data *d;
	rtemgr_intf *intf;
	int done = 0;
	int seq_nest = 0, field_start = 0;
	int idx_intf = 0;
	int nr_intf;
	void *tmp;

	enum {
		TOP_LEVEL,
		// rtemgr_data.cmd.*
		CMD,
		// rtemgr_data.cmd.val.*
		VAL,
		// rtemgr_data.cmd.val.extra*
		EXT,
		// rtemgr_data.interfaces[]
		INTF
	} field = TOP_LEVEL;

	enum {
		ITEM_NONE,
		// nr_intf
		NR_INTF,
		// cmd.action
		CMD_ACT,
		// cmd.error
		CMD_ERR,
		// cmd.val.v
		VAL_V,
		// cmd.val.s
		VAL_S,
		// cmd.val.extra.addr
		EXT_ADDR,
		// cmd.val.extra.rsize
		EXT_RSIZE,
		// interfaces[].id
		INTF_ID,
		// interfaces[].bus_type
		INTF_BUS,
		// interfaces[].name
		INTF_NAME,
		// interfaces[].path
		INTF_PATH,
		// interfaces[].domain
		INTF_DOM,
		// interfaces[].managed
		INTF_MANAGED,
		// interfaces[].partner
		INTF_PARTNER,
	} item = ITEM_NONE;

	if (!(d = rtemgr_data_alloc()))
		return NULL;

	yaml_parser_initialize(&parser);
	yaml_parser_set_input_string(&parser, input, size);

	while (!done) {
		if (!yaml_parser_parse(&parser, &event))
			goto error;

		done = (event.type == YAML_STREAM_END_EVENT);

		switch (event.type) {
		case YAML_SEQUENCE_START_EVENT:
			seq_nest++;
			break;
		case YAML_SEQUENCE_END_EVENT:
			seq_nest--;
			if (seq_nest <= field_start)
				field = TOP_LEVEL;
			if (field == VAL)
				field = CMD;
			if (field == EXT)
				field = VAL;
			break;
		case YAML_MAPPING_START_EVENT:
			break;
		case YAML_MAPPING_END_EVENT:
			if (field == INTF)
				idx_intf++;
			break;
		case YAML_SCALAR_EVENT:
			value = event.data.scalar.value;
			if (field == TOP_LEVEL && item == ITEM_NONE) {
				/* NOTE: "nr_intf" must appear before "interfaces" */
				if (strmatch(value, "nr_intf")) {
					item = NR_INTF;
					break;
				}
				if (strmatch(value, "cmd")) {
					field = CMD;
				} else if (strmatch(value, "interfaces")) {
					field = INTF;
				}
				field_start = seq_nest;
				break;
			}
			switch (field) {
			case TOP_LEVEL:
				switch (item) {
				case NR_INTF:
					nr_intf = strtoul(value, NULL, 0);
					while (nr_intf > d->nr_intf) {
						if (!rtemgr_data_alloc_interface(d))
							goto error;
					}
					intf = d->interfaces;
					break;
				}
				item = ITEM_NONE;
				break;
			case CMD:
				if (item == ITEM_NONE) {
					if (strmatch(value, "action")) {
						item = CMD_ACT;
					} else if (strmatch(value, "error")) {
						item = CMD_ERR;
					} else if (strmatch(value, "val")) {
						field = VAL;
					}
				} else {
					switch (item) {
					case CMD_ACT:
						d->cmd.action = strtoul(value, NULL, 0);
						break;
					case CMD_ERR:
						d->cmd.error = strtoul(value, NULL, 0);
						break;
					}
					item = ITEM_NONE;
				}
				break;
			case VAL:
				if (item == ITEM_NONE) {
					if (strmatch(value, "v")) {
						item = VAL_V;
					} else if (strmatch(value, "s")) {
						item = VAL_S;
					} else if (strmatch(value, "extra")) {
						field = EXT;
					}
				} else {
					switch (item) {
					case VAL_V:
						d->cmd.val.v = strdup(value);
						break;
					case VAL_S:
						d->cmd.val.s = strtoul(value, NULL, 0);
						break;
					}
					item = ITEM_NONE;
				}
				break;
			case EXT:
				if (item == ITEM_NONE) {
					if (strmatch(value, "addr")) {
						item = EXT_ADDR;
					} else if (strmatch(value, "rsize")) {
						item = EXT_RSIZE;
					}
				} else {
					switch (item) {
					case EXT_ADDR:
						d->cmd.val.extra.addr = strtoul(value, NULL, 0);
						break;
					case EXT_RSIZE:
						d->cmd.val.extra.rsize = strtoul(value, NULL, 0);
						break;
					}
					item = ITEM_NONE;
				}
				break;
			case INTF:
				if (item == ITEM_NONE) {
					if (strmatch(value, "id")) {
						item = INTF_ID;
					} else if (strmatch(value, "bus_type")) {
						item = INTF_BUS;
					} else if (strmatch(value, "name")) {
						item = INTF_NAME;
					} else if (strmatch(value, "path")) {
						item = INTF_PATH;
					} else if (strmatch(value, "domain")) {
						item = INTF_DOM;
					} else if (strmatch(value, "managed")) {
						item = INTF_MANAGED;
					} else if (strmatch(value, "partner")) {
						item = INTF_PARTNER;
					}
				} else {
					switch (item) {
					case INTF_ID:
						intf[idx_intf].id = strtoul(value, NULL, 0);
						break;
					case INTF_BUS:
						intf[idx_intf].bus_type = strtoul(value, NULL, 0);
						break;
					case INTF_NAME:
						strncpy(intf[idx_intf].name, value, sizeof(((rtemgr_intf *)0)->name));
						break;
					case INTF_PATH:
						strncpy(intf[idx_intf].path, value, sizeof(((rtemgr_intf *)0)->path));
						break;
					case INTF_DOM:
						intf[idx_intf].domain = strtoul(value, NULL, 0);
						break;
					case INTF_MANAGED:
						intf[idx_intf].managed = strtoul(value, NULL, 0);
						break;
					case INTF_PARTNER:
						strncpy(intf[idx_intf].partner, value, sizeof(((rtemgr_intf *)0)->partner));
						break;
					}
					item = ITEM_NONE;
				}
				break;
			}
			break;
		}
		yaml_event_delete(&event);
	}

	/* Decode base64 string to binary data */
	if (d->cmd.val.s && d->cmd.val.v) {
		tmp = malloc(d->cmd.val.s);
		if (!tmp)
			goto error;
		decode_base64(tmp, d->cmd.val.v);
		free(d->cmd.val.v);
		d->cmd.val.v = tmp;
	}

	yaml_parser_delete(&parser);
	return d;

error:
	rtemgr_data_free(d);
	yaml_parser_delete(&parser);
	return NULL;
}

static inline int
__rtemgr_encode_domain(int domain, const char *name, const struct rtecmd *cmd,
			void **out, size_t *len)
{
	rtemgr_data *d;
	rtemgr_intf *intf;
	unsigned char *buf;
	size_t bufsz, written;

	d = rtemgr_data_alloc();
	if (!d)
		return -1;

	intf = rtemgr_data_alloc_interface(d);
	if (!intf)
		goto free;

	memcpy(&d->cmd, cmd, sizeof(*cmd));
	d->cmd.action = RTECMD_XFER;
	intf->domain = domain;
	strncpy(intf->name, name, sizeof(intf->name));

	bufsz = cmd->val.s * 3 / 2;
	if (bufsz < 512)
		bufsz = 512;

	buf = malloc(bufsz);
	if (!buf)
		goto free;

	if (rtemgr_data_emit(d, buf, bufsz, &written))
		goto free;

	*out = buf;
	*len = written;
	return 0;
free:
	rtemgr_data_free(d);
	return -1;
}

int rtemgr_encode_domain(int domain, const char *name, const void *data,
			size_t len, void **out, size_t *written)
{
	struct rtecmd cmd = {0};

	if (!data || !len)
		return -1;
	cmd.val.s = len;
	cmd.val.v = malloc(len);
	if (!cmd.val.v)
		return -1;
	memcpy(cmd.val.v, data, len);
	return __rtemgr_encode_domain(domain, name, &cmd, out, written);
}

int rtemgr_gpio_encode_domain(int domain, const char *name, uint8_t value,
			void **out, size_t *written)
{
	return rtemgr_encode_domain(domain, name, value ? "1" : "0", 1,
				out, written);
}

int rtemgr_spi_encode_domain(int domain, const char *name,
			const uint8_t *data, uint16_t len, bool rdmode,
			void **out, size_t *written)
{
	struct rtecmd cmd = {0};
	struct evbuffer *buf;
	int i;

	if (!data || !len)
		return -1;

	buf = evbuffer_new();
	if (!buf)
		return -1;

	for (i = 0; i < len; i++)
		evbuffer_add_printf(buf, "0x%02x ", data[i]);

	cmd.val.s = evbuffer_get_length(buf) - 1;
	cmd.val.v = malloc(cmd.val.s);
	if (!cmd.val.v) {
		evbuffer_free(buf);
		return -1;
	}
	cmd.val.extra.rsize = rdmode ? len : 0;
	evbuffer_remove(buf, cmd.val.v, cmd.val.s);
	evbuffer_free(buf);
	return __rtemgr_encode_domain(domain, name, &cmd, out, written);
}

int rtemgr_i2c_encode_domain(int domain, const char *name, uint16_t addr,
			const uint8_t *data, uint16_t wlen, uint16_t rlen,
			void **out, size_t *written)
{
	struct rtecmd cmd = {0};
	struct evbuffer *buf;
	int i;

	if (!wlen && !rlen || (wlen && !data))
		return -1;

	buf = evbuffer_new();
	if (!buf)
		return -1;

	for (i = 0; i < wlen; i++)
		evbuffer_add_printf(buf, "0x%02x ", data[i]);

	if (evbuffer_get_length(buf))
		cmd.val.s = evbuffer_get_length(buf) - 1;

	if (cmd.val.s) {
		cmd.val.v = malloc(cmd.val.s);
		if (!cmd.val.v) {
			evbuffer_free(buf);
			return -1;
		}
		evbuffer_remove(buf, cmd.val.v, cmd.val.s);
	}
	cmd.val.extra.addr = addr;
	cmd.val.extra.rsize = rlen;
	evbuffer_free(buf);
	return __rtemgr_encode_domain(domain, name, &cmd, out, written);
}

int rtemgr_sysfs_encode_domain(int domain, const char *name, const char *attr,
			const char *newval, void **out, size_t *written)
{
	struct rtecmd cmd = {0};
	char buf[256];

	if (!attr)
		return -1;

	if (newval)
		snprintf(buf, sizeof(buf), "%s=%s", attr, newval);
	else
		snprintf(buf, sizeof(buf), "%s", attr);

	return rtemgr_encode_domain(domain, name, buf, strlen(buf),
				out, written);
}

int rtemgr_send_domain(int ctx, int domain, const char *name,
			const void *data, size_t len)
{
	void *out;
	size_t written;
	int err;

	err = rtemgr_encode_domain(domain, name, data, len, &out, &written);
	if (err)
		return err;
	return rteipc_send(ctx, out, written);
}

int rtemgr_gpio_send_domain(int ctx, int domain, const char *name,
			uint8_t value)
{
	return rtemgr_send_domain(ctx, domain, name, value ? "1" : "0", 1);
}

int rtemgr_spi_send_domain(int ctx, int domain, const char *name,
			const uint8_t *data, uint16_t len, bool rdmode)
{
	void *out;
	size_t written;
	int err;

	err = rtemgr_spi_encode_domain(domain, name, data, len, rdmode,
			&out, &written);
	if (err)
		return err;
	return rteipc_send(ctx, out, written);
}

int rtemgr_i2c_send_domain(int ctx, int domain, const char *name,
			uint16_t addr, const uint8_t *data, uint16_t wlen,
			uint16_t rlen)
{
	void *out;
	size_t written;
	int err;

	err = rtemgr_i2c_encode_domain(domain, name, addr, data, wlen, rlen,
			&out, &written);
	if (err)
		return err;
	return rteipc_send(ctx, out, written);
}

int rtemgr_sysfs_send_domain(int ctx, int domain, const char *name,
			const char *attr, const char *newval)
{
	void *out;
	size_t written;
	int err;

	err = rtemgr_sysfs_encode_domain(domain, name, attr, newval,
			&out, &written);
	if (err)
		return err;
	return rteipc_send(ctx, out, written);
}

rtemgr_data *rtemgr_decode(const unsigned char *data, size_t len)
{
	rtemgr_data *d = rtemgr_data_parse(data, len);
	if (!d || !d->interfaces)
		return NULL;
	return d;
}

void rtemgr_put_dh(rtemgr_data *d)
{
	rtemgr_data_free(d);
}

void *rtemgr_get_data(const rtemgr_data *d)
{
	return d->cmd.val.v;
}

size_t rtemgr_get_length(const rtemgr_data *d)
{
	return d->cmd.val.s;
}

char *rtemgr_get_name(const rtemgr_data *d)
{
	if (!d->interfaces)
		return NULL;
	return d->interfaces->name;
}

int rtemgr_get_domain(const rtemgr_data *d)
{
	if (!d->interfaces)
		return -1;
	return d->interfaces->domain;
}

int rtemgr_get_type(const rtemgr_data *d)
{
	if (!d->interfaces)
		return -1;
	return d->interfaces->bus_type;
}
