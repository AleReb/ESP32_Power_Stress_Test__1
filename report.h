#pragma once
#include <Preferences.h>

// One flash record at start and finish. Samples accumulate in RAM during a run.
enum Outcome : uint8_t { Empty, Measuring, Finished, Stopped, Interrupted };
struct LegacySummary {
  uint32_t version = 1;
  Outcome outcome = Empty;
  char mode[12] = {};
  uint32_t minutes = 0, period = 0;
  uint8_t peak = 0, cores = 0, wifi = 0, reset = 0;
  uint64_t seconds = 0, samples = 0, late = 0, steadySamples = 0;
  uint32_t worstLag = 0, minHeap = UINT32_MAX;
  double steadyMean = 0, steadyM2 = 0;
  float maxTemp = NAN;
};
struct SummaryV2 : LegacySummary {
  uint32_t runId = 0;
  uint64_t totalLag = 0, lag10 = 0, packetsOk = 0, packetsFailed = 0;
  uint32_t worstAt = 0;
};
struct RunSummary : SummaryV2 {
  bool serialLoad = false;
  uint64_t framesSent = 0, framesOk = 0, mismatches = 0, incomplete = 0, txErrors = 0;
  uint32_t maxEchoMs = 0;
};
RunSummary summary;
Preferences record;
bool storeReady = false;
bool saved = false;

void saveSummary() {
  saved = storeReady && record.putBytes("latest", &summary, sizeof(summary)) == sizeof(summary);
}

bool readSummary(const char *key, RunSummary &out) {
  const size_t size = record.getBytesLength(key);
  if (size == sizeof(LegacySummary)) {
    LegacySummary old;
    if (record.getBytes(key, &old, size) != size || old.version != 1) return false;
    static_cast<LegacySummary&>(out) = old;
  } else if (size == sizeof(SummaryV2)) {
    SummaryV2 old;
    if (record.getBytes(key, &old, size) != size || old.version > 2) return false;
    static_cast<SummaryV2&>(out) = old;
  } else if (size != sizeof(out) || record.getBytes(key, &out, size) != size ||
             out.version < 1 || out.version > 3) return false;
  return out.outcome <= Interrupted;
}

void restoreSummary() {
  storeReady = record.begin("stress-report", false);
  if (!storeReady || !readSummary("latest", summary)) {
    summary = RunSummary{};
    return;
  }
  saved = true;
  if (summary.outcome == Measuring) {
    summary.outcome = Interrupted;
    summary.reset = esp_reset_reason();
    saveSummary();
  }
}

void observeSample(uint32_t delayMs, uint32_t heap, float temp, bool steady, uint32_t rate) {
  ++summary.samples;
  summary.worstLag = max(summary.worstLag, delayMs);
  if (delayMs >= 100) ++summary.late;
  summary.minHeap = min(summary.minHeap, heap);
  if (isfinite(temp) && (!isfinite(summary.maxTemp) || temp > summary.maxTemp)) summary.maxTemp = temp;
  if (steady) {
    ++summary.steadySamples;
    const double delta = rate - summary.steadyMean;
    summary.steadyMean += delta / summary.steadySamples;
    summary.steadyM2 += delta * (rate - summary.steadyMean);
  }
}
