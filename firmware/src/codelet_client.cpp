#include "codelet_client.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr const char *kSnapshotPath = "/api/v1/snapshot";
constexpr size_t kUrlBufferBytes = 256;

bool isSuccessStatus(int status) {
  return status >= 200 && status < 300;
}

}  // namespace

CodeletClient::CodeletClient(SnapshotTransport *transport, const char *baseUrl, const char *apiToken)
    : transport_(transport), baseUrl_(baseUrl), apiToken_(apiToken) {}

ClientResult CodeletClient::fetchSnapshot(CodeletSnapshot &snapshot) {
  if (transport_ == nullptr || baseUrl_ == nullptr || baseUrl_[0] == '\0') {
    return {ClientStatus::AgentOffline, "client not configured"};
  }

  char url[kUrlBufferBytes];
  int written = snprintf(url, sizeof(url), "%s%s", baseUrl_, kSnapshotPath);
  if (written < 0 || static_cast<size_t>(written) >= sizeof(url)) {
    return {ClientStatus::AgentOffline, "snapshot url too long"};
  }

  static char response[CODELET_HTTP_BUFFER_BYTES];
  response[0] = '\0';

  TransportResult transportResult = transport_->get(url, apiToken_, response, sizeof(response));
  response[sizeof(response) - 1] = '\0';

  if (transportResult.status == TransportStatus::NetworkError) {
    return {ClientStatus::AgentOffline, transportResult.message};
  }

  if (transportResult.httpStatus == 401 || transportResult.httpStatus == 403) {
    return {ClientStatus::AuthFailed, "auth failed"};
  }

  if (!isSuccessStatus(transportResult.httpStatus)) {
    return {ClientStatus::AgentOffline, "agent offline"};
  }

  ParseResult parseResult = parseSnapshotJson(response, snapshot);
  if (parseResult.status != ParseStatus::Ok) {
    return {ClientStatus::InvalidData, parseResult.message};
  }

  return {ClientStatus::Ok, "ok"};
}
