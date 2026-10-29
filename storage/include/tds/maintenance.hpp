#pragma once

#include <atomic>
#include <thread>

namespace tds {

class JobStore;

// Hilo de mantenimiento periódico (ver especificación §3.4 y §7):
//   * marca `expired` los trabajos atascados y libera sus payloads;
//   * borra los datos de trabajos terminales pasado el periodo de retención;
//   * purga los metadatos de trabajos ya limpiados.
//
// Es idempotente y seguro con varios procesos: todas las operaciones son
// sentencias SQL atómicas. El primer ciclo se ejecuta tras un intervalo, no al
// arrancar, para no competir con las migraciones.
std::thread start_maintenance(const std::atomic<bool>& stop, JobStore& store,
                              int interval_seconds, int stale_minutes,
                              int client_confirm_hours, int retention_hours);

}  // namespace tds
