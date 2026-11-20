#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <atomic>
#include <math.h>
#include "portal.h"
#include "report.h"
#include "serial_load.h"

constexpr uint32_t CONTROL_WINDOW_MS = 100;
constexpr uint32_t TASK_YIELD_INTERVAL_US = 10000;
const char *AP_SSID = "ESP32-Power-Test";
WebServer server(80);
WebSocketsServer live(81);
uint32_t lastFrame = 0, udpRate = 0;
uint64_t previousPackets = 0, previousFrames = 0;
uint32_t serialRate = 0;
struct UdpResult { uint32_t run; bool ok; };
QueueHandle_t udpRequests = nullptr, udpResults = nullptr;
bool udpBusy = false, udpReady = false;
uint32_t udpInterval = 20;

void udpWorker(void *) {
  WiFiUDP udp;
  static const uint8_t payload[1024] = {};
  for (;;) {
    uint32_t run;
    xQueueReceive(udpRequests, &run, portMAX_DELAY);
    const bool ok = udp.beginPacket(IPAddress(192, 168, 4, 255), 20000) &&
                    udp.write(payload, sizeof(payload)) == sizeof(payload) && udp.endPacket();
    UdpResult result{run, ok};
    xQueueOverwrite(udpResults, &result);
  }
}
std::atomic<uint8_t> loadPercent{0}, activeCores{1};
std::atomic<uint32_t> iterations[2]{};
volatile float workResult[2]{};
uint32_t rates[2]{};
uint8_t workers = 0, peak = 100;
uint32_t period = 5, minutes = 30, lastSample = 0, lastPacket = 0, lag = 0;
uint64_t started = 0, elapsed = 0;
bool running = false, traffic = false;
String pattern = "pulse";
const char *state = "Lista";
float temperature = NAN;
const esp_reset_reason_t bootResetReason = esp_reset_reason();

const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWER_ON";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    default: return "OTHER";
  }
}

void stressWorker(void *argument) {
  const uint32_t index = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(argument));
  float result = 0.1f + index;
  for (;;) {
    const uint8_t target = loadPercent;
    const bool enabled = (index == 0) || (activeCores > 1);
    if (!enabled || target == 0) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    const uint32_t windowStart = micros();
    const uint32_t busyDuration = CONTROL_WINDOW_MS * 1000UL * target / 100UL;
    uint32_t lastYield = windowStart;
    uint32_t counter = 0;
    while (static_cast<uint32_t>(micros() - windowStart) < busyDuration) {
      // Floating-point and integer operations consume CPU cycles and generate heat.
      result = sqrtf(result * result + 1.137f) * 0.9997f;
      result += static_cast<float>(esp_random() & 0xFF) * 0.000001f;
      if (result > 20.0f) result = 0.1f;
      counter++;
      // Yield periodically to prevent starving Wi-Fi and system tasks.
      if ((counter & 0x3FFU) == 0 && static_cast<uint32_t>(micros() - lastYield) >= TASK_YIELD_INTERVAL_US) {
        vTaskDelay(1);
        lastYield = micros();
      }
      if (loadPercent != target || (index == 1 && activeCores == 1)) break;
    }
    workResult[index] = result;
    iterations[index].fetch_add(counter, std::memory_order_relaxed);
    const uint32_t elapsed = static_cast<uint32_t>(micros() - windowStart);
    if (elapsed < CONTROL_WINDOW_MS * 1000UL) {
      vTaskDelay(pdMS_TO_TICKS((CONTROL_WINDOW_MS * 1000UL - elapsed) / 1000UL + 1));
    } else {
      vTaskDelay(1);
    }
  }
}

uint64_t nowMs() {
  return esp_timer_get_time() / 1000ULL;
}

void finishTest(Outcome result) {
  running = false;
  loadPercent = 0;
  serialLoad::close();
  state = result == Finished ? "Finalizada" : "Detenida";
  summary.outcome = result;
  summary.seconds = elapsed / 1000;
  saveSummary();
}

