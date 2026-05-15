// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "ocudu/ngap/gateways/n2_connection_client_factory.h"
#include "ocudu/asn1/ngap/common.h"
#include "ocudu/asn1/ngap/ngap_pdu_contents.h"
#include "ocudu/cu_cp/cu_cp_ng_handler.h"
#include "ocudu/gateways/sctp_network_server_factory.h"
#include "ocudu/ngap/ngap_message.h"
#include "ocudu/pcap/dlt_pcap.h"
#include "ocudu/support/async/async_task.h"
#include "ocudu/support/error_handling.h"
#include "ocudu/support/io/transport_layer_address.h"

using namespace ocudu;
using namespace ocucp;

namespace {

/// Notifier for converting packed NGAP PDUs coming from the N2 GW into unpacked NGAP PDUs and forward them to the
/// CU-CP.
class sctp_to_n2_pdu_notifier final : public sctp_association_sdu_notifier
{
public:
  sctp_to_n2_pdu_notifier(std::unique_ptr<ngap_rx_message_notifier> cu_cp_rx_pdu_notifier_,
                          dlt_pcap&                                 pcap_writer_,
                          ocudulog::basic_logger&                   logger_) :
    cu_cp_rx_pdu_notifier(std::move(cu_cp_rx_pdu_notifier_)), pcap_writer(pcap_writer_), logger(logger_)
  {
  }

  bool on_new_sdu(byte_buffer sdu) override
  {
    // Unpack NGAP PDU.
    asn1::cbit_ref bref(sdu);
    ngap_message   msg;
    if (msg.pdu.unpack(bref) != asn1::OCUDUASN_SUCCESS) {
      logger.error("Couldn't unpack NGAP PDU");
      return false;
    }

    // Forward Rx PDU to pcap, if enabled.
    if (pcap_writer.is_write_enabled()) {
      pcap_writer.push_pdu(sdu.copy());
    }

    // Forward unpacked Rx PDU to the CU-CP.
    cu_cp_rx_pdu_notifier->on_new_message(msg);
    return true;
  }

private:
  std::unique_ptr<ngap_rx_message_notifier> cu_cp_rx_pdu_notifier;
  dlt_pcap&                                 pcap_writer;
  ocudulog::basic_logger&                   logger;
};

/// \brief Notifier for converting unpacked NGAP PDUs coming from the CU-CP into packed NGAP PDUs and forward them to
/// the N2 GW.
class n2_to_sctp_pdu_notifier final : public ngap_message_notifier
{
public:
  n2_to_sctp_pdu_notifier(std::unique_ptr<sctp_association_sdu_notifier> sctp_rx_pdu_notifier_,
                          dlt_pcap&                                      pcap_writer_,
                          ocudulog::basic_logger&                        logger_) :
    sctp_rx_pdu_notifier(std::move(sctp_rx_pdu_notifier_)), pcap_writer(pcap_writer_), logger(logger_)
  {
  }

  [[nodiscard]] bool on_new_message(const ngap_message& msg) override
  {
    // pack NGAP PDU into SCTP SDU.
    byte_buffer   tx_sdu{byte_buffer::fallback_allocation_tag{}};
    asn1::bit_ref bref(tx_sdu);
    if (msg.pdu.pack(bref) != asn1::OCUDUASN_SUCCESS) {
      logger.error("Failed to pack NGAP PDU");
      return false;
    }

    // Push Tx PDU to pcap.
    if (pcap_writer.is_write_enabled()) {
      pcap_writer.push_pdu(tx_sdu.copy());
    }

    // Forward packed Tx PDU to SCTP gateway.
    sctp_rx_pdu_notifier->on_new_sdu(std::move(tx_sdu));

    return true;
  }

private:
  std::unique_ptr<sctp_association_sdu_notifier> sctp_rx_pdu_notifier;
  dlt_pcap&                                      pcap_writer;
  ocudulog::basic_logger&                        logger;
};

/// Stub for the operation of the CU-CP without a core.
class ngap_gateway_local_stub final : public n2_connection_client
{
public:
  ngap_gateway_local_stub(amf_index_t amf_index_, dlt_pcap& pcap_) : amf_index(amf_index_), pcap_writer(pcap_) {}

  void attach_cu_cp(cu_cp_ng_handler& handler) override { cu_cp = &handler; }

  async_task<bool> connect_to_amf() override
  {
    return launch_async([this](coro_context<async_task<bool>>& ctx) {
      CORO_BEGIN(ctx);

      class cu_cp_tx_pdu_notifier final : public ngap_message_notifier
      {
      public:
        cu_cp_tx_pdu_notifier(ngap_gateway_local_stub& parent_) : parent(parent_) {}
        ~cu_cp_tx_pdu_notifier() override { parent.disconnect(); }

        [[nodiscard]] bool on_new_message(const ngap_message& msg) override
        {
          parent.handle_tx_message(msg);
          return true;
        }

      private:
        ngap_gateway_local_stub& parent;
      };

      ocudu_assert(cu_cp != nullptr, "attach_cu_cp must be called before connect_to_amf");

      // Local stub completes synchronously: deliver Tx to the CU-CP and store the Rx notifier we get back.
      cu_cp_rx_notifier = cu_cp->handle_new_amf_connection(amf_index, std::make_unique<cu_cp_tx_pdu_notifier>(*this));

      CORO_RETURN(cu_cp_rx_notifier != nullptr);
    });
  }

private:
  void disconnect() { cu_cp_rx_notifier.reset(); }

