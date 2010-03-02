/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
** Common definition between mysql server & client
*/

#ifndef DRIZZLED_COMMON_H
#define DRIZZLED_COMMON_H

#include <unistd.h>
#include <stdint.h>
#include <drizzled/korr.h>

/*
   This is included in the server and in the client.
   Options for select set by the yacc parser (stored in lex->options).

   XXX:
   log_event.h defines OPTIONS_WRITTEN_TO_BIN_LOG to specify what THD
   options list are written into binlog. These options can NOT change their
   values, or it will break replication between version.

   context is encoded as following:
   SELECT - Select_Lex_Node::options
   THD    - THD::options
   intern - neither. used only as
            func(..., select_node->options | thd->options | OPTION_XXX, ...)

   TODO: separate three contexts above, move them to separate bitfields.
*/

#define SELECT_DISTINCT         (UINT64_C(1) << 0)     // SELECT, user
#define SELECT_STRAIGHT_JOIN    (UINT64_C(1) << 1)     // SELECT, user
#define SELECT_DESCRIBE         (UINT64_C(1) << 2)     // SELECT, user
#define SELECT_SMALL_RESULT     (UINT64_C(1) << 3)     // SELECT, user
#define SELECT_BIG_RESULT       (UINT64_C(1) << 4)     // SELECT, user
#define OPTION_FOUND_ROWS       (UINT64_C(1) << 5)     // SELECT, user
#define SELECT_NO_JOIN_CACHE    (UINT64_C(1) << 7)     // intern
#define OPTION_BIG_TABLES       (UINT64_C(1) << 8)     // THD, user
#define OPTION_BIG_SELECTS      (UINT64_C(1) << 9)     // THD, user
#define OPTION_LOG_OFF          (UINT64_C(1) << 10)    // THD, user
#define TMP_TABLE_ALL_COLUMNS   (UINT64_C(1) << 12)    // SELECT, intern
#define OPTION_WARNINGS         (UINT64_C(1) << 13)    // THD, user
#define OPTION_AUTO_IS_NULL     (UINT64_C(1) << 14)    // THD, user, binlog
#define OPTION_FOUND_COMMENT    (UINT64_C(1) << 15)    // SELECT, intern, parser
#define OPTION_BUFFER_RESULT    (UINT64_C(1) << 17)    // SELECT, user
#define OPTION_NOT_AUTOCOMMIT   (UINT64_C(1) << 19)    // THD, user
#define OPTION_BEGIN            (UINT64_C(1) << 20)    // THD, intern

/* The following is used to detect a conflict with DISTINCT */
#define SELECT_ALL              (UINT64_C(1) << 24)    // SELECT, user, parser

/** The following can be set when importing tables in a 'wrong order'
   to suppress foreign key checks */
#define OPTION_NO_FOREIGN_KEY_CHECKS    (UINT64_C(1) << 26) // THD, user, binlog
/** The following speeds up inserts to InnoDB tables by suppressing unique
   key checks in some cases */
#define OPTION_RELAXED_UNIQUE_CHECKS    (UINT64_C(1) << 27) // THD, user, binlog
#define SELECT_NO_UNLOCK                (UINT64_C(1) << 28) // SELECT, intern
#define OPTION_SCHEMA_TABLE             (UINT64_C(1) << 29) // SELECT, intern
/** Flag set if setup_tables already done */
#define OPTION_SETUP_TABLES_DONE        (UINT64_C(1) << 30) // intern
/** If not set then the thread will ignore all warnings with level notes. */
#define OPTION_SQL_NOTES                (UINT64_C(1) << 31) // THD, user
#define OPTION_PROFILING                (UINT64_C(1) << 33)

/*
  Dont report errors for individual rows,
  But just report error on commit (or read ofcourse)
*/
#define OPTION_ALLOW_BATCH              (UINT64_C(1) << 33) // THD, intern (slave)

/**
  Maximum length of time zone name that we support
  (Time zone name is char(64) in db). mysqlbinlog needs it.
*/
#define MAX_TIME_ZONE_NAME_LENGTH       (NAME_LEN + 1)

#define HOSTNAME_LENGTH 60
#define SYSTEM_CHARSET_MBMAXLEN 4
#define USERNAME_CHAR_LENGTH 16
#define NAME_CHAR_LEN	64              /* Field/table name length */
#define NAME_LEN                (NAME_CHAR_LEN*SYSTEM_CHARSET_MBMAXLEN)
#define USERNAME_LENGTH         (USERNAME_CHAR_LENGTH*SYSTEM_CHARSET_MBMAXLEN)

#define SERVER_VERSION_LENGTH 60
#define SQLSTATE_LENGTH 5

/*
  Maximum length of comments
*/
#define TABLE_COMMENT_MAXLEN 2048
#define COLUMN_COMMENT_MAXLEN 1024
#define INDEX_COMMENT_MAXLEN 1024

/*
  USER_HOST_BUFF_SIZE -- length of string buffer, that is enough to contain
  username and hostname parts of the user identifier with trailing zero in
  MySQL standard format:
  user_name_part@host_name_part\0
*/
#define USER_HOST_BUFF_SIZE HOSTNAME_LENGTH + USERNAME_LENGTH + 2

/*
  You should add new commands to the end of this list, otherwise old
  servers won't be able to handle them as 'unsupported'.
*/

/*
  Length of random string sent by server on handshake; this is also length of
  obfuscated password, recieved from client
*/
#define SCRAMBLE_LENGTH 20
#define SCRAMBLE_LENGTH_323 8

