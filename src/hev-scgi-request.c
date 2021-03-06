/*
 ============================================================================
 Name        : hev-scgi-request.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2011 everyone.
 Description : 
 ============================================================================
 */

#include <string.h>

#include "hev-scgi-request.h"

static void hev_scgi_request_header_buffer_alloc(HevSCGIRequest *self,
			gsize size);
static void hev_scgi_request_input_stream_read_async_handler(GObject *source_object,
			GAsyncResult *res, gpointer user_data);
static void hev_scgi_request_input_stream_close_async_handler(GObject *source_object,
			GAsyncResult *res, gpointer user_data);
static void hev_scgi_request_parse_header(HevSCGIRequest *self);
static void hev_scgi_request_task_thread_handler(GTask *task, gpointer source_object,
			gpointer task_data, GCancellable *cancellable);

#define HEV_SCGI_REQUEST_GET_PRIVATE(obj)	(G_TYPE_INSTANCE_GET_PRIVATE((obj), HEV_TYPE_SCGI_REQUEST, HevSCGIRequestPrivate))

typedef struct _HevSCGIRequestPrivate HevSCGIRequestPrivate;

struct _HevSCGIRequestPrivate
{
	GInputStream *input_stream;
	gchar *header_buffer;
	gsize header_buffer_size;
	gsize header_buffer_fill_size;
	gsize header_size;
	guint header_head_size;
	GHashTable *header_hash_table;
};

G_DEFINE_TYPE(HevSCGIRequest, hev_scgi_request, G_TYPE_OBJECT);