void tickTest() {
  if (!running) return;
  elapsed = nowMs() - started;
  if (minutes && elapsed >= minutes * 60000ULL) {
    elapsed = minutes * 60000ULL;
    finishTest(Finished);
    return;
  }
  const uint32_t cycle = period * 1000;
  const uint32_t phase = elapsed % cycle;
  uint8_t target = peak;
  if (pattern == "guided") {
    const uint64_t total = minutes * 60000ULL;
    if (elapsed < total / 10) target = 0;
    else if (elapsed < total / 2) target = (elapsed - total / 10) % cycle < cycle / 2 ? peak : 0;
  }
  if (pattern == "pulse") target = phase < cycle / 2 ? peak : 0;
  if (pattern == "ramp") target = peak * phase / (cycle - 1);
  loadPercent = target;
}

void redirectHome() {
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

bool readNumber(const char *name, int low, int high, int &value) {
  String raw = server.arg(name);
  if (!raw.length() || raw.length() > 5) return false;
  for (unsigned i = 0; i < raw.length(); ++i)
    if (raw[i] < '0' || raw[i] > '9') return false;
  value = raw.toInt();
  return value >= low && value <= high;
}

void startTest() {
  int duration, percent, seconds, cores;
  String mode = server.arg("pattern");
  if (!readNumber("minutes", 0, 43200, duration) || !readNumber("peak", 0, 100, percent) ||
      !readNumber("period", 1, 3600, seconds) ||
      (server.arg("cores") != "auto" && !readNumber("cores", 1, 2, cores)) ||
      (mode != "pulse" && mode != "constant" && mode != "ramp" && mode != "guided") || (mode == "guided" && duration == 0)) {
    server.send(400, "text/plain", "Revisa los parametros de la prueba.");
    return;
  }
  if (server.arg("cores") == "auto") cores = workers;
  if (!cores || cores > workers) {
    server.send(409, "text/plain", "Faltan nucleos disponibles. Vuelve y selecciona menos nucleos.");
    return;
  }
  const bool useSerial = server.hasArg("serial");
  if (server.hasArg("wifi") && !udpReady) {
    server.send(503, "text/plain", "No hay memoria para iniciar el generador UDP.");
    return;
  }
  if (useSerial && !serialLoad::prepare()) {
    server.send(503, "text/plain; charset=utf-8", "UART1 no disponible. Reintenta o desmarca Tráfico serie interno.");
    return;
  }
  if (!useSerial) serialLoad::close();
  minutes = duration;
  peak = percent;
  period = seconds;
  activeCores = cores;
  pattern = mode;
  traffic = server.hasArg("wifi");
  udpInterval = 20;
  summary = RunSummary{};
  summary.version = 3;
  summary.serialLoad = useSerial;
  summary.runId = esp_random();
  previousPackets = 0;
  udpRate = 0;
  previousFrames = 0;
  serialRate = 0;
  summary.outcome = Measuring;
  mode.toCharArray(summary.mode, sizeof(summary.mode));
  summary.minutes = minutes;
  summary.period = period;
  summary.peak = peak;
  summary.cores = cores;
  summary.wifi = traffic;
  saveSummary();
  iterations[0].exchange(0);
  iterations[1].exchange(0);
  lastSample = millis();
  started = nowMs();
  running = true;
  state = "En curso";
  tickTest();
  redirectHome();
}

String statusJson() {
  const char *phase = "";
  if (running && pattern == "guided") {
    phase = elapsed < minutes * 6000ULL ? "Reposo" : elapsed < minutes * 30000ULL ? "Pulsos" : "Carga sostenida";
  }
  char json[560], temp[20] = "null";
  if (isfinite(temperature)) snprintf(temp, sizeof(temp), "%.1f", temperature);
  snprintf(json, sizeof(json),
    "{\"phase\":\"%s\",\"state\":\"%s\",\"running\":%s,\"elapsed\":%llu,\"minutes\":%u,\"load\":%u,"
    "\"rates\":[%u,%u],\"lag\":%u,\"temperature\":%s,\"heap\":%u,\"reset\":\"%s\",\"workers\":%u,"
    "\"sample_ms\":%u,\"udpRate\":%u,\"serialRate\":%u}",
    phase, state, running ? "true" : "false", (unsigned long long)(elapsed / 1000), minutes,
    unsigned(loadPercent.load()), rates[0], rates[1], lag, temp, ESP.getFreeHeap(),
    resetReasonName(bootResetReason), unsigned(workers), lastSample, udpRate, serialRate);
  return String(json);
}

void sendStatus() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", statusJson());
}

