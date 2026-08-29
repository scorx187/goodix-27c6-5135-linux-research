#include "goodix5135-tls-transport.h"

#include <gio/gio.h>

struct _Goodix5135TlsTransport
{
  Goodix5135Tls *tls;
};

static gboolean
goodix5135_tls_transport_flags_valid (guint8 flags)
{
  return flags == GOODIX5135_TLS_PACK_FLAGS_CONTROL ||
         flags == GOODIX5135_TLS_PACK_FLAGS_DATA;
}

static guint8
goodix5135_tls_transport_header_checksum (guint8  flags,
                                          guint16 payload_len)
{
  guint value;

  value = flags;
  value += payload_len & 0xffU;
  value += (payload_len >> 8) & 0xffU;

  return (guint8) (value & 0xffU);
}

Goodix5135TlsTransport *
goodix5135_tls_transport_new (const guint8 *psk,
                              gsize         psk_len,
                              GError      **error)
{
  Goodix5135TlsTransport *self;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  self = g_new0 (Goodix5135TlsTransport, 1);

  self->tls = goodix5135_tls_new (psk,
                                  psk_len,
                                  error);

  if (self->tls == NULL)
    {
      g_free (self);
      return NULL;
    }

  return self;
}

void
goodix5135_tls_transport_free (Goodix5135TlsTransport *self)
{
  if (self == NULL)
    return;

  goodix5135_tls_free (self->tls);
  self->tls = NULL;

  g_free (self);
}

gboolean
goodix5135_tls_transport_build_frame (guint8         flags,
                                      const guint8  *payload,
                                      gsize          payload_len,
                                      GByteArray    *frame,
                                      GError       **error)
{
  guint16 wire_length;
  guint8  header[GOODIX5135_TLS_PACK_HEADER_SIZE];

  g_return_val_if_fail (frame != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!goodix5135_tls_transport_flags_valid (flags))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Invalid Goodix5135 TLS transport flags");
      return FALSE;
    }

  if (payload_len > GOODIX5135_TLS_PACK_MAX_PAYLOAD)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Goodix5135 TLS payload is too large");
      return FALSE;
    }

  if (payload_len > 0 && payload == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Goodix5135 TLS payload pointer is NULL");
      return FALSE;
    }

  wire_length = (guint16) payload_len;

  header[0] = flags;
  header[1] = wire_length & 0xffU;
  header[2] = (wire_length >> 8) & 0xffU;
  header[3] = goodix5135_tls_transport_header_checksum (flags,
                                                        wire_length);

  g_byte_array_set_size (frame, 0);

  g_byte_array_append (frame,
                       header,
                       sizeof (header));

  if (payload_len > 0)
    g_byte_array_append (frame,
                         payload,
                         (guint) payload_len);

  return TRUE;
}

gboolean
goodix5135_tls_transport_parse_frame (const guint8   *frame,
                                      gsize           frame_len,
                                      guint8         *flags,
                                      const guint8  **payload,
                                      gsize          *payload_len,
                                      GError        **error)
{
  guint8  parsed_flags;
  guint16 declared;
  guint8  expected_checksum;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (frame == NULL ||
      frame_len < GOODIX5135_TLS_PACK_HEADER_SIZE)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Goodix5135 TLS frame is shorter than its header");
      return FALSE;
    }

  parsed_flags = frame[0];

  if (!goodix5135_tls_transport_flags_valid (parsed_flags))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Unexpected Goodix5135 TLS transport flags");
      return FALSE;
    }

  declared = ((guint16) frame[1]) |
             ((guint16) frame[2] << 8);

  expected_checksum =
    goodix5135_tls_transport_header_checksum (parsed_flags,
                                              declared);

  if (frame[3] != expected_checksum)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Goodix5135 TLS frame header checksum mismatch");
      return FALSE;
    }

  if (frame_len < GOODIX5135_TLS_PACK_HEADER_SIZE + (gsize) declared)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Goodix5135 TLS frame is shorter than declared");
      return FALSE;
    }

  if (flags != NULL)
    *flags = parsed_flags;

  if (payload != NULL)
    *payload = frame + GOODIX5135_TLS_PACK_HEADER_SIZE;

  if (payload_len != NULL)
    *payload_len = declared;

  return TRUE;
}

gboolean
goodix5135_tls_transport_feed_frame (Goodix5135TlsTransport *self,
                                     const guint8            *sensor_frame,
                                     gsize                    sensor_frame_len,
                                     GByteArray              *sensor_output_frame,
                                     GByteArray              *app_output,
                                     GError                 **error)
{
  g_autoptr(GByteArray) tls_wire = NULL;
  const guint8         *payload;
  gsize                 payload_len;
  guint8                flags;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (self->tls != NULL, FALSE);
  g_return_val_if_fail (sensor_output_frame != NULL, FALSE);
  g_return_val_if_fail (app_output != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_byte_array_set_size (sensor_output_frame, 0);
  g_byte_array_set_size (app_output, 0);

  if (!goodix5135_tls_transport_parse_frame (sensor_frame,
                                             sensor_frame_len,
                                             &flags,
                                             &payload,
                                             &payload_len,
                                             error))
    return FALSE;

  /*
   * Both 0xb0 and 0xb2 carry raw TLS bytes.
   *
   * The historical bridge uses 0xb0 during handshake and the same outer
   * framing for encrypted image traffic. 0xb2 is accepted as the other
   * known TLS data class, but it is never treated as message protocol.
   */
  tls_wire = g_byte_array_new ();

  if (!goodix5135_tls_feed (self->tls,
                            payload,
                            payload_len,
                            tls_wire,
                            app_output,
                            error))
    return FALSE;

  if (tls_wire->len > 0)
    {
      if (!goodix5135_tls_transport_build_frame (
            GOODIX5135_TLS_PACK_FLAGS_CONTROL,
            tls_wire->data,
            tls_wire->len,
            sensor_output_frame,
            error))
        return FALSE;
    }

  return TRUE;
}

gboolean
goodix5135_tls_transport_is_established (Goodix5135TlsTransport *self)
{
  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (self->tls != NULL, FALSE);

  return goodix5135_tls_is_established (self->tls);
}