#define NOT_NULL_FLAG	1		/* Field can't be NULL */
#define PRI_KEY_FLAG	2		/* Field is part of a primary key */
#define UNIQUE_KEY_FLAG 4		/* Field is part of a unique key */
#define MULTIPLE_KEY_FLAG 8		/* Field is part of a key */
#define BLOB_FLAG	16		/* Field is a blob */
#define UNSIGNED_FLAG	32		/* Field is unsigned */
#define BINARY_FLAG	128		/* Field is binary   */

/* The following are only sent to new clients */
#define ENUM_FLAG	256		/* field is an enum */
#define AUTO_INCREMENT_FLAG 512		/* field is a autoincrement field */
#define TIMESTAMP_FLAG	1024		/* Field is a timestamp */
#define SET_FLAG	2048		/* field is a set */
#define NO_DEFAULT_VALUE_FLAG 4096	/* Field doesn't have default value */
#define ON_UPDATE_NOW_FLAG 8192         /* Field is set to NOW on UPDATE */
#define PART_KEY_FLAG	16384		/* Intern; Part of some key */
#define GROUP_FLAG	32768		/* Intern: Group field */
#define UNIQUE_FLAG	65536		/* Intern: Used by sql_yacc */
#define BINCMP_FLAG	131072		/* Intern: Used by sql_yacc */
#define FIELD_STORAGE_FLAGS 22          /* Storage type: bit 22, 23 and 24 */
#define COLUMN_FORMAT_FLAGS 25          /* Column format: bit 25, 26 and 27 */
#define COLUMN_FORMAT_MASK 7

#define REFRESH_LOG		2	/* Start on new log file */
#define REFRESH_TABLES		4	/* close all tables */
#define REFRESH_STATUS		16	/* Flush status variables */

/* The following can't be set with mysql_refresh() */
#define REFRESH_READ_LOCK	16384	/* Lock tables for read */
#define REFRESH_FAST		32768	/* Intern flag */

#define SERVER_STATUS_IN_TRANS     1	/* Transaction has started */
#define SERVER_STATUS_AUTOCOMMIT   2	/* Server in auto_commit mode */
#define SERVER_MORE_RESULTS_EXISTS 8    /* Multi query - next query exists */
#define SERVER_QUERY_NO_GOOD_INDEX_USED 16
#define SERVER_QUERY_NO_INDEX_USED      32
/*
  The server was able to fulfill the clients request and opened a
  read-only non-scrollable cursor for a query. This flag comes
  in reply to COM_STMT_EXECUTE and COM_STMT_FETCH commands.
*/
#define SERVER_STATUS_CURSOR_EXISTS 64
/*
  This flag is sent when a read-only cursor is exhausted, in reply to
  COM_STMT_FETCH command.
*/
#define SERVER_STATUS_LAST_ROW_SENT 128
#define SERVER_STATUS_DB_DROPPED        256 /* A database was dropped */
#define SERVER_STATUS_NO_BACKSLASH_ESCAPES 512
/*
  Tell clients that this query was logged to the slow query log.
  Not yet set in the server, but interface is defined for applications
  to use.  See WorkLog 4098.
*/
#define SERVER_QUERY_WAS_SLOW           1024

#define DRIZZLE_ERRMSG_SIZE	512

#define ONLY_KILL_QUERY         1

#define MAX_INT_WIDTH           10      /* Max width for a LONG w.o. sign */
#define MAX_BIGINT_WIDTH        20      /* Max width for a LONGLONG */
#define MAX_CHAR_WIDTH		255	/* Max length for a CHAR colum */
#define MAX_BLOB_WIDTH		(uint32_t)16777216	/* Default width for blob */

#define DRIZZLE_PROTOCOL_NO_MORE_DATA 0xFE




#define packet_error (~(uint32_t) 0)


/* Shutdown/kill enums and constants */

/* Bits for THD::killable. */
#define DRIZZLE_SHUTDOWN_KILLABLE_CONNECT    (unsigned char)(1 << 0)
#define DRIZZLE_SHUTDOWN_KILLABLE_TRANS      (unsigned char)(1 << 1)
#define DRIZZLE_SHUTDOWN_KILLABLE_LOCK_TABLE (unsigned char)(1 << 2)
#define DRIZZLE_SHUTDOWN_KILLABLE_UPDATE     (unsigned char)(1 << 3)

#if defined(__cplusplus)

namespace drizzled
{

enum enum_server_command
{
  COM_SLEEP,
  COM_QUIT,
  COM_INIT_DB,
  COM_QUERY,
  COM_SHUTDOWN,
  COM_CONNECT,
  COM_PING,
  /* don't forget to update const char *command_name[] in sql_parse.cc */
  /* Must be last */
  COM_END
};


enum enum_field_types { DRIZZLE_TYPE_LONG,
                        DRIZZLE_TYPE_DOUBLE,
                        DRIZZLE_TYPE_NULL,
                        DRIZZLE_TYPE_TIMESTAMP,
                        DRIZZLE_TYPE_LONGLONG,
                        DRIZZLE_TYPE_DATETIME,
                        DRIZZLE_TYPE_DATE,
                        DRIZZLE_TYPE_VARCHAR,
                        DRIZZLE_TYPE_DECIMAL,
                        DRIZZLE_TYPE_ENUM,
                        DRIZZLE_TYPE_BLOB,
                        DRIZZLE_TYPE_MAX=DRIZZLE_TYPE_BLOB
};


  /* The following is for user defined functions */

enum Item_result {STRING_RESULT=0, REAL_RESULT, INT_RESULT, ROW_RESULT,
                  DECIMAL_RESULT};

} /* namespace drizzled */

#endif /* defined(__cplusplus) */

#endif /* DRIZZLED_COMMON_H */
