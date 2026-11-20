#pragma once
#include <driver/uart.h>
#include "report.h"

// UART1 routes its TX signal internally to RX. Leave the GPIO matrix untouched.
namespace serialLoad {
constexpr uart_port_t port = UART_NUM_1;
constexpr uint32_t periodMs = 5;
constexpr uint32_t timeoutMs = 100;
uint8_t expected[64], received[sizeof(expected)];
size_t used = 0, written = 0;
bool sending = false;
bool owned = false, waiting = false;
uint32_t sentAt = 0;

void close() {
  if (owned) {
    uart_set_loop_back(port, false);
    uart_driver_delete(port);
  }
  owned = waiting = sending = false;
  used = 0;
}

bool prepare() {
  if (!owned) {
    if (uart_is_driver_installed(port) ||
        uart_driver_install(port, 512, 0, 0, nullptr, 0) != ESP_OK) return false;
    owned = true;
    uart_config_t config{};
    config.baud_rate = 460800;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_APB;
    if (uart_param_config(port, &config) != ESP_OK || uart_set_loop_back(port, true) != ESP_OK) {
      close();
      return false;
    }
  }
  if (uart_wait_tx_done(port, pdMS_TO_TICKS(20)) != ESP_OK || uart_flush_input(port) != ESP_OK) return false;
  waiting = sending = false;
  used = 0;
  sentAt = millis() - periodMs;
  return true;
}

void service(uint32_t now) {
  if (!owned) return;
  if (waiting) {
    const int n = uart_read_bytes(port, received + used, sizeof(received) - used, 0);
    if (n > 0) used += n;
    if (used == sizeof(received)) {
      if (memcmp(expected, received, used) == 0) {
        ++summary.framesOk;
        summary.maxEchoMs = max(summary.maxEchoMs, now - sentAt);
      } else ++summary.mismatches;
    } else if (now - sentAt >= timeoutMs || n < 0) {
      ++summary.incomplete;
      uart_flush_input(port);
    } else return;
    waiting = false;
  }
  if (!sending) {
    if (now - sentAt < periodMs) return;
    sentAt = now;
    const uint64_t seq = summary.framesSent;
    memcpy(expected, &seq, sizeof(seq));
    for (size_t i = sizeof(seq); i < sizeof(expected); ++i) expected[i] = uint8_t(seq + 37 * i);
    written = 0;
    sending = true;
  }
  // Only fill currently available FIFO space. Never wait for TX from the web loop.
  const int n = uart_tx_chars(port, reinterpret_cast<const char *>(expected + written),
                              sizeof(expected) - written);
  if (n > 0) written += n;
  if (n < 0 || (written < sizeof(expected) && now - sentAt >= timeoutMs)) {
    ++summary.txErrors;
    sending = false;
    uart_flush_input(port);
    sentAt = now;
    return;
  }
  if (written < sizeof(expected)) return;
  sending = false;
  ++summary.framesSent;
  used = 0;
  waiting = true;
}
}
