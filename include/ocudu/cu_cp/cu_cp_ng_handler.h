// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/cu_cp/cu_cp_types.h"
#include "ocudu/ngap/ngap.h"
#include "ocudu/ran/plmn_identity.h"
#include <memory>

namespace ocudu {
namespace ocucp {

class ngap_rx_message_notifier;

/// \brief Handler of the NG interface of the CU-CP.
///
/// This interface is used to forward NGAP messages or TNL connection updates to the CU-CP.
class cu_cp_ng_handler
{
public:
  virtual ~cu_cp_ng_handler() = default;

  /// \brief Get the NG message handler interface.
  /// \param[in] plmn The PLMN of the NGAP.
  /// \return A pointer to the NG message handler interface if it was found, nullptr otherwise.
  virtual ngap_message_handler* get_ngap_message_handler(const plmn_identity& plmn) = 0;

  /// \brief Get the state of the AMF connections.
  /// \return True if all AMFs are connected, false otherwise.
  virtual bool amfs_are_connected() = 0;

  /// \brief Handle a newly established TNL association to a specific AMF.
  ///
  /// Invoked by the N2 gateway once an SCTP association is up. Tx/Rx notifiers are exchanged in a single call.
  ///
  /// \param amf_index The AMF this gateway serves.
  /// \param n2_tx_pdu_notifier Notifier the CU-CP will use to push NGAP Tx PDUs into the gateway.
  /// \return Notifier the gateway will use to forward NGAP PDUs from the AMF to the CU-CP.
  virtual std::unique_ptr<ngap_rx_message_notifier>
  handle_new_amf_connection(amf_index_t amf_index, std::unique_ptr<ngap_message_notifier> n2_tx_pdu_notifier) = 0;
};

} // namespace ocucp
} // namespace ocudu
