// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "configured_grant_scheduler_impl.h"

#include <optional>

using namespace ocudu;

/// Converts a CG periodicity enum value to a number of slots.
/// Returns std::nullopt for sub-slot periodicities (sym2, sym7, sym6), which are not supported.
static std::optional<unsigned> cg_periodicity_to_slots(cg_configuration::periodicity_t period)
{
  switch (period) {
    // Sub-slot periodicities — not supported.
    case cg_configuration::periodicity_t::sym2:
    case cg_configuration::periodicity_t::sym7:
    case cg_configuration::periodicity_t::sym6:
      return std::nullopt;
    // Normal CP (14 symbols/slot).
    case cg_configuration::periodicity_t::sym1x14:
    case cg_configuration::periodicity_t::sym1x12:
      return 1U;
    case cg_configuration::periodicity_t::sym2x14:
    case cg_configuration::periodicity_t::sym2x12:
      return 2U;
    case cg_configuration::periodicity_t::sym4x14:
    case cg_configuration::periodicity_t::sym4x12:
      return 4U;
    case cg_configuration::periodicity_t::sym5x14:
    case cg_configuration::periodicity_t::sym5x12:
      return 5U;
    case cg_configuration::periodicity_t::sym8x14:
    case cg_configuration::periodicity_t::sym8x12:
      return 8U;
    case cg_configuration::periodicity_t::sym10x14:
    case cg_configuration::periodicity_t::sym10x12:
      return 10U;
    case cg_configuration::periodicity_t::sym16x14:
    case cg_configuration::periodicity_t::sym16x12:
      return 16U;
    case cg_configuration::periodicity_t::sym20x14:
    case cg_configuration::periodicity_t::sym20x12:
      return 20U;
    case cg_configuration::periodicity_t::sym32x14:
    case cg_configuration::periodicity_t::sym32x12:
      return 32U;
    case cg_configuration::periodicity_t::sym40x14:
    case cg_configuration::periodicity_t::sym40x12:
      return 40U;
    case cg_configuration::periodicity_t::sym64x14:
    case cg_configuration::periodicity_t::sym64x12:
      return 64U;
    case cg_configuration::periodicity_t::sym80x14:
    case cg_configuration::periodicity_t::sym80x12:
      return 80U;
    case cg_configuration::periodicity_t::sym128x14:
    case cg_configuration::periodicity_t::sym128x12:
      return 128U;
    case cg_configuration::periodicity_t::sym160x14:
    case cg_configuration::periodicity_t::sym160x12:
      return 160U;
    case cg_configuration::periodicity_t::sym256x14:
    case cg_configuration::periodicity_t::sym256x12:
      return 256U;
    case cg_configuration::periodicity_t::sym320x14:
    case cg_configuration::periodicity_t::sym320x12:
      return 320U;
    case cg_configuration::periodicity_t::sym512x14:
    case cg_configuration::periodicity_t::sym512x12:
      return 512U;
    case cg_configuration::periodicity_t::sym640x14:
    case cg_configuration::periodicity_t::sym640x12:
      return 640U;
    case cg_configuration::periodicity_t::sym1024x14:
      return 1024U;
    case cg_configuration::periodicity_t::sym1280x14:
    case cg_configuration::periodicity_t::sym1280x12:
      return 1280U;
    case cg_configuration::periodicity_t::sym2560x14:
    case cg_configuration::periodicity_t::sym2560x12:
      return 2560U;
    case cg_configuration::periodicity_t::sym5120x14:
      return 5120U;
    default:
      return std::nullopt;
  }
}

configured_grant_scheduler_impl::configured_grant_scheduler_impl(const cell_configuration& cell_cfg_,
                                                                 ue_repository&            ues_) :
  cell_cfg(cell_cfg_), ues(ues_), logger(ocudulog::fetch_basic_logger("SCHED"))
{
  periodic_pusch_slot_wheel.resize(max_cg_slot_periodicity);
}

const ue_cell_configuration* configured_grant_scheduler_impl::get_ue_cfg(rnti_t rnti) const
{
  auto* u = ues.find_by_rnti(rnti);
  if (u != nullptr) {
    const auto* ue_cc = u->find_cell(cell_cfg.cell_index);
    if (ue_cc != nullptr) {
      return &ue_cc->cfg();
    }
  }
  return nullptr;
}

