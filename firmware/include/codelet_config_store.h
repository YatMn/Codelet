#pragma once

#include "codelet_runtime_config.h"

class RuntimeConfigStore {
 public:
  virtual ~RuntimeConfigStore() = default;
  virtual bool load(RuntimeConfig &config) = 0;
  virtual bool save(const RuntimeConfig &config) = 0;
  virtual bool clear() = 0;
};

bool hasValidRuntimeConfig(RuntimeConfigStore &store);

#ifndef CODELET_NATIVE_TEST
class PreferencesRuntimeConfigStore : public RuntimeConfigStore {
 public:
  bool load(RuntimeConfig &config) override;
  bool save(const RuntimeConfig &config) override;
  bool clear() override;
};
#endif