String networkState = "Solo punto de acceso";
bool joining = false, joined = false;
uint32_t joinStart = 0;
String pendingSsid, pendingKey;

String jsonEscape(const String &value) {
  String result;
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = value[i];
    if (c < 0x20) {
      char code[7];
      snprintf(code, sizeof(code), "\\u%04x", unsigned(c));
      result += code;
    } else {
      if (c == '"' || c == '\\') result += '\\';
      result += char(c);
    }
  }
  return result;
}

void joinNetwork() {
  if (server.client().localIP() != WiFi.softAPIP()) {
    server.send(403, "text/plain", "Configura la red desde el punto de acceso del ESP32.");
    return;
  }
  String ssid = server.arg("ssid"), key = server.arg("key");
  if (!ssid.length() || ssid.length() > 32 || key.length() > 63 || (key.length() && key.length() < 8)) {
    server.send(400, "text/plain", "Revisa el nombre de red y la clave.");
    return;
  }
  joined = false;
  joining = true;
  joinStart = millis();
  networkState = "Conectando";
  pendingSsid = ssid;
  pendingKey = key;
  server.sendHeader("Cache-Control", "no-store");
  if (server.uri() == "/api/network") {
    server.send(202, "application/json", "{\"state\":\"Conectando\",\"ip\":\"\",\"ssid\":\"\"}");
  } else {
    server.sendHeader("Location", "/#red");
    server.send(303, "text/plain", "");
  }
}

void updateNetwork() {
  if (pendingSsid.length()) {
    // Let the HTTP acknowledgement leave the AP before the radio changes channel.
    if (millis() - joinStart >= 350) {
      WiFi.begin(pendingSsid.c_str(), pendingKey.c_str());
      pendingSsid = "";
      pendingKey = "";
    }
    return;
  }
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    if (!joined) Serial.printf("Panel en red local: http://%s/\n", WiFi.localIP().toString().c_str());
    joined = true;
    joining = false;
    networkState = "Conectada";
  } else if (joining && millis() - joinStart >= 25000) {
    WiFi.disconnect();
    joining = false;
    networkState = "Sin conexión. Revisa SSID, clave y cobertura.";
  } else if (joined) {
    joined = false;
    networkState = "Se perdió la red local. El AP sigue disponible.";
  }
}

void setReference() {
  if (summary.outcome != Finished || !summary.samples) {
    server.send(409, "text/plain; charset=utf-8", "Completa una prueba con duración definida para usarla como referencia.");
    return;
  }
  if (!storeReady || record.putBytes("reference", &summary, sizeof(summary)) != sizeof(summary)) {
    server.send(503, "text/plain", "No se pudo guardar la referencia.");
    return;
  }
  redirectHome();
}