void configured_grant_scheduler_impl::add_ue(const ue_cell_configuration& ue_cfg)
{
  add_ue_to_wheel(ue_cfg);
}

void configured_grant_scheduler_impl::add_ue_to_wheel(const ue_cell_configuration& ue_cfg)
{
  if (ue_cfg.init_bwp().ul.ded() == nullptr or not ue_cfg.init_bwp().ul.ded()->cg_cfg.has_value()) {
    return;
  }
  const cg_configuration& cg_cfg = ue_cfg.init_bwp().ul.ded()->cg_cfg.value();

  // Only handle Type 1 CG (RRC-configured grant; Type 2 requires DCI activation).
  if (not cg_cfg.rrc_configured_ul_grant_cfg.has_value()) {
    return;
  }
  const auto& ul_grant = cg_cfg.rrc_configured_ul_grant_cfg.value();

  const auto period_slots = cg_periodicity_to_slots(cg_cfg.periodicity);
  if (not period_slots.has_value()) {
    logger.warning("rnti={}: Configured grant with sub-slot periodicity is not supported", ue_cfg.crnti);
    return;
  }

  // Fill the slot wheel at every slot where a CG PUSCH opportunity occurs.
  for (unsigned wheel_offset = ul_grant.time_domain_offset; wheel_offset < max_cg_slot_periodicity;
       wheel_offset += period_slots.value()) {
    auto& slot_wheel = periodic_pusch_slot_wheel[wheel_offset];

    if (std::find(slot_wheel.begin(), slot_wheel.end(), ue_cfg.crnti) != slot_wheel.end()) {
      logger.error("rnti={}: CG grant already present in slot wheel at offset={}", ue_cfg.crnti, wheel_offset);
      continue;
    }
    slot_wheel.push_back(ue_cfg.crnti);
  }
}

void configured_grant_scheduler_impl::rem_ue(const ue_cell_configuration& ue_cfg)
{
  if (ue_cfg.init_bwp().ul.ded() == nullptr or not ue_cfg.init_bwp().ul.ded()->cg_cfg.has_value()) {
    return;
  }
  const cg_configuration& cg_cfg = ue_cfg.init_bwp().ul.ded()->cg_cfg.value();

  if (not cg_cfg.rrc_configured_ul_grant_cfg.has_value()) {
    return;
  }
  const auto& ul_grant = cg_cfg.rrc_configured_ul_grant_cfg.value();

  const auto period_slots = cg_periodicity_to_slots(cg_cfg.periodicity);
  if (not period_slots.has_value()) {
    return;
  }

  for (unsigned wheel_offset = ul_grant.time_domain_offset; wheel_offset < max_cg_slot_periodicity;
       wheel_offset += period_slots.value()) {
    auto& slot_wheel = periodic_pusch_slot_wheel[wheel_offset];

    auto* it = std::find(slot_wheel.begin(), slot_wheel.end(), ue_cfg.crnti);
    if (it == slot_wheel.end()) {
      logger.error("rnti={}: CG grant not found in slot wheel at offset={} during UE removal", ue_cfg.crnti, wheel_offset);
      continue;
    }

    // Swap with last element and pop for O(1) removal.
    if (it != slot_wheel.end() - 1) {
      std::swap(*it, *(slot_wheel.end() - 1));
    }
    slot_wheel.pop_back();
  }
}

void configured_grant_scheduler_impl::reconf_ue(const ue_cell_configuration& new_ue_cfg,
                                                const ue_cell_configuration& old_ue_cfg)
{
  const auto* new_ul_ded = new_ue_cfg.init_bwp().ul.ded();
  const auto* old_ul_ded = old_ue_cfg.init_bwp().ul.ded();

  if (new_ul_ded != nullptr and old_ul_ded != nullptr and new_ul_ded->cg_cfg.has_value() and
      old_ul_ded->cg_cfg.has_value() and new_ul_ded->cg_cfg.value() == old_ul_ded->cg_cfg.value()) {
    // CG configuration unchanged — nothing to do.
    return;
  }

  rem_ue(old_ue_cfg);
  add_ue_to_wheel(new_ue_cfg);
}
