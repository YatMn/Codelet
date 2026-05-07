#pragma once

#include <stddef.h>
#include <stdint.h>

#include "codelet_config.h"
#include "codelet_snapshot.h"

enum class TransportStatus : uint8_t {
  Ok,
  NetworkError,
};

struct TransportResult {
  TransportStatus status;
  int httpStatus;
  const char *message;
};

class SnapshotTransport {
 public:
  virtual ~SnapshotTransport() = default;
  virtual TransportResult get(const char *url, const char *token, char *buffer, size_t bufferSize) = 0;
};

enum class ClientStatus : uint8_t {
  Ok,
  AgentOffline,
  AuthFailed,
  InvalidData,
};

struct ClientResult {
  ClientStatus status;
  const char *message;
};

class CodeletClient {
 public:
  CodeletClient(SnapshotTransport *transport, const char *baseUrl, const char *apiToken);
  ClientResult fetchSnapshot(CodeletSnapshot &snapshot);

 private:
  SnapshotTransport *transport_;
  const char *baseUrl_;
  const char *apiToken_;
};