void compareReference(String &text) {
  RunSummary ref;
  if (!storeReady || !readSummary("reference", ref) || ref.outcome != Finished) return;
  if ((ref.version == 1) != (summary.version == 1)) {
    text += "\nLa referencia usa otra versión de carga de radio. Guarda una nueva pasada de control para comparar.";
    return;
  }
  if (ref.version >= 2 && ref.runId == summary.runId) {
    text += "\nEsta pasada es la referencia. Ejecuta una segunda prueba con los mismos ajustes para ver diferencias.";
    return;
  }
  if (strcmp(ref.mode, summary.mode) || ref.peak != summary.peak || ref.period != summary.period ||
      ref.cores != summary.cores || ref.minutes != summary.minutes || ref.wifi != summary.wifi ||
      ref.serialLoad != summary.serialLoad) {
    text += "\nReferencia guardada con otros ajustes. Repite con el mismo perfil para comparar.";
    return;
  }
  text += "\nComparación con la referencia guardada (referencia → ensayo):";
  text += String("\nRetraso máximo: ") + ref.worstLag + " → " + summary.worstLag + " ms.";
  text += String(" Muestras retrasadas: ") + (unsigned long)ref.late + " → " + (unsigned long)summary.late + ".";
  if (ref.steadySamples >= 2 && summary.steadySamples >= 2 && ref.steadyMean > 0) {
    const double change = 100 * (summary.steadyMean / ref.steadyMean - 1);
    text += String("\nRendimiento sostenido: ") + String(ref.steadyMean, 0) + " → " +
            String(summary.steadyMean, 0) + " iter/s (" + String(change, 1) + "%).";
  }
}

