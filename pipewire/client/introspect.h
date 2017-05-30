/* PipeWire
 * Copyright (C) 2015 Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __PIPEWIRE_INTROSPECT_H__
#define __PIPEWIRE_INTROSPECT_H__

#include <spa/defs.h>
#include <spa/format.h>

#ifdef __cplusplus
extern "C" {
#endif

struct pw_context;

#include <pipewire/client/context.h>
#include <pipewire/client/properties.h>

/** \enum pw_node_state The different node states \memberof pw_node */
enum pw_node_state {
	PW_NODE_STATE_ERROR = -1,	/**< error state */
	PW_NODE_STATE_CREATING = 0,	/**< the node is being created */
	PW_NODE_STATE_SUSPENDED = 1,	/**< the node is suspended, the device might
					 *   be closed */
	PW_NODE_STATE_IDLE = 2,		/**< the node is running but there is no active
					 *   port */
	PW_NODE_STATE_RUNNING = 3,	/**< the node is running */
};

/** Convert a \ref pw_node_state to a readable string \memberof pw_node */
const char * pw_node_state_as_string(enum pw_node_state state);

/** \enum pw_direction The direction of a port \memberof pw_introspect */
enum pw_direction {
	PW_DIRECTION_INPUT = SPA_DIRECTION_INPUT,	/**< an input port direction */
	PW_DIRECTION_OUTPUT = SPA_DIRECTION_OUTPUT	/**< an output port direction */
};

/** Convert a \ref pw_direction to a readable string \memberof pw_introspect */
const char * pw_direction_as_string(enum pw_direction direction);

/** \enum pw_link_state The different link states \memberof pw_link */
enum pw_link_state {
	PW_LINK_STATE_ERROR = -2,	/**< the link is in error */
	PW_LINK_STATE_UNLINKED = -1,	/**< the link is unlinked */
	PW_LINK_STATE_INIT = 0,		/**< the link is initialized */
	PW_LINK_STATE_NEGOTIATING = 1,	/**< the link is negotiating formats */
	PW_LINK_STATE_ALLOCATING = 2,	/**< the link is allocating buffers */
	PW_LINK_STATE_PAUSED = 3,	/**< the link is paused */
	PW_LINK_STATE_RUNNING = 4,	/**< the link is running */
};

/** Convert a \ref pw_link_state to a readable string \memberof pw_link */
const char * pw_link_state_as_string(enum pw_link_state state);

/** \class pw_introspect
 *
 * The introspection methods and structures are used to get information
 * about the object in the PipeWire server
 */

/**  The core information. Extra information can be added in later versions \memberof pw_introspect */
struct pw_core_info {
	uint32_t id;			/**< server side id of the core */
#define PW_CORE_CHANGE_MASK_USER_NAME  (1 << 0)
#define PW_CORE_CHANGE_MASK_HOST_NAME  (1 << 1)
#define PW_CORE_CHANGE_MASK_VERSION    (1 << 2)
#define PW_CORE_CHANGE_MASK_NAME       (1 << 3)
#define PW_CORE_CHANGE_MASK_COOKIE     (1 << 4)
#define PW_CORE_CHANGE_MASK_PROPS      (1 << 5)
#define PW_CORE_CHANGE_MASK_ALL        (~0)
	uint64_t change_mask;		/**< bitfield of changed fields since last call */
	const char *user_name;		/**< name of the user that started the core */
	const char *host_name;		/**< name of the machine the core is running on */
	const char *version;		/**< version of the core */
	const char *name;		/**< name of the core */
	uint32_t cookie;		/**< a random cookie for identifying this instance of PipeWire */
	struct spa_dict *props;		/**< extra properties */
};

/** Update and existing \ref pw_core_info with \a update  \memberof pw_introspect */
struct pw_core_info *
pw_core_info_update(struct pw_core_info *info,
		    const struct pw_core_info *update);

/** Free a \ref pw_core_info  \memberof pw_introspect */
void pw_core_info_free(struct pw_core_info *info);

/**  Callback with information about the PipeWire core
 * \param c A \ref pw_context
 * \param res A result code
 * \param info a \ref pw_core_info
 * \param user_data user data as passed to \ref pw_context_get_core_info()
 *
 * \memberof pw_introspect
 */
typedef void (*pw_core_info_cb_t) (struct pw_context *c,
				   int res, const struct pw_core_info *info, void *user_data);

void pw_context_get_core_info(struct pw_context *context, pw_core_info_cb_t cb, void *user_data);

/** The module information. Extra information can be added in later versions \memberof pw_introspect */
struct pw_module_info {
	uint32_t id;		/**< server side id of the module */
	uint64_t change_mask;	/**< bitfield of changed fields since last call */
	const char *name;	/**< name of the module */
	const char *filename;	/**< filename of the module */
	const char *args;	/**< arguments passed to the module */
	struct spa_dict *props;	/**< extra properties */
};

/** Update and existing \ref pw_module_info with \a update \memberof pw_introspect */
struct pw_module_info *
pw_module_info_update(struct pw_module_info *info,
		      const struct pw_module_info *update);

