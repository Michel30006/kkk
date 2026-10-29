#include "tds/job.hpp"

#include <array>
#include <map>

namespace tds {
namespace {

constexpr std::array<std::pair<JobStatus, std::string_view>, 15> kStatuses{{
    {JobStatus::kCreated, "created"},
    {JobStatus::kQueued, "queued"},
    {JobStatus::kDownloading, "downloading"},
    {JobStatus::kEncoding, "encoding"},
    {JobStatus::kReadyForDelivery, "ready_for_delivery"},
    {JobStatus::kSending, "sending"},
    {JobStatus::kAwaitingClientConfirmation, "awaiting_client_confirmation"},
    {JobStatus::kCompleted, "completed"},
    {JobStatus::kCleaned, "cleaned"},
    {JobStatus::kFailedDownload, "failed_download"},
    {JobStatus::kFailedEncoding, "failed_encoding"},
    {JobStatus::kFailedDelivery, "failed_delivery"},
    {JobStatus::kFailedReconstruction, "failed_reconstruction"},
    {JobStatus::kCancelled, "cancelled"},
    {JobStatus::kExpired, "expired"},
}};

}  // namespace

std::string_view to_string(JobStatus status) {
  for (const auto& [value, text] : kStatuses)
    if (value == status) return text;
  return "unknown";
}

bool parse_status(std::string_view text, JobStatus& out) {
  for (const auto& [value, name] : kStatuses) {
    if (name == text) {
      out = value;
      return true;
    }
  }
  return false;
}

bool is_terminal(JobStatus status) {
  switch (status) {
    case JobStatus::kCleaned:
    case JobStatus::kCancelled:
    case JobStatus::kExpired:
    case JobStatus::kFailedDownload:
    case JobStatus::kFailedEncoding:
    case JobStatus::kFailedDelivery:
    case JobStatus::kFailedReconstruction:
      return true;
    default:
      return false;
  }
}

bool is_failure(JobStatus status) {
  return status == JobStatus::kFailedDownload ||
         status == JobStatus::kFailedEncoding ||
         status == JobStatus::kFailedDelivery ||
         status == JobStatus::kFailedReconstruction ||
         status == JobStatus::kCancelled || status == JobStatus::kExpired;
}

bool can_transition(JobStatus from, JobStatus to) {
  if (from == to) return true;
  if (is_terminal(from)) return false;
  switch (from) {
    case JobStatus::kCreated:
      return to == JobStatus::kQueued || is_failure(to);
    case JobStatus::kQueued:
      return to == JobStatus::kDownloading || is_failure(to);
    case JobStatus::kDownloading:
      return to == JobStatus::kEncoding || to == JobStatus::kQueued ||
             is_failure(to);
    case JobStatus::kEncoding:
      return to == JobStatus::kReadyForDelivery || to == JobStatus::kQueued ||
             is_failure(to);
    case JobStatus::kReadyForDelivery:
      return to == JobStatus::kSending || is_failure(to);
    case JobStatus::kSending:
      return to == JobStatus::kAwaitingClientConfirmation ||
             to == JobStatus::kReadyForDelivery || is_failure(to);
    case JobStatus::kAwaitingClientConfirmation:
      return to == JobStatus::kCompleted || to == JobStatus::kSending ||
             is_failure(to);
    case JobStatus::kCompleted:
      return to == JobStatus::kCleaned;
    default:
      return false;
  }
}

}  // namespace tds