void sendReport() {
  String text;
  text.reserve(1700);
  if (summary.outcome == Empty) text = "Aún no hay ensayo registrado.";
  else {
    const char *outcomes[] = {"", "En curso", "Finalizada", "Detenida manualmente", "Interrumpida por reinicio"};
    text = "ESP32 Stress Lab — Informe de estabilidad del ESP32\nOrigen de las muestras: telemetría interna de la placa.\n";
    text += String("Resultado: ") + outcomes[summary.outcome] + "\n";
    text += String("Patrón: ") + summary.mode + " | Duración programada: " + summary.minutes +
            " min (0 = continua) | Máximo: " + summary.peak + "% | Período: " + summary.period +
            " s | Núcleos: " + summary.cores + " | Tráfico Wi-Fi: " + (summary.wifi ? "sí" : "no") + "\n";
    if (summary.version >= 3)
      text += summary.serialLoad ? "UART1 interna activa (460800 baudios, 8N1).\n" : "Tráfico serie desactivado.\n";
    if (summary.outcome == Interrupted) {
      text += String("Motivo de arranque: ") + resetReasonName((esp_reset_reason_t)summary.reset) + ". ";
      text += summary.reset == ESP_RST_BROWNOUT ? "El chip registró una caída de alimentación. " : "Investiga el reinicio antes de atribuirlo a la fuente. ";
      text += "El corte ocurrió con el ensayo activo. Se perdieron las muestras en RAM.";
    } else {
      text += String("Tiempo observado: ") + (unsigned long)(running ? elapsed / 1000 : summary.seconds) +
              " s | Muestras: " + String((unsigned long)summary.samples) + "\n";
      if (summary.samples) {
        text += "Temporización del bucle: el ESP32 resta 1000 ms al tiempo entre dos muestras consecutivas de su reloj local. ";
        text += String("El intervalo más largo fue de ") + String(1000ULL + summary.worstLag) + " ms. La latencia de red se mide aparte.\n";
        if (summary.version >= 2) {
          text += String("Exceso medio: ") + String(double(summary.totalLag) / summary.samples, 2) +
                  " ms. Intervalos con exceso de 10 a 99 ms: " + String(summary.lag10 - summary.late) +
                  ". De 100 ms o más: " + String(summary.late) + " (" +
                  String(100.0 * summary.late / summary.samples, 2) + "%).\n";
          if (summary.worstLag) text += String("Primer pico máximo a los ") + summary.worstAt + " s del ensayo.\n";
        }
        text += String("Retraso máximo: ") + summary.worstLag + " ms | Muestras con retraso de 100 ms o más: " +
                String((unsigned long)summary.late) + " | Memoria libre mínima: " + summary.minHeap + " B\n";
        text += "Temperatura interna máxima: ";
        text += isfinite(summary.maxTemp) ? String(summary.maxTemp, 1) + " °C\n" : String("sin lectura\n");
      }
      if (summary.version >= 2 && summary.wifi) {
        text += String("Radio durante todo el ensayo: ") + String(summary.packetsOk) + " datagramas UDP de 1024 B aceptados para envío y " +
                String(summary.packetsFailed) + " intentos fallidos. Objetivo: hasta 50 envíos/s por el AP. ";
        text += "El contador verifica la entrega a la pila local. La recepción por otro equipo queda sin confirmar.\n";
      }
      if (summary.serialLoad) {
        text += String("Serie interna: enviadas ") + String(summary.framesSent) + ", verificadas " + String(summary.framesOk) +
                ", contenido distinto " + String(summary.mismatches) + ", lectura incompleta o fallida " + String(summary.incomplete) +
                ", errores de envío " + String(summary.txErrors) + ".\n";
        text += String("Pendientes al corte: ") + String(summary.framesSent - summary.framesOk - summary.mismatches - summary.incomplete) +
                ". Cada trama contiene 64 bytes. Se verifica el eco completo byte por byte.\n";
        if (summary.framesOk) text += String("Mayor tiempo hasta verificar el eco: ") + summary.maxEchoMs +
                " ms (incluye el tiempo hasta que el bucle atiende RX).\n";
        if (summary.mismatches || summary.incomplete || summary.txErrors)
          text += "Se observaron anomalías serie internas. Repite con idéntica carga y una fuente de referencia.\n";
      }
      if (summary.steadySamples >= 2 && summary.steadyMean > 0) {
        const double cv = 100 * sqrt(summary.steadyM2 / (summary.steadySamples - 1)) / summary.steadyMean;
        text += String("Carga sostenida: ") + String(summary.steadyMean, 0) + " iter/s en total | Variación relativa (desviación / media): " + String(cv, 1) + "%\n";
        text += String("Dispersión del trabajo por segundo: una desviación estándar equivale a ") +
                String(sqrt(summary.steadyM2 / (summary.steadySamples - 1)), 0) +
                " iter/s. Compara esta cifra con pasadas de la misma duración y configuración.\n";
      }
      if (summary.version >= 2 && summary.wifi && summary.packetsFailed)
        text += "Hubo errores al solicitar envíos UDP locales. Repite con los mismos clientes Wi-Fi antes de atribuirlos al convertidor.\n";
      if (summary.outcome == Measuring) text += "Informe provisional. Espera al final para comparar.";
      else if (summary.outcome == Stopped) text += "Ensayo parcial. Repite la duración completa para comparar.";
      else if (!summary.samples) text += "Sin muestras para evaluar.";
      else if (summary.late) text += "Completó el ensayo con retrasos de muestreo. Repite con la fuente de referencia y la misma actividad de red.";
      else text += "Completó el ensayo sin reinicio ni retrasos de muestreo de 100 ms o más.";
    }
    if (summary.outcome == Finished) compareReference(text);
    text += "\nEl umbral de retraso es orientativo. Compara varias pasadas con la misma placa, carga y cableado. Estas métricas no miden voltaje ni rizado.";
    if (!saved) text += "\nAlmacenamiento persistente no disponible.";
  }
  server.sendHeader("Cache-Control", "no-store");
  if (server.hasArg("download")) server.sendHeader("Content-Disposition", "attachment; filename=esp32-informe.txt");
  server.send(200, "text/plain; charset=utf-8", text);
}

