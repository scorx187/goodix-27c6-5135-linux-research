#include "goodix5135-tls.h"

#include <gio/gio.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <string.h>

#define GOODIX5135_TLS_IDENTITY "Client_identity"
#define GOODIX5135_TLS_CIPHER   "PSK-AES128-GCM-SHA256"
#define GOODIX5135_TLS_MAX_PSK  64

struct _Goodix5135Tls
{
  SSL_CTX *ctx;
  SSL     *ssl;

  guint8   psk[GOODIX5135_TLS_MAX_PSK];
  gsize    psk_len;
};

static void
goodix5135_tls_set_ssl_error (GError     **error,
                              const gchar *context)
{
  unsigned long code;
  gchar         buffer[256];

  code = ERR_get_error ();

  if (code != 0)
    {
      ERR_error_string_n (code, buffer, sizeof (buffer));

      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "%s: %s",
                   context,
                   buffer);
    }
  else
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "%s",
                   context);
    }
}

static unsigned int
goodix5135_tls_psk_server_cb (SSL           *ssl,
                              const char    *identity,
                              unsigned char *psk,
                              unsigned int   max_psk_len)
{
  Goodix5135Tls *self;

  self = SSL_get_app_data (ssl);

  if (self == NULL)
    return 0;

  if (identity == NULL)
    return 0;

  if (g_strcmp0 (identity, GOODIX5135_TLS_IDENTITY) != 0)
    return 0;

  if (self->psk_len > max_psk_len)
    return 0;

  memcpy (psk,
          self->psk,
          self->psk_len);

  return (unsigned int) self->psk_len;
}

static gboolean
goodix5135_tls_drain_wire (Goodix5135Tls *self,
                           GByteArray     *output,
                           GError        **error)
{
  BIO   *wbio;
  guint8 buffer[4096];

  g_return_val_if_fail (self != NULL, FALSE);

  wbio = SSL_get_wbio (self->ssl);

  if (wbio == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_FAILED,
                           "TLS write BIO is unavailable");
      return FALSE;
    }

  while (BIO_ctrl_pending (wbio) > 0)
    {
      int count;

      count = BIO_read (wbio,
                        buffer,
                        sizeof (buffer));

      if (count <= 0)
        {
          if (BIO_should_retry (wbio))
            continue;

          g_set_error_literal (error,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               "Failed to drain TLS write BIO");
          return FALSE;
        }

      if (output != NULL)
        g_byte_array_append (output,
                             buffer,
                             (guint) count);
    }

  return TRUE;
}

static gboolean
goodix5135_tls_drive_handshake (Goodix5135Tls *self,
                                GByteArray     *wire_output,
                                GError        **error)
{
  guint iteration;

  for (iteration = 0; iteration < 64; iteration++)
    {
      int result;
      int ssl_error;

      if (SSL_is_init_finished (self->ssl))
        return TRUE;

      result = SSL_do_handshake (self->ssl);

      if (!goodix5135_tls_drain_wire (self,
                                      wire_output,
                                      error))
        return FALSE;

      if (result == 1)
        return TRUE;

      ssl_error = SSL_get_error (self->ssl,
                                 result);

      if (ssl_error == SSL_ERROR_WANT_READ)
        return TRUE;

      if (ssl_error == SSL_ERROR_WANT_WRITE)
        continue;

      goodix5135_tls_set_ssl_error (error,
                                    "Goodix5135 TLS handshake failed");
      return FALSE;
    }

  g_set_error_literal (error,
                       G_IO_ERROR,
                       G_IO_ERROR_FAILED,
                       "Goodix5135 TLS handshake iteration limit reached");

  return FALSE;
}

static gboolean
goodix5135_tls_read_application (Goodix5135Tls *self,
                                 GByteArray     *wire_output,
                                 GByteArray     *app_output,
                                 GError        **error)
{
  guint8 buffer[16384];
  guint  iteration;

  for (iteration = 0; iteration < 64; iteration++)
    {
      size_t count = 0;
      int    result;
      int    ssl_error;

      result = SSL_read_ex (self->ssl,
                            buffer,
                            sizeof (buffer),
                            &count);

      if (!goodix5135_tls_drain_wire (self,
                                      wire_output,
                                      error))
        return FALSE;

      if (result == 1)
        {
          if (count > 0 && app_output != NULL)
            g_byte_array_append (app_output,
                                 buffer,
                                 (guint) count);

          continue;
        }

      ssl_error = SSL_get_error (self->ssl,
                                 result);

      if (ssl_error == SSL_ERROR_WANT_READ)
        return TRUE;

      if (ssl_error == SSL_ERROR_WANT_WRITE)
        continue;

      if (ssl_error == SSL_ERROR_ZERO_RETURN)
        return TRUE;

      goodix5135_tls_set_ssl_error (error,
                                    "Goodix5135 TLS application read failed");
      return FALSE;
    }

  g_set_error_literal (error,
                       G_IO_ERROR,
                       G_IO_ERROR_FAILED,
                       "Goodix5135 TLS application iteration limit reached");

  return FALSE;
}