  // Handle message sent by CU-CP.
  void handle_tx_message(const ngap_message& msg)
  {
    using namespace asn1::ngap;

    // Save message to pcap.
    if (pcap_writer.is_write_enabled()) {
      byte_buffer   packed_pdu;
      asn1::bit_ref bref{packed_pdu};
      if (msg.pdu.pack(bref) == asn1::OCUDUASN_SUCCESS) {
        pcap_writer.push_pdu(std::move(packed_pdu));
      } else {
        logger.warning("Failed to encode NGAP Tx PDU.");
      }
    }

    if (msg.pdu.type().value == ngap_pdu_c::types_opts::init_msg and
        msg.pdu.init_msg().value.type().value == ngap_elem_procs_o::init_msg_c::types_opts::ng_setup_request) {
      // CU-CP is requesting an NG Setup. Automatically reply with NG Setup Response.

      const auto& req = msg.pdu.init_msg().value.ng_setup_request();
      ocudu_assert(req->supported_ta_list.size() > 0, "NG Setup Request has no supported TA list");
      const auto& broadcast_plmns = req->supported_ta_list[0].broadcast_plmn_list;

      // Generate fake NG Setup Response.
      ngap_message resp;
      resp.pdu.set_successful_outcome().load_info_obj(ASN1_NGAP_ID_NG_SETUP);
      auto& ng_resp = resp.pdu.successful_outcome().value.ng_setup_resp();
      ng_resp->amf_name.from_string("localamf");
      ng_resp->served_guami_list.resize(broadcast_plmns.size());
      for (unsigned i = 0; i != ng_resp->served_guami_list.size(); ++i) {
        ng_resp->served_guami_list[i].guami.plmn_id = req->supported_ta_list[0].broadcast_plmn_list[i].plmn_id;
        ng_resp->served_guami_list[i].guami.amf_region_id.from_number(0x2);
        ng_resp->served_guami_list[i].guami.amf_set_id.from_number(0x40);
        ng_resp->served_guami_list[i].guami.amf_pointer.from_number(0x0);
      }
      ng_resp->relative_amf_capacity = 255;

      // Support for the same PLMNs and Slices as in NG Setup request.
      ng_resp->plmn_support_list.resize(broadcast_plmns.size());
      for (unsigned i = 0; i != broadcast_plmns.size(); ++i) {
        auto& out_plmn              = ng_resp->plmn_support_list[i];
        out_plmn.plmn_id            = broadcast_plmns[i].plmn_id;
        out_plmn.slice_support_list = broadcast_plmns[i].tai_slice_support_list;
      }

      // Send NG Setup Response back to CU-CP.
      send_rx_pdu_to_cu_cp(resp);
    }
  }

  // Forward NGAP message to CU-CP.
  void send_rx_pdu_to_cu_cp(const ngap_message& msg)
  {
    ocudu_assert(cu_cp_rx_notifier != nullptr, "Adapter is disconnected");

    if (pcap_writer.is_write_enabled()) {
      // PCAP writer is enabled. Encode ASN.1 message and send to PCAP.
      byte_buffer         bytes;
      asn1::bit_ref       bref{bytes};
      asn1::OCUDUASN_CODE code = msg.pdu.pack(bref);
      if (code != asn1::OCUDUASN_SUCCESS) {
        logger.warning("Failed to encode NGAP Rx PDU. NGAP PCAP will miss some messages.");
      } else {
        pcap_writer.push_pdu(std::move(bytes));
      }
    }

    // Push message to CU-CP.
    cu_cp_rx_notifier->on_new_message(msg);
  }

  const amf_index_t       amf_index;
  dlt_pcap&               pcap_writer;
  ocudulog::basic_logger& logger = ocudulog::fetch_basic_logger("CU-CP");

  cu_cp_ng_handler*                         cu_cp = nullptr;
  std::unique_ptr<ngap_rx_message_notifier> cu_cp_rx_notifier;
};

/// \brief NGAP gateway over SCTP. The gNB acts as a client to the AMF: it only initiates outgoing TNL associations,
/// it never accepts inbound ones. \ref sctp_network_server is used internally because it is the class that today
/// exposes the non-blocking, multihomed \c sctp_connectx() driven by SCTP_COMM_UP notifications; it is expected to be
/// renamed to a plain \c sctp_network_gateway once the legacy \c sctp_network_client is deprecated.
class n2_sctp_gateway_client final : public n2_connection_client, public sctp_network_association_factory
{
public:
  n2_sctp_gateway_client(amf_index_t                          amf_index_,
                         io_broker&                           broker,
                         task_executor&                       io_rx_executor,
                         task_executor&                       ctrl_exec_,
                         const sctp_network_connector_config& sctp_,
                         dlt_pcap&                            pcap_) :
    amf_index(amf_index_), sctp_cfg(sctp_), pcap_writer(pcap_)
  {
    // SCTP server socket so that sctp_connectx() is non-blocking and the COMM_UP/CANT_STR_ASSOC notifications drive the
    // async connect task.
    sctp_server =
        create_sctp_network_server(sctp_network_server_config{sctp_cfg, broker, io_rx_executor, ctrl_exec_, *this});
    report_error_if_not(sctp_server != nullptr, "Failed to create N2 SCTP gateway");
  }