static void hev_scgi_request_dispose(GObject * obj)
{
	HevSCGIRequest * self = HEV_SCGI_REQUEST_CAST(obj);
	HevSCGIRequestPrivate * priv = HEV_SCGI_REQUEST_GET_PRIVATE(self);

	g_debug("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

	if(priv->input_stream)
	{
		g_input_stream_close_async(priv->input_stream,
					0, NULL,
					hev_scgi_request_input_stream_close_async_handler,
					NULL);
	}

	G_OBJECT_CLASS(hev_scgi_request_parent_class)->dispose(obj);
}

static void hev_scgi_request_finalize(GObject * obj)
{
	HevSCGIRequest * self = HEV_SCGI_REQUEST_CAST(obj);
	HevSCGIRequestPrivate * priv = HEV_SCGI_REQUEST_GET_PRIVATE(self);

	g_debug("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

	if(priv->header_hash_table)
	{
		g_hash_table_destroy(priv->header_hash_table);
		priv->header_hash_table = NULL;
	}

	if(priv->header_buffer)
	{
		g_slice_free1(priv->header_buffer_size + 1,
					priv->header_buffer);
		priv->header_buffer = NULL;
		priv->header_buffer_size = 0;
	}

	G_OBJECT_CLASS(hev_scgi_request_parent_class)->finalize(obj);
}

static GObject * hev_scgi_request_constructor(GType type, guint n, GObjectConstructParam * param)
{
	g_debug("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);
	return G_OBJECT_CLASS(hev_scgi_request_parent_class)->constructor(type, n, param);
}

static void hev_scgi_request_constructed(GObject * obj)
{
	g_debug("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

	G_OBJECT_CLASS(hev_scgi_request_parent_class)->constructed(obj);
}

static void hev_scgi_request_class_init(HevSCGIRequestClass * klass)
{
	GObjectClass * obj_class = G_OBJECT_CLASS(klass);

	g_debug("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

	obj_class->constructor = hev_scgi_request_constructor;
	obj_class->constructed = hev_scgi_request_constructed;
	obj_class->dispose = hev_scgi_request_dispose;
	obj_class->finalize = hev_scgi_request_finalize;

	g_type_class_add_private(klass, sizeof(HevSCGIRequestPrivate));
}

static void hev_scgi_request_init(HevSCGIRequest * self)
{
	HevSCGIRequestPrivate * priv = HEV_SCGI_REQUEST_GET_PRIVATE(self);

	g_debug("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

	priv->input_stream = NULL;

	priv->header_buffer = NULL;
	priv->header_buffer_size = 0;
	priv->header_buffer_fill_size = 0;
	priv->header_size = 0;
	priv->header_head_size = 0;

	priv->header_hash_table = g_hash_table_new(g_str_hash,
				g_str_equal);
	if(!priv->header_hash_table)
	  g_critical("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);
}

/**
 * _hev_scgi_request_new
 *
 * Creates a request.
 *
 * Returns: (type HevSCGIRequest): A #HevSCGIRequest.
 *
 * Since: 0.0.1
 */
GObject * _hev_scgi_request_new(void)
{
	g_debug("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);
	return g_object_new(HEV_TYPE_SCGI_REQUEST, NULL);
}

/**
 * _hev_scgi_request_set_input_stream
 * @self: A #HevSCGIRequest
 * @input_stream: A #GInputStream
 *
 * Sets the input stream @self is for.
 *
 * Since: 0.0.1
 */
void _hev_scgi_request_set_input_stream(HevSCGIRequest *self,
			GInputStream *input_stream)
{
	HevSCGIRequestPrivate *priv = NULL;

	g_debug("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);
	
	g_return_if_fail(HEV_IS_SCGI_REQUEST(self));
	g_return_if_fail(G_IS_INPUT_STREAM(input_stream));
	priv = HEV_SCGI_REQUEST_GET_PRIVATE(self);

	if(priv->input_stream)
	  g_object_unref(priv->input_stream);

	priv->input_stream = g_object_ref(input_stream);
}

/**
 * hev_scgi_request_get_input_stream
 * @self: A #HevSCGIRequest
 *
 * Gets the input stream @self is for.
 *
 * Returns: (transfer none) (type GInputStream): A #GInputStream owned by @self. Do not free.
 *
 * Since: 0.0.1
 */
GInputStream * hev_scgi_request_get_input_stream(HevSCGIRequest *self)
{
	HevSCGIRequestPrivate *priv = NULL;

	g_debug("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);
	
	g_return_val_if_fail(HEV_IS_SCGI_REQUEST(self), NULL);
	priv = HEV_SCGI_REQUEST_GET_PRIVATE(self);

	return priv->input_stream;
}

/**
 * _hev_scgi_request_read_header_async
 * @self: A #HevSCGIRequest
 * @cancellable: (allow-none): optional #GCancellable object, %NULL to ignore.
 * @callback: (scope async): callback to call when the request is satisfied
 * @user_data: (closure): User data to pass to @callback.
 *
 * Writes header #HevSCGIRequest is for.
 *
 * Since: 0.0.1
 */
void _hev_scgi_request_read_header_async(HevSCGIRequest *self,
			GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	HevSCGIRequestPrivate *priv = NULL;
	GTask *task = NULL;

	g_debug("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);
	
	g_return_if_fail(HEV_IS_SCGI_REQUEST(self));
	g_return_if_fail(callback);
	priv = HEV_SCGI_REQUEST_GET_PRIVATE(self);
	g_return_if_fail(NULL!=priv->input_stream);

	task = g_task_new(self, cancellable, callback, user_data);

	hev_scgi_request_header_buffer_alloc(self, 128);

	priv->header_size = 16;
	g_input_stream_read_async(priv->input_stream,
				priv->header_buffer, priv->header_size, 0, NULL,
				hev_scgi_request_input_stream_read_async_handler,
				task);
}

/**
 * _hev_scgi_request_read_header_finish
 * @self: A #HevSCGIRequest
 * @res: A #GAsyncResult
 * @error: A #GError location to store the error occurring, or %NULL to ignore.
 *
 * Finishes an asynchronous read operation.
 *
 * Returns: %TRUE if the request was read successfully.
 */
gboolean _hev_scgi_request_read_header_finish(HevSCGIRequest *self, GAsyncResult *res,
			GError **error)
{
	g_debug("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

	g_return_val_if_fail(HEV_IS_SCGI_REQUEST(self), FALSE);

	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);

	return g_task_propagate_boolean(G_TASK(res), error);
}

static void hev_scgi_request_header_buffer_alloc(HevSCGIRequest *self,
			gsize size)
{
	HevSCGIRequestPrivate *priv = NULL;
	gpointer buffer = NULL;

	g_debug("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);
	
	g_return_if_fail(HEV_IS_SCGI_REQUEST(self));
	g_return_if_fail(0<size);
	priv = HEV_SCGI_REQUEST_GET_PRIVATE(self);

	buffer = g_slice_alloc(size + 1);
	if(priv->header_buffer)
	{
		g_memmove(buffer, priv->header_buffer,
					priv->header_buffer_fill_size);
		g_slice_free1(priv->header_buffer_size + 1,
					priv->header_buffer);
	}
	priv->header_buffer = buffer;
	priv->header_buffer_size = size;
}

static void hev_scgi_request_input_stream_read_async_handler(GObject *source_object,
			GAsyncResult *res, gpointer user_data)
{
	GTask *task = G_TASK(user_data);
	GObject *request = g_async_result_get_source_object(G_ASYNC_RESULT(task));
	HevSCGIRequest *self = HEV_SCGI_REQUEST_CAST(request);
	HevSCGIRequestPrivate *priv = HEV_SCGI_REQUEST_GET_PRIVATE(self);
	gssize size = 0;
	GError *error = NULL;

	g_debug("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

	/* Unref 'request' get from g_async_result_get_source_object */
	g_object_unref(request);

	size = g_input_stream_read_finish(G_INPUT_STREAM(source_object),
				res, &error);
	/* Call callback when connection closed by remote or error */
	if(0 >= size)
	{
		g_task_return_error(task, error);
		g_object_unref(task);

		return;
	}

	priv->header_buffer_fill_size += size;

	/* Parse real header size */
	if (0 == priv->header_head_size)
	{
		gchar *str;
		guint64 header_size;

		priv->header_buffer[priv->header_buffer_fill_size] = '\0';
		str = strchr (priv->header_buffer, ':');
		if(!str)
		{
			if (priv->header_buffer_fill_size >= priv->header_size)
			  goto invalid;

			goto again;
		}

		*str = '\0';
		header_size = g_ascii_strtoull(priv->header_buffer, NULL, 10);
		if (0 == header_size)
		  goto invalid;

		priv->header_head_size = (str - priv->header_buffer) + 1;
		priv->header_size = header_size + priv->header_head_size + 1;
		if(priv->header_buffer_size < priv->header_size)
		  hev_scgi_request_header_buffer_alloc(self, priv->header_size);
	}

	if(priv->header_buffer_fill_size < priv->header_size)
	  goto again;

	g_task_run_in_thread(task, hev_scgi_request_task_thread_handler);
	return;

invalid:
	g_task_return_new_error(task, G_IO_ERROR,
				G_IO_ERROR_INVALID_DATA, "Invalid data!");
	g_object_unref(task);
	return;

again:
	g_input_stream_read_async(priv->input_stream,
				priv->header_buffer+priv->header_buffer_fill_size,
				priv->header_size-priv->header_buffer_fill_size,
				0, NULL,
				hev_scgi_request_input_stream_read_async_handler,
				task);
}

static void hev_scgi_request_input_stream_close_async_handler(GObject *source_object,
			GAsyncResult *res, gpointer user_data)
{
	g_debug("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

	g_input_stream_close_finish(G_INPUT_STREAM(source_object),
				res, NULL);
	g_object_unref(source_object);
}

static void hev_scgi_request_parse_header(HevSCGIRequest *self)
{
	HevSCGIRequestPrivate *priv = HEV_SCGI_REQUEST_GET_PRIVATE(self);
	gchar *fields;
	gchar *fields_end;

	g_debug("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

	fields = priv->header_buffer + priv->header_head_size;
	fields_end = priv->header_buffer + priv->header_size - 1;

	for (; fields<fields_end;)
	{
		gchar *key;
		gchar *value;

		key = fields;
		value = key + strlen (key) + 1;
		g_hash_table_insert(priv->header_hash_table, key, value);

		fields = value + strlen (value) + 1;
	}
}

static void
hev_scgi_request_task_thread_handler(GTask *task, gpointer source_object,
			gpointer task_data, GCancellable *cancellable)
{
	HevSCGIRequest *self = HEV_SCGI_REQUEST_CAST(source_object);

	/* Parse header fields */
	hev_scgi_request_parse_header(self);

	g_task_return_boolean(task, TRUE);
	g_object_unref(task);
}

/**
 * hev_scgi_request_get_header_hash_table
 * @self: A #HevSCGIRequest
 *
 * Gets the header hash table @self is for.
 *
 * Returns: (transfer none) (element-type utf8 utf8): A #GHashTable owned by @self. Do not free.
 *
 * Since: 0.0.1
 */
GHashTable * hev_scgi_request_get_header_hash_table(HevSCGIRequest *self)
{
	HevSCGIRequestPrivate *priv = NULL;

	g_debug("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

	g_return_val_if_fail(HEV_IS_SCGI_REQUEST(self), NULL);
	priv = HEV_SCGI_REQUEST_GET_PRIVATE(self);

	return priv->header_hash_table;
}

