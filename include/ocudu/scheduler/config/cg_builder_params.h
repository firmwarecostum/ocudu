// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/adt/bounded_integer.h"
#include "ocudu/ran/sps/cg_configuration.h"
#include <optional>

namespace ocudu {

struct cg_builder_params {
  /// Normal CP (14 symbols/slot).
  /// For 14 symbols slots, values={1, 2, 4, 5, 8, 10, 16, 20, 32, 40, 64, 80, 128, 160, 256, 320, 512, 640, 1024, 1280,
  /// 2560, 5120}.
  /// For 23 symbols slots, values={1, 2, 4, 5, 8, 10, 16, 20, 32, 40, 64, 80, 128, 160, 256, 320, 512, 640, 1024, 1280,
  /// 2560, 5120}.
  cg_configuration::periodicity_t  periodicity = cg_configuration::periodicity_t::sym40x14;
  unsigned slot_offset        = 0;
  unsigned nof_rbs            = 10;
  unsigned mcs                = 5;
  unsigned nof_harq_processes = 4;
};

} // namespace ocudu