  ~n2_sctp_gateway_client() override
  {
    if (sctp_server) {
      sctp_server->stop();
    }
  }

  void attach_cu_cp(cu_cp_ng_handler& handler) override
  {
    cu_cp = &handler;
    // TODO: the gNB is purely a client at N2 and never accepts inbound associations from the AMF, but `listen()` is
    // currently the only public entry point that subscribes the socket to the io_broker. Replace with a connect-only
    // subscription path when the SCTP gateway is refactored.
    bool result = sctp_server->listen();
    report_error_if_not(result, "Failed to start N2 SCTP gateway");
  }

  async_task<bool> connect_to_amf() override
  {
    return launch_async([this, result = false](coro_context<async_task<bool>>& ctx) mutable {
      CORO_BEGIN(ctx);
      ocudu_assert(cu_cp != nullptr, "attach_cu_cp must be called before connect_to_amf");

      logger.debug("Establishing TNL connection to {} ({}:{})...",
                   sctp_cfg.dest_name,
                   sctp_cfg.connect_addresses[0],
                   sctp_cfg.connect_port);

      CORO_AWAIT_VALUE(result, sctp_server->connect(resolve_dest_addrs()));

      if (not result) {
        logger.error("Failed to establish N2 TNL connection to AMF on {}:{}.",
                     sctp_cfg.connect_addresses[0],
                     sctp_cfg.connect_port);
        CORO_EARLY_RETURN(false);
      }

      logger.info("{}: Connection to {} on {}:{} was established",
                  sctp_cfg.if_name,
                  sctp_cfg.dest_name,
                  sctp_cfg.connect_addresses[0],
                  sctp_cfg.connect_port);
      fmt::print("{}: Connection to {} on {}:{} completed\n",
                 sctp_cfg.if_name,
                 sctp_cfg.dest_name,
                 sctp_cfg.connect_addresses[0],
                 sctp_cfg.connect_port);

      CORO_RETURN(true);
    });
  }

  // sctp_network_association_factory: called on SCTP_COMM_UP for both outgoing and (theoretical) incoming associations.
  std::unique_ptr<sctp_association_sdu_notifier>
  create(std::unique_ptr<sctp_association_sdu_notifier> sctp_send_notifier,
         sctp_association_info /*assoc_info*/) override
  {
    ocudu_assert(cu_cp != nullptr, "attach_cu_cp must be called before SCTP associations come up");

    // Wrap the SCTP sender into an N2 Tx notifier and deliver it to the CU-CP in exchange for the Rx notifier.
    auto n2_sender = std::make_unique<n2_to_sctp_pdu_notifier>(std::move(sctp_send_notifier), pcap_writer, logger);

    std::unique_ptr<ngap_rx_message_notifier> rx_notifier =
        cu_cp->handle_new_amf_connection(amf_index, std::move(n2_sender));
    if (rx_notifier == nullptr) {
      return nullptr;
    }

    return std::make_unique<sctp_to_n2_pdu_notifier>(std::move(rx_notifier), pcap_writer, logger);
  }

private:
  std::vector<transport_layer_address> resolve_dest_addrs() const
  {
    std::vector<transport_layer_address> dest_addrs;
    dest_addrs.reserve(sctp_cfg.connect_addresses.size());
    for (const auto& addr_str : sctp_cfg.connect_addresses) {
      transport_layer_address addr = transport_layer_address::create_from_string(addr_str);
      addr.set_port(static_cast<uint16_t>(sctp_cfg.connect_port));
      dest_addrs.push_back(addr);
    }
    return dest_addrs;
  }

  const amf_index_t                   amf_index;
  const sctp_network_connector_config sctp_cfg;
  dlt_pcap&                           pcap_writer;
  ocudulog::basic_logger&             logger = ocudulog::fetch_basic_logger("CU-CP");

  cu_cp_ng_handler*                    cu_cp = nullptr;
  std::unique_ptr<sctp_network_server> sctp_server;
};

} // namespace

std::unique_ptr<n2_connection_client>
ocudu::ocucp::create_n2_connection_client(const n2_connection_client_config& params)
{
  if (std::holds_alternative<n2_connection_client_config::no_core>(params.mode)) {
    // Connection to local AMF stub.
    return std::make_unique<ngap_gateway_local_stub>(params.amf_index, params.pcap);
  }

  // Connection to AMF through SCTP.
  const auto& nw_mode = std::get<n2_connection_client_config::network>(params.mode);
  return std::make_unique<n2_sctp_gateway_client>(
      params.amf_index, nw_mode.broker, nw_mode.io_rx_executor, nw_mode.ctrl_exec, nw_mode.sctp, params.pcap);
}
