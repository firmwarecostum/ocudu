// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "mock_amf.h"
#include "tests/unittests/ngap/ngap_test_messages.h"
#include "ocudu/adt/mutexed_mpmc_queue.h"
#include "ocudu/asn1/ngap/common.h"
#include "ocudu/asn1/ngap/ngap_pdu_contents.h"
#include "ocudu/cu_cp/cu_cp_ng_handler.h"
#include "ocudu/cu_cp/cu_cp_types.h"
#include "ocudu/ngap/ngap_message.h"
#include "ocudu/ocudulog/ocudulog.h"
#include "ocudu/support/async/async_task.h"
#include <atomic>

using namespace ocudu;
using namespace ocucp;

/// \brief Mock class for the interface between CU-CP and AMF that accounts for the fact that the CU-CP may push PDUs
/// from different threads.
class synchronized_mock_amf : public mock_amf
{
public:
  explicit synchronized_mock_amf(amf_index_t amf_index_) : amf_index(amf_index_), rx_pdus(1024), pending_tx_pdus(16) {}

  void attach_cu_cp(cu_cp_ng_handler& handler) override { cu_cp = &handler; }

  async_task<bool> connect_to_amf() override
  {
    return launch_async([this](coro_context<async_task<bool>>& ctx) {
      CORO_BEGIN(ctx);

      class sync_mock_pdu_notifier : public ngap_message_notifier
      {
      public:
        sync_mock_pdu_notifier(synchronized_mock_amf& parent_) : parent(parent_) {}
        ~sync_mock_pdu_notifier() override { parent.rx_pdu_notifier.reset(); }

        [[nodiscard]] bool on_new_message(const ngap_message& msg) override
        {
          // If a NG Reset is sent, we inject a NG Reset Acknowledge message.
          if (msg.pdu.type().value == asn1::ngap::ngap_pdu_c::types_opts::init_msg &&
              msg.pdu.init_msg().proc_code == ASN1_NGAP_ID_NG_RESET) {
            auto& ng_reset = msg.pdu.init_msg().value.ng_reset();

            ngap_message ng_reset_ack = generate_ng_reset_ack(
                (ng_reset->reset_type.type() == asn1::ngap::reset_type_c::types_opts::options::part_of_ng_interface)
                    ? ng_reset->reset_type.part_of_ng_interface()
                    : asn1::ngap::ue_associated_lc_ng_conn_list_l{});
            parent.push_tx_pdu(ng_reset_ack);
          }

          // If a PDU response has been previously enqueued, we send it now.
          if (not parent.pending_tx_pdus.empty()) {
            ngap_message tx_pdu;
            bool         discard = parent.pending_tx_pdus.try_pop(tx_pdu);
            (void)discard;
            parent.push_tx_pdu(tx_pdu);
          }

          bool success = parent.rx_pdus.push_blocking(msg);
          report_error_if_not(success, "Queue is full");
          return true;
        }

      private:
        synchronized_mock_amf& parent;
      };

      if (connection_dropped) {
        ocudulog::fetch_basic_logger("TEST").warning("AMF connection already dropped");
        CORO_EARLY_RETURN(false);
      }

      // Ensure any existing notifier is properly reset before assigning a new one.
      if (rx_pdu_notifier) {
        rx_pdu_notifier.reset();
      }

      ocudu_assert(cu_cp != nullptr, "attach_cu_cp must be called before connect_to_amf");
      rx_pdu_notifier = cu_cp->handle_new_amf_connection(amf_index, std::make_unique<sync_mock_pdu_notifier>(*this));
      CORO_RETURN(rx_pdu_notifier != nullptr);
    });
  }

  bool try_pop_rx_pdu(ngap_message& pdu) override { return rx_pdus.try_pop(pdu); }

  void push_tx_pdu(const ngap_message& pdu) override { rx_pdu_notifier->on_new_message(pdu); }

  void enqueue_next_tx_pdu(const ngap_message& pdu) override { pending_tx_pdus.push_blocking(pdu); }

  void drop_connection() override
  {
    connection_dropped = true;
    if (rx_pdu_notifier) {
      rx_pdu_notifier.reset();
    }
  }

  void allow_reconnection() override { connection_dropped = false; }

private:
  using ngap_pdu_queue = concurrent_queue<ngap_message,
                                          concurrent_queue_policy::locking_mpmc,
                                          concurrent_queue_wait_policy::condition_variable>;

  const amf_index_t amf_index;
  ngap_pdu_queue    rx_pdus;

  std::atomic<bool> connection_dropped = false;
  cu_cp_ng_handler* cu_cp              = nullptr;

  std::unique_ptr<ngap_rx_message_notifier> rx_pdu_notifier;

  // Tx PDUs to send once the NG connection is set up.
  ngap_pdu_queue pending_tx_pdus;
};

std::unique_ptr<mock_amf> ocudu::ocucp::create_mock_amf(amf_index_t amf_index)
{
  return std::make_unique<synchronized_mock_amf>(amf_index);
}