Goodix5135Tls *
goodix5135_tls_new (const guint8 *psk,
                    gsize         psk_len,
                    GError      **error)
{
  Goodix5135Tls *self;
  BIO           *rbio;
  BIO           *wbio;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (psk == NULL ||
      psk_len == 0 ||
      psk_len > GOODIX5135_TLS_MAX_PSK)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Invalid Goodix5135 TLS PSK");
      return NULL;
    }

  self = g_new0 (Goodix5135Tls, 1);

  memcpy (self->psk,
          psk,
          psk_len);

  self->psk_len = psk_len;

  self->ctx = SSL_CTX_new (TLS_server_method ());

  if (self->ctx == NULL)
    goto ssl_error;

  if (SSL_CTX_set_min_proto_version (self->ctx,
                                     TLS1_2_VERSION) != 1)
    goto ssl_error;

  if (SSL_CTX_set_max_proto_version (self->ctx,
                                     TLS1_2_VERSION) != 1)
    goto ssl_error;

  if (SSL_CTX_set_cipher_list (self->ctx,
                               GOODIX5135_TLS_CIPHER) != 1)
    goto ssl_error;

  SSL_CTX_set_psk_server_callback (self->ctx,
                                   goodix5135_tls_psk_server_cb);

  self->ssl = SSL_new (self->ctx);

  if (self->ssl == NULL)
    goto ssl_error;

  SSL_set_app_data (self->ssl,
                    self);

  rbio = BIO_new (BIO_s_mem ());
  wbio = BIO_new (BIO_s_mem ());

  if (rbio == NULL || wbio == NULL)
    {
      BIO_free (rbio);
      BIO_free (wbio);
      goto ssl_error;
    }

  BIO_set_mem_eof_return (rbio,
                          -1);

  BIO_set_mem_eof_return (wbio,
                          -1);

  SSL_set_bio (self->ssl,
               rbio,
               wbio);

  SSL_set_accept_state (self->ssl);

  return self;

ssl_error:
  goodix5135_tls_set_ssl_error (error,
                                "Failed to initialize Goodix5135 TLS engine");

  goodix5135_tls_free (self);

  return NULL;
}

void
goodix5135_tls_free (Goodix5135Tls *self)
{
  if (self == NULL)
    return;

  if (self->ssl != NULL)
    SSL_free (self->ssl);

  if (self->ctx != NULL)
    SSL_CTX_free (self->ctx);

  OPENSSL_cleanse (self->psk,
                   sizeof (self->psk));

  g_free (self);
}

gboolean
goodix5135_tls_feed (Goodix5135Tls *self,
                     const guint8  *input,
                     gsize          input_len,
                     GByteArray    *wire_output,
                     GByteArray    *app_output,
                     GError       **error)
{
  BIO *rbio;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (input_len > 0 && input == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "TLS input pointer is NULL");
      return FALSE;
    }

  rbio = SSL_get_rbio (self->ssl);

  if (rbio == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_FAILED,
                           "TLS read BIO is unavailable");
      return FALSE;
    }

  if (input_len > 0)
    {
      gsize offset = 0;

      while (offset < input_len)
        {
          int count;

          count = BIO_write (rbio,
                             input + offset,
                             (int) MIN (input_len - offset,
                                        (gsize) G_MAXINT));

          if (count <= 0)
            {
              if (BIO_should_retry (rbio))
                continue;

              g_set_error_literal (error,
                                   G_IO_ERROR,
                                   G_IO_ERROR_FAILED,
                                   "Failed to feed Goodix5135 TLS read BIO");
              return FALSE;
            }

          offset += (gsize) count;
        }
    }

  if (!SSL_is_init_finished (self->ssl))
    {
      if (!goodix5135_tls_drive_handshake (self,
                                           wire_output,
                                           error))
        return FALSE;
    }

  if (SSL_is_init_finished (self->ssl))
    {
      if (!goodix5135_tls_read_application (self,
                                            wire_output,
                                            app_output,
                                            error))
        return FALSE;
    }

  return goodix5135_tls_drain_wire (self,
                                    wire_output,
                                    error);
}

gboolean
goodix5135_tls_is_established (Goodix5135Tls *self)
{
  g_return_val_if_fail (self != NULL, FALSE);

  return SSL_is_init_finished (self->ssl);
}
