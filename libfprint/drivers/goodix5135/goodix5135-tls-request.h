#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define GOODIX5135_TLS_REQUEST_COMMAND          0xd0U
#define GOODIX5135_TLS_REQUEST_LOGICAL_LENGTH   10U
#define GOODIX5135_TLS_REQUEST_USB_LENGTH       64U

typedef enum
{
  GOODIX5135_TLS_REQUEST_TRANSACTION_IDLE = 0,
  GOODIX5135_TLS_REQUEST_TRANSACTION_WAIT_OUT,
  GOODIX5135_TLS_REQUEST_TRANSACTION_WAIT_ACK,
  GOODIX5135_TLS_REQUEST_TRANSACTION_DONE,
  GOODIX5135_TLS_REQUEST_TRANSACTION_FAILED,
} Goodix5135TlsRequestTransactionState;

typedef struct
{
  Goodix5135TlsRequestTransactionState state;
} Goodix5135TlsRequestTransaction;

gboolean
goodix5135_tls_request_build (guint8 *packet,
                              gsize   packet_size,
                              gsize  *logical_length);

gboolean
goodix5135_tls_request_parse_ack (const guint8 *data,
                                  gsize         data_length);

void
goodix5135_tls_request_transaction_init (
  Goodix5135TlsRequestTransaction *transaction);

gboolean
goodix5135_tls_request_transaction_begin (
  Goodix5135TlsRequestTransaction *transaction,
  guint8                          *packet,
  gsize                            packet_size,
  gsize                           *logical_length);

gboolean
goodix5135_tls_request_transaction_out_complete (
  Goodix5135TlsRequestTransaction *transaction,
  gboolean                         transport_can_advance);

gboolean
goodix5135_tls_request_transaction_ack_complete (
  Goodix5135TlsRequestTransaction *transaction,
  gboolean                         transport_can_advance,
  const guint8                    *data,
  gsize                            data_length);

G_END_DECLS
