// Estados de trabajo y transiciones válidas. Ver especificación §6.
#pragma once

#include <string>
#include <string_view>

namespace tds {

enum class JobStatus {
  kCreated,
  kQueued,
  kDownloading,
  kEncoding,
  kReadyForDelivery,
  kSending,
  kAwaitingClientConfirmation,
  kCompleted,
  kCleaned,
  kFailedDownload,
  kFailedEncoding,
  kFailedDelivery,
  kFailedReconstruction,
  kCancelled,
  kExpired,
};

std::string_view to_string(JobStatus status);
bool parse_status(std::string_view text, JobStatus& out);
bool is_terminal(JobStatus status);
bool is_failure(JobStatus status);
bool can_transition(JobStatus from, JobStatus to);

}  // namespace tds
