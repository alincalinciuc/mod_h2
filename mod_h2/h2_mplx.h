/* Copyright 2015 greenbytes GmbH (https://www.greenbytes.de)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __mod_h2__h2_mplx__
#define __mod_h2__h2_mplx__

/**
 * The stream multiplexer. It pushes h2_buckets from the connection
 * thread to the stream task threads and vice versa.
 *
 * There is one h2_mplx instance for each h2_session, which sits on top
 * of a particular httpd conn_rec. Input goes from the connection to
 * the stream tasks. Output goes from the stream tasks to the connection,
 * e.g. the client.
 *
 * For each stream, there can be at most "H2StreamMaxMemSize" output bytes
 * queued in the multiplexer. If a task thread tries to write more
 * data, it is blocked until space becomes available.
 *
 * Writing input is never blocked here. This needs to be handled via
 * HTTP2 flow control against the client.
 */

struct apr_pool_t;
struct apr_thread_mutex_t;
struct apr_thread_cond_t;
struct h2_bucket;
struct h2_config;
struct h2_response;

typedef struct h2_mplx h2_mplx;

/*******************************************************************************
 * Object lifecycle and information.
 ******************************************************************************/

/**
 * Create the multiplexer for the given HTTP2 session.
 */
h2_mplx *h2_mplx_create(conn_rec *c, apr_pool_t *master);

/**
 * Destroys the multiplexer. Cleans up memory. Should only be called
 * upon session destruction.
 */
void h2_mplx_destroy(h2_mplx *mplx);

/**
 * Get the id of the multiplexer, same as the session id it belongs to.
 */
long h2_mplx_get_id(h2_mplx *mplx);

/**
 * Get the memory pool used by the multiplexer itself.
 */
apr_pool_t *h2_mplx_get_pool(h2_mplx *mplx);

/**
 * Get the main connection this multiplexer works for.
 */
conn_rec *h2_mplx_get_conn(h2_mplx *mplx);

/**
 * Aborts the multiplexer. It will answer all future invocation with
 * APR_ECONNABORTED, leading to early termination of ongoing tasks.
 */
void h2_mplx_abort(h2_mplx *mplx);

/*******************************************************************************
 * IO lifetime of streams.
 ******************************************************************************/
/**
 * Prepares the multiplexer to handle in-/output on the given stream id.
 */
apr_status_t h2_mplx_open_io(h2_mplx *mplx, int stream_id);

/**
 * Ends handling of in-/ouput on the given stream id.
 */
void h2_mplx_close_io(h2_mplx *mplx, int stream_id);

/* Return != 0 iff the multiplexer has data for the given stream. 
 */
int h2_mplx_out_has_data_for(h2_mplx *m, int stream_id);

/**
 * Waits on output data from any stream in this session to become available. 
 * Returns APR_TIMEUP if no data arrived in the given time.
 */
apr_status_t h2_mplx_out_trywait(h2_mplx *m, apr_interval_time_t timeout,
                                 struct apr_thread_cond_t *iowait);

/*******************************************************************************
 * Input handling of streams.
 ******************************************************************************/

/**
 * Reads a h2_bucket for the given stream_id. Will return ARP_EAGAIN when
 * called with APR_NONBLOCK_READ and no data present. Will return APR_EOF
 * when the end of the stream input has been reached.
 */
apr_status_t h2_mplx_in_read(h2_mplx *mplx, apr_read_type_e block,
                             int stream_id, struct h2_bucket **pbucket,
                             struct apr_thread_cond_t *iowait);

/**
 * Appends data to the input of the given stream. Storage of input data is
 * not subject to flow control.
 */
apr_status_t h2_mplx_in_write(h2_mplx *mplx, int stream_id, 
                              struct h2_bucket *bucket);

/**
 * Closes the input for the given stream_id.
 */
apr_status_t h2_mplx_in_close(h2_mplx *m, int stream_id);

/**
 * Returns != 0 iff the input for the given stream has been closed. There
 * could still be data queued, but it can be read without blocking.
 */
int h2_mplx_in_has_eos_for(h2_mplx *m, int stream_id);

/*******************************************************************************
 * Output handling of streams.
 ******************************************************************************/

/**
 * Gets a response from a stream that is ready for submit. Will return
 * NULL if none is available.
 */
struct h2_response *h2_mplx_pop_response(h2_mplx *m);

/**
 * Reads output data from the given stream. Will never block, but
 * return APR_EAGAIN until data arrives or the stream is closed.
 */
apr_status_t h2_mplx_out_read(h2_mplx *mplx, int stream_id, 
                              struct h2_bucket **pbucket, int *peos);

/**
 * Opens the output for the given stream with the specified response.
 */
apr_status_t h2_mplx_out_open(h2_mplx *mplx, int stream_id,
                              struct h2_response *response);

/**
 * Writes data to the output of the given stream. With APR_BLOCK_READ, it
 * is subject to flow control, otherwise it return APR_EAGAIN if the 
 * limit on queued data has been reached.
 */
apr_status_t h2_mplx_out_write(h2_mplx *mplx, apr_read_type_e block,
                               int stream_id, struct h2_bucket *bucket,
                               struct apr_thread_cond_t *iowait);

/**
 * Closes the output stream. Readers of this stream will get all pending 
 * data and then only APR_EOF as result. 
 */
apr_status_t h2_mplx_out_close(h2_mplx *m, int stream_id);

/*******************************************************************************
 * Error handling of streams.
 ******************************************************************************/

/**
 * Resets the given stream. Indicate, which error occured, if any. This
 * (so far) only works when the response has not already been queued.
 */
apr_status_t h2_mplx_out_reset(h2_mplx *m, int stream_id, apr_status_t status);

#endif /* defined(__mod_h2__h2_mplx__) */
