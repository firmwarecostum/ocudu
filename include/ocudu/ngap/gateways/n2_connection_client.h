// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/ngap/ngap.h"
#include "ocudu/support/async/async_task.h"

namespace ocudu {
namespace ocucp {

class cu_cp_ng_handler;

/// This interface notifies the receeption of new NGAP messages over the NGAP interface.
class ngap_rx_message_notifier
{
public:
  virtual ~ngap_rx_message_notifier() = default;

  /// \brief This callback is invoked on each received NGAP message.
  /// \param[in] msg The received NGAP message.
  virtual void on_new_message(const ngap_message& msg) = 0;
};

/// Gateway used by the CU-CP to talk to an AMF over N2. The gNB is always a client to the AMF: it initiates the TNL
/// association and never accepts inbound ones.
class n2_connection_client
{
public:
  virtual ~n2_connection_client() = default;

  /// \brief Attach the CU-CP NG handler. Must be called before \ref connect_to_amf.
  ///
  /// On a successful association the gateway calls \ref cu_cp_ng_handler::handle_new_amf_connection on the attached
  /// handler.
  virtual void attach_cu_cp(cu_cp_ng_handler& handler) = 0;

  /// \brief Initiate a new TNL association to the AMF.
  ///
  /// Returns true on a successful association, false otherwise. On success, the CU-CP handler has already been called
  /// with the Tx notifier before the task completes.
  virtual async_task<bool> connect_to_amf() = 0;
};

} // namespace ocucp
} // namespace ocudu
