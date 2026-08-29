#include "goodix5135-tls-session.h"

#include <gio/gio.h>

struct _Goodix5135TlsSession
{
  Goodix5135TlsTransport  *transport;
  Goodix5135TlsSessionState state;
};

static gboolean
goodix5135_tls_session_error_state (GError                    **error,
                                    const gchar                *operation,
                                    Goodix5135TlsSessionState   state)
{
  g_set_error (error,
               G_IO_ERROR,
               G_IO_ERROR_INVALID_ARGUMENT,
               "%s is invalid in TLS session state %u",
               operation,
               (guint) state);

  return FALSE;
}

Goodix5135TlsSession *
goodix5135_tls_session_new (const guint8 *psk,
                            gsize         psk_len,
                            GError      **error)
{
  Goodix5135TlsSession *self;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  self = g_new0 (Goodix5135TlsSession, 1);

  self->transport =
    goodix5135_tls_transport_new (psk,
                                  psk_len,
                                  error);

  if (self->transport == NULL)
    {
      g_free (self);
      return NULL;
    }

  self->state = GOODIX5135_TLS_SESSION_STATE_NEW;

  return self;
}

void
goodix5135_tls_session_free (Goodix5135TlsSession *self)
{
  if (self == NULL)
    return;

  goodix5135_tls_transport_free (self->transport);
  self->transport = NULL;

  g_free (self);
}

gboolean
goodix5135_tls_session_start (Goodix5135TlsSession       *self,
                              guint8                     *command,
                              Goodix5135TlsSessionAction *action,
                              GError                    **error)
{
  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (command != NULL, FALSE);
  g_return_val_if_fail (action != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (self->state != GOODIX5135_TLS_SESSION_STATE_NEW)
    return goodix5135_tls_session_error_state (
      error,
      "TLS session start",
      self->state);

  *command = GOODIX5135_TLS_COMMAND_REQUEST;
  *action = GOODIX5135_TLS_SESSION_ACTION_REQUEST_D0;

  self->state =
    GOODIX5135_TLS_SESSION_STATE_WAIT_SENSOR;

  return TRUE;
}

gboolean
goodix5135_tls_session_feed_sensor_frame (
  Goodix5135TlsSession       *self,
  const guint8               *sensor_frame,
  gsize                       sensor_frame_len,
  GByteArray                 *host_frame,
  GByteArray                 *app_output,
  guint8                     *command,
  Goodix5135TlsSessionAction *action,
  GError                    **error)
{
  gboolean handshake_phase;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (host_frame != NULL, FALSE);
  g_return_val_if_fail (app_output != NULL, FALSE);
  g_return_val_if_fail (command != NULL, FALSE);
  g_return_val_if_fail (action != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  handshake_phase =
    self->state == GOODIX5135_TLS_SESSION_STATE_WAIT_SENSOR;

  if (!handshake_phase &&
      self->state != GOODIX5135_TLS_SESSION_STATE_READY)
    return goodix5135_tls_session_error_state (
      error,
      "TLS sensor frame",
      self->state);

  *command = 0;
  *action = GOODIX5135_TLS_SESSION_ACTION_NONE;

  g_byte_array_set_size (host_frame, 0);
  g_byte_array_set_size (app_output, 0);

  if (!goodix5135_tls_transport_feed_frame (
        self->transport,
        sensor_frame,
        sensor_frame_len,
        host_frame,
        app_output,
        error))
    return FALSE;

  if (!handshake_phase)
    {
      if (host_frame->len != 0)
        {
          g_set_error_literal (
            error,
            G_IO_ERROR,
            G_IO_ERROR_NOT_SUPPORTED,
            "Post-handshake outbound TLS traffic is not yet wired");
          return FALSE;
        }

      return TRUE;
    }

  if (host_frame->len > 0)
    {
      self->state =
        GOODIX5135_TLS_SESSION_STATE_HOST_FRAME_PENDING;

      *action =
        GOODIX5135_TLS_SESSION_ACTION_SEND_FRAME;

      return TRUE;
    }

  if (goodix5135_tls_transport_is_established (
        self->transport))
    {
      self->state =
        GOODIX5135_TLS_SESSION_STATE_WAIT_D4_ACK;

      *command = GOODIX5135_TLS_COMMAND_ESTABLISHED;
      *action = GOODIX5135_TLS_SESSION_ACTION_SEND_D4;

      return TRUE;
    }

  self->state =
    GOODIX5135_TLS_SESSION_STATE_WAIT_SENSOR;

  return TRUE;
}

gboolean
goodix5135_tls_session_host_frame_sent (
  Goodix5135TlsSession       *self,
  guint8                     *command,
  Goodix5135TlsSessionAction *action,
  GError                    **error)
{
  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (command != NULL, FALSE);
  g_return_val_if_fail (action != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (self->state !=
      GOODIX5135_TLS_SESSION_STATE_HOST_FRAME_PENDING)
    return goodix5135_tls_session_error_state (
      error,
      "TLS host frame completion",
      self->state);

  *command = 0;
  *action = GOODIX5135_TLS_SESSION_ACTION_NONE;

  if (goodix5135_tls_transport_is_established (
        self->transport))
    {
      self->state =
        GOODIX5135_TLS_SESSION_STATE_WAIT_D4_ACK;

      *command = GOODIX5135_TLS_COMMAND_ESTABLISHED;
      *action = GOODIX5135_TLS_SESSION_ACTION_SEND_D4;

      return TRUE;
    }

  self->state =
    GOODIX5135_TLS_SESSION_STATE_WAIT_SENSOR;

  return TRUE;
}

gboolean
goodix5135_tls_session_d4_ack (
  Goodix5135TlsSession       *self,
  Goodix5135TlsSessionAction *action,
  GError                    **error)
{
  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (action != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (self->state !=
      GOODIX5135_TLS_SESSION_STATE_WAIT_D4_ACK)
    return goodix5135_tls_session_error_state (
      error,
      "TLS D4 acknowledgement",
      self->state);

  if (!goodix5135_tls_transport_is_established (
        self->transport))
    {
      g_set_error_literal (
        error,
        G_IO_ERROR,
        G_IO_ERROR_FAILED,
        "TLS D4 acknowledgement arrived before TLS establishment");

      return FALSE;
    }

  self->state =
    GOODIX5135_TLS_SESSION_STATE_READY;

  *action = GOODIX5135_TLS_SESSION_ACTION_READY;

  return TRUE;
}

Goodix5135TlsSessionState
goodix5135_tls_session_get_state (Goodix5135TlsSession *self)
{
  g_return_val_if_fail (
    self != NULL,
    GOODIX5135_TLS_SESSION_STATE_NEW);

  return self->state;
}

gboolean
goodix5135_tls_session_is_ready (Goodix5135TlsSession *self)
{
  g_return_val_if_fail (self != NULL, FALSE);

  return self->state ==
         GOODIX5135_TLS_SESSION_STATE_READY;
}
