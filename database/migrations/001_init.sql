-- Esquema inicial del sistema de transferencia (PostgreSQL).
-- Los datos codificados se guardan en partes pequeñas, nunca en un único
-- campo gigante, y se borran tras la confirmación del cliente.

CREATE TABLE IF NOT EXISTS schema_migrations (
  version integer PRIMARY KEY,
  applied_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS jobs (
  job_id              text PRIMARY KEY,
  status              text NOT NULL,
  source_url          text,
  recipient           text NOT NULL,
  size_bytes          bigint,
  sha256              text,
  encoding_version    integer NOT NULL DEFAULT 1,
  transfer_id         text NOT NULL,
  total_chunks        integer NOT NULL DEFAULT 0,
  chunk_limit_bytes   integer NOT NULL DEFAULT 0,
  part_bytes          integer NOT NULL DEFAULT 65536,
  data_parts          integer NOT NULL DEFAULT 0,
  client_token_hash   text NOT NULL,
  error_code          text,
  error_detail        text,
  attempts            integer NOT NULL DEFAULT 0,
  claimed_by          text,
  claim_expires_at    timestamptz,
  created_at          timestamptz NOT NULL DEFAULT now(),
  updated_at          timestamptz NOT NULL DEFAULT now(),
  completed_at        timestamptz,
  cleaned_at          timestamptz,
  CONSTRAINT jobs_status_check CHECK (status IN (
    'created','queued','downloading','encoding','ready_for_delivery','sending',
    'awaiting_client_confirmation','completed','cleaned','failed_download',
    'failed_encoding','failed_delivery','failed_reconstruction','cancelled','expired'
  ))
);

CREATE INDEX IF NOT EXISTS jobs_status_created_idx ON jobs (status, created_at);
CREATE INDEX IF NOT EXISTS jobs_updated_idx ON jobs (updated_at);

CREATE TABLE IF NOT EXISTS job_data_parts (
  job_id        text NOT NULL REFERENCES jobs(job_id) ON DELETE CASCADE,
  part_index    integer NOT NULL,
  payload       text NOT NULL,
  payload_bytes bigint NOT NULL,
  PRIMARY KEY (job_id, part_index)
);

CREATE TABLE IF NOT EXISTS chunk_deliveries (
  job_id      text NOT NULL REFERENCES jobs(job_id) ON DELETE CASCADE,
  chunk_index integer NOT NULL,
  attempts    integer NOT NULL DEFAULT 0,
  last_error  text,
  sent_at     timestamptz,
  acked_at    timestamptz,
  PRIMARY KEY (job_id, chunk_index)
);

CREATE TABLE IF NOT EXISTS job_events (
  id     bigserial PRIMARY KEY,
  job_id text NOT NULL REFERENCES jobs(job_id) ON DELETE CASCADE,
  at     timestamptz NOT NULL DEFAULT now(),
  stage  text NOT NULL,
  detail text
);

CREATE INDEX IF NOT EXISTS job_events_job_idx ON job_events (job_id, id);
