#pragma once

#include <glib.h>

#include "goodix5135-tls-transport.h"

G_BEGIN_DECLS

#define GOODIX5135_TLS_COMMAND_REQUEST     0xd0U
#define GOODIX5135_TLS_COMMAND_ESTABLISHED 0xd4U

typedef struct _Goodix5135TlsSession Goodix5135TlsSession;

typedef enum
{
  GOODIX5135_TLS_SESSION_STATE_NEW = 0,
  GOODIX5135_TLS_SESSION_STATE_WAIT_SENSOR,
  GOODIX5135_TLS_SESSION_STATE_HOST_FRAME_PENDING,
  GOODIX5135_TLS_SESSION_STATE_WAIT_D4_ACK,
  GOODIX5135_TLS_SESSION_STATE_READY,
} Goodix5135TlsSessionState;

typedef enum
{
  GOODIX5135_TLS_SESSION_ACTION_NONE = 0,
  GOODIX5135_TLS_SESSION_ACTION_REQUEST_D0,
  GOODIX5135_TLS_SESSION_ACTION_SEND_FRAME,
  GOODIX5135_TLS_SESSION_ACTION_SEND_D4,
  GOODIX5135_TLS_SESSION_ACTION_READY,
} Goodix5135TlsSessionAction;

Goodix5135TlsSession *
goodix5135_tls_session_new (const guint8 *psk,
                            gsize         psk_len,
                            GError      **error);

void
goodix5135_tls_session_free (Goodix5135TlsSession *self);

gboolean
goodix5135_tls_session_start (Goodix5135TlsSession       *self,
                              guint8                     *command,
                              Goodix5135TlsSessionAction *action,
                              GError                    **error);

gboolean
goodix5135_tls_session_feed_sensor_frame (
  Goodix5135TlsSession       *self,
  const guint8               *sensor_frame,
  gsize                       sensor_frame_len,
  GByteArray                 *host_frame,
  GByteArray                 *app_output,
  guint8                     *command,
  Goodix5135TlsSessionAction *action,
  GError                    **error);

gboolean
goodix5135_tls_session_host_frame_sent (
  Goodix5135TlsSession       *self,
  guint8                     *command,
  Goodix5135TlsSessionAction *action,
  GError                    **error);

gboolean
goodix5135_tls_session_d4_ack (
  Goodix5135TlsSession       *self,
  Goodix5135TlsSessionAction *action,
  GError                    **error);

Goodix5135TlsSessionState
goodix5135_tls_session_get_state (Goodix5135TlsSession *self);

gboolean
goodix5135_tls_session_is_ready (Goodix5135TlsSession *self);

G_END_DECLS
