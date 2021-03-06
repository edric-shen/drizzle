/* vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 * Drizzle Client & Protocol Library
 *
 * Copyright (C) 2008 Eric Day (eday@oddments.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 *     * The names of its contributors may not be used to endorse or
 * promote products derived from this software without specific prior
 * written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * @file
 * @brief Struct Definitions
 */

#pragma once

#include <sys/types.h>
#include <openssl/ssl.h>

#ifdef NI_MAXHOST
# define LIBDRIZZLE_NI_MAXHOST NI_MAXHOST
#else
# define LIBDRIZZLE_NI_MAXHOST 1025
#endif

#ifdef __cplusplus
#include <cstddef>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup drizzle
 */
struct drizzle_st
{
  uint16_t error_code;
  int options;
  drizzle_verbose_t verbose;
  uint32_t con_count;
  uint32_t pfds_size;
  uint32_t query_count;
  uint32_t query_new;
  uint32_t query_running;
  int last_errno;
  int timeout;
  drizzle_con_st *con_list;
  void *context;
  drizzle_context_free_fn *context_free_fn;
  drizzle_event_watch_fn *event_watch_fn;
  void *event_watch_context;
  drizzle_log_fn *log_fn;
  void *log_context;
  struct pollfd *pfds;
  drizzle_query_st *query_list;
  char sqlstate[DRIZZLE_MAX_SQLSTATE_SIZE + 1];
  char last_error[DRIZZLE_MAX_ERROR_SIZE];
};

/**
 * @ingroup drizzle_con
 */
struct drizzle_con_tcp_st
{
  in_port_t port;
  struct addrinfo *addrinfo;
  char *host;
  char host_buffer[LIBDRIZZLE_NI_MAXHOST];
};

/**
 * @ingroup drizzle_con
 */
struct drizzle_con_uds_st
{
  char path_buffer[LIBDRIZZLE_NI_MAXHOST];
};

/**
 * @ingroup drizzle_con
 */
struct drizzle_con_st
{
  uint8_t packet_number;
  uint8_t protocol_version;
  uint8_t state_current;
  short events;
  short revents;
  int capabilities;
  drizzle_charset_t charset;
  drizzle_command_t command;
  int options;
  drizzle_con_socket_t socket_type;
  drizzle_con_status_t status;
  uint32_t max_packet_size;
  uint32_t result_count;
  uint32_t thread_id;
  int backlog;
  int fd;
  size_t buffer_size;
  size_t command_offset;
  size_t command_size;
  size_t command_total;
  size_t packet_size;
  struct addrinfo *addrinfo_next;
  uint8_t *buffer_ptr;
  uint8_t *command_buffer;
  uint8_t *command_data;
  void *context;
  drizzle_con_context_free_fn *context_free_fn;
  drizzle_st *drizzle;
  drizzle_con_st *next;
  drizzle_con_st *prev;
  drizzle_query_st *query;
  drizzle_result_st *result;
  drizzle_result_st *result_list;
  uint8_t *scramble;
  union
  {
    drizzle_con_tcp_st tcp;
    drizzle_con_uds_st uds;
  } socket;
  uint8_t buffer[DRIZZLE_MAX_BUFFER_SIZE];
  char db[DRIZZLE_MAX_DB_SIZE];
  char password[DRIZZLE_MAX_PASSWORD_SIZE];
  uint8_t scramble_buffer[DRIZZLE_MAX_SCRAMBLE_SIZE];
  char server_version[DRIZZLE_MAX_SERVER_VERSION_SIZE];
  char server_extra[DRIZZLE_MAX_SERVER_EXTRA_SIZE];
  drizzle_state_fn *state_stack[DRIZZLE_STATE_STACK_SIZE];
  char user[DRIZZLE_MAX_USER_SIZE];
  SSL_CTX *ssl_context;
  SSL *ssl;
  drizzle_ssl_state_t ssl_state;
};

/**
 * @ingroup drizzle_query
 */
struct drizzle_query_st
{
  drizzle_st *drizzle;
  drizzle_query_st *next;
  drizzle_query_st *prev;
  int options;
  drizzle_query_state_t state;
  drizzle_con_st *con;
  drizzle_result_st *result;
  const char *string;
  size_t size;
  void *context;
  drizzle_query_context_free_fn *context_free_fn;

#ifdef __cplusplus

  drizzle_query_st() :
    drizzle(NULL),
    next(NULL),
    prev(NULL),
    options(0),
    state(DRIZZLE_QUERY_STATE_INIT),
    con(NULL),
    result(NULL),
    string(NULL),
    size(0),
    context(NULL),
    context_free_fn(NULL)
  { 
  }

#endif
};

/**
 * @ingroup drizzle_result
 */
struct drizzle_result_st
{
  drizzle_con_st *con;
  drizzle_result_st *next;
  drizzle_result_st *prev;
  int options;

  char info[DRIZZLE_MAX_INFO_SIZE];
  uint16_t error_code;
  char sqlstate[DRIZZLE_MAX_SQLSTATE_SIZE + 1];
  uint64_t insert_id;
  uint16_t warning_count;
  uint64_t affected_rows;

  uint16_t column_count;
  uint16_t column_current;
  drizzle_column_st *column_list;
  drizzle_column_st *column;
  drizzle_column_st *column_buffer;

  uint64_t row_count;
  uint64_t row_current;

  uint16_t field_current;
  size_t field_total;
  size_t field_offset;
  size_t field_size;
  drizzle_field_t field;
  drizzle_field_t field_buffer;

  uint64_t row_list_size;
  drizzle_row_t row;
  drizzle_row_t *row_list;
  size_t *field_sizes;
  size_t **field_sizes_list;

#ifdef __cplusplus

  drizzle_result_st() :
    con(NULL),
    next(NULL),
    prev(NULL),
    options(0),
    error_code(0),
    insert_id(0),
    warning_count(0),
    affected_rows(0),
    column_count(0),
    column_current(0),
    column_list(NULL),
    column(NULL),
    column_buffer(NULL),
    row_count(0),
    row_current(0),
    field_current(0),
    field_total(0),
    field_offset(0),
    field_size(0),
    field(),
    field_buffer(),
    row_list_size(0),
    row(),
    row_list(NULL),
    field_sizes(NULL),
    field_sizes_list(NULL)
  {
    info[0]= 0;
    sqlstate[0]= 0;
  }

#endif
};

/**
 * @ingroup drizzle_column
 */
struct drizzle_column_st
{
  drizzle_result_st *result;
  drizzle_column_st *next;
  drizzle_column_st *prev;
  int options;
  char catalog[DRIZZLE_MAX_CATALOG_SIZE];
  char db[DRIZZLE_MAX_DB_SIZE];
  char table[DRIZZLE_MAX_TABLE_SIZE];
  char orig_table[DRIZZLE_MAX_TABLE_SIZE];
  char name[DRIZZLE_MAX_COLUMN_NAME_SIZE];
  char orig_name[DRIZZLE_MAX_COLUMN_NAME_SIZE];
  drizzle_charset_t charset;
  uint32_t size;
  size_t max_size;
  drizzle_column_type_t type;
  int flags;
  uint8_t decimals;
  uint8_t default_value[DRIZZLE_MAX_DEFAULT_VALUE_SIZE];
  size_t default_value_size;
};

#ifdef __cplusplus
}
#endif