void setup() {
  udpRequests = xQueueCreate(1, sizeof(uint32_t));
  udpResults = xQueueCreate(1, sizeof(UdpResult));
  udpReady = udpRequests && udpResults &&
      xTaskCreate(udpWorker, "UdpStress", 4096, nullptr, 1, nullptr) == pdPASS;
  Serial.begin(115200);
  for (int i = 0; i < portNUM_PROCESSORS && i < 2; ++i) {
    if (xTaskCreatePinnedToCore(stressWorker, "StressWorker", 4096,
        reinterpret_cast<void *>(static_cast<uintptr_t>(i)), 1, nullptr, i) != pdPASS) break;
    ++workers;
  }
  restoreSummary();
  previousPackets = summary.packetsOk;
  previousFrames = summary.framesOk;
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  while (!WiFi.softAP(AP_SSID)) delay(1000);
  Serial.printf("ESP32 Stress Lab | %s | http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/report", HTTP_GET, sendReport);
  server.on("/reference", HTTP_POST, setReference);
  server.on("/network", HTTP_POST, joinNetwork);
  server.on("/network", HTTP_GET, redirectHome);
  server.on("/api/network", HTTP_POST, joinNetwork);
  server.on("/api/network", HTTP_GET, []() {
    String ip = joined ? WiFi.localIP().toString() : "";
    server.sendHeader("Cache-Control", "no-store");
    String ssid = joined ? jsonEscape(WiFi.SSID()) : "";
    server.send(200, "application/json", String("{\"state\":\"") + networkState + "\",\"ip\":\"" + ip + "\",\"ssid\":\"" + ssid + "\"}");
  });
  server.on("/start", HTTP_POST, startTest);
  server.on("/stop", HTTP_POST, []() {
    tickTest();
    if (running) finishTest(Stopped);
    redirectHome();
  });
  server.begin();
  live.begin();
  live.onEvent([](uint8_t client, WStype_t type, uint8_t *data, size_t size) {
    if (type == WStype_TEXT && size == 4 && memcmp(data, "ping", 4) == 0)
      live.sendTXT(client, "pong");
  });
  lastSample = millis();
}

void loop() {
  tickTest();
  updateNetwork();
  server.handleClient();
  live.loop();
  const uint32_t now = millis(), interval = now - lastSample;
  if (running && summary.serialLoad) serialLoad::service(now);
  if (interval >= 1000) {
    lag = interval - 1000;
    udpRate = (summary.packetsOk - previousPackets) * 1000 / interval;
    previousPackets = summary.packetsOk;
    serialRate = (summary.framesOk - previousFrames) * 1000 / interval;
    previousFrames = summary.framesOk;
    for (int i = 0; i < 2; ++i)
      rates[i] = uint64_t(iterations[i].exchange(0)) * 1000 / interval;
#if CONFIG_IDF_TARGET_ESP32S3
    temperature = temperatureRead();
#endif
    if (running) {
      summary.totalLag += lag;
      if (lag >= 10) ++summary.lag10;
      if (lag > summary.worstLag) summary.worstAt = elapsed / 1000;
      const bool steady = peak && (pattern == "constant" ||
          (pattern == "guided" && elapsed >= minutes * 30000ULL + interval + CONTROL_WINDOW_MS));
      observeSample(lag, ESP.getFreeHeap(), temperature, steady, rates[0] + rates[1]);
    }
    lastSample = now;
  }
  UdpResult result;
  if (udpBusy && xQueueReceive(udpResults, &result, 0) == pdTRUE) {
    udpBusy = false;
    if (running && result.run == summary.runId) {
      if (result.ok) ++summary.packetsOk;
      else ++summary.packetsFailed;
      udpInterval = result.ok ? 20 : min(udpInterval * 2, uint32_t(200));
    }
  }
  if (running && traffic && !udpBusy && now - lastPacket >= udpInterval) {
    lastPacket = now;
    udpBusy = xQueueSend(udpRequests, &summary.runId, 0) == pdTRUE;
  }
  if (now - lastFrame >= 250) {
    lastFrame = now;
    if (live.connectedClients() > 0) {
      String frame = statusJson();
      live.broadcastTXT(frame);
    }
  }
  delay(2);
}

