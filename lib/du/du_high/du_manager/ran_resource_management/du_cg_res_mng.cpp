// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "du_cg_res_mng.h"
#include "du_ue_resource_config.h"
#include "ocudu/scheduler/config/serving_cell_config_factory.h"

using namespace ocudu;
using namespace odu;

du_cg_policy_single_ue::du_cg_policy_single_ue(span<const du_cell_config> cell_cfg_list_) :
  cell_cfg_list(cell_cfg_list_)
{
}

bool du_cg_policy_single_ue::alloc_resources(cell_group_config& cell_grp_cfg)
{
  for (auto cell_entry : cell_grp_cfg.cells) {
    auto&                 cell_cfg_ded = cell_entry.second;
    const du_cell_config& cell_cfg     = cell_cfg_list[cell_cfg_ded.serv_cell_cfg.cell_index];

    // Skip cells without CG configured.
    if (not cell_cfg.ran.init_bwp.cg_cfg.has_value()) {
      continue;
    }

    // Build the full CG configuration from the cell-level defaults. This populates all fields of cg_configuration,
    // including rrc_configured_ul_grant_cfg, from the cg_builder_params stored in the cell config.
    const cg_configuration default_cg_cfg =
        config_helpers::make_default_ue_cell_config(cell_cfg.ran, cell_cfg_ded.serv_cell_cfg.cell_index)
            .serv_cell_cfg.ul_config.value()
            .init_ul_bwp.cg_cfg.value();

    // > Common parameters: fill serving_cell_cfg with the full CG configuration.
    cell_cfg_ded.serv_cell_cfg.ul_config->init_ul_bwp.cg_cfg.emplace(default_cg_cfg);

    // > UE-specific parameters: fill the per-UE BWP config with the slot offset and frequency allocation.
    ocudu_assert(not cell_cfg_ded.bwps.empty(), "UE cell config must have at least one BWP configured");
    ocudu_assert(default_cg_cfg.rrc_configured_ul_grant_cfg.has_value(),
                 "rrc_configured_ul_grant must be set for a Type 1 CG");
    cell_cfg_ded.init_bwp().ul.cg.cg_offset = cell_cfg.ran.init_bwp.cg_cfg->slot_offset;
    cell_cfg_ded.init_bwp().ul.cg.vrbs      = default_cg_cfg.rrc_configured_ul_grant_cfg->vrbs;
    // NOTE: This is temporary code, a refactor is needed. CS-RNTI cannot be set here, but needs to be handled by an
    // RNTI manager.
    cell_cfg_ded.init_bwp().ul.cg.cs_rnti = to_rnti(0xe0ef);
    cell_grp_cfg.pcg_cfg.cs_rnti          = cell_cfg_ded.init_bwp().ul.cg.cs_rnti;
  }

  return true;
}

void du_cg_policy_single_ue::dealloc_resources(cell_group_config& cell_grp_cfg)
{
  for (auto cell_entry : cell_grp_cfg.cells) {
    auto& cell_cfg_ded = cell_entry.second;

    if (not cell_cfg_ded.serv_cell_cfg.ul_config.has_value()) {
      continue;
    }

    // Reset the CG configuration. This signals that the resource has been returned.
    cell_cfg_ded.serv_cell_cfg.ul_config->init_ul_bwp.cg_cfg.reset();
  }
}
