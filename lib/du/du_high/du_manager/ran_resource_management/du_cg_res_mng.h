// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "du_cg_resource_manager.h"
#include "ocudu/du/du_cell_config.h"

namespace ocudu {
namespace odu {

struct cell_group_config;

/// \brief Configured Grant resource manager for the single-UE case.
///
/// This class implements the CG resource allocation assuming a single UE: no inter-UE collision avoidance is
/// performed when assigning CG parameters.
class du_cg_policy_single_ue : public du_cg_resource_manager
{
public:
  explicit du_cg_policy_single_ue(span<const du_cell_config> cell_cfg_list_);

  bool alloc_resources(cell_group_config& cell_grp_cfg) override;

  void dealloc_resources(cell_group_config& cell_grp_cfg) override;

private:
  span<const du_cell_config> cell_cfg_list;
};

} // namespace odu
} // namespace ocudu