/** Free a \ref pw_module_info \memberof pw_introspect */
void pw_module_info_free(struct pw_module_info *info);



/** Callback with information about a module
 * \param c A \ref pw_context
 * \param res A result code
 * \param info a \ref pw_module_info
 * \param user_data user data as passed to \ref pw_context_list_module_info()
 *
 * \memberof pw_introspect
 */
typedef void (*pw_module_info_cb_t) (struct pw_context *c,
				     int res, const struct pw_module_info *info, void *user_data);

void
pw_context_list_module_info(struct pw_context *context,
			    pw_module_info_cb_t cb, void *user_data);


void
pw_context_get_module_info_by_id(struct pw_context *context,
				 uint32_t id, pw_module_info_cb_t cb, void *user_data);

/** The client information. Extra information can be added in later versions \memberof pw_introspect */
struct pw_client_info {
	uint32_t id;		/**< server side id of the client */
	uint64_t change_mask;	/**< bitfield of changed fields since last call */
	struct spa_dict *props;	/**< extra properties */
};

/** Update and existing \ref pw_client_info with \a update \memberof pw_introspect */
struct pw_client_info *
pw_client_info_update(struct pw_client_info *info,
		      const struct pw_client_info *update);

/** Free a \ref pw_client_info \memberof pw_introspect */
void pw_client_info_free(struct pw_client_info *info);


/** Callback with information about a client
 * \param c A \ref pw_context
 * \param res A result code
 * \param info a \ref pw_client_info
 * \param user_data user data as passed to \ref pw_context_list_client_info()
 *
 * \memberof pw_introspect
 */
typedef void (*pw_client_info_cb_t) (struct pw_context *c,
				     int res, const struct pw_client_info *info, void *user_data);

void
pw_context_list_client_info(struct pw_context *context,
			    pw_client_info_cb_t cb, void *user_data);

void
pw_context_get_client_info_by_id(struct pw_context *context,
				 uint32_t id, pw_client_info_cb_t cb, void *user_data);

/** The node information. Extra information can be added in later versions \memberof pw_introspect */
struct pw_node_info {
	uint32_t id;				/**< server side id of the node */
	uint64_t change_mask;			/**< bitfield of changed fields since last call */
	const char *name;			/**< name the node, suitable for display */
	uint32_t max_inputs;			/**< maximum number of inputs */
	uint32_t n_inputs;			/**< number of inputs */
	uint32_t n_input_formats;		/**< number of input formats */
	struct spa_format **input_formats;	/**< array of input formats */
	uint32_t max_outputs;			/**< maximum number of outputs */
	uint32_t n_outputs;			/**< number of outputs */
	uint32_t n_output_formats;		/**< number of output formats */
	struct spa_format **output_formats;	/**< array of output formats */
	enum pw_node_state state;		/**< the current state of the node */
	const char *error;			/**< an error reason if \a state is error */
	struct spa_dict *props;			/**< the properties of the node */
};

struct pw_node_info *
pw_node_info_update(struct pw_node_info *info,
		    const struct pw_node_info *update);

void
pw_node_info_free(struct pw_node_info *info);

/** Callback with information about a node
 * \param c A \ref pw_context
 * \param res A result code
 * \param info a \ref pw_node_info
 * \param user_data user data as passed to \ref pw_context_list_node_info()
 *
 * \memberof pw_introspect
 */
typedef void (*pw_node_info_cb_t) (struct pw_context *c,
				   int res, const struct pw_node_info *info, void *user_data);

void
pw_context_list_node_info(struct pw_context *context, pw_node_info_cb_t cb, void *user_data);

void
pw_context_get_node_info_by_id(struct pw_context *context,
			       uint32_t id, pw_node_info_cb_t cb, void *user_data);


/** The link information. Extra information can be added in later versions \memberof pw_introspect */
struct pw_link_info {
	uint32_t id;			/**< server side id of the link */
	uint64_t change_mask;		/**< bitfield of changed fields since last call */
	uint32_t output_node_id;	/**< server side output node id */
	uint32_t output_port_id;	/**< output port id */
	uint32_t input_node_id;		/**< server side input node id */
	uint32_t input_port_id;		/**< input port id */
};

struct pw_link_info *
pw_link_info_update(struct pw_link_info *info,
		    const struct pw_link_info *update);

void
pw_link_info_free(struct pw_link_info *info);


/** Callback with information about a link
 * \param c A \ref pw_context
 * \param res A result code
 * \param info a \ref pw_link_info
 * \param user_data user data as passed to \ref pw_context_list_link_info()
 *
 * \memberof pw_introspect
 */
typedef void (*pw_link_info_cb_t) (struct pw_context *c,
				   int res, const struct pw_link_info *info, void *user_data);

void
pw_context_list_link_info(struct pw_context *context, pw_link_info_cb_t cb, void *user_data);

void
pw_context_get_link_info_by_id(struct pw_context *context,
			       uint32_t id, pw_link_info_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* __PIPEWIRE_INTROSPECT_H__ */